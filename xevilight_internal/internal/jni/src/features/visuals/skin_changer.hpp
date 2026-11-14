
// ================================================================================
// TG Bot: t.me/imguisource_bot
// Main Channel (ImGui Source): t.me/ImGui_Source_FIXNEY
// Reserve Channel (shit src 🧨): t.me/il2cpp_src
// ================================================================================


#pragma once

#include "features/aim/runtime.hpp"
#include "game/skin_catalog.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace astra::skins {

namespace ed = astra::esp::detail;

// ── settings / status ───────────────────────────────────────────────────────
inline std::atomic<bool> enabled{false};
inline std::atomic<int> currentWeapon{0};

inline std::mutex g_choiceMutex;
inline std::unordered_map<int, int> g_choices;

inline std::mutex g_statusMutex;
inline std::string g_statusText = "Disabled";

inline std::string status() {
    std::lock_guard<std::mutex> lock(g_statusMutex);
    return g_statusText;
}
inline void setStatus(const char* text) {
    std::lock_guard<std::mutex> lock(g_statusMutex);
    g_statusText = text ? text : "";
}
inline bool traceFrame() {
    static unsigned frame = 0;
    ++frame;
    return frame <= 8 || (frame % 120) == 0;
}
inline int choice(int weapon) {
    std::lock_guard<std::mutex> lock(g_choiceMutex);
    auto it = g_choices.find(weapon);
    return it == g_choices.end() ? 0 : it->second;
}
inline void choose(int weapon, int skin) {
    std::lock_guard<std::mutex> lock(g_choiceMutex);
    if (skin != 0) g_choices[weapon] = skin;
    else g_choices.erase(weapon);
}
inline void resetChoices() {
    std::lock_guard<std::mutex> lock(g_choiceMutex);
    g_choices.clear();
}

// ── memory helpers ──────────────────────────────────────────────────────────
inline bool pl(std::uintptr_t p) { return p > 0x10000 && ed::plausibleAddress(reinterpret_cast<void*>(p), 1); }
template <typename T>
inline bool rd(std::uintptr_t address, T& value) {
    return ed::readValue(reinterpret_cast<void*>(address), value);
}
inline std::uintptr_t rp(std::uintptr_t address) {
    std::uintptr_t value = 0;
    return rd(address, value) ? value : 0;
}
inline std::int32_t ri(std::uintptr_t address) {
    std::int32_t value = 0;
    return rd(address, value) ? value : 0;
}
inline std::uint8_t rb(std::uintptr_t address) {
    std::uint8_t value = 0;
    return rd(address, value) ? value : 0;
}
inline bool wr(std::uintptr_t address, const void* source, std::size_t bytes) {
    if (!address || !source || !bytes) return false;
    if (!ed::writable(reinterpret_cast<void*>(address), bytes)) return false;
    std::memcpy(reinterpret_cast<void*>(address), source, bytes);
    return true;
}
inline bool typeIs(std::uintptr_t object, const char* name) {
    if (!pl(object)) return false;
    return ed::nameEquals(ed::className(ed::objectClass(reinterpret_cast<void*>(object))), name);
}

// ── il2cpp dictionary readers ───────────────────────────────────────────────
struct __attribute__((packed)) DictHeader {
    std::uintptr_t array{};
    std::int32_t count{}, freeList{}, freeCount{}, version{};
};
struct IntEntry {
    std::int32_t hash, next, key, pad;
    std::uintptr_t value;
};
struct StrEntry {
    std::int32_t hash, next;
    std::uintptr_t key, value;
};
struct ContentEntry {
    std::int32_t hash, next;
    std::uint8_t weapon, pad[3];
    std::int32_t skin;
    std::uintptr_t value;
};
static_assert(sizeof(IntEntry) == 24, "il2cpp dictionary entry layout");
static_assert(sizeof(StrEntry) == 24, "il2cpp dictionary entry layout");
static_assert(sizeof(ContentEntry) == 24, "il2cpp dictionary entry layout");

