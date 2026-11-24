# ESR Filter Editor: architecture

An in-game editor for loot filters of the celestialrayone loot filter plugin,
for Eastern Sun Resurrected. A D2RLoader plugin DLL
(`d2rl-sully-loot-filter-editor.dll`, plugin id `sully.loot-filter-editor`).

## Pieces

| Folder | What | Depends on | Tested |
|---|---|---|---|
| `src/document/` | The filter file as a document: reads it with every comment, blank line and heading kept, exposes parts, sections and rules, applies edits, writes it back. An untouched file is written back byte for byte. | standard C++ only | on Linux, `tests/` |
| `src/gamedata/` | The mod's tables and strings, read from the mod's data folder: item types, bases (and Mythicals), uniques, sets, affixes, superior bonuses, stats with their tooltip wording, in the player's language. | standard C++ only | on Linux, against the real mod files |
| `src/overlay/` | The window: a Win32 overlay on its own UI thread, taken from the ESR Wiki overlay (window, DPI scaling, cursor, controller, on-screen keyboard, input block, UI strings). Knows nothing about filters: it hosts views. | Win32, GDI/GDI+, XInput, HID | in game |
| `src/ui/` | The editor's views (browse, rule card, add-rule steps) drawn with the overlay's toolkit, on top of document + gamedata. | the three above | in game, plus logic tests on Linux |
| `src/plugin.cpp` | D2RLoader entry: services, key bindings, console command, the bridge lease, opening the window. | the SDK | in game |
| `tools/filtercheck/` | Checks for Sully's QoL Filter (see its README). | g++, Python | Linux |

The loot filter plugin's editor bridge (`editor_bridge_api.h` in the
loot-filter source) gives the hovered item, its matched rules and stats, and
the loaded filter's state. It is the only link to the loot filter.

## Rules

- `document` and `gamedata` use only standard C++20, no Windows headers, so
  they build and are tested on Linux with g++/clang. Text is UTF-8
  `std::string` everywhere; the overlay converts to UTF-16 at the Win32 edge.
- Threads: the overlay has its own UI thread. Game-thread work (the bridge,
  D2RLoader services) is queued to the game thread; results come back as
  plain copies. `document` and `gamedata` objects belong to the overlay thread.
- The editor never changes the loot filter's behaviour: it edits files, and
  the loot filter's file watcher reloads them.
- No `disabledRules` in files the game loads: the loot filter warns about
  unknown top-level settings. A rule the player switches off is left out of
  the merged copy (downloaded filters), or listed in a small file next to the
  filter (filters the player made). Files that already have `disabledRules`
  (the web editor's scheme) are still read, those rules shown as off.
- The plugin's data folder is `<mod>\d2rloader\plugins\esr-filter-editor`
  (images, locales). The window's images are the game's own art and are not
  kept in this repository; the install script copies them from the ESR Wiki's
  installed folder.
- The pause menu's Loot Filter button sends `PausePanelMessage:OpenLootFilter`;
  the editor consumes it and opens its window. The editor can send the same
  message on with a pass-through flag to open the game's own loot filter screen.
- Where the mod's data lives: `<game>\mods\<mod>\<mod>.mpq\data\global\excel\*.txt`
  and `...\data\local\lng\strings\*.json` (ESR ships the mod as a folder).
- Translations: the editor's own texts in `locales\<code>\ui-strings.txt`
  (one `English<TAB>text` line each, English as the fallback), as in the wiki
  overlay; game texts (item, stat, skill names) come from the game's strings.
  The 13 languages: enUS deDE esES frFR itIT koKR plPL ruRU zhCN zhTW esMX jaJP ptBR.
- Style: C++20, `auto f() -> T`, short plain comments, no exceptions across
  the C boundary (`noexcept` entry points that catch everything).

## Filter conventions the editor follows

See the project's `filter-maintenance.md`: permanent `R###` ids for the
author's rules, `U###` for a player's own rules, players' changes kept
separately and re-applied on updates.
