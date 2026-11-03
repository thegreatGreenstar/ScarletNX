/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* The plugins built into ScarletNX: their names, the games and versions they know, and the instructions they write.
   The launcher lists them and the runtime applies them; a plugin folder in the data folder can add another, and a
   folder with the same id as a built-in one is left out */
#ifndef SNX_BUILTIN_PLUGINS_H
#define SNX_BUILTIN_PLUGINS_H

typedef struct {
    const char* id;
    const char* name;
    const char* title;
    const char* titles;   /* the game's title IDs, hex, a comma or a space between two */
    const char* versions; /* the versions it knows, as Settings shows them */
} SnxBuiltinPlugin;

/* one instruction a built-in plugin writes, for one main-module build: the build's first 8 bytes as a number, the
   offset from the main module's start, the instruction it writes, and the instruction it must find there (now, when
   has_now is 1; an older build's instruction may be left out) */
typedef struct {
    unsigned long long build;
    unsigned int offset, word, now;
    int has_now;
} SnxBuiltinPatch;

extern const SnxBuiltinPlugin snx_builtin_plugins[];
extern const int snx_builtin_plugin_count;

/* the patches of built-in plugin index, and how many: NULL for none */
const SnxBuiltinPatch* snx_builtin_plugin_patches(int index, int* count);
#endif
