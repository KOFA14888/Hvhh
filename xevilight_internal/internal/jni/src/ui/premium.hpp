
// ================================================================================
// Bot: @imguisource_bot | TG: @ImGui_Source_FIXNEY & @il2cpp_src
// ================================================================================

// ASTRA_MULOAD_GUI_V2
// FloatingModMenu / BlackStyle shell + Astra feature set.
// Layout, fonts, colors, navigation and launcher are ported from the
// muload project; tabs and feature wiring stay Astra's.
#pragma once
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"
#include "features/aim/core.hpp"
#include "features/aim/assists.hpp"
#include "features/visuals/esp_runtime.hpp"
#include "ui/fonts/muload_fonts.hpp"
#include "ui/muload/theme.hpp"

// Defined in entry.cpp right above this include.
extern std::vector<EspScreenBox> g_espScreenBoxes;

// ── session state ────────────────────────────────────────────────────────────
static ImVec4 g_accent4{.00f, .26f, .62f, 1.f};
static ImVec4 g_espBoxCol{.94f, .95f, .97f, 1.f}, g_espHpCol{.36f, .87f, .65f, 1.f};
static ImVec4 g_espWepCol{.91f, .92f, .95f, 1.f}, g_espAmmoCol{.68f, .72f, .79f, 1.f};
static float g_S = 1.0f;
static bool menuOpen() { return g_menuOpen.load(std::memory_order_relaxed); }
static void setMenuOpen(bool open) { g_menuOpen.store(open, std::memory_order_relaxed); }
static int g_tab = 0;
static bool previewEnabled = true;
static float g_rainbowPhase = 0.0f;
static ImVec2 g_menuPos{-1.0f, -1.0f};
static ImVec2 g_menuScreen{0.0f, 0.0f};

struct KwB {
    bool esp_enabled = false, esp_box = true, esp_hp = true, esp_armor = true;
    bool esp_wep = true, esp_ammo = true;
    bool watermark = false;
};
static KwB g_b;

static const char* tabs[] = {XS("Aim"), XS("Visuals"), XS("Silent"), XS("Anti-Aim"), XS("Config")};

#include "premium_esp_extra.hpp"
#include "premium_arrows.hpp"

// ── aim projection cache (unchanged from Astra) ──────────────────────────────
namespace astra_premium {
struct Projection {
    float width = 0, height = 0, p00 = 0, p11 = 0, shiftX = 0, shiftY = 0;
    double captured = 0;
};
inline std::mutex projectionMutex;
inline Projection projection;
inline double monotonicSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
inline void setAimProjection(float w, float h, float p00, float p11, float sx = 0, float sy = 0) {
    std::lock_guard<std::mutex> lock(projectionMutex);
    projection = {w, h, p00, p11, sx, sy, monotonicSeconds()};
}
inline void clearAimProjection() {
    std::lock_guard<std::mutex> lock(projectionMutex);
    projection = {};
}
}

static void updateAimProjection() {
    if (!astra::aim::configuration().normal.showFov) {
        astra_premium::clearAimProjection();
        return;
    }
    astra::esp::CameraMatrixCache m{};
    std::uintptr_t camera = 0;
    if (!astra::esp::detail::readCameraForPresent(m, camera)) {
        astra_premium::clearAimProjection();
        return;
    }
    const auto& p = m.projection.value;
    float scale = -p[11];
    if (!std::isfinite(scale) || scale <= .0001f) {
        astra_premium::clearAimProjection();
        return;
    }
    for (int index : {1, 3, 4, 7, 12, 13, 15})
        if (std::fabs(p[index] / scale) > .001f) {
            astra_premium::clearAimProjection();
            return;
        }
    auto size = ImGui::GetIO().DisplaySize;
    float p00 = std::fabs(p[0] / scale), p11 = std::fabs(p[5] / scale);
    float sx = p[8] / scale, sy = p[9] / scale;
    auto g = astra::aim::assists::fovGeometry(
        astra::aim::configuration().normal.fov, size.x, size.y, p00, p11, sx, sy);
    if (!g.valid) {
        astra_premium::clearAimProjection();
        return;
    }
    astra_premium::setAimProjection(size.x, size.y, p00, p11, sx, sy);
}