inline bool sampleDict(std::uintptr_t object, DictHeader& out, int limit = 20000) {
    DictHeader a{}, b{};
    if (!pl(object) || !rd(object + 0x18, a)) return false;
    if (a.count < 0 || a.count > limit || !pl(a.array)) return false;
    std::uintptr_t capacity = 0;
    if (!rd(a.array + 0x18, capacity) || capacity < static_cast<std::uintptr_t>(a.count) || capacity > 65536)
        return false;
    if (!rd(object + 0x18, b) || std::memcmp(&a, &b, sizeof(a)) != 0) return false;
    out = a;
    return true;
}

inline bool sameDict(std::uintptr_t object, const DictHeader& expected) {
    DictHeader now{};
    return sampleDict(object, now) && now.array == expected.array && now.count == expected.count &&
           now.version == expected.version;
}

template <typename E>
inline bool dictEntries(std::uintptr_t object, DictHeader& snapshot, std::vector<E>& out, int limit = 20000) {
    if (!sampleDict(object, snapshot, limit)) return false;
    out.resize(static_cast<std::size_t>(snapshot.count));
    if (out.empty()) return true;
    const std::size_t bytes = out.size() * sizeof(E);
    if (ed::safeReadMemory(out.data(), reinterpret_cast<void*>(snapshot.array + 0x20), bytes) !=
        static_cast<ssize_t>(bytes))
        return false;
    return sameDict(object, snapshot);
}

inline bool managedString(std::uintptr_t object, std::string& out) {
    out.clear();
    if (!pl(object)) return false;
    std::int32_t count = 0;
    if (!rd(object + 0x10, count) || count < 1 || count > 160) return false;
    std::uint16_t data[160]{};
    if (ed::safeReadMemory(data, reinterpret_cast<void*>(object + 0x14), std::size_t(count) * 2) != count * 2)
        return false;
    std::int32_t verify = 0;
    if (!rd(object + 0x10, verify) || verify != count) return false;
    out.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        if (data[i] < 32 || data[i] > 126) return false;
        out.push_back(static_cast<char>(data[i]));
    }
    return true;
}

inline std::uintptr_t named(std::uintptr_t dictionary, const std::string& key) {
    DictHeader snapshot{};
    std::vector<StrEntry> entries;
    if (!dictEntries(dictionary, snapshot, entries)) return 0;
    std::string name;
    for (const auto& entry : entries) {
        if (entry.hash < 0) continue;
        if (managedString(entry.key, name) && name == key) return sameDict(dictionary, snapshot) ? entry.value : 0;
    }
    return 0;
}

// ── view-model reload (ported skin_visual_refresh) ─────────────────────────
// The external build queues the game's own Action<Lifetime> (xzl.yeon) through
// UnityEngine.UnitySynchronizationContext; that callback rebuilds arms and the
// weapon from the presentation cache we just patched. Offsets are the verified
// Standoff 2 1.1.0 ARM64 profile (libunity load bias).
inline constexpr std::uintptr_t kReloadCallbackRva = 0xaa3f940;
inline constexpr std::uintptr_t kSyncContextSlotRva = 0xc8c9898;

inline bool enqueueReload(std::uintptr_t behaviour) {
    if (!typeIs(behaviour, "xzl") || rb(behaviour + 0x28)) return false;
    const auto unity = ed::g_runtime.unityBase;
    const auto callback = rp(behaviour + 0xb0);
    if (!unity || !callback) return false;
    if (rp(callback + 0x10) != unity + kReloadCallbackRva || rp(callback + 0x40) != behaviour) {
        ASTRA_LOG(XS("SKIN enqueue bad callback=%p rva=%p unity=%p"), reinterpret_cast<void*>(callback),
                  reinterpret_cast<void*>(rp(callback + 0x10)), reinterpret_cast<void*>(unity));
        return false;
    }
    const auto klass = rp(rp(unity + kSyncContextSlotRva));
    if (!klass) return false;
    const auto context = rp(rp(klass + 0xf8) + 0x30);
    if (!typeIs(context, "UnitySynchronizationContext")) return false;
    const auto queue = rp(context + 0x18);
    if (!pl(queue)) return false;

    struct header {
        std::uintptr_t items;
        std::int32_t count, version;
    };
    struct work {
        std::uintptr_t callback, state, wait;
    };
    header first{}, last{};
    work prior{}, desired{callback, 0, 0}, check{};
    if (!rd(queue + 0x10, first) || first.count || !pl(first.items)) return false;
    const auto capacity = rp(first.items + 0x18), slot = first.items + 0x20;
    if (!capacity || capacity > 4096 || !rd(slot, prior) || prior.callback || prior.state || prior.wait)
        return false;
    if (!rd(queue + 0x10, last) || std::memcmp(&first, &last, sizeof(first)) != 0) return false;
    if (!wr(slot, &desired, sizeof(desired))) return false;
    if (!rd(queue + 0x10, last) || std::memcmp(&first, &last, sizeof(first)) != 0) {
        if (rp(queue + 0x10) == first.items && ri(queue + 0x18) == 0 && rd(slot, check) &&
            std::memcmp(&check, &desired, sizeof(check)) == 0)
            wr(slot, &prior, sizeof(prior));
        return false;
    }
    const std::int32_t count = 1;
    return wr(queue + 0x18, &count, 4);
}

