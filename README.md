# ESR Filter Editor (milestone 1)

This is a small D2RLoader plugin for Diablo II Resurrected.
It is the first step toward an in-game loot filter editor.
There is no window yet.

## What it does

You bind one key in the Controls menu.
Point at a ground item with the mouse, or target it with a controller, and press the key.
The plugin asks the loot filter plugin about that item.
Then it prints three lines in the chat window:

1. The item: name, code, quality, tier, item level, sockets, ethereal, and "not in Chronicle" or "in Chronicle" (needs the Chronicle Tracker).
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
- Visual Studio 2026 (the "Desktop development with C++" part) and CMake 3.29 or newer.
- The folder `d2rloader_plugins` next to this folder.
  If it is somewhere else, see the options below.

## Build (Windows)

Open the "x64 Native Tools Command Prompt for VS 2026" and go to this folder.

    cmake -S . -B build -G "Visual Studio 18 2026" -A x64
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
- [ ] Stats, identified Magic or Superior item: a fourth chat line says "Stats: N (listed in the log)". In the log, the line "hovered item stats" lists each stat as stat,param=value. Check that a few values match the item's tooltip (for example its Faster Cast Rate or Life).
- [ ] Stats, unidentified item: the first line ends with "unidentified" and the stats line says "none, unidentified".
- [ ] The specialization that shows Magic items as identified: with it, does a Magic item on the ground say "unidentified" or not, and does it list stats? Note which.
- [ ] Nothing hovered: press the key with nothing under the cursor. Nothing happens, no message.
- [ ] Loot filter without the bridge: press the key. One console line says the bridge is not available. It is not repeated for the same problem.
- [ ] Console command: open the console and type `lfeditor`. It says whether the bridge is acquired, its version, and the filter state (loaded, title, id, level, Show Hidden, hover, rule count, errors, warnings).
- [ ] Check the plugin log: each key press on an item adds one long line with every field and rule.
- [ ] Chronicle, not found yet: point at a unique, set item or runeword your character has not found yet. Line 1 ends with "not in Chronicle". The log line has `chronicle=1`.
- [ ] Chronicle, found: point at one your character has found. Line 1 ends with "in Chronicle" (`chronicle=2`).
- [ ] Chronicle, other items: a magic, rare or plain item shows neither (`chronicle=0`).
- [ ] Chronicle filter condition: put this rule near the top of your filter's rules and save:

      { "comment": "Not in Chronicle", "chronicle": "missing", "show": true, "labelBorder": { "color": [255, 215, 0], "width": 2 }, "notify": true }

  A unique, set item or runeword you have not found gets a gold border and a chat line when it drops. One you have found does not.
  The key lists the rule (R1 if it is first) for the first kind and not for the second.
  A later rule with its own labelBorder replaces the border.
- [ ] Without the Chronicle Tracker: remove its DLL (or set `enabled = false` in its config) and start the game. No item says "in Chronicle" or "not in Chronicle", and the rule above never matches. Once an item has dropped, type `lootfilter` in the console: it says the chronicle service is not acquired, so chronicle conditions do not match. The plugin log has one warning about it, not one per item.

## Notes

- The chat lines go through the loot filter's own chat. If the launcher's chat notifications switch is off, they do not show; the plugin log still has them.

- The plugin asks for the bridge the first time you press the key, not when the game starts.
  This is because plugins load in no fixed order.

## Finding a game button's message (lfeditor-uilog)

Type `lfeditor-uilog` in the console to turn the UI message log on, click a
button (for example the pause menu's Loot Filter button), then run the command
again to turn it off. The plugin log then has lines like
`Loot Filter Editor: UI message PanelManager:OpenPanel:...`. Nothing is
changed or blocked; the log only records what the game sends.

## Milestone 2: the window (test build)

Build and install with `build-and-install.bat` (game closed), then in game:

- [ ] Bind "Open Filter Editor" in Controls (group Loot Filter). The key opens the window; the key or Escape closes it.
- [ ] The pause menu's **Loot Filter** button opens the window instead of the game's screen.
- [ ] `lfeditor-gamefilter` in the console opens the game's own loot filter screen.
- [ ] The demo window: the list of 1000 rows scrolls (wheel, touchpad), the search box filters it, toggles, the drop-down, the number box (-/+, typing) and the confirm dialog work with the mouse and the keyboard.
- [ ] With a controller: the D-pad reaches every control, the left stick moves the cursor and A clicks, the on-screen keyboard types into the search box, B goes back, the help window shows the buttons. Xbox and PlayStation if you have both.
- [ ] The window follows the game window (move, resize, minimise, alt-tab), and looks right at your display scaling.
- [ ] While the window is open the game ignores the controller (the log says "controller block installed").
- [ ] `lfeditor-selftest` in the console: the filter is "read" and "saved back byte for byte", and the mod data is "read", with the times.

Send the console lines and the plugin log lines starting with `Loot Filter Editor:`.

## Milestone 2b: browsing the filter (test build)

Build and install with `build-and-install.bat` (game closed). Then, in game, with Sully's QoL Filter loaded:

- [ ] Open the editor. For about a second it may say "Reading the mod's item data…". Then it shows the filter: its title at the top, the parts and sections on the left, and the rules on the right. The log says "the mod's data is read (N ms ...)".
- [ ] Each rule shows a coloured action (Hide, Always show, Highlight, Rename), its items by name, and when it applies. Read a few and check they match what the rule does.
- [ ] The big topics (Protection, Drop Announcements, ...) start closed. A click, Enter or A opens or closes one; its sections show under it. Choosing a topic or a section (or LB / RB on a controller) shows only its rules. "All rules" shows all 729. Hovering a topic or section explains it; nothing else has a tooltip.
- [ ] Type into the search box (for example `maximum curses`, `zod`, `outside towns`, `R125`). Only rules whose own words have it stay; topics and sections without a match leave the left list, the others count their matches. In a long item list the item found comes first.
- [ ] The Level drop-down starts on the level the game uses, marked "(in use)". Rules for other levels are greyed out with "Not on this level". "Every level" shows them all normally.
- [ ] Open a rule (double click, Enter or A). Its card lists every item, every condition and every effect. A renamed item shows its new name in the game's colours. Back returns to the list.
- [ ] No key legend under the window.
- [ ] **Game loot filter settings** closes the editor and opens the game's own screen (the log says "asked the game to open its loot filter screen", then "OpenLootFilter passed to the game"). Choose another level there, open the editor again: the level follows.
- [ ] Edit the filter file in a text editor while the game runs, save it, then open the editor again: it shows the change.
- [ ] `lfeditor-demo` still opens the controls demo over the list.
