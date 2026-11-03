/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* Plugins: folders of the launcher's plugins/ that patch a game's code while it runs, which the launcher turns on in
   settings.ini's plugins_on=. A plugin names the title IDs and the versions it knows; the runtime applies its patch
   only to a module of that build and marks what it compiled stale, as the game's own code changes do */
#ifndef SNX_PLUGINS_H
#define SNX_PLUGINS_H
#include "nx/loader/module_abi.h"

/* --plugins-dir=DIR and --plugins-on=ID,ID: the folders of the plugins, and the ones on. Both are taken out of argv
   before the game's settings read it. Called before loader_load, once the game's title ID is known. 0, else nonzero */
int plugins_options(int* argc, char** argv);
/* the patches of the plugins on that name the game's title and this module's build, written over the module before
   its pages are made read-only: called by loader.c for each module, main only */
void plugins_patch_module(const SnxModInfo* mi);
/* after jit_init: each patched function leaves dispatch, so the code the JIT compiles is the patched code */
void plugins_code_changed(void);
#endif
