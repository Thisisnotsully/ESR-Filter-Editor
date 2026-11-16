# Loot Filter Editor (milestone 1)

This is a small D2RLoader plugin for Diablo II Resurrected.
It is the first step toward an in-game loot filter editor.
There is no window yet.

## What it does

You bind one key in the Controls menu.
Point at a ground item with the mouse, or target it with a controller, and press the key.
The plugin asks the loot filter plugin about that item.
Then it prints three lines in the chat window:

1. The item: name, code, quality, tier, item level, sockets, ethereal.
2. Whether the filter shows or hides it, and which rule hides it.
3. The rules that match it (the first three, with a "+N more" if there are more).

Example:

    Filter editor: Colossus Blade (7gd) - Unique - Elite - ilvl 85 - ethereal
    Shown
    Rules: R4 Uniques; R12 Ethereal bases; R30 Keep all weapons

If nothing is under the cursor, nothing happens.
Everything is also written to the plugin log, with full rule titles.

The plugin only reads.
It never changes the filter or the game.
It hooks nothing.

## What you need

- D2RLoader with the plugin SDK this repo uses.
- The loot filter plugin, built with the editor bridge.
  A loot filter without the bridge will not work with this plugin.
- Visual Studio 2022 (the "Desktop development with C++" part) and CMake 3.29 or newer.
- The folder `d2rloader_plugins` next to this folder.
  If it is somewhere else, see the options below.

## Build (Windows)

Open the "x64 Native Tools Command Prompt for VS 2022" and go to this folder.

    cmake -S . -B build -G "Visual Studio 17 2022" -A x64
    cmake --build build --config Release

Or with Ninja:

    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build build

The DLL is `d2rl-sully-loot-filter-editor.dll`.
Look for it in `build\Release` (Visual Studio) or `build` (Ninja).

Options you can add to the first command with `-D`:

- `-DD2R_MOD_DIR="F:/Diablo II Resurrected/mods/MyMod"` copies the DLL into the mod after every build.
- `-DD2RL_SDK_DIR=...` is the PluginSDK folder.
- `-DLOOT_FILTER_SRC_DIR=...` is the loot filter source folder (it holds `editor_bridge_api.h`).

## Install

Copy the DLL into `<your mod>\d2rloader\plugins`.
Put it next to the loot filter DLL.
That loot filter DLL must be the build with the editor bridge.

## Bind the key

1. Start the game.
2. Open Options, then Controls.
3. Find the group "Loot Filter".
4. Bind "Edit Hovered Item (Filter Editor)" to a key.
   It has no default key.

## Test checklist

- [ ] Mouse hover: point at a ground item, press the key. Three lines appear in chat.
- [ ] Controller target: target an item with the controller, press the key. Same three lines.
- [ ] Show Hidden Items off: items the filter hides cannot be pointed at, so nothing happens.
- [ ] Show Hidden Items on: turn it on with the loot filter key. Point at a hidden item. Line 2 says "Hidden by R..." (or "Hidden: no rule matched...").
- [ ] Item labels up: hold Alt (or turn on the game's "always show items") and point at an item's name label, not the item itself. Note whether the key still works. The loot filter's hover hook may only see the item, not its label; this test tells us.
- [ ] Two items lying on top of each other: point at one, press the key, and check the chat names the item you meant.
- [ ] Nothing hovered: press the key with nothing under the cursor. Nothing happens, no message.
- [ ] Loot filter without the bridge: press the key. One console line says the bridge is not available. It is not repeated for the same problem.
- [ ] Console command: open the console and type `lfeditor`. It says whether the bridge is acquired, its version, and the filter state (loaded, title, id, level, Show Hidden, hover, rule count, errors, warnings).
- [ ] Check the plugin log: each key press on an item adds one long line with every field and rule.

## Notes

- The chat lines go through the loot filter's own chat. If the launcher's chat notifications switch is off, they do not show; the plugin log still has them.

- The plugin asks for the bridge the first time you press the key, not when the game starts.
  This is because plugins load in no fixed order.