inline bool g_refreshPending = false;
inline int g_refreshAttempts = 0;
inline std::uintptr_t g_refreshLocal = 0, g_refreshBehaviour = 0;
inline std::chrono::steady_clock::time_point g_refreshStart{}, g_refreshNext{};

inline void requestRefresh() {
    g_refreshPending = true;
    g_refreshAttempts = 0;
    g_refreshStart = std::chrono::steady_clock::now();
    g_refreshNext = g_refreshStart;
}

inline void refreshTick(std::uintptr_t local) {
    if (!g_refreshPending) return;
    ASTRA_LOG(XS("SKIN refresh tick local=%p attempts=%d"), reinterpret_cast<void*>(local), g_refreshAttempts);
    const auto now = std::chrono::steady_clock::now();
    if (g_refreshAttempts >= 3 || now - g_refreshStart >= std::chrono::seconds(8)) {
        g_refreshPending = false;
        return;
    }
    if (now < g_refreshNext) return;
    if (!local) {
        g_refreshPending = false;
        return;
    }
    if (local != g_refreshLocal) {
        g_refreshLocal = local;
        g_refreshBehaviour = 0;
    }
    if (!g_refreshBehaviour || !typeIs(g_refreshBehaviour, "xzl") || rp(g_refreshBehaviour + 0x18) != local)
        g_refreshBehaviour = reinterpret_cast<std::uintptr_t>(
            astra::aim::runtime::component(reinterpret_cast<void*>(local), XS("xzl")));
    ASTRA_LOG(XS("SKIN refresh behaviour=%p"), reinterpret_cast<void*>(g_refreshBehaviour));
    if (!enqueueReload(g_refreshBehaviour)) {
        g_refreshNext = now + std::chrono::milliseconds(8);
        return;
    }
    ++g_refreshAttempts;
    g_refreshNext = now + std::chrono::milliseconds(1500);
}

// ── catalog / provider state (game thread only) ─────────────────────────────
struct Edit {
    std::uintptr_t address{};
    std::uint64_t before{}, after{};
    std::size_t size{};
};

inline std::uintptr_t g_provider{};
inline std::uintptr_t g_activeContent{}, g_activeList{}, g_activeItems{};
inline int g_sourceSkin{};
inline int g_selectedSkin{};
inline std::vector<Edit> g_edits;

struct Parked {
    std::uintptr_t content{}, list{}, items{};
    int selected{}, source{};
    std::vector<Edit> edits;
};
inline std::unordered_map<int, Parked> g_parked;

inline std::unordered_map<int, std::uintptr_t> g_definitions;
inline std::unordered_map<std::string, std::uintptr_t> g_packs;
inline DictHeader g_packSnapshot{};
inline std::uintptr_t g_packObject{}, g_packArray{};
inline int g_packCount{}, g_packCursor{};
inline bool g_scanning{}, g_ready{}, g_refresh{true};

inline std::chrono::steady_clock::time_point g_retryAt{};
inline bool g_retryValid{};

inline bool hasEdits() { return !g_edits.empty() || !g_parked.empty(); }

