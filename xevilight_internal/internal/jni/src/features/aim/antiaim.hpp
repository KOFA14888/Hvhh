
// ================================================================================
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Telegram Bot: t.me/imguisource_bot
// ImGui Source: t.me/ImGui_Source_FIXNEY
// shit src 🧨: t.me/il2cpp_src
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ================================================================================


// антипопадайки💀💀

namespace antiaim {

namespace model {
struct Quat { float x, y, z, w; };
using QuatFn = Quat (*)(void*);
using FloatFn = float (*)(void*);
inline constexpr std::uintptr_t kBodyGetter = 0xAA8ADBC;
inline constexpr std::uintptr_t kLookGetter = 0xAA8B700;
inline constexpr std::uintptr_t kVerticalGetter = 0xAA8CC98;

inline int g_state = 0;  // 0 = untouched, 1 = ready, <0 = failed

inline void* g_data = nullptr;
inline double g_at = 0;
inline Quat g_body{}, g_look{};
inline float g_vertical = 0;
inline std::atomic<bool> g_live{false};

inline bool live(void* self) {
    return g_live.load(std::memory_order_relaxed) && self && self == g_data &&
           autoNow() - g_at <= 250.0;
}



inline Quat readQuat(const void* self, std::size_t offset) {
    const auto* f = reinterpret_cast<const float*>(static_cast<const char*>(self) + offset);
    return Quat{f[0], f[1], f[2], f[3]};
}
inline Quat detourBody(void* self) {
    if (live(self)) return g_body;
    return self ? readQuat(self, 0x00) : Quat{};
}
inline Quat detourLook(void* self) {
    if (live(self)) return g_look;
    return self ? readQuat(self, 0x10) : Quat{};
}
inline float detourVertical(void* self) {
    if (live(self)) return g_vertical;
    return self ? *reinterpret_cast<const float*>(static_cast<const char*>(self) + 0xA8) : 0.f;
}

inline void install(std::uintptr_t base) {
    if (g_state) return;
    g_state = -1;
    if (!base) return;
    const auto body = base + kBodyGetter, look = base + kLookGetter, vertical = base + kVerticalGetter;
    if (!bytesMatch(body, {0x2d460400, 0x2d470c02, 0xd65f03c0}) ||
        !bytesMatch(look, {0x2d4d0400, 0x2d4e0c02, 0xd65f03c0}) ||
        !bytesMatch(vertical, {0xbd40a800, 0xd65f03c0}))
        return;
    const bool a = xev::hook::jump(reinterpret_cast<void*>(body),
                                   reinterpret_cast<void*>(&detourBody));
    const bool b = xev::hook::jump(reinterpret_cast<void*>(look),
                                   reinterpret_cast<void*>(&detourLook));
    const bool c = xev::hook::jump(reinterpret_cast<void*>(vertical),
                                   reinterpret_cast<void*>(&detourVertical));
    g_state = (a && b && c) ? 1 : -2;
}

/

inline void publish(void* self, float pitch, float yaw) {
    if (!self || !std::isfinite(pitch) || !std::isfinite(yaw)) {
        g_live.store(false, std::memory_order_relaxed);
        return;
    }
    const float half = yaw * 0.008726646259971648f;
    g_body = {0.f, std::sin(half), 0.f, std::cos(half)};
    g_look = g_body;
    g_vertical = std::clamp(pitch / 90.f, -1.f, 1.f);
    g_data = self;
    g_at = autoNow();
    g_live.store(true, std::memory_order_relaxed);
    install(ed::g_runtime.unityBase);
}

inline void clear() { g_live.store(false, std::memory_order_relaxed); }
}  // namespace model

inline float wrapYaw(float y) { return std::isfinite(y) ? std::remainder(y, 360.f) : 0.f; }

struct Plan {
    bool valid = false;
    Angles angles{};
};


inline Plan plan(const Angles& real, double nowMs, const AntiAim& aa) {
    Plan out{};
    if (!std::isfinite(real.pitch) || !std::isfinite(real.yaw) || !std::isfinite(nowMs)) return out;

    float yaw = real.yaw + aa.offset;
    if (aa.yawMode == 1) {
        yaw = real.yaw + aa.offset + float(std::fmod(nowMs * double(aa.speed) / 1000.0, 360.0));
    } else if (aa.yawMode == 2) {
        const bool far = std::fmod(nowMs * double(aa.flickRate) / 1000.0, 2.0) >= 1.0;
        yaw = real.yaw + aa.offset + (far ? aa.flickAngle : -aa.flickAngle);
    }

    float pitch = real.pitch;
    if (aa.pitchMode == 0) {
        pitch = -89.f;  // up
    } else if (aa.pitchMode == 1) {
        pitch = 89.f;  // down
    } else {
        // fake down: mostly down, snapped up on the fake window
        const bool fake = std::fmod(nowMs * double(aa.fakeRate) / 1000.0, 2.0) >= 1.0;
        pitch = fake ? -89.f : 89.f;
    }

    out.valid = true;
    out.angles = {std::clamp(pitch, -89.f, 89.f), wrapYaw(yaw)};
    return out;
}

struct State {
    bool valid = false;
    silent_command::Identity id{};
    double at = 0;
    CommandLook value{};
};
inline State g_state{};

inline bool active(void* cmd) {
    if (!g_state.valid || !cmd) return false;
    void* p = localPlayer();
    void* world = nullptr;
    ed::PlayerSnapshot local{};
    if (!localAlive(p, local, world)) return false;
    const auto identity = silentIdentity(cmd, p, world, local.activeWeapon);
    return silent_command::same(g_state.id, identity) && autoNow() - g_state.at <= 120.0;
}

inline void spoof(void* cmd) {
    g_state.valid = false;
    model::clear();
    auto c = configuration();
    if (!c.antiAim.enabled || !g_commandLook || !cmd) return;
    void* p = localPlayer();
    void* world = nullptr;
    ed::PlayerSnapshot local{};
    if (!localAlive(p, local, world)) return;
    const auto identity = silentIdentity(cmd, p, world, local.activeWeapon);
    if (!identity.command || !ed::writable(reinterpret_cast<void*>(identity.command + 0x1c), 12)) return;

    Angles real{};
    CommandLook look{};
    if (field(cmd, 0x1c, look) && look.has == 1 && std::isfinite(look.pitch) && std::isfinite(look.yaw)) {
        real = {look.pitch, look.yaw};
    } else {
        Vec3 origin{}, forward{};
        if (!transformPose(object(local.weaponPart, offsets::field::nrk::kHitTransform), origin, forward))
            return;
        real = anglesFor(forward);
    }

    const auto planned = plan(real, autoNow(), c.antiAim);
    if (!planned.valid) return;
    const CommandLook value{1, {0, 0, 0}, planned.angles.pitch, planned.angles.yaw};
    g_commandLook(cmd, value, nullptr);

    CommandLook actual{};
    if (!field(cmd, 0x1c, actual) || actual.has != 1 || actual.pitch != value.pitch ||
        actual.yaw != value.yaw)
        return;
    g_state = {true, identity, autoNow(), value};

    if (c.antiAim.localModel) {.
        std::uintptr_t data = 0;
        void* anim = component(p, XS("chc"));
        if (anim && field(anim, 0xa0, data) && data > 0x10000 &&
            ed::readable(reinterpret_cast<void*>(data), 0xB0))
            model::publish(reinterpret_cast<void*>(data), planned.angles.pitch,
                           planned.angles.yaw);
    }
}
}  // namespace antiaim
