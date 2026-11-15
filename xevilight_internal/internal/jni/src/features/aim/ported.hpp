
// ================================================================================
// [TG] @imguisource_bot -> @ImGui_Source_FIXNEY (ImGui Source) -> @il2cpp_src (shit src 🧨)
// ================================================================================



#pragma once
#include <atomic>
#include <cmath>
#include <cstring>

namespace ported {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

// ── shared helpers ──────────────────────────────────────────────────────────
inline double nowSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

inline float wrapDegrees(float value) {
    return std::isfinite(value) ? std::remainder(value, 360.f) : 0.f;
}

inline float radians(float degrees) { return degrees * 0.01745329251994329577f; }
inline float degrees(float radiansValue) { return radiansValue * 57.29577951308232f; }


inline void* componentLoose(void* player, const char* name) {
    auto* container = object(player, 0x20);
    if (!container) return nullptr;
    for (std::size_t offset : {0x10u, 0x18u, 0x20u, 0x28u, 0x30u, 0x38u}) {
        auto* array = object(container, offset);
        std::uintptr_t count = 0;
        if (!field(array, 0x18, count) || count > 256) continue;
        for (std::uintptr_t i = 0; i < count; ++i) {
            auto* candidate = object(array, 0x20 + i * 8);
            if (candidate && named(candidate, name)) return candidate;
        }
    }
    auto* dictionary = object(container, 0x40);
    auto* entries = object(dictionary, 0x18);
    std::int32_t count = 0;
    std::uintptr_t capacity = 0;
    if (!field(dictionary, 0x20, count) || count < 0 || count > 256 ||
        !field(entries, 0x18, capacity) || capacity < static_cast<unsigned>(count) || capacity > 512)
        return nullptr;
    for (std::int32_t i = 0; i < count; ++i) {
        std::int32_t hash = -1;
        const auto offset = 0x20 + std::size_t(i) * 0x18;
        if (!field(entries, offset, hash) || hash < 0) continue;
        auto* candidate = object(entries, offset + 0x10);
        if (candidate && named(candidate, name)) return candidate;
    }
    return nullptr;
}

// ── normal aim: direct `got` rotation writes ────────────────────────────────
struct Quat { float x{}, y{}, z{}, w{}; };
struct RotAccess { std::uintptr_t hierarchy{}; std::int32_t index{}; std::int32_t padding{}; };
static_assert(sizeof(RotAccess) == 16, "TransformAccess layout");

struct RotSample {
    void* managed = nullptr;
    std::uintptr_t native = 0, entries = 0, address = 0;
    Quat value{};
    Vec3 position{};
};

inline Quat quatProduct(const Quat& a, const Quat& b) {
    return {
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
    };
}

// Unity Quaternion.Euler applies Z, then X, then Y: qY * qX * qZ.
inline Quat unityEuler(float xDeg, float yDeg, float zDeg) {
    const float half = 0.008726646259971648f;
    const float x = xDeg * half, y = yDeg * half, z = zDeg * half;
    return quatProduct(
        quatProduct(Quat{0.f, std::sin(y), 0.f, std::cos(y)},
                    Quat{std::sin(x), 0.f, 0.f, std::cos(x)}),
        Quat{0.f, 0.f, std::sin(z), std::cos(z)});
}

inline bool readTransformSample(void* managed, RotSample& out) {
    out = {};
    if (!managed || !named(managed, XS("Transform"))) return false;
    std::uintptr_t native = 0;
    if (!field(managed, 0x10, native) || !native) return false;
    RotAccess access{};
    if (!ed::readValue(reinterpret_cast<void*>(native + 0x28), access) ||
        !access.hierarchy || access.index < 0 || access.index > 8192)
        return false;
    std::uintptr_t entries = 0;
    if (!field(reinterpret_cast<void*>(access.hierarchy), 0x18, entries) || !entries) return false;
    const auto address = entries + std::uintptr_t(access.index) * 48 + 16;
    Quat value{};
    Vec3 position{};
    if (!ed::readValue(reinterpret_cast<void*>(address), value) ||
        !ed::readValue(reinterpret_cast<void*>(address - 16), position))
        return false;
    out = {managed, native, entries, address, value, position};
    return true;
}

inline bool readRotation(void* managed, bool pitch, RotSample& out) {
    RotSample sample{};
    if (!readTransformSample(managed, sample)) return false;
    const float norm = sample.value.x * sample.value.x + sample.value.y * sample.value.y +
                       sample.value.z * sample.value.z + sample.value.w * sample.value.w;
    if (!std::isfinite(norm) || std::fabs(norm - 1.f) > .02f || std::fabs(sample.value.z) > .01f ||
        std::fabs(pitch ? sample.value.y : sample.value.x) > .01f)
        return false;
    out = sample;
    return true;
}

// Thread-safe transform read used by the render-thread aim path.  It does not
// call the Unity API, so it is safe after the game's camera update.
inline bool readTransformPose(void* managed, Vec3& position, Vec3& forward) {
    RotSample sample{};
    if (!readTransformSample(managed, sample)) return false;
    position = sample.position;
    const auto& q = sample.value;
    forward = normalized({2.f * (q.x * q.z + q.w * q.y),
                          2.f * (q.y * q.z - q.w * q.x),
                          1.f - 2.f * (q.x * q.x + q.y * q.y)});
    return finite(position) && length(forward) > .9f;
}

inline bool stillValid(const RotSample& sample) {
    std::uintptr_t native = 0;
    RotAccess access{};
    Quat value{};
    if (!field(sample.managed, 0x10, native) || native != sample.native) return false;
    if (!ed::readValue(reinterpret_cast<void*>(sample.native + 0x28), access) ||
        !access.hierarchy || access.index < 0 || access.index > 8192)
        return false;
    std::uintptr_t entries = 0;
    if (!field(reinterpret_cast<void*>(access.hierarchy), 0x18, entries) || entries != sample.entries) return false;
    if (!ed::readValue(reinterpret_cast<void*>(sample.address), value)) return false;
    return std::memcmp(&value, &sample.value, sizeof(value)) == 0;
}

inline bool writeQuat(std::uintptr_t address, const Quat& value) {
    if (!address || !ed::writable(reinterpret_cast<void*>(address), sizeof(value))) return false;
    std::memcpy(reinterpret_cast<void*>(address), &value, sizeof(value));
    return true;
}

struct DirectAngles { float pitch = 0, yaw = 0; };

inline bool readDirectAngles(void* controller, DirectAngles& out) {
    out = {};
    if (!controller) return false;
    RotSample pitch{}, yaw{};
    if (!readRotation(object(controller, 0x48), true, pitch) ||
        !readRotation(object(controller, 0x50), false, yaw) ||
        pitch.address == yaw.address)
        return false;
    // Quaternion stores the half angle: Unity's euler angle is 2*atan2.
    out.pitch = 2.f * degrees(std::atan2(pitch.value.x, pitch.value.w));
    out.yaw = 2.f * degrees(std::atan2(yaw.value.y, yaw.value.w));
    return std::isfinite(out.pitch) && std::isfinite(out.yaw) && std::fabs(out.pitch) <= 89.5f;
}

inline bool applyDirect(void* controller, float deltaPitch, float deltaYaw, float cap = 18.f) {
    if (!controller || !std::isfinite(deltaPitch) || !std::isfinite(deltaYaw) ||
        !std::isfinite(cap) || cap <= 0 || cap > 180.f ||
        std::fabs(deltaPitch) > cap || std::fabs(deltaYaw) > cap)
        return false;
    RotSample pitch{}, yaw{};
    if (!readRotation(object(controller, 0x48), true, pitch) ||
        !readRotation(object(controller, 0x50), false, yaw) ||
        pitch.address == yaw.address)
        return false;
    const float px = 2.f * degrees(std::atan2(pitch.value.x, pitch.value.w));
    const float yy = 2.f * degrees(std::atan2(yaw.value.y, yaw.value.w));
    if (!std::isfinite(px) || !std::isfinite(yy) || std::fabs(px) > 89.5f) return false;
    const Quat nextPitch = unityEuler(std::clamp(px + deltaPitch, -89.f, 89.f), 0.f, 0.f);
    const Quat nextYaw = unityEuler(0.f, wrapDegrees(yy + deltaYaw), 0.f);
    if (!stillValid(pitch) || !stillValid(yaw)) return false;
    return writeQuat(pitch.address, nextPitch) && writeQuat(yaw.address, nextYaw);
}

struct DirectState {
    bool saved = false;
    void* controller = nullptr;
    void* player = nullptr;
    void* world = nullptr;
    float pitch = 0, yaw = 0;
    double last = 0;
};
inline DirectState g_direct{};

inline void directReset() { g_direct = {}; }

inline void restoreDirect() {
    if (!g_direct.saved) return;
    auto save = g_direct;
    g_direct.saved = false;
    auto* local = localPlayer();
    if (!local || local != save.player || !save.controller) return;
    ed::PlayerSnapshot state{};
    void* world = nullptr;
    if (!localAlive(local, state, world) || world != save.world) return;
    DirectAngles current{};
    if (!readDirectAngles(save.controller, current)) return;
    applyDirect(save.controller, wrapDegrees(save.pitch - current.pitch),
                wrapDegrees(save.yaw - current.yaw), 180.f);
}

// The external build selects and applies on the render thread from the live
// camera view matrix.  The player callback only publishes the selected point;
// the present path computes the correction relative to the current view and
// writes it to `got`.  This avoids depending on the weapon transform layout.
struct DirectTarget {
    bool valid = false;
    void* controller = nullptr;
    void* player = nullptr;
    void* world = nullptr;
    Vec3 point{}, viewOrigin{}, viewForward{};
    double published = 0;
};
inline DirectTarget g_directTarget{};
inline std::atomic<u64> g_directTargetSequence{0};

inline void publishDirectTarget(bool valid, void* controller, void* player, void* world,
                                Vec3 point, Vec3 viewOrigin = {}, Vec3 viewForward = {}) {
    const u64 sequence = g_directTargetSequence.load(std::memory_order_relaxed);
    g_directTargetSequence.store(sequence + 1, std::memory_order_release);
    std::atomic_thread_fence(std::memory_order_release);
    g_directTarget = {valid, controller, player, world, point, viewOrigin, viewForward, nowSeconds()};
    g_directTargetSequence.store(sequence + 2, std::memory_order_release);
}

inline bool snapshotDirectTarget(DirectTarget& out) {
    for (int attempt = 0; attempt < 4; ++attempt) {
        const u64 before = g_directTargetSequence.load(std::memory_order_acquire);
        if (before & 1) continue;
        out = g_directTarget;
        std::atomic_thread_fence(std::memory_order_acquire);
        if (g_directTargetSequence.load(std::memory_order_acquire) == before) return true;
    }
    return false;
}

// Same source as the external build: the live Unity world-to-camera matrix.
// Column-major storage: value[12..14] is the camera position, value[8..10] is
// the camera back axis, so the view forward is its negation.
struct CameraBasis { Vec3 origin{}, forward{}; };
inline bool cameraBasis(CameraBasis& out) {
    astra::esp::CameraMatrixCache cache{};
    std::uintptr_t camera = 0;
    if (!ed::readCameraForPresent(cache, camera)) return false;
    const auto& v = cache.worldToCamera.value;
    // Exactly the external `matrix` field mapping used by aim_math::angles_to:
    // m11,m12,m13 = v[0],v[1],v[2]; m41,m42,m43 = v[12],v[13],v[14].
    out.origin = {-(v[0] * v[12] + v[1] * v[13] + v[2] * v[14]),
                  -(v[4] * v[12] + v[5] * v[13] + v[6] * v[14]),
                  -(v[8] * v[12] + v[9] * v[13] + v[10] * v[14])};
    out.forward = normalized({-v[2], -v[6], -v[10]});
    return finite(out.origin) && length(out.forward) > .9f;
}

// aim_math::angles_to from the external build: angular delta between the live
// view forward and the target, measured from the view origin.
inline bool anglesTo(const Vec3& target, const CameraBasis& view, float& pitch, float& yaw) {
    pitch = yaw = 0;
    const Vec3 delta = sub(target, view.origin);
    const float flat = std::hypot(delta.x, delta.z);
    const float forwardFlat = std::hypot(view.forward.x, view.forward.z);
    if (!finite(delta) || !std::isfinite(flat) || !std::isfinite(forwardFlat) ||
        flat < .001f || forwardFlat < .001f)
        return false;
    yaw = wrapDegrees((std::atan2(delta.x, delta.z) -
                       std::atan2(view.forward.x, view.forward.z)) * 57.295779513f);
    pitch = (std::atan2(view.forward.y, forwardFlat) -
             std::atan2(delta.y, flat)) * 57.295779513f;
    return std::isfinite(pitch) && std::isfinite(yaw);
}

inline void directAimSelect() {
    auto config = configuration();
    const auto& settings = config.normal;
    if (!settings.enabled || config.silent.enabled || (config.pauseInMenu && g_menuOpen.load())) {
        publishDirectTarget(false, nullptr, nullptr, nullptr, Vec3{});
        g_normalHookReady.store(false, std::memory_order_relaxed);
        return;
    }
    if (!ed::isUnityMainCallback()) return;
    auto* local = localPlayer();
    ed::PlayerSnapshot state{};
    void* world = nullptr;
    if (!local || !localAlive(local, state, world)) {
        publishDirectTarget(false, nullptr, nullptr, nullptr, Vec3{});
        g_normalHookReady.store(false, std::memory_order_relaxed);
        return;
    }
    if (settings.onAttack && !attacking(state.weaponPart)) {
        publishDirectTarget(false, nullptr, nullptr, nullptr, Vec3{});
        g_normalState.store(4);
        return;
    }
    void* controller = componentLoose(local, XS("got"));
    if (!controller) controller = component(local, XS("got"));
    if (!controller) {
        publishDirectTarget(false, nullptr, nullptr, nullptr, Vec3{});
        g_normalHookReady.store(false, std::memory_order_relaxed);
        debug::normalReason.store(7);
        g_normalState.store(3);
        return;
    }
    CameraBasis view{};
    if (!cameraBasis(view)) {
        // The camera snapshot can be one callback behind; the weapon hit
        // transform is a valid fallback for target selection only.
        Vec3 origin{}, forward{};
        if (!readTransformPose(object(state.weaponPart, offsets::field::nrk::kHitTransform), origin, forward)) {
            publishDirectTarget(false, nullptr, nullptr, nullptr, Vec3{});
            debug::normalReason.store(9);
            g_normalState.store(3);
            return;
        }
        view.origin = origin;
        view.forward = forward;
    }
    // The external build does not require its wall data for normal aim.  If the
    // optional visibility profile failed to validate, keep aiming instead of
    // rejecting every target forever.
    Settings selectSettings = settings;
    if (selectSettings.visibleCheck && !g_visibilityReady.load())
        selectSettings.visibleCheck = false;
    auto target = select(selectSettings, world, state.team, view.origin, view.forward, g_lastNormal);
    if (!target.valid) {
        publishDirectTarget(false, nullptr, nullptr, nullptr, Vec3{});
        g_normalState.store(5);
        debug::normalReason.store(10);
        return;
    }
    const Vec3 point = predictPoint(target, settings, view.origin, true);
    if (!finite(point)) {
        publishDirectTarget(false, nullptr, nullptr, nullptr, Vec3{});
        g_normalState.store(5);
        debug::normalReason.store(14);
        return;
    }
    publishDirectTarget(true, controller, local, world, point, view.origin, view.forward);
    g_normalHookReady.store(true, std::memory_order_relaxed);
}

// Called from drawOverlay on the present thread, after the game's camera update.
inline void directAimApply() {
    auto config = configuration();
    const auto& settings = config.normal;
    if (!settings.enabled || config.silent.enabled || (config.pauseInMenu && g_menuOpen.load())) {
        restoreDirect();
        g_normalHookReady.store(false, std::memory_order_relaxed);
        return;
    }
    DirectTarget target{};
    const double now = nowSeconds();
    if (!snapshotDirectTarget(target) || !target.valid || !target.controller ||
        now - target.published > .15) {
        restoreDirect();
        g_normalState.store(5);
        return;
    }
    auto* local = localPlayer();
    ed::PlayerSnapshot state{};
    void* world = nullptr;
    if (!local || local != target.player || !localAlive(local, state, world) || world != target.world) {
        restoreDirect();
        g_normalState.store(3);
        return;
    }
    CameraBasis view{};
    if (!cameraBasis(view)) {
        view.origin = target.viewOrigin;
        view.forward = target.viewForward;
    }
    if (!finite(view.origin) || length(view.forward) < .9f) {
        restoreDirect();
        g_normalState.store(3);
        debug::normalReason.store(9);
        return;
    }
    float pitchDelta = 0, yawDelta = 0;
    if (!anglesTo(target.point, view, pitchDelta, yawDelta)) {
        restoreDirect();
        g_normalState.store(5);
        debug::normalReason.store(14);
        return;
    }
    const double dt = g_direct.last > 0 ? std::clamp(now - g_direct.last, .001, .05) : .016;
    g_direct.last = now;
    // External `aim_math::correction`: 0.5 s or less snaps within 18 deg,
    // otherwise the gain and the 180 deg/s limit are applied.
    float dp = 0, dy = 0;
    const float cap = 18.f;
    if (settings.smoothing < .0005f) {
        dp = std::clamp(pitchDelta, -cap, cap);
        dy = std::clamp(yawDelta, -cap, cap);
    } else {
        const float gain = 1.f - std::exp(-static_cast<float>(dt) * 1000.f / (settings.smoothing * 1000.f));
        const float limit = 180.f * static_cast<float>(dt);
        dp = std::clamp(pitchDelta * gain, -limit, limit);
        dy = std::clamp(yawDelta * gain, -limit, limit);
        dp = std::clamp(dp, -cap, cap);
        dy = std::clamp(dy, -cap, cap);
    }
    DirectAngles current{};
    if (!readDirectAngles(target.controller, current)) {
        restoreDirect();
        g_normalState.store(3);
        debug::normalReason.store(14);
        return;
    }
    if (settings.backCamera && !g_direct.saved) {
        g_direct = {true, target.controller, local, world, current.pitch, current.yaw, now};
        debug::backSaves.fetch_add(1);
    }
    if (applyDirect(target.controller, dp, dy, cap)) {
        g_normalApplications.fetch_add(1);
        g_normalState.store(6);
        debug::normalReason.store(11);
    } else {
        g_normalState.store(3);
        debug::normalReason.store(14);
    }
}

// ── silent aim: ray emitter + network serializers ───────────────────────────
struct SilentVec { float x{}, y{}, z{}; };
struct SilentRay { SilentVec origin{}, direction{}; };
struct SilentSpan { SilentRay* rays = nullptr; std::int32_t count = 0, padding = 0; };
struct SilentLook { u32 has = 1; float pitch = 0, yaw = 0; };
struct SilentCommand { u64 words[10]{}; };
static_assert(sizeof(SilentSpan) == 16, "SilentSpan layout");
static_assert(sizeof(SilentLook) == 12, "SilentLook layout");
static_assert(sizeof(SilentCommand) == 80, "SilentCommand layout");

struct SilentState {
    u32 active = 0;
    u64 owner = 0, weapon = 0;
    SilentVec point{}, forward{}, origin{};
    float pitch = 0, yaw = 0, range = 150, cosFov = 0;
    double expires = 0;
};


struct SilentShot {
    std::atomic<u32> sequence{0};
    u32 key = 0;
    double at = 0;
    SilentVec before{}, desired{};
};
inline SilentShot g_silentShots[128]{};
inline std::atomic<u32> g_silentShotCount{0};
inline SilentState g_silentStateData{};
inline std::atomic<u64> g_silentSequence{0};
inline std::atomic<bool> g_silentHooksReady{false};
inline void* g_rayOriginal = nullptr;
inline void* g_writeOriginal = nullptr;
inline void* g_compressOriginal = nullptr;
inline bool g_rayHooked = false, g_writeHooked = false, g_compressHooked = false;

inline void publishSilent(bool active, u64 owner, u64 weapon, const Vec3& point,
                          const Vec3& forward, const Vec3& origin, float pitch,
                          float yaw, float range, float cosFov = 0) {
    const u64 sequence = g_silentSequence.load(std::memory_order_relaxed);
    g_silentSequence.store(sequence + 1, std::memory_order_release);
    std::atomic_thread_fence(std::memory_order_release);
    g_silentStateData.active = active ? 1u : 0u;
    g_silentStateData.owner = owner;
    g_silentStateData.weapon = weapon;
    g_silentStateData.point = {point.x, point.y, point.z};
    g_silentStateData.forward = {forward.x, forward.y, forward.z};
    g_silentStateData.origin = {origin.x, origin.y, origin.z};
    g_silentStateData.pitch = pitch;
    g_silentStateData.yaw = yaw;
    g_silentStateData.range = range;
    g_silentStateData.cosFov = cosFov;
    g_silentStateData.expires = nowSeconds() + .15;
    g_silentSequence.store(sequence + 2, std::memory_order_release);
}

inline bool snapshotSilent(SilentState& out) {
    for (int attempt = 0; attempt < 4; ++attempt) {
        const u64 before = g_silentSequence.load(std::memory_order_acquire);
        if (before & 1) continue;
        out = g_silentStateData;
        std::atomic_thread_fence(std::memory_order_acquire);
        if (g_silentSequence.load(std::memory_order_acquire) == before) return true;
    }
    return false;
}

inline bool activeSilent(SilentState& out) {
    if (!snapshotSilent(out) || !out.active) return false;
    return nowSeconds() <= out.expires;
}

inline bool saneSilentFloat(float value) {
    return std::isfinite(value) && value > -1000000.f && value < 1000000.f;
}
inline bool saneSilentVec(const SilentVec& value) {
    return saneSilentFloat(value.x) && saneSilentFloat(value.y) && saneSilentFloat(value.z);
}

// Port of the external 1.1.0 solve_look.  Given the original spread ray and the
// desired target direction, find the command look that makes the game's ray
// point at the target.  This is what keeps silent aim server-side correct.
inline bool solveSilentLook(const SilentVec& before, const SilentVec& desired,
                            const SilentLook& input, SilentLook& out, bool wide) {
    constexpr float rad = 0.01745329251994329577f;
    if (input.has != 1 || !saneSilentFloat(input.pitch) || !saneSilentFloat(input.yaw) ||
        !saneSilentVec(before) || !saneSilentVec(desired))
        return false;
    const float inPitch = wrapDegrees(input.pitch);
    const float inYaw = wrapDegrees(input.yaw);
    if (std::fabs(inPitch) > 89.f) return false;
    const float p = inPitch * rad, y = inYaw * rad;
    const float sp = std::sin(p), cp = std::cos(p), sy = std::sin(y), cy = std::cos(y);
    const float vx = cy * before.x - sy * before.z;
    const float b = sy * before.x + cy * before.z;
    const float vy = cp * before.y + sp * b;
    const float vz = -sp * before.y + cp * b;
    const float planar = std::sqrt(vy * vy + vz * vz);
    if (planar < 1e-6f) return false;
    const float ratio = -desired.y / planar;
    if (std::fabs(ratio) > 1.f) return false;
    const float pitch = std::atan2(vy, vz) + std::atan2(ratio, std::sqrt(1.f - ratio * ratio));
    const float yaw = std::atan2(desired.x, desired.z) -
                      std::atan2(vx, std::sin(pitch) * vy + std::cos(pitch) * vz);
    out = {1, pitch / rad, wrapDegrees(yaw / rad)};
    return saneSilentFloat(out.pitch) && saneSilentFloat(out.yaw) &&
           std::fabs(out.pitch) < 89.f &&
           std::fabs(wrapDegrees(out.pitch - inPitch)) < (wide ? 180.f : 45.f) &&
           std::fabs(wrapDegrees(out.yaw - inYaw)) < (wide ? 180.f : 45.f);
}

inline u32 currentCommandToken() {
    u32 token = 0;
    auto* command = g_liveCommand;
    // Read-only, pointer-checked; no IL2CPP API from the ray/network threads.
    if (command && autoNow() - g_liveCommandAt <= 250.0 && field(command, 0x10, token))
        return token;
    return 0;
}

inline void recordSilentShot(const SilentVec& before, const SilentVec& desired) {
    const u32 token = currentCommandToken();
    const u32 slot = g_silentShotCount.fetch_add(1, std::memory_order_relaxed) & 127u;
    auto& shot = g_silentShots[slot];
    u32 sequence = shot.sequence.load(std::memory_order_relaxed) + 1;
    if (sequence & 1u) ++sequence;
    shot.sequence.store(sequence, std::memory_order_release);
    shot.key = token;
    shot.at = nowSeconds();
    shot.before = before;
    shot.desired = desired;
    shot.sequence.store(sequence + 1, std::memory_order_release);
}

inline bool findSilentShot(u32 token, SilentVec& before, SilentVec& desired) {
    const u32 count = g_silentShotCount.load(std::memory_order_acquire);
    const double now = nowSeconds();
    for (u32 i = 0; i < 128u && i < count; ++i) {
        auto& shot = g_silentShots[(count - 1u - i) & 127u];
        const u32 sequence = shot.sequence.load(std::memory_order_acquire);
        if (!sequence || (sequence & 1u)) continue;
        const bool keyMatches = shot.key == token;
        const bool recentUnkeyed = shot.key == 0 && now - shot.at <= .25;
        if (!keyMatches && !recentUnkeyed) continue;
        before = shot.before;
        desired = shot.desired;
        if (shot.sequence.load(std::memory_order_acquire) == sequence &&
            saneSilentVec(before) && saneSilentVec(desired))
            return true;
    }
    return false;
}


struct SilentEncodeCache {
    bool valid = false;
    u32 token = 0, key = 0;
    u16 attack = 0;
    SilentLook look{};
    float mx = 0, my = 0;
};
inline SilentEncodeCache g_silentEncode[256]{};

inline bool silentEnabled() {
    auto config = configuration();
    return config.silent.enabled && !(config.pauseInMenu && g_menuOpen.load());
}

inline void transformCommand(SilentCommand& command) {
    if (!silentEnabled()) return;
    auto* bytes = reinterpret_cast<u8*>(&command);
    const u16 attack = static_cast<u16>(bytes[0x32]) | (static_cast<u16>(bytes[0x33]) << 8);
    if (!attack) return;
    auto* look = reinterpret_cast<SilentLook*>(bytes + 0x1c);
    auto* move = reinterpret_cast<float*>(bytes + 0x28);
    if (look->has != 1 || !saneSilentFloat(look->pitch) || !saneSilentFloat(look->yaw)) return;
    const u32 token = *reinterpret_cast<const u32*>(bytes + 0x10);
    const u32 key = *reinterpret_cast<const u32*>(bytes + 0x14);
    auto& cache = g_silentEncode[token & 255u];
    if (cache.valid && cache.token == token && cache.key == key && cache.attack == attack) {
        *look = cache.look;
        move[0] = cache.mx;
        move[1] = cache.my;
        return;
    }
    SilentVec before{}, desired{};
    SilentLook corrected{};
    if (findSilentShot(token, before, desired) &&
        solveSilentLook(before, desired, *look, corrected, false)) {

        const float mx = move[0], my = move[1];
        if (saneSilentFloat(mx) && saneSilentFloat(my) &&
            std::fabs(mx) <= 1.001f && std::fabs(my) <= 1.001f) {
            const float delta = radians(wrapDegrees(corrected.yaw - look->yaw));
            const float cs = std::cos(delta), sn = std::sin(delta);
            move[0] = cs * mx + sn * my;
            move[1] = -sn * mx + cs * my;
        }
        *look = corrected;
    }
    cache = {true, token, key, attack, *look, move[0], move[1]};
}

inline void silentRayHook(void* weapon, void* spanPointer, void* context, void* method) {
    using Original = void (*)(void*, void*, void*, void*);
    auto original = reinterpret_cast<Original>(g_rayOriginal);
    if (original) original(weapon, spanPointer, context, method);

    SilentState state{};
    if (!activeSilent(state) || !spanPointer || !context) return;
    auto* span = reinterpret_cast<SilentSpan*>(spanPointer);
    if (span->count != 1 || !span->rays) return;
    u64 owner = 0, weaponId = 0;
    if (!ed::readValue(context, owner) || !ed::readValue(reinterpret_cast<u8*>(context) + 0x50, weaponId))
        return;
    if (owner != state.owner || weaponId != state.weapon) return;
    auto& ray = span->rays[0];
    const float dx = state.point.x - ray.origin.x;
    const float dy = state.point.y - ray.origin.y;
    const float dz = state.point.z - ray.origin.z;
    const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (!std::isfinite(distance) || distance < .1f || distance > state.range) return;
    const SilentVec desired{dx / distance, dy / distance, dz / distance};
    if (!saneSilentVec(desired) || !saneSilentVec(ray.direction) || !saneSilentVec(state.forward))
        return;
    const float rayNorm = ray.direction.x * ray.direction.x + ray.direction.y * ray.direction.y +
                          ray.direction.z * ray.direction.z;
    if (!(rayNorm > .99f && rayNorm < 1.01f)) return;
    if (state.cosFov > 0.f) {
        const float dot = desired.x * state.forward.x + desired.y * state.forward.y +
                          desired.z * state.forward.z;
        if (dot < state.cosFov) return;
    }
    recordSilentShot(ray.direction, desired);
    ray.direction = desired;
}

inline void silentWriteHook(void* self, void* stream, void* resolver,
                            const SilentCommand* command, void* method) {
    using Original = void (*)(void*, void*, void*, const SilentCommand*, void*);
    auto original = reinterpret_cast<Original>(g_writeOriginal);
    if (!original) return;
    if (!command || !silentEnabled()) {
        original(self, stream, resolver, command, method);
        return;
    }
    SilentCommand copy = *command;
    transformCommand(copy);
    original(self, stream, resolver, &copy, method);
}

inline void silentCompressHook(void* self, void* stream, void* resolver, void* compressor,
                               const SilentCommand* current, const SilentCommand* previous, void* method) {
    using Original = void (*)(void*, void*, void*, void*, const SilentCommand*, const SilentCommand*, void*);
    auto original = reinterpret_cast<Original>(g_compressOriginal);
    if (!original) return;
    if (!current || !previous || !silentEnabled()) {
        original(self, stream, resolver, compressor, current, previous, method);
        return;
    }
    SilentCommand a = *current, b = *previous;
    transformCommand(a);
    transformCommand(b);
    original(self, stream, resolver, compressor, &a, &b, method);
}

inline bool signatureAt(std::uintptr_t address, std::initializer_list<u32> words) {
    unsigned index = 0;
    for (u32 expected : words) {
        u32 actual = 0;
        if (!ed::readValue(reinterpret_cast<void*>(address + index * 4), actual) || actual != expected)
            return false;
        ++index;
    }
    return true;
}

inline bool installSilentHooks() {
#if defined(__aarch64__)
    if (g_silentHooksReady.load(std::memory_order_acquire)) return true;
    static double lastAttempt = 0;
    const double attemptNow = nowSeconds();
    if (attemptNow - lastAttempt < 1.0) return false;
    lastAttempt = attemptNow;
    auto base = ed::g_runtime.unityBase;
    if (!base) return false;
    // The RVAs and first four instructions are the working external 1.1.0
    // profile.  Validate every site before touching text.
    constexpr std::uintptr_t kRay = 0x9CB0A44;
    constexpr std::uintptr_t kWrite = 0x800A3D4;
    constexpr std::uintptr_t kCompress = 0x7FFC6F4;
    if (!signatureAt(base + kRay, {0xd10683ff, 0x6d1533ed, 0x6d162beb, 0x6d1723e9}) ||
        !signatureAt(base + kWrite, {0xd10243ff, 0x6d0423e9, 0xf9002bfe, 0xa9065ff8}) ||
        !signatureAt(base + kCompress, {0xd10303ff, 0x6d042beb, 0x6d0523e9, 0xa9067bfd})) {
        ASTRA_LOG(XS("ported silent: 1.1.0 signatures mismatch"));
        return false;
    }
    if (!g_rayHooked) {
        if (!xev::hook::trampoline(reinterpret_cast<void*>(base + kRay),
                                   reinterpret_cast<void*>(&silentRayHook), &g_rayOriginal, 16)) {
            ASTRA_LOG(XS("ported silent: ray trampoline failed"));
            return false;
        }
        g_rayHooked = true;
    }
    if (!g_writeHooked) {
        if (!xev::hook::trampoline(reinterpret_cast<void*>(base + kWrite),
                                   reinterpret_cast<void*>(&silentWriteHook), &g_writeOriginal, 16)) {
            ASTRA_LOG(XS("ported silent: write trampoline failed"));
            return false;
        }
        g_writeHooked = true;
    }
    if (!g_compressHooked) {
        if (!xev::hook::trampoline(reinterpret_cast<void*>(base + kCompress),
                                   reinterpret_cast<void*>(&silentCompressHook), &g_compressOriginal, 16)) {
            ASTRA_LOG(XS("ported silent: compress trampoline failed"));
            return false;
        }
        g_compressHooked = true;
    }
    g_silentHooksReady.store(true, std::memory_order_release);
    g_silentHookReady.store(true, std::memory_order_release);
    ASTRA_LOG(XS("ported silent hooks ready (ray + write + compress)"));
    return true;
#else
    return false;
#endif
}

inline void silentTick() {
    auto config = configuration();
    const auto& settings = config.silent;
    if (!settings.enabled || (config.pauseInMenu && g_menuOpen.load())) {
        publishSilent(false, 0, 0, {}, {}, {}, 0, 0, 0);
        g_silentState.store(0);
        return;
    }
    if (!ed::isUnityMainCallback()) return;
    if (!installSilentHooks()) {
        g_silentState.store(1);
        return;
    }
    auto* local = localPlayer();
    ed::PlayerSnapshot state{};
    void* world = nullptr;
    if (!local || !localAlive(local, state, world)) {
        publishSilent(false, 0, 0, {}, {}, {}, 0, 0, 0);
        g_silentState.store(3);
        return;
    }
    Vec3 origin{}, forward{};
    if (!transformPose(object(state.weaponPart, offsets::field::nrk::kHitTransform), origin, forward)) {
        publishSilent(false, 0, 0, {}, {}, {}, 0, 0, 0);
        g_silentState.store(3);
        return;
    }
    auto target = selectSilent(settings, world, state.team, origin, forward, g_lastSilent);
    if (!target.valid) {
        publishSilent(false, 0, 0, {}, {}, {}, 0, 0, 0);
        g_silentState.store(5);
        return;
    }
    const Vec3 point = predictPoint(target, settings, origin, true);
    const auto angle = anglesFor(sub(point, origin));
    if (!finite(point) || !std::isfinite(angle.pitch) || !std::isfinite(angle.yaw)) {
        publishSilent(false, 0, 0, {}, {}, {}, 0, 0, 0);
        g_silentState.store(5);
        return;
    }
    u64 owner = 0;
    if (!field(local, 0x10, owner) || !owner) {
        publishSilent(false, 0, 0, {}, {}, {}, 0, 0, 0);
        g_silentState.store(3);
        return;
    }
    publishSilent(true, owner, reinterpret_cast<u64>(state.activeWeapon),
                  point, forward, origin, angle.pitch, angle.yaw, settings.maxDistance,
                  settings.fov < 179.f ? std::cos(radians(settings.fov)) : 0.f);
    g_silentState.store(6);
}

}  // namespace ported
