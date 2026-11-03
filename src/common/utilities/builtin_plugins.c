/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#include "common/utilities/builtin_plugins.h"
#include <stddef.h>

const SnxBuiltinPlugin snx_builtin_plugins[] = {
    {"ToTK", "ToTK (Disable DynamicRes)", "The Legend of Zelda: Tears of the Kingdom", "0100F2C0115B6000",
     "1.2.1"},
    {"ToTK_LOD", "ToTK (Disable DynLOD)", "The Legend of Zelda: Tears of the Kingdom", "0100F2C0115B6000",
     "1.2.1"},
};
const int snx_builtin_plugin_count = (int)(sizeof snx_builtin_plugins / sizeof snx_builtin_plugins[0]);

/* ToTK: the scene viewport is the view's base size times the dynamic resolution scale. The two multiplies that apply
   the scale to the width and the height become copies of the base size, so the viewport stays at the base size. */
static const SnxBuiltinPatch totk_patches[] = {
    {0x9B4E43650501A4D4ULL, 0x009434E0, 0x1E204028, 0x1E210808, 1},
    {0x9B4E43650501A4D4ULL, 0x009434E4, 0x1E204049, 0x1E220809, 1},
};

/* ToTK: the quality level a frame takes is chosen by a frame-time check. The branch becomes unconditional, so every
   frame takes the level that keeps the distant detail. */
static const SnxBuiltinPatch totk_lod_patches[] = {
    {0x9B4E43650501A4D4ULL, 0x00C7EB4C, 0x140000C0, 0x5400180C, 1},
};

const SnxBuiltinPatch* snx_builtin_plugin_patches(int index, int* count) {
    if (index == 0) {
        *count = (int)(sizeof totk_patches / sizeof totk_patches[0]);
        return totk_patches;
    }
    if (index == 1) {
        *count = (int)(sizeof totk_lod_patches / sizeof totk_lod_patches[0]);
        return totk_lod_patches;
    }
    *count = 0;
    return NULL;
}
