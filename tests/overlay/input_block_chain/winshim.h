// Linux stand-ins for the Windows calls input_block.cpp makes.
#pragma once
#include <sys/mman.h>
#include <cstddef>
#include <cstdint>
typedef unsigned long DWORD; typedef int BOOL; typedef void* HMODULE; typedef const wchar_t* LPCWSTR;
#define MEM_RESERVE 0x2000
#define MEM_COMMIT 0x1000
#define PAGE_EXECUTE_READWRITE 0x40
struct SYSTEM_INFO { DWORD dwAllocationGranularity; };
inline void GetSystemInfo(SYSTEM_INFO* i) { i->dwAllocationGranularity = 0x10000; }
inline void* VirtualAlloc(void* at, std::size_t size, DWORD, DWORD) {
    at = (void*)((std::uintptr_t)at & ~(std::uintptr_t)0xFFFF);
    void* p = mmap(at, size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    return p == MAP_FAILED ? nullptr : p;
}
inline BOOL VirtualProtect(void*, std::size_t, DWORD, DWORD* old) { *old = 0; return 1; }
inline void* GetCurrentProcess() { return nullptr; }
inline BOOL FlushInstructionCache(void*, const void*, std::size_t) { return 1; }
extern "C" std::uint8_t* gFakeGameBase;
inline HMODULE GetModuleHandleW(const wchar_t*) { return gFakeGameBase; }
struct MEMORY_BASIC_INFORMATION { DWORD State; };
inline std::size_t VirtualQuery(const void*, MEMORY_BASIC_INFORMATION* m, std::size_t) { m->State = MEM_COMMIT; return sizeof(*m); }
