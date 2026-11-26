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
- [ ] (Replaced in 0.11 by **Help**.) **Game loot filter settings** closed the editor and opened the game's own screen (the log says "asked the game to open its loot filter screen", then "OpenLootFilter passed to the game"). Choose another level there, open the editor again: the level follows.
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

- [ ] **+ Add a rule** in the editor's top bar opens the steps. The step chips along the top jump between steps (steps that do not apply are greyed out); LB / RB go back and forth with a controller.
- [ ] 1 What happens: **Hide**, **Show** or **Always Show (all strictness levels)**.
- [ ] 2 Kind: White and superior bases / Magic items / Rare items / Unique items / Set items / Runes, gems and other items. Choosing one goes straight to its items.
- [ ] 3 Items: only items of that kind. Bases, magic and rare: groups on the left (only bases that come in that rarity: rings and amulets for magic and rare, not for bases). Unique: Normal / Exceptional / Elite tier, rings, amulets, charms and jewels, then Ancient Coupons by Normal / Exceptional / Elite. Set: one list per set. The search finds items across all lists.
- [ ] 4 Tier (bases, magic, rare only): Normal / Exceptional / Elite the items have; Mythical bases with / without / only; White and Superior for bases. Skipped when there is nothing to choose.
- [ ] 5 Bonuses (bases with only Superior ticked, and magic): the bonuses they can roll, by their real names ("+4 to Teleport", "Level 20 Might Aura When Equipped", no "item_aura" or "item_nonclassskill"). Tick one, set "At least". Rare, unique and set: no bonuses step.
- [ ] Magic, tick a bonus, then go back and choose Unique: no crash, and nothing of the magic choice is left.
- [ ] 6 Levels (Hide and Show only; Always Show is on every level).
- [ ] 7 Look (Show and Always Show only): border, background and name colour (buttons in their colour, or None / the game's), a light beam and its colour, a drop sound with **Play**, a chat line (`{name}` is the item's name) in a colour. **Any colour for** Border / Background / Light beam opens a colour square and hue strip (mouse drag; arrows, Page Up / Down; D-pad). The picture on the right shows the label as it will look and updates at once.
- [ ] The sound preview plays at the volume the game will use (the loot filter's `file_sound_volume`, 80 unless changed, times the filter volume). Choosing a sound plays it once.
- [ ] 8 the rule in words. **Add rule**: back to the list, where the rule shows as yours ("U001").
- [ ] In game: a hide rule hides even what Sully's filter always shows; a Show / Always Show rule keeps an item Sully hides, with its look (the look is last in the filter so its colours win), and is never hidden, even by another of your own hide rules.
- [ ] A rule with bonuses leaves unidentified items alone.
- [ ] Your rule's card: **Rule is on**, **Change this rule** (the steps again, filled in; a Show rule on every level comes back as Always Show, which does the same), **Delete this rule** (asks; Cancel is first).
- [ ] The edit-item key on a ground item some rules match: a screen names the item, says whether it is shown or hidden (and by which rule), and lists every rule that matched it with what it does ("hides it" in red for the hiding one). **Open >** (click, Enter or A) opens the rule's card. A rule whose title has no id is listed but does not open. **Add a new rule for this item** opens the steps with the item chosen.
- [ ] The edit-item key on an item no rule matches: the steps open straight away, with the item chosen and its kind set. A unique or set item opens on that exact item; an Ancient Coupon as a unique. On a Superior item with a bonus, the bonus is ticked at its value. On an unidentified item, no stats. Nothing under the cursor: nothing happens. Right after the game starts (data still loading) it still opens, a moment later.
- [ ] Naming a version: on an author's filter (Sully's), the first change (a rule turned off, a new rule, the volume) opens **Your own version** with a name already filled in ("Sully's QoL Filter (my version)"). Change it or keep it, then **Make my version**: the game switches to it, and it shows under that name in the Filter list. A name with \ / : * ? " < > | or one you already use is refused with a reason. **Cancel** changes nothing.
- [ ] Several versions: choose Sully's filter again in the Filter list and change something. The same screen offers a new name and, under it, the versions you already have: pick one to put the change into it, or name a new one. Each version keeps its own changes; Sully's updates come through to all of them.
- [ ] **Filter** drop-down in the top bar (where the level was): every filter the loot filter can load (yours, the repository ones, the mod's), the one in use chosen. Choosing another switches the game to it; the editor shows it a moment later. The line under the bar names the level in use, and rules off on that level stay grey.
- [ ] **Filter volume** on the main window: drag it, or Left / Right, or the D-pad. It is saved in your version when you let go (the first time it asks to make your version; no puts it back) and plays a sound at the new volume. **Test** plays it again. The volume is the filter's `soundVolume`: every sound of the filter gets louder or quieter.
- [ ] Your version's file: a "My Rules" part first (your shows, the show-only copy of each Show / Always Show rule with a look, then your hides) and a "My Highlights" part last.

## Milestone 5: a new filter from scratch (test build)

Build and install with `build-and-install.bat` (it now also copies `data\new-filter` into `esr-filter-editor\new-filter` next to the DLL).

- [ ] **Filter** drop-down → **+ New filter…** opens the questions. The drop-down itself stays on the filter in use. The step chips jump between the ten steps; LB / RB go back and forth.
- [ ] 1 Name: a name ("My filter", or "My filter 2" when taken) and an author. A name that cannot be a file name, or one you already use, shows why under it and Create stays off. Start from **Recommended** (everything Sully's filter does) or **Minimal** (the valuables always shown, little hidden).
- [ ] 2 Levels: 1 to 5 levels; keep Sully's names (their translations stay) or type your own; the level a new character starts on.
- [ ] 3-7 the parts (Always show, Gold and potions, Gems and odds and ends, Gear, Uniques sets jewellery coupons, Announcements and labels). Each part is a line with a tick (click to choose it, Enter or A turns it on or off); its steps are under it. Choose a step: on or off, from level / to level, from character level, gold amount (gold steps), in towns too (hide steps). **+ Add a step like this** adds a copy below it (yours: **Remove this step**).
- [ ] 8 Look: a label style on the left (Uniques, Set items, Mythicals, Mid runes, ...), its border, background and light beam (a colour button, **As it is**, or **Any colour for** with the colour square). The picture shows it. Filter volume with **Test**.
- [ ] 9 Check: the filter in words and its rule count. **Create the filter**: the game switches to it, the editor shows it, and it is in the Filter list under its name.
- [ ] In game: the new filter works like Sully's (Recommended, nothing changed). With 3 levels, the level-4-only rules are gone; the levels menu shows your names.
- [ ] Sounds play (copied from Sully's filter). Icons show when Sully's QoL-Filter folder is installed; without it the filter has no icons, and no errors.
- [ ] The made filter takes your own rules and changes like any of your versions (it needs no naming: it is yours).

## 0.9: style icons, step names, inventory items (test build)

Build and install with `build-and-install.bat` (it builds the loot filter from the fork too: the bridge is version 2).

- [ ] The add-rule steps read: 1 What happens, 2 Item Type, 3 Items, 4 Tier, 5 Stats, 6 Strictness, 7 Style, 8 Check. The new-filter questions say Strictness and Style.
- [ ] Style: no colour square any more; the colour buttons are wider and their names fit (Set, Magic, Unique, Rare for the name colours).
- [ ] Style: **Minimap icon** lists the filter's icons (Sully's: Myth, charm, cube, gem, gem2, gemP, goldstar, map, redD, rune, scroll, star); **Size** 8-64 px (27 to start). The Minimap picture shows the icon at that size next to the player's dot. In game the item gets that icon on the minimap. Changing the rule later shows the icon and size again.
- [ ] Edit-item key on an item in the inventory, stash, cube, equipped or at a vendor: the editor opens on it. With rules matching: "In your inventory, stash or cube. If it dropped here: Shown / Hidden by R…". Without: the steps, the item chosen. The log line says location=1 (2 equipped, 3 vendor).
- [ ] With the game's comparison tooltip showing (your equipped item beside it), the key still takes the item under the cursor, not the equipped one.
- [ ] The key works while the inventory, stash or a vendor is open (if it does nothing there, say so: the editor's key may not reach it while a panel is open).
- [ ] Ground items work as before.

## 0.10: the edit-item key asks what you want (test build)

- [ ] Point at an item (ground, inventory, stash, cube, equipped, vendor) and press the edit-item key. **This item** opens: its name, what the filter does with it now ("Shown.", "Shown: R010 never hides it.", "Hidden by R121."), and **The rules that match it (N)** (the old list, each rule opening its card).
- [ ] **What do you want to do?** A shown item offers Hide / Change the style / Always show; a hidden one Stop hiding / Always show / Change the style, and **Or turn off R…** (turns off the rule hiding it, in your version).
- [ ] **Which items?** From the narrowest to the broadest, with how many items each is: a superior base with its stats → that base → its group (Staves) → that tier in Weapons → every white and superior weapon; a magic or rare item → its base → its group (Rings) → Rings & Amulets → every magic item; a unique → only it → every unique on that base → its group → its tier → every unique; a set item → only it → the whole set → every set item; an Ancient Coupon → it → its tier → every coupon; a rune → it → it and every higher rune of its family (Ber: Ber, Jah, Cham, Zod; none for the special runes) → every rune.
- [ ] **On which strictness levels?** This level only / This level and stricter ones (the start for Hide) / Every level (the start for showing and a style). Always show has no levels.
- [ ] A line under it says how it meets the filter: "R010 never hides it now. Your rule comes first, so it hides them anyway." and the like.
- [ ] **Hide them** / **Show them** / **Always show them** make the rule (your version is made first if you have none) and go back to the rules: it is there as yours. **Choose the style…** opens the steps on Style, filled in. **More options…** opens the steps on the items, filled in; ticking items there replaces a broad choice.
- [ ] In game: "every superior elite-tier item in Weapons" hides superior elite weapons but not exceptional ones or armour; "every magic item" shows every magic item; "Ber and every higher rune" shows Jah but not Sur.

## 0.11: Help

- [ ] **Help** in the top bar (where **Game loot filter settings** was) opens a guide: Getting around, The edit-item key, Changing a rule, Adding a rule, Your own versions, A new filter, Good to know. Choose a topic on the left (mouse, arrows, D-pad); its page scrolls on the right. Back returns to the rules.
- [ ] The game's own loot filter screen is no longer reachable from the editor. Its jobs: the filter is chosen in the editor's Filter list; the level with the loot filter's **Next Filter Level** key.

## 0.12: set item tiers, rune colours

- [ ] Add a rule → Set items: the lists start with Normal tier, Exceptional tier, Elite tier, and Rings, amulets, charms, jewels; then **By set**, one list per set. Searching finds each item once.
- [ ] Add a rule → Runes, gems and other: runes are listed by the colour of their names (Runes: White, Red, Yellow, Orange, Green, Gold, Purple, Turquoise) and **Runes: classic and others** (El to Zod). Hu is with the green runes, as the game shows it.
- [ ] Edit-item key, a set item: Only it → the whole set → every set item of its tier → every set item.
- [ ] Edit-item key, an ESR rune: Only it → it and every higher rune (for I to Wo) → every rune of its colour → every rune. A classic rune: … → every classic rune (El to Zod). An Ancient Coupon: … → every normal-tier (exceptional-tier, elite-tier) coupon → every coupon.