static astra::aim::assists::FovGeometry currentFovGeometry(float degrees) {
    astra_premium::Projection p;
    {
        std::lock_guard<std::mutex> lock(astra_premium::projectionMutex);
        p = astra_premium::projection;
    }
    double age = astra_premium::monotonicSeconds() - p.captured;
    auto display = ImGui::GetIO().DisplaySize;
    if (age < 0 || age >= .25 || std::fabs(p.width - display.x) > 1 ||
        std::fabs(p.height - display.y) > 1)
        return {};
    return astra::aim::assists::fovGeometry(degrees, p.width, p.height, p.p00, p.p11, p.shiftX, p.shiftY);
}

static void renderFov(const astra::aim::Settings& settings) {
    if (!settings.showFov) return;
    auto g = currentFovGeometry(settings.fov);
    if (!g.valid || g.coversScreen) return;
    auto* d = ImGui::GetBackgroundDrawList();
    d->PushClipRect(ImVec2(0, 0), ImGui::GetIO().DisplaySize, true);
    d->PathClear();
    for (int i = 0; i <= 256; ++i) {
        float a = float(i) * 6.28318530718f / 256;
        d->PathLineTo(ImVec2(g.centerX + std::cos(a) * g.radiusX, g.centerY + std::sin(a) * g.radiusY));
    }
    d->PathStroke(ImGui::ColorConvertFloat4ToU32(g_accent4), 0, 2.0f);
    d->PopClipRect();
}

// ── ESP overlay (Astra rendering, unchanged) ─────────────────────────────────
static void renderEsp() {
    if (!g_b.esp_enabled || !g_espBoxesEnabled.load()) return;
    std::vector<EspScreenBox> boxes;
    {
        std::lock_guard<std::mutex> lock(g_espMutex);
        boxes = g_espScreenBoxes;
    }
    auto* d = ImGui::GetBackgroundDrawList();
    d->PushClipRect(ImVec2(0, 0), ImGui::GetIO().DisplaySize, true);
    for (const auto& b : boxes) astra_esp_extra::draw(d, b);
    d->PopClipRect();
}

// ── preview (Astra tuning helper) ────────────────────────────────────────────
static void preview() {
    const float w = std::max(1.f, ImGui::GetContentRegionAvail().x);
    const auto p = ImGui::GetCursorScreenPos();
    const float h = 260.0f, cx = p.x + w * .5f;
    ImGui::Dummy(ImVec2(w, h));
    auto* d = ImGui::GetWindowDrawList();
    d->PushClipRect(p, ImVec2(p.x + w, p.y + h), true);
    d->AddRectFilled(p, ImVec2(p.x + w, p.y + h), IM_COL32(10, 10, 10, 255), 3.f);
    for (float x = p.x + 20; x < p.x + w; x += 24) d->AddLine(ImVec2(x, p.y), ImVec2(x, p.y + h), IM_COL32(255, 255, 255, 5));
    for (float y = p.y + 20; y < p.y + h; y += 24) d->AddLine(ImVec2(p.x, y), ImVec2(p.x + w, y), IM_COL32(255, 255, 255, 5));
    const float top = p.y + 25, bottom = p.y + h - 65, bw = std::min(w * .42f, 80.f);
    const float bodyHeight = bottom - top;
    auto body = IM_COL32(62, 66, 76, 255);
    d->AddCircleFilled(ImVec2(cx, top + bodyHeight * .09f), bodyHeight * .065f, body, 24);
    d->AddRectFilled(ImVec2(cx - bw * .17f, top + bodyHeight * .18f),
                     ImVec2(cx + bw * .17f, top + bodyHeight * .59f), body, 5);
    for (int side : {-1, 1}) {
        d->AddLine(ImVec2(cx + side * bw * .22f, top + bodyHeight * .22f),
                   ImVec2(cx + side * bw * .33f, top + bodyHeight * .62f), body, 8);
        d->AddLine(ImVec2(cx + side * bw * .10f, top + bodyHeight * .56f),
                   ImVec2(cx + side * bw * .20f, bottom - 4), body, 9);
    }
    if (g_b.esp_enabled) {
        EspScreenBox b{};
        b.left = cx - bw * .5f;
        b.right = cx + bw * .5f;
        b.top = top;
        b.bottom = bottom;
        b.enemy = true;
        b.statsValid = true;
        b.weaponValid = true;
        b.hp = 74;
        b.maxHp = 100;
        b.armor = 60;
        b.maxArmor = 100;
        if (astra_esp_extra::settings.lowHealth) b.hp = 20;
        b.weaponId = 7;
        b.magazineAmmo = astra_esp_extra::settings.emptyMagazine ? 0 : 24;
        b.reserveAmmo = 90;
        b.distanceUnits = 24.f;
        b.distanceValid = true;
        astra_esp_extra::draw(d, b);
    }
    d->AddRect(p, ImVec2(p.x + w, p.y + h), IM_COL32(40, 40, 40, 255), 3.f);
    d->PopClipRect();
}