inline void resetCatalog() {
    g_definitions.clear();
    g_packs.clear();
    g_packSnapshot = {};
    g_packObject = g_packArray = 0;
    g_packCount = g_packCursor = 0;
    g_scanning = g_ready = false;
    g_refresh = true;
}

inline bool readPacks(std::uintptr_t list, std::vector<std::uintptr_t>& out, std::uintptr_t& items,
                      std::uintptr_t& capacity) {
    items = rp(list + 0x10);
    const std::int32_t count = ri(list + 0x18);
    capacity = rp(items + 0x18);
    if (!pl(items) || count < 1 || count > 8 || capacity < static_cast<std::uintptr_t>(count) || capacity > 32)
        return false;
    out.resize(static_cast<std::size_t>(count));
    if (ed::safeReadMemory(out.data(), reinterpret_cast<void*>(items + 0x20), std::size_t(count) * 8) != count * 8)
        return false;
    return rp(list + 0x10) == items && ri(list + 0x18) == count;
}

inline std::uintptr_t content(std::uint8_t weapon, int skin) {
    if (!g_provider || !typeIs(g_provider, "shk")) return 0;
    DictHeader snapshot{};
    std::vector<ContentEntry> entries;
    if (!dictEntries(rp(g_provider + 0x40), snapshot, entries, 4096)) return 0;
    for (const auto& entry : entries) {
        if (entry.hash < 0 || entry.weapon != weapon || entry.skin != skin) continue;
        if (typeIs(entry.value, "gxa")) return sameDict(rp(g_provider + 0x40), snapshot) ? entry.value : 0;
    }
    return 0;
}

inline bool ownsContent() {
    return g_activeContent && content(static_cast<std::uint8_t>(currentWeapon.load()), g_sourceSkin) == g_activeContent &&
           rp(g_activeContent + 0x28) == g_activeList && rp(g_activeList + 0x10) == g_activeItems;
}

inline bool appendEdit(std::uintptr_t address, std::uint64_t after, std::size_t size) {
    std::uint64_t before = 0;
    if (!rd(address, before)) return false;
    if (std::memcmp(&before, &after, size) == 0) return true;
    g_edits.push_back({address, before, after, size});
    return true;
}

inline void restore() {
    if (g_edits.empty()) {
        g_activeContent = g_activeList = g_activeItems = 0;
        g_selectedSkin = 0;
        return;
    }
    const bool owned = ownsContent();
    int restored = 0;
    if (owned) {
        for (auto it = g_edits.rbegin(); it != g_edits.rend(); ++it) {
            std::uint64_t now = 0;
            if (rd(it->address, now) && std::memcmp(&now, &it->after, it->size) == 0 &&
                wr(it->address, &it->before, it->size))
                ++restored;
        }
    }
    if (restored) requestRefresh();
    g_edits.clear();
    g_activeContent = g_activeList = g_activeItems = 0;
    g_selectedSkin = 0;
}

inline void park() {
    if (g_activeContent)
        g_parked[currentWeapon.load()] = {g_activeContent, g_activeList, g_activeItems, g_selectedSkin, g_sourceSkin,
                                          std::move(g_edits)};
    g_edits.clear();
    g_activeContent = g_activeList = g_activeItems = 0;
    g_selectedSkin = 0;
}

inline void unpark(int id) {
    auto it = g_parked.find(id);
    if (it == g_parked.end()) return;
    auto parked = std::move(it->second);
    g_parked.erase(it);
    g_activeContent = parked.content;
    g_activeList = parked.list;
    g_activeItems = parked.items;
    g_selectedSkin = parked.selected;
    g_sourceSkin = parked.source;
    g_edits = std::move(parked.edits);
}

inline void restoreAll() {
    const int original = currentWeapon.load();
    park();
    while (!g_parked.empty()) {
        const int id = g_parked.begin()->first;
        currentWeapon.store(id);
        unpark(id);
        restore();
    }
    currentWeapon.store(original);
}

inline const skin_catalog::entry* definition(int id) {
    const auto first = std::begin(skin_catalog::entries), last = std::end(skin_catalog::entries);
    const auto it = std::lower_bound(first, last, id, [](const auto& e, int value) { return e.id < value; });
    return it != last && it->id == id ? it : nullptr;
}

