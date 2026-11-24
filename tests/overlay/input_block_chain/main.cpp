// Runs the real stubs from two copies of input_block.cpp (two "plugins") against a stand-in game.
#include <dlfcn.h>
#include <sys/mman.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <vector>

extern "C" { __attribute__((visibility("default"))) std::uint8_t* gFakeGameBase = nullptr; }

struct Record { std::uint32_t key, pressed, called; float x, y; std::uint32_t kind, axisCalled; };
static Record rec;

constexpr std::uintptr_t kSite = 0x13B3A2, kHandler = 0x13EDD0, kAxis = 0x13F470;
static void put(std::uint8_t* at, std::vector<std::uint8_t> b) { std::memcpy(at, b.data(), b.size()); }
static void put64(std::vector<std::uint8_t>& v, std::uint64_t x) { for (int i = 0; i < 8; ++i) v.push_back((x >> (8 * i)) & 0xFF); }

using Press = void(__attribute__((ms_abi))*)(unsigned, unsigned);
using Axis = void(__attribute__((ms_abi))*)(unsigned, float, float);

struct Plugin { void* h; bool (*install)(); void (*remove)(); void (*open)(bool); };
static Plugin load(const char* path) {
    Plugin p{};
    p.h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!p.h) { std::printf("dlopen %s: %s\n", path, dlerror()); std::exit(2); }
    p.install = (bool (*)())dlsym(p.h, "TestInstall");
    p.remove = (void (*)())dlsym(p.h, "TestRemove");
    p.open = (void (*)(bool))dlsym(p.h, "TestSetOpen");
    return p;
}

static int failures = 0;
static void check(bool ok, const char* what) { if (!ok) { ++failures; std::printf("FAIL: %s\n", what); } }

