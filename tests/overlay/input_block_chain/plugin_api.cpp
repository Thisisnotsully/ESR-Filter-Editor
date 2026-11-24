// One stand-in "plugin": its own copy of input_block.cpp, with an open/closed switch for its window.
#include "input_block.hpp"
#include "debug_log.hpp"
#include <atomic>
#include <cstdio>
#ifndef BLOCK_NS
#define BLOCK_NS esrfe::overlay
#endif
static std::atomic<bool> gOpen{false};
extern "C" __attribute__((visibility("default"))) bool TestInstall() {
    BLOCK_NS::DebugSink() = [](const std::string& m) { std::printf("   log: %s\n", m.c_str()); };
    return BLOCK_NS::inputblock::Install([] { return gOpen.load(); });
}
extern "C" __attribute__((visibility("default"))) void TestRemove() { BLOCK_NS::inputblock::Remove(); }
extern "C" __attribute__((visibility("default"))) void TestSetOpen(bool v) { gOpen = v; }