inline bool apply(int selected, int actualSkin) {
    const int weapon = currentWeapon.load();
    const auto* target = selected == -1 ? nullptr : definition(selected);
    if (selected != -1 && (!target || target->weapon != weapon || !g_definitions.count(selected))) return false;
    if (selected == -1 && weapon != 70) return false;

    const auto source = content(static_cast<std::uint8_t>(weapon), actualSkin);
    const auto base = content(static_cast<std::uint8_t>(weapon), 0);
    if (!source || !base) return false;

    std::uintptr_t params[3]{};
    std::vector<std::uintptr_t> desired;
    bool statTrack = false;

    if (selected == -1) {
        const auto hands = content(71, 0);
        std::uintptr_t items = 0, capacity = 0;
        if (!hands || !readPacks(rp(hands + 0x28), desired, items, capacity)) return false;
        for (int i = 0; i < 3; ++i) params[i] = rp(hands + 0x10 + i * 8);
    } else {
        const auto found = g_packs.find("inventory/" + std::to_string(selected));
        if (found == g_packs.end()) return false;
        const auto skin = found->second;
        const auto entry = rp(skin + 0x10);
        const auto def = g_definitions.find(selected);
        if (def == g_definitions.end() || entry != def->second || ri(entry + 0x18) != selected) return false;
        statTrack = typeIs(entry, "StatTrackSkinDefinition");
        desired.push_back(skin);

        const int offsets[3] = {0x38, 0x28, 0x30};
        for (int i = 0; i < 3; ++i) params[i] = rp(base + 0x10 + i * 8);

        if (target->content_template[0]) {
            const std::string templ = target->content_template;
            const auto pack = g_packs.find(templ);
            if (pack == g_packs.end()) return false;
            desired.push_back(pack->second);
            for (int i = 0; i < 3; ++i) {
                params[i] = named(rp(rp(g_provider + offsets[i]) + 0x40), templ);
                if (!params[i]) return false;
            }
        } else {
            std::vector<std::uintptr_t> original;
            std::uintptr_t items = 0, capacity = 0;
            if (!readPacks(rp(base + 0x28), original, items, capacity) || original.empty()) return false;
            desired.push_back(original.front());
        }

        const auto fallback = rp(g_provider + 0x48);
        if (!typeIs(fallback, "tjv`1")) return false;
        desired.push_back(fallback);
    }

    const char* types[3] = {"WeaponMeshParameters", "WeaponAnimationParameters", "WeaponSoundParameters"};
    for (int i = 0; i < 3; ++i) {
        if (!typeIs(params[i], types[i]) && !(i == 2 && typeIs(params[i], "GunSoundParameters"))) return false;
    }

    std::vector<std::uintptr_t> original;
    std::uintptr_t items = 0, capacity = 0;
    const auto list = rp(source + 0x28);
    if (!readPacks(list, original, items, capacity) || desired.empty() || desired.size() > capacity) return false;
    if (!g_edits.empty()) return false;

    g_activeContent = source;
    g_activeList = list;
    g_activeItems = items;
    g_sourceSkin = actualSkin;
    g_selectedSkin = selected;

    bool prepared = true;
    for (std::size_t i = 0; i < desired.size() && prepared; ++i)
        prepared = appendEdit(items + 0x20 + i * 8, desired[i], 8);
    for (int i = 0; i < 3 && prepared; ++i) prepared = appendEdit(source + 0x10 + i * 8, params[i], 8);
    prepared = prepared && appendEdit(source + 0x30, statTrack ? 1 : 0, 1) &&
               appendEdit(source + 0x34, 0, 4) &&
               appendEdit(list + 0x18, static_cast<std::uint64_t>(desired.size()), 4);
    if (!prepared || !ownsContent()) {
        g_edits.clear();
        g_activeContent = g_activeList = g_activeItems = 0;
        return false;
    }

    for (const auto& edit : g_edits) {
        std::uint64_t now = 0;
        if (!rd(edit.address, now) || std::memcmp(&now, &edit.before, edit.size) != 0) {
            g_edits.clear();
            g_activeContent = g_activeList = g_activeItems = 0;
            return false;
        }
    }

    std::size_t written = 0;
    for (const auto& edit : g_edits) {
        std::uint64_t verify = 0;
        if (!wr(edit.address, &edit.after, edit.size) || !rd(edit.address, verify) ||
            std::memcmp(&verify, &edit.after, edit.size) != 0) {
            g_edits.resize(written + 1);
            restore();
            return false;
        }
        ++written;
    }
    return true;
}