// ── tab content ──────────────────────────────────────────────────────────────
static astra::aim::Configuration edit;
static bool dirty = false, resetRequested = false;

static void mark(bool changed) {
    if (changed) dirty = true;
}

static void aimTab() {
    if (ImGui::CollapsingHeader(XS("Aimbot"), ImGuiTreeNodeFlags_DefaultOpen)) {
        mark(ImGui::Checkbox(XS("Enabled##aim"), &edit.normal.enabled));
        mark(ImGui::Checkbox(XS("Visible check##aim"), &edit.normal.visibleCheck));
        mark(ImGui::SliderFloat(XS("FOV##aim"), &edit.normal.fov, .1f, 180.f, XS("%.1f deg")));
        mark(ImGui::Checkbox(XS("Show FOV##aim"), &edit.normal.showFov));
        mark(ImGui::SliderFloat(XS("Smoothing##aim"), &edit.normal.smoothing, 0.f, 1.f, XS("%.3f s")));
        ImGui::TextDisabled(XS("0 = instant; higher = smoother."));
    }
    if (ImGui::CollapsingHeader(XS("Recoil control"))) {
        mark(ImGui::Checkbox(XS("RCS##aim"), &edit.normal.rcs));
        mark(ImGui::SliderFloat(XS("RCS strength##aim"), &edit.normal.rcsStrength, 0.f, 100.f, XS("%.0f%%")));
    }
    if (ImGui::CollapsingHeader(XS("Target selection"), ImGuiTreeNodeFlags_DefaultOpen)) {
        for (unsigned i = 0; i < unsigned(astra::aim::Body::Count); ++i) {
            bool value = (edit.normal.bodies & (1u << i)) != 0;
            if (ImGui::Checkbox(astra::aim::kBodyNames[i], &value)) {
                if (value)
                    edit.normal.bodies |= 1u << i;
                else
                    edit.normal.bodies &= ~(1u << i);
                dirty = true;
            }
        }
        if (!edit.normal.bodies) ImGui::TextDisabled(XS("Select at least one body region."));

        ImGui::Separator();
        if (!edit.normal.enabled)
            ImGui::TextDisabled(XS("Status: disabled"));
        else if (edit.pauseInMenu && menuOpen())
            ImGui::TextDisabled(XS("Status: paused while menu is open"));
        else if (!astra::aim::g_normalHookReady.load())
            ImGui::TextDisabled(XS("Status: waiting for angle runtime hook"));
        else
            ImGui::Text(XS("Status: %s"), astra::aim::stateText(astra::aim::g_normalState.load()));
        if (edit.normal.visibleCheck && !astra::aim::g_visibilityReady.load())
            ImGui::TextDisabled(XS("Visibility profile not ready; targeting is blocked."));
        if (edit.normal.rcs && !astra::aim::g_rcsReady.load())
            ImGui::TextDisabled(XS("RCS profile not ready"));

        ImGui::Spacing();
        ImGui::Text(XS("RCS: enabled=%d ready=%d strength=%.0f"), int(edit.normal.rcs),
                    int(astra::aim::g_rcsReady.load()), double(edit.normal.rcsStrength));
        ImGui::Text(XS("Reads: %llu | Rejected: %llu | Applied: %llu"),
                    (unsigned long long)astra::aim::debug::rcsReads.load(),
                    (unsigned long long)astra::aim::debug::rcsRejected.load(),
                    (unsigned long long)astra::aim::debug::rcsApplied.load());
        ImGui::Text(XS("Last recoil: pitch=%.4f yaw=%.4f"), double(astra::aim::debug::recoilPitch.load()),
                    double(astra::aim::debug::recoilYaw.load()));
        ImGui::Text(XS("Attack raw: %d"), astra::aim::debug::attackRaw.load());
        if (edit.normal.showFov) {
            auto g = currentFovGeometry(edit.normal.fov);
            if (!g.valid)
                ImGui::TextDisabled(XS("FOV overlay: waiting for fresh camera projection"));
            else if (g.coversScreen)
                ImGui::TextDisabled(XS("FOV covers the screen; outline is outside the view."));
        }
    }
}

