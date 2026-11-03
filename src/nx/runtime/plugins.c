/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* Plugins: a folder each of the data folder's plugins/, a plugin.ini that names the game and the versions it knows,
   and a patch file for each build: 8 hex digits of the main module's offset and the instruction there, then the
   instruction the plugin knows when it writes one. The patches are written before the module's pages are made
   read-only, then the functions they touched leave dispatch (jit_code_changed), so what runs is compiled from the
   patched code */
#include "nx/runtime/plugins.h"
#include "common/utilities/builtin_plugins.h"
#include "common/utilities/common.h"
#include "common/utilities/endian.h"
#include "common/utilities/game_ini.h"
#include "nx/hle/hle.h"
#include "nx/hle/kernel/kernel.h"
#include "nx/memory/mem.h"
#include "nx/runtime/guest_jit.h"
#include "nx/runtime/platform/log.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PLUGINS_MAX    32
#define PLUGIN_PATCHES 64

typedef struct {
    char id[64];
    char name[96];
    char title[160];
    char titles[160];
    char versions[160];
    int enabled;
    int builtin; /* its index in snx_builtin_plugins, -1 for a folder */
} Plugin;

typedef struct {
    u64 guest;
    u32 word;
} Patch;

static struct {
    char dir[1024];
    char on[2048];
    Plugin plugin[PLUGINS_MAX];
    int count, scanned;
    Patch patch[PLUGIN_PATCHES];
    int patches;
} g;

static int token_in(const char* list, const char* token) {
    size_t n = strlen(token);
    const char* p = list;
    while (p && *p) {
        const char* end;
        while (*p == ',' || *p == ' ' || *p == '\t' || *p == ';') p++;
        if (!*p) break;
        end = p + strcspn(p, ", \t;");
        if ((size_t)(end - p) == n && !strncmp(p, token, n)) return 1;
        p = end;
    }
    return 0;
}

/* the manifest: its name, title, the title IDs it names and the versions it knows */
static void plugin_read(Plugin* p, const char* folder) {
    char* path = snx_path_join(folder, "plugin.ini");
    char *name = path ? snx_ini_value(path, "name") : NULL;
    char *title = path ? snx_ini_value(path, "title") : NULL;
    char *titles = path ? snx_ini_value(path, "titles") : NULL;
    char *versions = path ? snx_ini_value(path, "versions") : NULL;
    const char* slash = strrchr(folder, '/');
    const char* back = strrchr(folder, '\\');
    if (back && (!slash || back > slash)) slash = back;
    snprintf(p->id, sizeof p->id, "%s", slash ? slash + 1 : folder);
    snprintf(p->name, sizeof p->name, "%s", name && *name ? name : p->id);
    snprintf(p->title, sizeof p->title, "%s", title && *title ? title : p->name);
    snprintf(p->titles, sizeof p->titles, "%s", titles ? titles : "");
    snprintf(p->versions, sizeof p->versions, "%s", versions ? versions : "");
    free(path);
    free(name);
    free(title);
    free(titles);
    free(versions);
}

/* the built-in plugins, then each folder of plugins/ that is not one of them; the ones on are named in plugins_on= */
static int plugin_named(const char* id) {
    int i;
    for (i = 0; i < g.count; ++i)
        if (!strcmp(g.plugin[i].id, id)) return 1;
    return 0;
}
static void plugins_scan(void) {
    char** names = NULL;
    size_t count = 0, i;
    int b;
    if (g.scanned) return;
    g.scanned = 1;
    g.count = 0;
    for (b = 0; b < snx_builtin_plugin_count && g.count < PLUGINS_MAX; ++b) {
        const SnxBuiltinPlugin* src = &snx_builtin_plugins[b];
        Plugin* p = &g.plugin[g.count++];
        memset(p, 0, sizeof *p);
        snprintf(p->id, sizeof p->id, "%s", src->id);
        snprintf(p->name, sizeof p->name, "%s", src->name);
        snprintf(p->title, sizeof p->title, "%s", src->title);
        snprintf(p->titles, sizeof p->titles, "%s", src->titles);
        snprintf(p->versions, sizeof p->versions, "%s", src->versions);
        p->enabled = token_in(g.on, p->id);
        p->builtin = b;
    }
    if (!g.dir[0] || snx_list_dir(g.dir, &names, &count)) return;
    for (i = 0; i < count && g.count < PLUGINS_MAX; ++i) {
        char* folder = snx_path_join(g.dir, names[i]);
        char* manifest = folder ? snx_path_join(folder, "plugin.ini") : NULL;
        if (manifest && snx_is_file(manifest)) {
            Plugin* p = &g.plugin[g.count];
            memset(p, 0, sizeof *p);
            plugin_read(p, folder);
            if (!plugin_named(p->id)) { /* a folder with a built-in plugin's id is left out */
                p->enabled = token_in(g.on, p->id);
                p->builtin = -1;
                ++g.count;
            }
        }
        free(manifest);
        free(folder);
    }
    for (i = 0; i < count; ++i) free(names[i]);
    free(names);
}

