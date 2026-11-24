# ESR Filter Editor (milestone 1)

This is a small D2RLoader plugin for Diablo II Resurrected.
It is the first step toward an in-game loot filter editor.
There is no window yet.

## What it does

An in-game loot filter editor for Eastern Sun Resurrected, in a window over the game (mouse, keyboard and controller).

- **Browse** the loaded filter: its parts and sections, every rule in plain words, a search, the filter levels.
- **Change** the author's rules: on or off, hide / always show, filter levels, single items. The changes go into the
  player's own version of the filter, which keeps getting the author's updates.
- **Add** rules in seven short steps: what happens (hide, always show, highlight), which items, tier, rarity, stats
  (only the bonuses the items can roll), filter levels, a last check.
- **The edit-item key**: point at a ground item (mouse or controller target) and press it. The editor opens on a new
  rule for that item: its base, tier and rarity chosen, and for Superior and Magic items its bonuses ticked at their
  values. Nothing under the cursor: nothing happens. `lfeditor-hoverchat` also prints the item in chat (for checking).
- The pause menu's **Loot Filter** button opens the editor; its **Game loot filter settings** button opens the game's own
  screen.

The author's filter is never written; see docs/ARCHITECTURE.md for the files the editor keeps.

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

- [ ] Mouse hover: point at a ground item, press the key. The editor opens on a new rule for it (Milestone 4). With `lfeditor-hoverchat` on, three lines also appear in chat.
- [ ] Controller target: target an item with the controller, press the key. Same.
- [ ] Show Hidden Items off: items the filter hides cannot be pointed at, so nothing happens.
- [ ] Show Hidden Items on: turn it on with the loot filter key. Point at a hidden item. Line 2 says "Hidden by R123 | ..." with the rule's own id from its title (a rule without an id in its title shows as "rule #N", its place in the file), or "Hidden: no rule matched...".
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
  The key lists the rule ("rule #1 Not in Chronicle" if it is first: that title has no id) for the first kind and not for the second.
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

## Milestone 3: changing rules (test build)

Build and install with `build-and-install.bat` (game and launcher closed). With Sully's QoL Filter loaded:

- [ ] Open a rule. On the right: **Rule is on**, **What it does** (Hide / Always show / Neither) on rules that hide or show, the **filter levels**, and the **items** (rules with two or more).
- [ ] Change something. The editor asks "Make your own version?". **Cancel** changes nothing. **Make my version**: the status says the game now uses your version, and the game's loot filter switches to "Sully's QoL Filter (my version)" at the same level (the loot filter log says it loaded it).
- [ ] In `Saved Games\Diablo II Resurrected\mods\Eastern_Sun_Resurrected\LootFilters`: `Sullys-QoL-Filter (my version).json`, `esr-filter-editor\Sullys-QoL-Filter.changes.json`, and the `sounds` and `icons` folders.
- [ ] Switch a rule off: it is gone from the game (an item it hid shows, an item it labelled is plain). In the list it shows **Off**, greyed.
- [ ] Change a hide rule to **Always show** and drop that item: it shows. Untick one item of a long list: only that one changes.
- [ ] Untick every level, or the last item: the editor refuses and says why.
- [ ] **Undo my changes to this rule** puts Sully's version back. Changed rules say "changed" in the list.
- [ ] Sounds and minimap icons still work with your version.
- [ ] Choose Sully's original filter in the launcher and open the editor: your changes are still there; the next change switches back to your version.
- [ ] Update check (I can make the test file): put an older or newer Sully filter in `QoL-Filter\`, start the game: the log says your copy is up to date. If a rule you changed was changed by Sully too, opening the editor asks; Escape keeps yours.

## Milestone 4: adding rules (test build)

Build and install with `build-and-install.bat` (game and launcher closed).

- [ ] **+ Add a rule** in the editor's top bar opens the steps. The step chips along the top jump between steps; LB / RB go back and forth with a controller.
- [ ] 1 What happens: Hide them / Always show them / Highlight them. Highlight shows the colours, the light beam and the chat line.
- [ ] 2 Items: a group on the left, its items on the right (tick with a click, Enter or A). The search finds items by name across every group. "Chosen (N)" lists what is ticked.
- [ ] 3 Tier: only Normal / Exceptional / Elite the items have; Mythical bases: with, without, only (only when a Mythical is ticked). The step is skipped when there is nothing to choose.
- [ ] 4 Rarity: only the rarities the items come in.
- [ ] 5 Stats: with only Superior (or only Magic) ticked, the bonuses those items can roll; tick one, set "At least". With both, or neither, it says what to choose.
- [ ] 6 Levels, 7 the rule in words. **Add rule**: back to the list, where the rule shows as yours ("U001").
- [ ] In game: a hide rule hides even what Sully's filter always shows; an always-show rule keeps an item Sully hides; a highlight keeps its colour (it is last in the filter) and is never hidden.
- [ ] A rule with stats leaves unidentified items alone.
- [ ] Your rule's card: **Rule is on**, **Change this rule** (the steps again, filled in), **Delete this rule** (asks; Cancel is first).
- [ ] The edit-item key on a ground item: the steps open with the item chosen. On a Superior item with a bonus, the bonus is ticked at its value. On an unidentified item, no stats. Nothing under the cursor: nothing happens. Right after the game starts (data still loading) it still opens, a moment later.
- [ ] Your version's file: a "My Rules" part first (your hides and shows, and a show-only copy of each highlight) and a "My Highlights" part last.