// ── catalog scan (incremental, 256 pack keys per tick) ──────────────────────
inline void scanStep() {
    if (!g_provider) return;
    if (!g_scanning) {
        if (!g_refresh) return;
        g_refresh = false;
        g_ready = false;
        g_definitions.clear();
        g_packs.clear();

        DictHeader snapshot{};
        std::vector<IntEntry> skins;
        const auto source = rp(g_provider + 0x10);
        if (!source || !dictEntries(rp(source + 0x18), snapshot, skins)) {
            g_refresh = true;
            g_retryAt = std::chrono::steady_clock::now() + std::chrono::seconds(1);
            g_retryValid = true;
            setStatus("Catalog unavailable");
            return;
        }
        for (const auto& entry : skins)
            if (entry.hash >= 0 && entry.key > 0 && pl(entry.value)) g_definitions[entry.key] = entry.value;

        g_packObject = rp(rp(g_provider + 0x20) + 0x30);
        if (!sampleDict(g_packObject, g_packSnapshot, 40000)) {
            g_refresh = true;
            g_retryAt = std::chrono::steady_clock::now() + std::chrono::seconds(1);
            g_retryValid = true;
            setStatus("Catalog unavailable");
            return;
        }
        g_packArray = g_packSnapshot.array;
        g_packCount = g_packSnapshot.count;
        g_packCursor = 0;
        g_scanning = true;
    }

    if (!sameDict(g_packObject, g_packSnapshot)) {
        resetCatalog();
        return;
    }

    const int end = std::min(g_packCursor + 256, g_packCount);
    if (end > g_packCursor) {
        std::vector<StrEntry> entries(static_cast<std::size_t>(end - g_packCursor));
        const std::size_t bytes = entries.size() * sizeof(StrEntry);
        if (ed::safeReadMemory(entries.data(), reinterpret_cast<void*>(g_packArray + 0x20 +
                                                                     std::size_t(g_packCursor) * sizeof(StrEntry)),
                               bytes) != static_cast<ssize_t>(bytes)) {
            resetCatalog();
            return;
        }
        std::string key;
        for (const auto& entry : entries) {
            if (entry.hash < 0 || !pl(entry.value)) continue;
            if (managedString(entry.key, key) && !key.empty()) g_packs.emplace(std::move(key), entry.value);
        }
        g_packCursor = end;
    }

    if (g_packCursor >= g_packCount) {
        g_scanning = false;
        g_ready = true;
        setStatus("Catalog ready");
    } else {
        setStatus("Loading catalog");
    }
}

// ── per-player tick (Unity main thread) ─────────────────────────────────────
struct RefreshGuard {
    std::uintptr_t local;
    ~RefreshGuard() { refreshTick(local); }
};

