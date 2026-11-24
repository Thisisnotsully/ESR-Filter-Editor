# Controller block chaining test

The ESR Wiki and the Filter Editor both carry `input_block.cpp`. The second plugin to load finds the first one's stub at the game's call sites and chains in front of it.

`run.sh` builds two stand-in plugins from the real file, patches a stand-in game whose call sites and prologues match the game's bytes, and runs the real stub machine code. It needs Linux x86-64 and g++. It checks every combination of the two windows being open or closed, in both load orders, and on removal.

    tests/overlay/input_block_chain/run.sh                                  # editor + editor
    tests/overlay/input_block_chain/run.sh <wiki>/source/src esrwiki        # editor + wiki

A wiki build from before chaining existed passes only when it loads first. The editor chains onto it, but it cannot chain onto the editor.
