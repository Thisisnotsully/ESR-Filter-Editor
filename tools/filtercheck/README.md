# Filter checks

Run before every change to Sully's QoL Filter goes to GitHub.

    tools/filtercheck/check.sh <Sullys-QoL-Filter.json> <mod excel dir> <loot-filter source dir>

It runs two checks and says `ALL CHECKS PASSED` only when both pass:

1. **The plugin's own reader and engine** (`check.cpp`): the filter must load
   with 0 errors and 0 warnings, against the mod's real item types, stats,
   sounds, skills, icons and sound files. Then every test item in
   `scenarios.txt` goes through the engine and must be shown or hidden, and
   matched or not matched by the rules it names.
2. **The filter's conventions** (`lint.py`):
   - every rule title is `R### | what it does | action`, and each R### is used
     once (ids are permanent; it prints the next unused one);
   - a code list with Mythical bases has all 61 (all 56 weapons for a
     weapons-only rule), and never `7wd`;
   - every item code exists in the mod;
   - every translated text has all 13 languages.

`scenarios.txt` grows with each filter change: add the items the change is
about, with what should happen to them.

It needs a Linux shell with g++ (C++20) and Python 3. The mod's tables are not
kept in this repository; copy `data/global/excel` from the mod.

`filter_spans.py` is a helper for scripted edits: it finds each rule's text in
the file and rewrites code lists in their own layout, so a change only touches
the lines it is about.