inline void tick(void* player, bool menuOpen) {
    const bool trace = traceFrame();
    if (trace) ASTRA_LOG(XS("SKIN enter p=%p en=%d menu=%d edits=%d"), player, int(enabled.load()), int(menuOpen), int(g_edits.size()));
    if (!player) return;
    void* local = astra::aim::runtime::localPlayer();
    if (!local || local != player) return;
    if (!enabled.load(std::memory_order_relaxed) && !menuOpen && !hasEdits()) return;
    if (trace) ASTRA_LOG(XS("SKIN local=%p"), local);

    void* world = nullptr;
    ed::PlayerSnapshot snapshot{};
    if (!astra::aim::runtime::localAlive(local, snapshot, world) || snapshot.hp == 0) {
        if (trace) ASTRA_LOG(XS("SKIN not alive hp=%d"), int(snapshot.hp));
        if (!enabled.load(std::memory_order_relaxed)) restoreAll();
        setStatus("Waiting for respawn");
        return;
    }
    if (trace) ASTRA_LOG(XS("SKIN alive hp=%d world=%p"), int(snapshot.hp), world);

    RefreshGuard refresh{reinterpret_cast<std::uintptr_t>(local)};

    static void* cachedLocal = nullptr;
    static std::uintptr_t animComponent = 0;
    if (cachedLocal != local) {
        cachedLocal = local;
        animComponent = 0;
    }
    if (!animComponent || !typeIs(animComponent, "chc"))
        animComponent = reinterpret_cast<std::uintptr_t>(astra::aim::runtime::component(local, XS("chc")));

    std::uintptr_t data = 0;
    if (!animComponent || !rd(animComponent + 0xa0, data) || !pl(data)) {
        if (trace) ASTRA_LOG(XS("SKIN no data anim=%p"), reinterpret_cast<void*>(animComponent));
        if (!enabled.load(std::memory_order_relaxed)) restoreAll();
        setStatus("Waiting for weapon");
        return;
    }
    if (trace) ASTRA_LOG(XS("SKIN anim=%p data=%p"), reinterpret_cast<void*>(animComponent), reinterpret_cast<void*>(data));

    const auto provider = rp(data + 0xd8);
    if (!typeIs(provider, "shk")) {
        if (trace) ASTRA_LOG(XS("SKIN bad provider=%p"), reinterpret_cast<void*>(provider));
        restoreAll();
        animComponent = 0;
        setStatus("Loader unavailable");
        return;
    }
    if (trace) ASTRA_LOG(XS("SKIN provider=%p"), reinterpret_cast<void*>(provider));

    if (provider != g_provider) {
        restoreAll();
        g_provider = provider;
        resetCatalog();
    }

    const std::uint8_t weaponId = rb(data + 0xd0);
    const int actualSkin = ri(data + 0xd4);
    if (trace) ASTRA_LOG(XS("SKIN weapon id=%d skin=%d"), int(weaponId), actualSkin);
    if (weaponId < 1 || weaponId > 127 || actualSkin < 0) return;

    if (weaponId != currentWeapon.load(std::memory_order_relaxed)) {
        park();
        currentWeapon.store(weaponId, std::memory_order_relaxed);
        unpark(weaponId);
    }
    if (!g_edits.empty() && actualSkin != g_sourceSkin) restore();

    const int requested = choice(weaponId);
    if (trace) ASTRA_LOG(XS("SKIN choice=%d"), requested);
    if (!enabled.load(std::memory_order_relaxed)) {
        restoreAll();
        setStatus("Disabled");
    } else if (!requested) {
        restore();
        setStatus("Default skin");
    }

    if ((menuOpen || enabled.load(std::memory_order_relaxed)) && (g_refresh || g_scanning)) scanStep();
    if (trace) ASTRA_LOG(XS("SKIN scan refresh=%d scanning=%d ready=%d packs=%d defs=%d"), int(g_refresh), int(g_scanning), int(g_ready), int(g_packs.size()), int(g_definitions.size()));
    if (!enabled.load(std::memory_order_relaxed) || !requested) return;

    if (!g_ready) {
        const auto* target = definition(requested);
        if (!target || !g_definitions.count(requested) ||
            !g_packs.count("inventory/" + std::to_string(requested)) ||
            (target->content_template[0] && !g_packs.count(target->content_template))) {
            setStatus("Loading catalog");
            return;
        }
    }

    if (g_activeContent) {
        if (requested != g_selectedSkin || !ownsContent()) restore();
        else {
            setStatus("Skin selected");
            return;
        }
    }
    if (requested == actualSkin) {
        setStatus("Skin already applied");
        return;
    }
    if (g_retryValid && std::chrono::steady_clock::now() < g_retryAt) return;

    if (apply(requested, actualSkin)) {
        g_retryValid = false;
        requestRefresh();
        setStatus("Skin selected");
    } else {
        g_retryAt = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        g_retryValid = true;
        setStatus("Skin unavailable");
    }
}

}  // namespace astra::skins
