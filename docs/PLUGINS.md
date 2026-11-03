# Plugins

A plugin changes a game while the game runs. ScarletNX does not recompile the game, and the plugin makes no ExeFS
patch. The launcher's **Settings > Plugins** lists the plugins and turns each one on or off. Every plugin is off until
it is turned on. Turning one on asks once and names it.

The first plugin, `ToTK`, is built into ScarletNX itself. It needs no folder and no file beside the game.

Plugins can crash the game, corrupt a save and stop working between a game or ScarletNX update. Expect breakage.

## Extra plugins

A folder in the data folder's `plugins/` adds a plugin that is not built in. One folder is one plugin. A folder whose
ID is the same as a built-in plugin is left out, so a built-in plugin always has the patches that came with this
version of ScarletNX.

```
plugins/Another/
  plugin.ini
  082CE09B06E33A12.patch
  9B4E43650501A4D4.patch
```

`plugin.ini`:

```ini
[plugin]
name = Another
title = Another Game
titles = 0100000000000000
versions = 1.0.0, 1.2.1
```

- `name` is the row's name. Without it the folder's name is used.
- `title` is the game's name, and `titles` is the title IDs the plugin patches, in hex, a comma or a space between
  two. A game that is not named is left as it is, and the log says so.
- `versions` is what the launcher shows. It is for the player: the runtime matches the build, not this line.

A patch file is named after the first 8 bytes of the main module's build ID, 16 hex digits. Its lines are:

```
# offset instruction-now instruction-the-plugin-writes
0103474C 14000015 A9BE7BFD
```

All three values are hex. The offset is from the start of the main module. The instruction is its A64 word. The third
value is optional. When it is there and the game does not have that instruction, the plugin is not applied at all, and
the log says which instruction it found. When the build ID has no patch file, the log says the plugin does not know
that version, and the game is left as it is.

The runtime writes the instructions before it makes the module read-only. It then tells the runtime's code
invalidation that those lines changed, as the game's own `IC IVAU` does (`docs/ARCHITECTURE.md`, "Code the game
patches"). The functions compiled from those lines leave dispatch, and the JIT compiles the patched code when the
game reaches it.

## ToTK

Two built-in plugins target update 1.2.1. Neither needs a `plugins/` folder.

- `ToTK (Disable DynamicRes)` patches the two multiplies at `0x9434E0` and `0x9434E4` that apply the game's
  dynamic-resolution scale to the scene viewport, so the viewport keeps the view's base size.
- `ToTK (Disable DynLOD)` patches the frame-time quality branch at `0x00C7EB4C`, so the frame takes the quality
  level that keeps the distant detail.

The offsets come from the game's own code and from the community's "Visual Fixes" and "Remove QuaReduction" patches.
The base game 1.0.0 is not patched yet: its offsets are not known, so the log says the plugin does not know that
build and the game is left as it is.

Status: both plugins write their instructions and the runtime invalidates the code as for any plugin, but on
ScarletNX the game keeps its own 30 Hz frame cadence, so its frame-time logic did not lower the visible resolution in
the scenes tested (the tutorial island, including one host CPU core and a 20 FPS host cap). Only a secondary 960x540
pass changed, and it does not reach the screen. They need a scene, a build or a run where the game's own logic fires
before they can be called verified.
