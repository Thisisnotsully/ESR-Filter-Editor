#!/bin/sh
# Checks a filter: the loot filter plugin's own reader and engine (check.cpp),
# then the filter's conventions (lint.py).
#
#   tools/filtercheck/check.sh <filter.json> <mod excel dir> <loot-filter source dir>
#
# <mod excel dir>: a copy of the mod's data/global/excel (weapons, armor, misc,
#   itemtypes, itemstatcost, sounds, skills .txt). Not kept in this repository.
# <loot-filter source dir>: d2rloader_plugins/plugins/loot-filter.
# The filter's icons and sounds folders are looked up next to the filter.
# Exits non-zero when either check finds a problem.
set -e
here=$(cd "$(dirname "$0")" && pwd)
filter=$1; data=$2; src=$3
[ -n "$src" ] || { echo "usage: $0 <filter.json> <mod excel dir> <loot-filter source dir>"; exit 2; }
bin="$here/build/check"
mkdir -p "$here/build"
if [ ! -x "$bin" ] || [ -n "$(find "$here/check.cpp" "$src" -newer "$bin" -name '*.cpp' -o -newer "$bin" -name '*.h' 2>/dev/null | head -1)" ]; then
    g++ -std=c++20 -O1 -D__cdecl= -I"$src" "$here/check.cpp" "$src/filter_program.cpp" "$src/filter_engine.cpp" \
        "$src/formula.cpp" "$src/stat_values.cpp" "$src/json_value.cpp" -o "$bin"
fi
status=0
"$bin" "$filter" "$data" "$(dirname "$filter")" "$here/scenarios.txt" || status=1
python3 -I "$here/lint.py" "$filter" "$data" || status=1
[ $status -eq 0 ] && echo "ALL CHECKS PASSED" || echo "CHECKS FAILED"
exit $status
