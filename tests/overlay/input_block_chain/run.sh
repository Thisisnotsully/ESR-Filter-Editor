#!/bin/sh
# Runs the real controller-block stubs from two plugins' copies of input_block.cpp against a stand-in game (x86-64
# Linux, the stubs' Windows calling convention via ms_abi). Usage: run.sh [second input_block.cpp dir] [its namespace]
# With no arguments the editor's copy plays both plugins; pass the wiki's src folder and esrwiki to test the pair.
set -e
here=$(cd "$(dirname "$0")" && pwd); src="$here/../../../src/overlay"; out=$(mktemp -d)
other=${1:-$src}; ns=${2:-esrfe::overlay}
build() {  # dir namespace output
    sed 's/extern "C" bool EsrBlock/extern "C" __attribute__((ms_abi)) bool EsrBlock/' "$1/input_block.cpp" > "$out/ib_$3.cpp"
    g++ -std=c++20 -O1 -fPIC -shared -fvisibility=hidden -DBLOCK_NS="$2" -I"$here/inc" -I"$1" \
        "$out/ib_$3.cpp" "$here/plugin_api.cpp" -o "$out/$3.so"
}
build "$src" esrfe::overlay plugin_a
build "$other" "$ns" plugin_b
g++ -std=c++20 -O1 -rdynamic "$here/main.cpp" -ldl -o "$out/chain"
cd "$out" && ./chain