/* 1 when the plugin names title_id, or names no title at all */
static int title_named(const Plugin* p, u64 title_id) {
    const char* at = p->titles;
    if (!p->titles[0]) return 1;
    while (*at) {
        char* end = NULL;
        unsigned long long v;
        while (*at == ',' || *at == ' ' || *at == '\t' || *at == ';') at++;
        if (!*at) break;
        v = strtoull(at, &end, 16);
        if (end != at && v == title_id) return 1;
        at = end != at ? end : at + 1;
    }
    return 0;
}

/* the build id's first 8 bytes as a patch file names it: 16 hex digits, upper case */
static void build_name(const u8* id, char out[17]) {
    static const char hex[] = "0123456789ABCDEF";
    int i;
    for (i = 0; i < 8; ++i) {
        out[2 * i] = hex[id[i] >> 4];
        out[2 * i + 1] = hex[id[i] & 15];
    }
    out[16] = 0;
}

/* the build's patch file in folder, <16 hex>.patch in either case: NULL when there is none. Owned */
static int same_hex_name(const char* a, const char* b) {
    int i;
    for (i = 0; i < 16; ++i)
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return 0;
    return 1;
}
static char* patch_file(const char* folder, const u8* build_id) {
    char want[17], **names = NULL;
    size_t count = 0, i;
    char* found = NULL;
    build_name(build_id, want);
    if (snx_list_dir(folder, &names, &count)) return NULL;
    for (i = 0; i < count && !found; ++i) {
        size_t n = strlen(names[i]);
        if (n == 22 && !memcmp(names[i] + 16, ".patch", 6) && same_hex_name(names[i], want))
            found = snx_path_join(folder, names[i]);
    }
    for (i = 0; i < count; ++i) free(names[i]);
    free(names);
    return found;
}

/* offset instruction [the instruction there now], all hex: 1 when the line is one */
static int patch_line(const char* line, u64* offset, u32* word, u32* now, int* has_now) {
    unsigned long long a, b, c;
    int n;
    while (*line == ' ' || *line == '\t') line++;
    if (!*line || *line == '#' || *line == ';' || !isxdigit((unsigned char)*line)) return 0;
    n = sscanf(line, "%llx %llx %llx", &a, &b, &c);
    if (n < 2) return 0;
    *offset = a;
    *word = (u32)b;
    *has_now = n >= 3;
    *now = (u32)c;
    return 1;
}

static u64 build_number(const u8* id) {
    u64 v = 0;
    int i;
    for (i = 0; i < 8; ++i) v = v << 8 | id[i];
    return v;
}

/* the built-in plugin's patches for this build, checked and written: how many, 0 when it knows none */
static int builtin_patches(const Plugin* p, const SnxModInfo* mi) {
    const SnxBuiltinPatch* list;
    int count = 0, i, found = 0, written = 0;
    char version[17];
    u64 build = build_number(mi->build_id);
    build_name(mi->build_id, version);
    list = snx_builtin_plugin_patches(p->builtin, &count);
    for (i = 0; i < count; ++i) {
        if (list[i].build != build) continue;
        found = 1;
        if (list[i].offset > mi->size || mi->size - list[i].offset < 4) {
            WARN("plugin: %s patches %x, past the module of build %s: not applied\n", p->name, list[i].offset, version);
            return 0;
        }
        if (list[i].has_now && snx_ld_le32(GP(mi->base + list[i].offset)) != list[i].now) {
            WARN("plugin: %s knows %08x at %x of build %s, and the game has %08x: not applied\n", p->name, list[i].now,
                 list[i].offset, version, snx_ld_le32(GP(mi->base + list[i].offset)));
            return 0;
        }
    }
    if (!found) {
        WARN("plugin: %s knows no build %s of the game (the versions it knows are %s): not applied\n", p->name, version,
             p->versions[0] ? p->versions : "none listed");
        return 0;
    }
    for (i = 0; i < count && written < PLUGIN_PATCHES; ++i) {
        Patch patch;
        if (list[i].build != build) continue;
        patch.guest = mi->base + list[i].offset;
        patch.word = list[i].word;
        snx_st_le32(GP(patch.guest), patch.word);
        g.patch[g.patches] = patch;
        ++g.patches;
        ++written;
    }
    return written;
}

/* the plugin's patches for this build, checked and written over the module: how many, 0 when it has none for the
   build or one does not fit, which the log says */
