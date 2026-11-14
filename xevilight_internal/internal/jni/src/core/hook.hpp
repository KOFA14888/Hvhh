
// ================================================================================
// Protected source: @imguisource_bot
// Official channels: t.me/ImGui_Source_FIXNEY & t.me/il2cpp_src
// ================================================================================

// жеский инлайн хук bypa$$
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <vector>

#include "obf.hpp"

namespace xev::hook {

using Addr = std::uintptr_t;

// ── owned executable ranges ─────────────────────────────────────────────────
// Every relay/trampoline page we allocate is recorded here.  The /proc filter
// in core/bypass.cpp hides exactly these ranges instead of every anonymous
// executable mapping in the process (hiding all of them is a fingerprint of
// its own: a stock Unity process still has ART JIT pages).
struct span { Addr begin, end; };
inline span g_spans[96]{};
inline std::atomic<unsigned> g_spanCount{0};

inline void registerSpan(void* address, std::size_t size) {
    if (!address || !size) return;
    const unsigned slot = g_spanCount.load(std::memory_order_relaxed);
    if (slot >= 96) return;
    g_spans[slot].begin = reinterpret_cast<Addr>(address);
    g_spans[slot].end = g_spans[slot].begin + size;
    g_spanCount.store(slot + 1, std::memory_order_release);
}

inline long pageSize() {
    long size = sysconf(_SC_PAGESIZE);
    return size >= 4096 && size <= 65536 ? size : 4096;
}

inline bool fits(Addr site, Addr dest) {
    if (!site || !dest || (site & 3) || (dest & 3)) return false;
    return dest >= site ? (dest - site) < 0x08000000u : (site - dest) <= 0x08000000u;
}

inline bool branchWord(Addr site, Addr dest, std::uint32_t& word) {
    if (!fits(site, dest)) return false;
    const std::intptr_t delta = dest >= site ? std::intptr_t(dest - site)
                                             : -std::intptr_t(site - dest);
    word = 0x14000000u | (std::uint32_t(delta / 4) & 0x03ffffffu);
    return true;
}

// Raw syscalls on purpose: map discovery must not depend on (or be rewritten
// by) the /proc sanitiser that itself calls allocateNear().
inline std::vector<std::pair<Addr, Addr>> selfMaps() {
    std::vector<std::pair<Addr, Addr>> maps;
    const int fd = static_cast<int>(::syscall(SYS_openat, AT_FDCWD,
                                              XS("/proc/self/maps"),
                                              O_RDONLY | O_CLOEXEC));
    if (fd < 0) return maps;
    char chunk[4096];
    char line[512];
    std::size_t used = 0;
    for (;;) {
        const long got = ::syscall(SYS_read, fd, chunk, sizeof chunk);
        if (got <= 0) break;
        for (long i = 0; i < got; ++i) {
            if (chunk[i] == '\n') {
                line[used] = '\0';
                unsigned long long begin = 0, end = 0;
                if (used && std::sscanf(line, "%llx-%llx", &begin, &end) == 2 && begin < end)
                    maps.emplace_back(Addr(begin), Addr(end));
                used = 0;
                continue;
            }
            if (used + 1 < sizeof line) line[used++] = chunk[i];
        }
    }
    ::syscall(SYS_close, fd);
    return maps;
}

// Nearest free page inside branch range, allocated with MAP_FIXED_NOREPLACE and
// validated afterwards (older kernels may ignore the flag).
inline void* allocateNear(Addr site, std::size_t size) {
    const long rawPage = pageSize();
    const Addr page = Addr(rawPage);
    if (size < page) size = page;
    constexpr Addr span = 0x08000000u;
    auto maps = selfMaps();
    if (maps.empty()) return MAP_FAILED;
    std::sort(maps.begin(), maps.end());

    std::vector<Addr> candidates;
    const Addr low = site > span ? site - span : 0;
    const Addr high = site + span;
    Addr cursor = low;
    auto addGap = [&](Addr begin, Addr end) {
        if (end <= begin || end - begin < size) return;
        const Addr first = (begin + page - 1) & ~(page - 1);
        const Addr last = (end - size) & ~(page - 1);
        if (first > last) return;
        candidates.push_back(std::clamp(site & ~(page - 1), first, last));
        candidates.push_back(first);
        candidates.push_back(last);
    };
    for (const auto& m : maps) {
        if (m.second <= low) continue;
        if (m.first >= high) break;
        if (m.first > cursor) addGap(cursor, std::min(m.first, high));
        cursor = std::max(cursor, std::min(m.second, high));
        if (cursor >= high) break;
    }
    if (cursor < high) addGap(cursor, high);
    if (candidates.empty()) return MAP_FAILED;
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
    auto distance = [&](Addr a) { return a >= site ? a - site : site - a; };
    std::sort(candidates.begin(), candidates.end(), [&](Addr a, Addr b) {
        const auto da = distance(a), db = distance(b);
        return da == db ? a < b : da < db;
    });
    if (candidates.size() > 512) candidates.resize(512);

    for (Addr candidate : candidates) {
        if (!fits(site, candidate)) continue;
        errno = 0;
        void* result = mmap(reinterpret_cast<void*>(candidate), size,
                            PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS | 0x100000 /* NOREPLACE */, -1, 0);
        if (result == MAP_FAILED && errno == EINVAL)
            result = mmap(reinterpret_cast<void*>(candidate), size,
                          PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (result == MAP_FAILED) continue;
        if (fits(site, reinterpret_cast<Addr>(result))) return result;
        munmap(result, size);
    }
    return MAP_FAILED;
}

inline bool makeExecutable(void* address, std::size_t size) {
    return mprotect(address, size, PROT_READ | PROT_EXEC) == 0;
}

// /proc/self/mem is the primary code-write path: FOLL_FORCE writes a read-only
// private mapping without touching its protection. That matters on Android 16
// where mprotect(PROT_WRITE) on some libc pages (seen on the pread64 page)
// kills the process with SIGSEGV, and it also removes the RW/RWX window that a
// racing maps scan could catch.
inline int codeMemFd() {
    static std::atomic<int> cached{-2};
    int fd = cached.load(std::memory_order_acquire);
    if (fd != -2) return fd;
    fd = static_cast<int>(::syscall(SYS_openat, AT_FDCWD, XS("/proc/self/mem"),
                                    O_RDWR | O_CLOEXEC));
    cached.store(fd, std::memory_order_release);
    return fd;
}

// Atomic single-instruction patch. Never RWX; the mprotect branch is only a
// fallback for kernels/domains where /proc/self/mem writes are denied.
inline bool patchWord(Addr site, std::uint32_t word,
                      std::uint32_t* previous = nullptr) {
    if (!site || (site & 3)) return false;
    auto* cell = reinterpret_cast<std::uint32_t*>(site);
    if (previous) *previous = __atomic_load_n(cell, __ATOMIC_ACQUIRE);

    const int mem = codeMemFd();
    if (mem >= 0) {
        const ssize_t written = ::pwrite(mem, &word, sizeof word, static_cast<off_t>(site));
        if (written == static_cast<ssize_t>(sizeof word)) {
            __builtin___clear_cache(reinterpret_cast<char*>(site),
                                    reinterpret_cast<char*>(site) + 4);
            return true;
        }
    }

    const Addr rawPage = Addr(pageSize());
    const Addr page = site & ~(rawPage - 1);
    const Addr pageEnd = (site + 4 + rawPage - 1) & ~(rawPage - 1);
    const std::size_t span = std::size_t(pageEnd - page);
    if (mprotect(reinterpret_cast<void*>(page), span, PROT_READ | PROT_WRITE) != 0)
        return false;
    __atomic_store_n(cell, word, __ATOMIC_RELEASE);
    __builtin___clear_cache(reinterpret_cast<char*>(site), reinterpret_cast<char*>(site) + 4);
    return mprotect(reinterpret_cast<void*>(page), span, PROT_READ | PROT_EXEC) == 0;
}

inline std::uint32_t loadWord(Addr site) {
    return __atomic_load_n(reinterpret_cast<std::uint32_t*>(site), __ATOMIC_ACQUIRE);
}

// PC-relative or otherwise position-dependent encodings cannot be copied into a
// trampoline without relocation; reject them instead of guessing.
inline bool relocatable(std::uint32_t w) {
    if ((w & 0x1f000000u) == 0x10000000u) return false;  // adr / adrp
    if ((w & 0x7c000000u) == 0x14000000u) return false;  // b / bl
    if ((w & 0xff000010u) == 0x54000000u) return false;  // b.cond
    if ((w & 0x7e000000u) == 0x34000000u) return false;  // cbz / cbnz
    if ((w & 0x7e000000u) == 0x36000000u) return false;  // tbz / tbnz
    if ((w & 0x3b000000u) == 0x18000000u) return false;  // ldr/prfm literal (GPR + SIMD)
    return true;
}

inline void writeRelay(std::uint32_t* at, const void* detour) {
    at[0] = 0x58000050u;  // ldr x16, #8
    at[1] = 0xd61f0200u;  // br  x16
    std::memcpy(at + 2, &detour, sizeof detour);
}

// Detour reimplements the original behaviour itself; the prologue is not used.
inline bool jump(void* target, void* detour, std::uint32_t* previous = nullptr) {
    if (!target || !detour) return false;
    const Addr site = reinterpret_cast<Addr>(target);
    const long rawPage = pageSize();
    void* relay = allocateNear(site, std::size_t(rawPage));
    if (relay == MAP_FAILED) return false;
    writeRelay(reinterpret_cast<std::uint32_t*>(relay), detour);
    if (!makeExecutable(relay, std::size_t(rawPage))) {
        munmap(relay, std::size_t(rawPage));
        return false;
    }
    registerSpan(relay, std::size_t(rawPage));
    std::uint32_t branch = 0;
    if (!branchWord(site, reinterpret_cast<Addr>(relay), branch)) return false;
    return patchWord(site, branch, previous);
}

// Standard inline hook: copies `bytes` prologue instructions into a near
// trampoline and appends an absolute jump back to target + bytes.
inline bool trampoline(void* target, void* detour, void** original, std::size_t bytes = 16) {
    if (!target || !detour || !original) return false;
    if (bytes == 0 || (bytes & 3) || bytes > 64) return false;
    const Addr site = reinterpret_cast<Addr>(target);
    for (std::size_t offset = 0; offset < bytes; offset += 4)
        if (!relocatable(loadWord(site + offset))) return false;

    const long rawPage = pageSize();
    void* block = allocateNear(site, std::size_t(rawPage));
    if (block == MAP_FAILED) return false;
    auto* words = reinterpret_cast<std::uint32_t*>(block);
    writeRelay(words, detour);

    auto* tramp = reinterpret_cast<std::uint32_t*>(reinterpret_cast<char*>(block) + 64);
    std::memcpy(tramp, reinterpret_cast<const void*>(site), bytes);
    auto* back = tramp + bytes / 4;
    writeRelay(back, reinterpret_cast<const void*>(site + bytes));

    if (!makeExecutable(block, std::size_t(rawPage))) {
        munmap(block, std::size_t(rawPage));
        return false;
    }
    registerSpan(block, std::size_t(rawPage));
    std::uint32_t branch = 0;
    if (!branchWord(site, reinterpret_cast<Addr>(block), branch)) return false;
    *original = tramp;  // publish before the site starts branching into us
    return patchWord(site, branch);
}

}  // namespace xev::hook