int main() {
    auto* base = (std::uint8_t*)mmap((void*)0x200000000ULL, 0x200000, PROT_READ | PROT_WRITE | PROT_EXEC,
                                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    gFakeGameBase = base;
    std::uint8_t* site = base + kSite;
    // caller: sub rsp,0x28 / call handler / add rsp,0x28 / ret   (the call is the site)
    put(site - 4, {0x48, 0x83, 0xEC, 0x28});
    std::int32_t rel = (std::int32_t)(kHandler - (kSite + 5));
    put(site, {0xE8, (std::uint8_t)rel, (std::uint8_t)(rel >> 8), (std::uint8_t)(rel >> 16), (std::uint8_t)(rel >> 24)});
    put(site + 5, {0x48, 0x83, 0xC4, 0x28, 0xC3});
    // handler: the real 21-byte prologue, record ecx/edx, the matching epilogue
    std::vector<std::uint8_t> h = {0x40, 0x55, 0x53, 0x57, 0x41, 0x55, 0x48, 0x8D, 0xAC, 0x24, 0xA8, 0xFD, 0xFF, 0xFF,
                                   0x48, 0x81, 0xEC, 0x58, 0x03, 0x00, 0x00};
    h.push_back(0x48); h.push_back(0xB8); put64(h, (std::uint64_t)&rec);           // mov rax, &rec
    for (auto b : {0x89, 0x08, 0x89, 0x50, 0x04, 0xC7, 0x40, 0x08, 0x01, 0x00, 0x00, 0x00}) h.push_back((std::uint8_t)b);
    for (auto b : {0x48, 0x81, 0xC4, 0x58, 0x03, 0x00, 0x00, 0x41, 0x5D, 0x5F, 0x5B, 0x5D, 0xC3}) h.push_back((std::uint8_t)b);
    put(base + kHandler, h);
    // axis dispatcher: the real 27-byte prologue, record xmm6/xmm7 (x, y) and ebp (kind), the matching epilogue
    std::vector<std::uint8_t> a = {0x40, 0x53, 0x55, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x50, 0x0F, 0x29, 0x74, 0x24, 0x40,
                                   0x8B, 0xE9, 0x0F, 0x29, 0x7C, 0x24, 0x30, 0x0F, 0x28, 0xF1, 0x0F, 0x28, 0xFA};
    a.push_back(0x48); a.push_back(0xB8); put64(a, (std::uint64_t)&rec.x);          // mov rax, &rec.x
    for (auto b : {0xF3, 0x0F, 0x11, 0x30, 0xF3, 0x0F, 0x11, 0x78, 0x04, 0x89, 0x68, 0x08, 0xC7, 0x40, 0x0C, 0x01, 0x00, 0x00, 0x00})
        a.push_back((std::uint8_t)b);
    for (auto b : {0x0F, 0x28, 0x74, 0x24, 0x40, 0x0F, 0x28, 0x7C, 0x24, 0x30, 0x48, 0x83, 0xC4, 0x50, 0x41, 0x5E, 0x5D, 0x5B, 0xC3})
        a.push_back((std::uint8_t)b);
    put(base + kAxis, a);
    std::uint8_t siteOriginal[5], axisOriginal[5];
    std::memcpy(siteOriginal, site, 5); std::memcpy(axisOriginal, base + kAxis, 5);

    auto press = (Press)(site - 4);
    auto axis = (Axis)(base + kAxis);
    auto run = [&](const char* label, bool blocked) {
        rec = {};
        press(7, 1);
        char m[200];
        std::snprintf(m, sizeof m, "%s: press %s", label, blocked ? "blocked" : "passed"); check((rec.called == 1) == !blocked && (blocked || (rec.key == 7 && rec.pressed == 1)), m);
        rec = {};
        press(7, 0);
        std::snprintf(m, sizeof m, "%s: release always passes", label); check(rec.called == 1 && rec.key == 7 && rec.pressed == 0, m);
        rec = {};
        axis(3, 0.5f, -0.25f);
        std::snprintf(m, sizeof m, "%s: stick %s", label, blocked ? "centred" : "passed");
        check(rec.axisCalled == 1 && rec.kind == 3 && (blocked ? (rec.x == 0 && rec.y == 0) : (rec.x == 0.5f && rec.y == -0.25f)), m);
    };

    for (int order = 0; order < 2; ++order) {
        Plugin first = load(order == 0 ? "./plugin_a.so" : "./plugin_b.so");
        Plugin second = load(order == 0 ? "./plugin_b.so" : "./plugin_a.so");
        run("nothing installed", false);
        check(first.install(), "first installs");
        first.open(false); run("first only, closed", false);
        first.open(true); run("first only, open", true); first.open(false);
        check(second.install(), "second installs (chains)");
        for (int s = 0; s < 4; ++s) {
            first.open(s & 1); second.open(s & 2);
            char label[64]; std::snprintf(label, sizeof label, "both, first %s second %s", (s & 1) ? "open" : "closed", (s & 2) ? "open" : "closed");
            run(label, s != 0);
        }
        first.open(false); second.open(false);
        if (order == 0) {  // second removes first, then first
            second.remove(); first.open(true); run("second removed, first open", true); first.open(false);
            first.remove(); run("both removed", false);
            check(std::memcmp(site, siteOriginal, 5) == 0 && std::memcmp(base + kAxis, axisOriginal, 5) == 0, "game bytes restored");
        } else {           // first removes first: its bytes are under the second's, so it leaves them
            first.remove(); first.open(true); run("first removed (inactive), first open", false);
            second.open(true); run("first removed, second open", true); second.open(false);
            second.remove(); run("both removed", false);
            check(std::memcmp(site, siteOriginal, 5) != 0, "site still points at the first's inactive stub (expected)");
        }
        // Reset the game bytes for the next order (a fresh process in real life).
        std::memcpy(site, siteOriginal, 5); std::memcpy(base + kAxis, axisOriginal, 5);
        dlclose(second.h); dlclose(first.h);
    }
    std::printf(failures == 0 ? "ALL STUB CHAIN TESTS PASSED\n" : "%d FAILURES\n", failures);
    return failures != 0;
}