static int plugin_patches(const Plugin* p, const SnxModInfo* mi) {
    char* folder;
    char* file;
    size_t size = 0;
    char* text;
    char version[17];
    Patch local[PLUGIN_PATCHES];
    u64 offset;
    u32 word, now;
    int has_now, count = 0, i;
    if (p->builtin >= 0) return builtin_patches(p, mi);
    folder = snx_path_join(g.dir, p->id);
    file = patch_file(folder, mi->build_id);
    text = file ? (char*)snx_read_file(file, &size) : NULL;
    (void)size;
    build_name(mi->build_id, version);
    if (!text) {
        WARN("plugin: %s knows no build %s of the game (the versions it knows are %s): not applied\n", p->name, version,
             p->versions[0] ? p->versions : "none listed");
        free(file);
        free(folder);
        return 0;
    }
    /* every line is checked before the module is written: a mismatched instruction leaves it as it was */
    for (char* line = text; *line;) {
        char* next = line + strcspn(line, "\n");
        if (*next) *next++ = 0;
        if (patch_line(line, &offset, &word, &now, &has_now)) {
            if (count == PLUGIN_PATCHES) {
                WARN("plugin: %s has more than %d patches for build %s: not applied\n", p->name, PLUGIN_PATCHES,
                     version);
                count = 0;
                goto done;
            }
            if (offset > mi->size || mi->size - offset < 4) {
                WARN("plugin: %s patches %llx, past the module of build %s: not applied\n", p->name,
                     (unsigned long long)offset, version);
                count = 0;
                goto done;
            }
            if (has_now && snx_ld_le32(GP(mi->base + offset)) != now) {
                WARN("plugin: %s knows %08x at %llx of build %s, and the game has %08x: not applied\n", p->name, now,
                     (unsigned long long)offset, version, snx_ld_le32(GP(mi->base + offset)));
                count = 0;
                goto done;
            }
            local[count].guest = mi->base + offset;
            local[count++].word = word;
        }
        line = next;
    }
    for (i = 0; i < count; ++i) {
        snx_st_le32(GP(local[i].guest), local[i].word);
        if (g.patches < PLUGIN_PATCHES) g.patch[g.patches++] = local[i];
    }
done:
    free(text);
    free(file);
    free(folder);
    return count;
}

void plugins_patch_module(const SnxModInfo* mi) {
    int i;
    if (!mi || !mi->name || strcmp(mi->name, "main")) return;
    plugins_scan();
    if (!g.count) return;
    g.patches = 0;
    for (i = 0; i < g.count; ++i) {
        Plugin* p = &g.plugin[i];
        u64 title = g_hle.kernel.title_id;
        int n;
        if (!p->enabled) continue;
        if (!title_named(p, title)) {
            LOG("plugin: %s is on, and the game %016llx is not one it names: not applied\n", p->name,
                (unsigned long long)title);
            continue;
        }
        n = plugin_patches(p, mi);
        if (n)
            LOG("plugin: %s%s%s patches main at %d place%s\n", p->name, p->versions[0] ? " " : "",
                p->versions[0] ? p->versions : "", n, n == 1 ? "" : "s");
    }
}

void plugins_code_changed(void) {
    int i;
    for (i = 0; i < g.patches; ++i) jit_code_changed(g.patch[i].guest, 4);
    if (g.patches)
        LOG("plugin: %d patched instruction%s left the compiled code: the JIT compiles the game's code as the plugin "
            "left it\n",
            g.patches, g.patches == 1 ? "" : "s");
}

int plugins_options(int* argc, char** argv) {
    int read = 1, write = 1;
    while (read < *argc && strcmp(argv[read], "--")) {
        const char* arg = argv[read];
        const char* value = NULL;
        char* slot = NULL;
        size_t cap = 0;
        if (!strcmp(arg, "--plugins-dir")) {
            slot = g.dir;
            cap = sizeof g.dir;
        } else if (!strncmp(arg, "--plugins-dir=", 14)) {
            slot = g.dir;
            cap = sizeof g.dir;
            value = arg + 14;
        } else if (!strcmp(arg, "--plugins-on")) {
            slot = g.on;
            cap = sizeof g.on;
        } else if (!strncmp(arg, "--plugins-on=", 13)) {
            slot = g.on;
            cap = sizeof g.on;
            value = arg + 13;
        }
        if (!slot) {
            argv[write++] = argv[read++];
            continue;
        }
        if (!value) {
            if (read + 1 >= *argc) return snx_error("%s requires a value", arg);
            value = argv[++read];
        }
        snprintf(slot, cap, "%s", value);
        ++read;
    }
    while (read < *argc) argv[write++] = argv[read++];
    argv[write] = NULL;
    *argc = write;
    return 0;
}