static void visualsTab() {
    if (ImGui::CollapsingHeader(XS("ESP"), ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox(XS("Enabled##esp"), &g_b.esp_enabled);
        ImGui::Checkbox(XS("Bounding box##esp"), &g_b.esp_box);
        ImGui::ColorEdit4(XS("Box color##esp"), &g_espBoxCol.x, ImGuiColorEditFlags_NoInputs);
        ImGui::Checkbox(XS("Health bar##esp"), &g_b.esp_hp);
        ImGui::ColorEdit4(XS("Health color##esp"), &g_espHpCol.x, ImGuiColorEditFlags_NoInputs);
        ImGui::Checkbox(XS("Armor bar##esp"), &g_b.esp_armor);
        ImGui::ColorEdit4(XS("Armor color##esp"), &astra_esp_extra::settings.armorColor.x, ImGuiColorEditFlags_NoInputs);
        ImGui::Checkbox(XS("Weapon info##esp"), &g_b.esp_wep);
        ImGui::ColorEdit4(XS("Weapon color##esp"), &g_espWepCol.x, ImGuiColorEditFlags_NoInputs);
        ImGui::Checkbox(XS("Ammo##esp"), &g_b.esp_ammo);
        ImGui::ColorEdit4(XS("Ammo color##esp"), &g_espAmmoCol.x, ImGuiColorEditFlags_NoInputs);
    }
    if (ImGui::CollapsingHeader(XS("ESP style"))) {
        auto& a = astra_esp_extra::settings;
        ImGui::Combo(XS("Frame style##esp"), &astra_esp_extra::frameStyle,
                     XS("Classic\0Corners\0Rounded\0Dashed\0Side brackets\0"));
        ImGui::Checkbox(XS("Box fill##esp"), &a.fill);
        ImGui::SliderFloat(XS("Fill opacity##esp"), &a.fillAlpha, 0.f, .35f, XS("%.2f"));
        ImGui::Checkbox(XS("Snaplines##esp"), &a.snaplines);
        ImGui::Checkbox(XS("Box head marker##esp"), &a.headMarker);
        ImGui::SliderFloat(XS("Line width##esp"), &a.thickness, 1.f, 4.f, XS("%.1f"));
        ImGui::SliderFloat(XS("ESP text scale##esp"), &a.textScale, .5f, 2.f, XS("%.2f"));
        ImGui::Checkbox(XS("Text shadow##esp"), &a.shadow);
        ImGui::Checkbox(XS("Text outline##esp"), &a.outline);
        ImGui::Checkbox(XS("HP / armor divisions##esp"), &astra_esp_extra::barDivisions);
        ImGui::Checkbox(XS("Soft frame halo##esp"), &astra_esp_extra::softHalo);
        ImGui::SliderFloat(XS("Halo opacity##esp"), &astra_esp_extra::haloOpacity, 0.f, .25f, XS("%.2f"));
        ImGui::Checkbox(XS("Arrows##esp"), &astra_arrows::settings.enabled);
        ImGui::ColorEdit4(XS("Arrow color##esp"), &astra_arrows::settings.color.x, ImGuiColorEditFlags_NoInputs);
        ImGui::Checkbox(XS("Arrow halo##esp"), &astra_arrows::settings.glow);
        ImGui::SliderFloat(XS("Arrow size##esp"), &astra_arrows::settings.size, 7.f, 24.f, XS("%.0f px"));
        ImGui::SliderFloat(XS("Arrow inset##esp"), &astra_arrows::settings.inset, 30.f, 180.f, XS("%.0f px"));
    }
    if (ImGui::CollapsingHeader(XS("Information"))) {
        auto& info = astra_esp_extra::settings;
        ImGui::Checkbox(XS("Distance##info"), &info.distance);
        ImGui::SliderFloat(XS("Meters per world unit##info"), &info.metersPerUnit, .01f, 10.f, XS("%.2f"));
        ImGui::Checkbox(XS("HP number##info"), &info.hpNumber);
        ImGui::Checkbox(XS("Armor number##info"), &info.armorNumber);
        ImGui::Checkbox(XS("Low health marker##info"), &info.lowHealth);
        ImGui::SliderFloat(XS("Low HP threshold##info"), &info.lowHealthPercent, 5.f, 50.f, XS("%.0f%%"));
        ImGui::Checkbox(XS("Empty magazine marker##info"), &info.emptyMagazine);
        ImGui::TextDisabled(XS("Distance requires fresh local and enemy positions."));
    }
    if (ImGui::CollapsingHeader(XS("Third person / Camera test V2"))) {
        ImGui::PushID(XS("astra-camera-panel"));
        astra_camera_test::controls();
        ImGui::PopID();
    }
    if (ImGui::CollapsingHeader(XS("Hitlog / Hitsound"))) {
        bool enabled = astra_hitlog::enabled.load();
        if (ImGui::Checkbox(XS("Hitlog##hitlog"), &enabled)) {
            astra_hitlog::enabled.store(enabled);
            if (!enabled) {
                std::lock_guard<std::mutex> lock(astra_hitlog::mutex);
                astra_hitlog::lines.clear();
                astra_hitlog::localWorld = nullptr;
            }
        }
        const int state = astra_hitlog::state.load();
        ImGui::TextDisabled(XS("%s"), state == 2 ? XS("Hook installed")
                               : state == -1 ? XS("Code mismatch")
                               : state == -2 ? XS("Hook installation failed")
                               : state == 1 ? XS("Installing...")
                                            : XS("Waiting for enabled / local player"));
        ImGui::Text(XS("Events: %llu | Local hits: %llu"), astra_hitlog::received.load(),
                    astra_hitlog::accepted.load());
        astra_hitlog::deathDiagnostics();
    }
    if (ImGui::CollapsingHeader(XS("GPU Night mode"))) {
        astra_night_diag::controls();
    }
    if (ImGui::CollapsingHeader(XS("Preview"))) {
        ImGui::Checkbox(XS("Show ESP preview##preview"), &previewEnabled);
        if (previewEnabled) preview();
    }
}

static void silentTab() {
    static bool sa_enabled = false, sa_vischeck = false, sa_backtrack = false, sa_prediction = false;
    static float sa_fov = 15.f, sa_smooth = 0.05f, sa_backtrack_ms = 200.f;
    static int sa_hitbox = 0;
    static const char* sa_hitboxes = {};
    ImGui::Checkbox(XS("Silent Aim##silent"), &sa_enabled);
    ImGui::TextDisabled(XS("Fires at target position without moving crosshair."));
    if (ImGui::CollapsingHeader(XS("Target"), ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat(XS("FOV##silent"), &sa_fov, 1.f, 180.f, XS("%.1f deg"));
        ImGui::SliderFloat(XS("Smoothing##silent"), &sa_smooth, 0.f, 1.f, XS("%.3f s"));
        ImGui::Combo(XS("Hitbox##silent"), &sa_hitbox, sa_hitboxes);
    }
    if (ImGui::CollapsingHeader(XS("Options"))) {
        ImGui::Checkbox(XS("Visible check##silent"), &sa_vischeck);
        ImGui::Checkbox(XS("Backtrack##silent"), &sa_backtrack);
        if (sa_backtrack) ImGui::SliderFloat(XS("Backtrack window##silent"), &sa_backtrack_ms, 0.f, 400.f, XS("%.0f ms"));
        ImGui::Checkbox(XS("Movement prediction##silent"), &sa_prediction);
    }
    ImGui::Separator();
    ImGui::TextDisabled(XS("Runtime is present; this build keeps it disabled."));
}

static void antiAimTab() {
    static bool aa_enabled = false;
    static int aa_pitch = 0, aa_yaw = 0;
    static bool aa_random_pitch = false, aa_jitter = false;
    static float aa_jitter_range = 35.f, aa_jitter_speed = 1.f;
    static bool aa_spin = false;
    static float aa_spin_speed = 8.f;
    static int aa_spin_dir = 0;
    static bool aa_fakelag = false;
    static float aa_fakelag_ticks = 12.f;
    static bool aa_fakelag_var = false;
    static float aa_fakelag_var_range = 4.f;
    static bool aa_lby = false, aa_slide = false, aa_edge = false, aa_micro_move = false;
    static float aa_edge_range = 48.f, aa_micro_move_range = 6.f;
    static int aa_on_shot_mode = 0;
    static float aa_manual_angle = 90.f;
    static bool aa_manual_left = false, aa_manual_right = false;
    static const char* aa_pitch_modes = {};
    static const char* aa_yaw_modes = {};
    static const char* aa_spin_dirs = {};
    static const char* aa_shot_modes = {};

    ImGui::Checkbox(XS("Anti-Aim##aa"), &aa_enabled);
    if (ImGui::CollapsingHeader(XS("Pitch"), ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Combo(XS("Mode##aa-pitch"), &aa_pitch, aa_pitch_modes);
        ImGui::Checkbox(XS("Random pitch##aa"), &aa_random_pitch);
    }
    if (ImGui::CollapsingHeader(XS("Yaw"), ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Combo(XS("Mode##aa-yaw"), &aa_yaw, aa_yaw_modes);
        if (aa_yaw == 4) {
            ImGui::Checkbox(XS("Spin##aa"), &aa_spin);
            ImGui::SliderFloat(XS("Spin speed##aa"), &aa_spin_speed, 1.f, 30.f, XS("%.0f deg/tick"));
            ImGui::Combo(XS("Direction##aa"), &aa_spin_dir, aa_spin_dirs);
        }
        if (aa_yaw == 5) {
            ImGui::Checkbox(XS("Jitter##aa"), &aa_jitter);
            ImGui::SliderFloat(XS("Jitter range##aa"), &aa_jitter_range, 5.f, 180.f, XS("%.0f deg"));
            ImGui::SliderFloat(XS("Jitter speed##aa"), &aa_jitter_speed, .5f, 5.f, XS("%.1f"));
        }
        if (aa_yaw == 6) {
            ImGui::SliderFloat(XS("Manual angle##aa"), &aa_manual_angle, -180.f, 180.f, XS("%.0f deg"));
            ImGui::Checkbox(XS("Manual left##aa"), &aa_manual_left);
            ImGui::Checkbox(XS("Manual right##aa"), &aa_manual_right);
        }
    }
    if (ImGui::CollapsingHeader(XS("On shot"))) {
        ImGui::Combo(XS("Mode##aa-shot"), &aa_on_shot_mode, aa_shot_modes);
    }
    if (ImGui::CollapsingHeader(XS("Fake lag"))) {
        ImGui::Checkbox(XS("Fake lag##aa"), &aa_fakelag);
        if (aa_fakelag) {
            ImGui::SliderFloat(XS("Ticks##aa"), &aa_fakelag_ticks, 1.f, 22.f, XS("%.0f"));
            ImGui::Checkbox(XS("Variance##aa"), &aa_fakelag_var);
            if (aa_fakelag_var) ImGui::SliderFloat(XS("Variance range##aa"), &aa_fakelag_var_range, 1.f, 8.f, XS("%.0f"));
        }
    }
    if (ImGui::CollapsingHeader(XS("Extra"))) {
        ImGui::Checkbox(XS("LBY breaker##aa"), &aa_lby);
        ImGui::Checkbox(XS("Slide##aa"), &aa_slide);
        ImGui::Checkbox(XS("Edge detection##aa"), &aa_edge);
        if (aa_edge) ImGui::SliderFloat(XS("Edge range##aa"), &aa_edge_range, 16.f, 128.f, XS("%.0f"));
        ImGui::Checkbox(XS("Micro movement##aa"), &aa_micro_move);
        if (aa_micro_move) ImGui::SliderFloat(XS("Micro range##aa"), &aa_micro_move_range, 1.f, 20.f, XS("%.1f"));
    }
    ImGui::Separator();
    ImGui::TextDisabled(XS("Runtime is present; this build keeps it disabled."));
}

static void resetAll() {
    astra_camera_test::reset();
    astra_arrows::reset();
    astra_esp_extra::reset();
    astra_night::publish(astra_night::Settings{});
    edit = astra::aim::Configuration{};
    dirty = true;
    resetRequested = true;
    g_b = KwB{};
    previewEnabled = true;
    g_espBoxCol = ImVec4(.94f, .95f, .97f, 1.f);
    g_espHpCol = ImVec4(.36f, .87f, .65f, 1.f);
    g_espWepCol = ImVec4(.91f, .92f, .95f, 1.f);
    g_espAmmoCol = ImVec4(.68f, .72f, .79f, 1.f);
}

static void configTab() {
    ImGui::Checkbox(XS("Watermark##cfg"), &g_b.watermark);
    ImGui::Checkbox(XS("Show ESP preview##cfg"), &previewEnabled);
    if (ImGui::Button(XS("Close menu"), ImVec2(ImGui::GetContentRegionAvail().x, 0))) setMenuOpen(false);
    ImGui::Separator();
    ImGui::TextDisabled(XS("Astra / Free"));
    ImGui::Text(XS("Profile: %s"), g_offsetProfileReady.load() ? XS("ready") : XS("not matched"));
    ImGui::Text(XS("Aim hook: %s"), astra::aim::g_normalHookReady.load() ? XS("ready") : XS("waiting"));
    ImGui::Text(XS("ESP hook: %s"), astra::esp::g_playerUpdateHookReady.load() ? XS("ready") : XS("waiting"));
    ImGui::Text(XS("Hitlog hook: %d"), astra_hitlog::state.load());
    ImGui::Text(XS("Frame: %.1f ms"), 1000.0f / std::max(1.0f, ImGui::GetIO().Framerate));
    ImGui::Separator();
    if (ImGui::Button(XS("Reset settings"), ImVec2(ImGui::GetContentRegionAvail().x, 0)))
        ImGui::OpenPopup(XS("Reset settings?"));
    if (ImGui::BeginPopupModal(XS("Reset settings?"), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(XS("Restore all controls to defaults?"));
        if (ImGui::Button(XS("Reset##confirm"), ImVec2(100, 0))) {
            resetAll();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(XS("Cancel##confirm"), ImVec2(100, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

static void tabContent() {
    switch (g_tab) {
        case 0: aimTab(); break;
        case 1: visualsTab(); break;
        case 2: silentTab(); break;
        case 3: antiAimTab(); break;
        default: configTab(); break;
    }
}

// ── shell ────────────────────────────────────────────────────────────────────
static void drawLauncher() {
    ImGui::SetNextWindowPos(ImVec2(18.f, 42.f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(150.f, 150.f), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 0.f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;
    if (ImGui::Begin(XS("##astra-open"), nullptr, flags)) {
        ImGui::SetCursorPos(ImVec2(25.f, 25.f));
        if (ImGui::Button(XS("OPEN"), ImVec2(100.f, 100.f))) setMenuOpen(!menuOpen());
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
}

static void drawWatermark() {
    if (!g_b.watermark) return;
    auto* d = ImGui::GetBackgroundDrawList();
    ImFont* font = astra::fonts::base ? astra::fonts::base : ImGui::GetFont();
    d->AddText(font, 30.0f, ImVec2(100.f, 30.f), IM_COL32(255, 255, 255, 255), XS("ASTRA | APK"));
}

static void drawMenu() {
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 screen = io.DisplaySize;
    const float w = std::min(1200.f, screen.x - 12.f);
    const float h = std::min(760.f, screen.y - 24.f);
    if (w < 400.f || h < 300.f) return;
    if (g_menuPos.x < 0.0f || screen.x != g_menuScreen.x || screen.y != g_menuScreen.y) {
        g_menuPos = ImVec2((screen.x - w) * .5f, (screen.y - h) * .5f);
        g_menuScreen = screen;
    }
    g_menuPos.x = std::clamp(g_menuPos.x, 0.0f, std::max(0.0f, screen.x - w));
    g_menuPos.y = std::clamp(g_menuPos.y, 0.0f, std::max(0.0f, screen.y - h));
    ImGui::SetNextWindowPos(g_menuPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin(XS("##astra-menu"), nullptr, flags)) {
        const ImVec2 pos = ImGui::GetWindowPos();
        const ImGuiStyle& shellStyle = ImGui::GetStyle();
        astra::gui::rainbowLine(ImGui::GetWindowDrawList(), pos.x + 4.f,
                                pos.y + shellStyle.WindowBorderSize, w - 8.f, 8.0f,
                                g_rainbowPhase, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(0, 0));
        if (astra::fonts::big) ImGui::PushFont(astra::fonts::big);
        ImGui::TextUnformatted(XS("Astra"));
        if (astra::fonts::big) ImGui::PopFont();
        if (astra::fonts::med) ImGui::PushFont(astra::fonts::med);
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(150, 150, 150, 255));
        ImGui::TextUnformatted(XS("APK"));
        ImGui::PopStyleColor();
        if (astra::fonts::med) ImGui::PopFont();
        ImGui::PopStyleVar(5);
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Drag by the header: invisible handle covering title/description.
        const ImVec2 afterHeader = ImGui::GetCursorScreenPos();
        const float headerH = std::max(1.0f, afterHeader.y - pos.y);
        ImGui::SetCursorScreenPos(ImVec2(pos.x + shellStyle.WindowBorderSize,
                                        pos.y + shellStyle.WindowBorderSize));
        ImGui::InvisibleButton(XS("##menu-drag"), ImVec2(std::max(1.0f, w - shellStyle.WindowBorderSize * 2.0f),
                                                    std::max(1.0f, headerH - shellStyle.WindowBorderSize)));
        if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            ImVec2 moved = g_menuPos;
            moved.x += ImGui::GetIO().MouseDelta.x;
            moved.y += ImGui::GetIO().MouseDelta.y;
            moved.x = std::clamp(moved.x, 0.0f, std::max(0.0f, screen.x - w));
            moved.y = std::clamp(moved.y, 0.0f, std::max(0.0f, screen.y - h));
            // Applied through SetNextWindowPos on the next frame so the
            // already-submitted title/rainbow do not shear against the body.
            g_menuPos = moved;
        }
        ImGui::SetCursorScreenPos(afterHeader);

        const float navW = std::max(110.f, w / 9.916f);
        const float contentH = ImGui::GetContentRegionAvail().y;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 8));
        ImGui::BeginChild(XS("##astra-nav"), ImVec2(navW, contentH), false, ImGuiWindowFlags_NoScrollbar);
        float btn = std::min(100.f, (contentH - 4.f * 8.f) / 5.f);
        btn = std::max(40.f, btn);
        const char* icons[] = {ICON_FA_CROSSHAIRS, ICON_FA_EYE, ICON_FA_CIRCLE_NOTCH,
                               ICON_FA_SHIELD, ICON_FA_GEAR};
        for (int i = 0; i < 5; ++i) {
            ImGui::SetCursorPosX((navW - btn) * .5f);
            ImGui::PushID(i);
            ImFont* navIcon = astra::fonts::iconLarge ? astra::fonts::iconLarge : astra::fonts::icon;
            if (navIcon) ImGui::PushFont(navIcon);
            const bool pressed = ImGui::Button(icons[i], ImVec2(btn, btn));
            const bool hovered = ImGui::IsItemHovered();
            if (navIcon) ImGui::PopFont();
            if (pressed) g_tab = i;
            if (hovered) ImGui::SetTooltip(XS("%s"), tabs[i]);
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::SameLine(0, 12.f);
        ImGui::BeginChild(XS("##astra-content"), ImVec2(0, contentH), false,
                          ImGuiWindowFlags_AlwaysVerticalScrollbar);
        tabContent();
        ImGui::EndChild();
    }
    ImGui::End();
}

static void commit() {
    if (!dirty) return;
    auto c = resetRequested ? astra::aim::Configuration{} : astra::aim::configuration();
    c.normal.enabled = edit.normal.enabled;
    c.normal.visibleCheck = edit.normal.visibleCheck;
    c.normal.fov = edit.normal.fov;
    c.normal.showFov = edit.normal.showFov;
    c.normal.bodies = edit.normal.bodies;
    c.normal.smoothing = edit.normal.smoothing;
    c.normal.rcs = edit.normal.rcs;
    c.normal.rcsStrength = edit.normal.rcsStrength;
    c.silent.enabled = false;
    c.autoFire.enabled = false;
    astra::aim::publish(c);
    dirty = false;
    resetRequested = false;
}

static void renderMinimalMenu() {
    edit = astra::aim::configuration();
    dirty = false;
    resetRequested = false;
    astra::aim::g_menuOpen.store(menuOpen(), std::memory_order_release);

    updateAimProjection();
    renderEsp();
    astra_arrows::render();
    renderFov(edit.normal);
    if (menuOpen()) drawMenu();
    drawLauncher();
    drawWatermark();

    commit();
    astra::aim::g_menuOpen.store(menuOpen(), std::memory_order_release);
}
