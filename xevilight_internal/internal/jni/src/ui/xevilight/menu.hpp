
// ================================================================================
// TG Links: bot=t.me/imguisource_bot main=t.me/ImGui_Source_FIXNEY extra=t.me/il2cpp_src
// ================================================================================

//pasted✅
#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"

#include "features/aim/assists.hpp"
#include "features/aim/automation.hpp"
#include "features/aim/core.hpp"
#include "features/aim/silent_diagnostics.hpp"
#include "features/visuals/astra_camera_test.hpp"
#include "features/visuals/astra_hitlog.hpp"
#include "features/visuals/astra_night.hpp"
#include "features/visuals/esp_runtime.hpp"
#include "ui/xevilight/theme.hpp"
#include "ui/xevilight/widgets.hpp"
#include "ui/skin_gallery.hpp"

using EspScreenBox = astra::esp::ScreenBox;
extern std::vector<EspScreenBox> g_espScreenBoxes;
extern std::mutex g_espMutex;
extern std::atomic<bool> g_espBoxesEnabled;
extern std::atomic<bool> g_offsetProfileReady;
extern std::atomic<std::uintptr_t> g_unityBase;

static bool menuOpen() { return g_menuOpen.load(std::memory_order_relaxed); }
static void setMenuOpen(bool open) { g_menuOpen.store(open, std::memory_order_relaxed); }

struct KwB {
    bool esp_enabled = false, esp_box = true, esp_hp = true, esp_armor = true;
    bool esp_wep = true, esp_ammo = true;
};
static KwB g_b;

static ImVec4 g_espBoxCol{.94f, .95f, .97f, 1.f};
static ImVec4 g_espHpCol{.36f, .87f, .65f, 1.f};
static ImVec4 g_espWepCol{.91f, .92f, .95f, 1.f};
static ImVec4 g_espAmmoCol{.68f, .72f, .79f, 1.f};
static float g_S = 1.f;  // ESP optical scale, set each frame by the shell

// These two read the globals above at include time.
#include "ui/premium_arrows.hpp"
#include "ui/premium_esp_extra.hpp"

namespace xevm {

using namespace xev::ui;

static astra::aim::Configuration edit;
static bool g_dirty = false, g_reset = false;

// Ordinary Dear ImGui controls. PushID keeps repeated labels distinct across pages.
static void section(const char* label) { ImGui::SeparatorText(label); }
static bool toggle(const char* id, bool& value, const char* label) {
    ImGui::PushID(id); bool changed = ImGui::Checkbox(label, &value); ImGui::PopID(); return changed;
}
static bool slider(const char* id, const char* label, float& value, float lo, float hi, const char* fmt) {
    ImGui::PushID(id); bool changed = ImGui::SliderFloat(label, &value, lo, hi, fmt); ImGui::PopID(); return changed;
}
static void selectRow(const char* id, const char* label, int& value, const char* const* items, int count, bool* dirty) {
    ImGui::PushID(id); bool changed = ImGui::Combo(label, &value, items, count); ImGui::PopID();
    if (changed && dirty) *dirty = true;
}
static void colorRow(const char* id, const char* label, ImVec4& color, bool* dirty) {
    ImGui::PushID(id); bool changed = ImGui::ColorEdit4(label, &color.x); ImGui::PopID();
    if (changed && dirty) *dirty = true;
}
static bool buttonRow(const char* id, const char* label) {
    ImGui::PushID(id); bool pressed = ImGui::Button(label); ImGui::PopID(); return pressed;
}
static bool chips(const char* id, std::uint32_t& mask, const char* const* names, int count) {
    bool changed = false; ImGui::PushID(id);
    for (int i = 0; i < count; ++i) {
        ImGui::PushID(i); bool selected = (mask & (1u << i)) != 0;
        if (ImGui::Checkbox(names[i], &selected)) {
            if (selected) mask |= 1u << i; else mask &= ~(1u << i);
            changed = true;
        }
        ImGui::PopID();
    }
    ImGui::PopID(); return changed;
}
static void kv(const char* key, const char* value) { ImGui::Text("%s: %s", key, value); }

// ── FOV projection ──────────────────────────────────────────────────────────
struct Projection {
    float width = 0, height = 0, p00 = 0, p11 = 0, shiftX = 0, shiftY = 0;
    double captured = 0;
};
static std::mutex projectionMutex;
static Projection projection;

static double nowSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

static void clearProjection() {
    std::lock_guard<std::mutex> guard(projectionMutex);
    projection = {};
}

static void updateProjection() {
    auto config = astra::aim::configuration();
    if (!config.normal.showFov && !config.silent.showFov) {
        clearProjection();
        return;
    }
    astra::esp::CameraMatrixCache cache{};
    std::uintptr_t camera = 0;
    if (!astra::esp::detail::readCameraForPresent(cache, camera)) {
        clearProjection();
        return;
    }
    const auto& p = cache.projection.value;
    const float scale = -p[11];
    if (!std::isfinite(scale) || scale <= .0001f) {
        clearProjection();
        return;
    }
    for (int index : {1, 3, 4, 7, 12, 13, 15})
        if (std::fabs(p[index] / scale) > .001f) {
            clearProjection();
            return;
        }
    const ImVec2 size = ImGui::GetIO().DisplaySize;
    const float p00 = std::fabs(p[0] / scale), p11 = std::fabs(p[5] / scale);
    const float sx = p[8] / scale, sy = p[9] / scale;
    const auto geometry = astra::aim::assists::fovGeometry(std::max(config.normal.fov, config.silent.fov),
                                                           size.x, size.y, p00, p11, sx, sy);
    if (!geometry.valid) {
        clearProjection();
        return;
    }
    std::lock_guard<std::mutex> guard(projectionMutex);
    projection = {size.x, size.y, p00, p11, sx, sy, nowSeconds()};
}

static astra::aim::assists::FovGeometry fovGeometry(float degrees) {
    Projection snapshot;
    {
        std::lock_guard<std::mutex> guard(projectionMutex);
        snapshot = projection;
    }
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const double age = nowSeconds() - snapshot.captured;
    if (age < 0 || age >= .25 || std::fabs(snapshot.width - display.x) > 1 || std::fabs(snapshot.height - display.y) > 1)
        return {};
    return astra::aim::assists::fovGeometry(degrees, snapshot.width, snapshot.height, snapshot.p00, snapshot.p11,
                                            snapshot.shiftX, snapshot.shiftY);
}

static void renderFov(const astra::aim::Settings& settings, ImU32 color) {
    if (!settings.showFov) return;
    const auto geometry = fovGeometry(settings.fov);
    if (!geometry.valid || geometry.coversScreen) return;
    auto* draw = ImGui::GetBackgroundDrawList();
    draw->PushClipRect(ImVec2(0, 0), ImGui::GetIO().DisplaySize, true);
    draw->PathClear();
    for (int i = 0; i <= 256; ++i) {
        const float a = float(i) * 6.28318530718f / 256.f;
        draw->PathLineTo(ImVec2(geometry.centerX + std::cos(a) * geometry.radiusX,
                                geometry.centerY + std::sin(a) * geometry.radiusY));
    }
    draw->PathStroke(color, 0, 1.6f);
    draw->PopClipRect();
}

static void renderEsp() {
    if (!g_b.esp_enabled || !g_espBoxesEnabled.load()) return;
    std::vector<EspScreenBox> boxes;
    {
        std::lock_guard<std::mutex> guard(g_espMutex);
        boxes = g_espScreenBoxes;
    }
    auto* draw = ImGui::GetBackgroundDrawList();
    draw->PushClipRect(ImVec2(0, 0), ImGui::GetIO().DisplaySize, true);
    for (const auto& box : boxes) astra_esp_extra::draw(draw, box);
    draw->PopClipRect();
}

// ── preview ─────────────────────────────────────────────────────────────────
// ── pages ───────────────────────────────────────────────────────────────────
static void resetAll();

static void pageRage() {
    section(XS("Aim"));
    if (toggle(XS("aim.on"), edit.normal.enabled, XS("Aim"))) g_dirty = true;
    if (toggle(XS("aim.visible"), edit.normal.visibleCheck, XS("Visible check"))) g_dirty = true;
    if (toggle(XS("aim.showfov"), edit.normal.showFov, XS("Show FOV"))) g_dirty = true;
    if (toggle(XS("aim.rcs"), edit.normal.rcs, XS("Recoil control"))) g_dirty = true;
    if (slider(XS("aim.fov"), XS("FOV"), edit.normal.fov, 1.f, 180.f, XS("%.0f deg"))) g_dirty = true;
    if (slider(XS("aim.smooth"), XS("Smoothing"), edit.normal.smoothing, 0.f, 1.f, XS("%.2f s"))) g_dirty = true;
    if (slider(XS("aim.rcsstr"), XS("RCS strength"), edit.normal.rcsStrength, 0.f, 100.f, XS("%.0f%%"))) g_dirty = true;
    if (toggle(XS("aim.pred"), edit.normal.prediction, XS("Prediction"))) g_dirty = true;
    if (slider(XS("aim.tick"), XS("Prediction tick"), edit.normal.predictionTicks, 0.f, 10.f, XS("%.0f tick"))) g_dirty = true;
    if (toggle(XS("aim.back"), edit.normal.backCamera, XS("Back camera"))) g_dirty = true;

    section(XS("Target"));
    if (chips(XS("aim.bodies"), edit.normal.bodies, astra::aim::kBodyNames, 8)) g_dirty = true;
    static const char* priorities[] = {XS("Angle"), XS("Distance"), XS("Health")};
    selectRow(XS("aim.priority"), XS("Priority"), edit.normal.priority, priorities, 3, &g_dirty);

    section(XS("Silent aim"));
    if (toggle(XS("silent.on"), edit.silent.enabled, XS("Silent aim"))) g_dirty = true;
    if (toggle(XS("silent.visible"), edit.silent.visibleCheck, XS("Visible check"))) g_dirty = true;
    if (toggle(XS("silent.showfov"), edit.silent.showFov, XS("Show FOV"))) g_dirty = true;
    if (toggle(XS("silent.lock"), edit.silent.lockTarget, XS("Lock target"))) g_dirty = true;
    if (toggle(XS("silent.multi"), edit.silent.multipoints, XS("Multipoints"))) g_dirty = true;
    if (toggle(XS("silent.pred"), edit.silent.prediction, XS("Prediction"))) g_dirty = true;
    if (slider(XS("silent.fov"), XS("Silent FOV"), edit.silent.fov, 1.f, 180.f, XS("%.0f deg"))) g_dirty = true;
    if (slider(XS("silent.dist"), XS("Max distance"), edit.silent.maxDistance, 10.f, 300.f, XS("%.0f m"))) g_dirty = true;
    if (slider(XS("silent.scale"), XS("Point scale"), edit.silent.pointScale, 0.f, .9f, XS("%.2f"))) g_dirty = true;
    if (slider(XS("silent.tick"), XS("Prediction tick"), edit.silent.predictionTicks, 0.f, 10.f, XS("%.0f tick"))) g_dirty = true;
    if (slider(XS("silent.lead"), XS("Extra lead"), edit.silent.predictionMs, -150.f, 150.f, XS("%.0f ms"))) g_dirty = true;

    section(XS("Automation"));
    if (toggle(XS("auto.on"), edit.autoFire.enabled, XS("Autofire"))) g_dirty = true;
    if (toggle(XS("auto.scope"), edit.autoFire.autoScope, XS("Auto scope"))) g_dirty = true;
    if (slider(XS("auto.interval"), XS("Shot interval"), edit.autoFire.intervalMs, 30.f, 1000.f, XS("%.0f ms"))) g_dirty = true;

    section(XS("Anti-aim"));
    auto& aa = edit.antiAim;
    if (toggle(XS("aa.on"), aa.enabled, XS("Anti-aim"))) g_dirty = true;
    if (toggle(XS("aa.model"), aa.localModel, XS("Local model"))) g_dirty = true;
    static const char* aaPitch[] = {XS("Up"), XS("Down"), XS("Fake down")};
    static const char* aaYaw[] = {XS("Backward"), XS("Spin"), XS("Flick")};
    selectRow(XS("aa.pitch"), XS("Pitch"), aa.pitchMode, aaPitch, 3, &g_dirty);
    selectRow(XS("aa.yaw"), XS("Yaw"), aa.yawMode, aaYaw, 3, &g_dirty);
    if (aa.yawMode == 0 || aa.yawMode == 2) {
        if (slider(XS("aa.offset"), XS("Yaw offset"), aa.offset, -180.f, 180.f, XS("%.0f deg"))) g_dirty = true;
    }
    if (aa.yawMode == 1) {
        if (slider(XS("aa.speed"), XS("Spin speed"), aa.speed, 30.f, 720.f, XS("%.0f deg/s"))) g_dirty = true;
    }
    if (aa.yawMode == 2) {
        if (slider(XS("aa.flick"), XS("Flick angle"), aa.flickAngle, 5.f, 90.f, XS("%.0f deg"))) g_dirty = true;
        if (slider(XS("aa.flickrate"), XS("Flick rate"), aa.flickRate, .5f, 10.f, XS("%.1f Hz"))) g_dirty = true;
    }
    if (aa.pitchMode == 2) {
        if (slider(XS("aa.fakerate"), XS("Fake rate"), aa.fakeRate, .5f, 10.f, XS("%.1f Hz"))) g_dirty = true;
    }
}

static void pageVisuals() {
    section(XS("ESP"));
    toggle(XS("esp.on"), g_b.esp_enabled, XS("Player ESP"));
    toggle(XS("esp.box"), g_b.esp_box, XS("Bounding box"));
    toggle(XS("esp.hp"), g_b.esp_hp, XS("Health bar"));
    toggle(XS("esp.armor"), g_b.esp_armor, XS("Armor bar"));
    toggle(XS("esp.wep"), g_b.esp_wep, XS("Weapon info"));
    toggle(XS("esp.ammo"), g_b.esp_ammo, XS("Ammo"));
    colorRow(XS("esp.boxcol"), XS("Box color"), g_espBoxCol, nullptr);
    colorRow(XS("esp.hpcol"), XS("Health color"), g_espHpCol, nullptr);
    colorRow(XS("esp.armorcol"), XS("Armor color"), astra_esp_extra::settings.armorColor, nullptr);
    colorRow(XS("esp.wepcol"), XS("Weapon color"), g_espWepCol, nullptr);
    colorRow(XS("esp.ammocol"), XS("Ammo color"), g_espAmmoCol, nullptr);

    section(XS("Frame"));
    auto& style = astra_esp_extra::settings;
    static const char* frames[] = {XS("Classic"), XS("Corners"), XS("Rounded"), XS("Dashed"), XS("Side brackets")};
    selectRow(XS("esp.frame"), XS("Frame style"), astra_esp_extra::frameStyle, frames, 5, nullptr);
    toggle(XS("esp.fill"), style.fill, XS("Box fill"));
    slider(XS("esp.fillalpha"), XS("Fill opacity"), style.fillAlpha, 0.f, .35f, XS("%.2f"));
    toggle(XS("esp.snap"), style.snaplines, XS("Snaplines"));
    toggle(XS("esp.head"), style.headMarker, XS("Head marker"));
    toggle(XS("esp.shadow"), style.shadow, XS("Text shadow"));
    toggle(XS("esp.outline"), style.outline, XS("Text outline"));
    slider(XS("esp.thick"), XS("Line width"), style.thickness, 1.f, 4.f, XS("%.1f"));
    slider(XS("esp.text"), XS("Text scale"), style.textScale, .5f, 2.f, XS("%.2f"));
    toggle(XS("esp.div"), astra_esp_extra::barDivisions, XS("HP / armor divisions"));

    section(XS("Information"));
    toggle(XS("esp.dist"), style.distance, XS("Distance"));
    toggle(XS("esp.hpnum"), style.hpNumber, XS("HP number"));
    toggle(XS("esp.armornum"), style.armorNumber, XS("Armor number"));
    toggle(XS("esp.low"), style.lowHealth, XS("Low health marker"));
    slider(XS("esp.lowpct"), XS("Low HP threshold"), style.lowHealthPercent, 5.f, 50.f, XS("%.0f%%"));
    toggle(XS("esp.empty"), style.emptyMagazine, XS("Empty magazine"));

    section(XS("Arrows"));
    toggle(XS("arrow.on"), astra_arrows::settings.enabled, XS("Off-screen arrows"));
    toggle(XS("arrow.glow"), astra_arrows::settings.glow, XS("Arrow halo"));
    colorRow(XS("arrow.color"), XS("Arrow color"), astra_arrows::settings.color, nullptr);
    slider(XS("arrow.size"), XS("Arrow size"), astra_arrows::settings.size, 7.f, 24.f, XS("%.0f px"));
    slider(XS("arrow.inset"), XS("Arrow inset"), astra_arrows::settings.inset, 30.f, 180.f, XS("%.0f px"));

    section(XS("World"));
    astra_night::Settings night = astra_night::configuration();
    bool nightChanged = toggle(XS("night.on"), night.enabled, XS("GPU night mode"));
    float strength = night.strength * 100.f;
    if (slider(XS("night.strength"), XS("Night strength"), strength, 0.f, 100.f, XS("%.0f%%"))) {
        night.strength = std::clamp(strength * .01f, 0.f, 1.f);
        nightChanged = true;
    }
    if (nightChanged) astra_night::publish(night);

    section(XS("Camera"));
    astra_camera_test::V camera;
    {
        std::lock_guard<std::mutex> guard(astra_camera_test::lock);
        camera = astra_camera_test::settings;
    }
    bool cameraOn = astra_camera_test::enabled.load();
    bool cameraChanged = toggle(XS("cam.on"), cameraOn, XS("Third person camera"));
    cameraChanged |= slider(XS("cam.height"), XS("Camera height"), camera.y, 0.f, 4.f, XS("%.2f m"));
    cameraChanged |= slider(XS("cam.distance"), XS("Camera distance"), camera.z, .5f, 8.f, XS("%.2f m"));
    if (cameraChanged) {
        astra_camera_test::enabled.store(cameraOn);
        astra_camera_test::restore_requested.store(!cameraOn);
        if (cameraOn) astra_camera_test::lease.store(astra_camera_test::nowMs());
        astra_camera_test::resetSmoothing();
        std::lock_guard<std::mutex> guard(astra_camera_test::lock);
        astra_camera_test::settings = camera;
    }
    if (buttonRow(XS("cam.reset"), XS("Reset camera"))) astra_camera_test::reset();

}

static void pageSkins() {
    namespace gallery = xev::gallery;
    bool enabled = astra::skins::enabled.load(std::memory_order_relaxed);
    if (ImGui::Checkbox(XS("Enable skin changer"), &enabled))
        astra::skins::enabled.store(enabled, std::memory_order_relaxed);
    ImGui::Text("%s", astra::skins::status().c_str());
    if (!gallery::load()) { ImGui::TextUnformatted(XS("Gallery atlas failed to load.")); return; }
    const char* modes[] = {XS("Weapons"), XS("Knives")};
    ImGui::Combo(XS("Category"), &gallery::g_mode, modes, 2);
    if (gallery::g_mode == 0 && ImGui::BeginCombo(XS("Weapon"), gallery::weaponLabel(gallery::g_weapon))) {
        for (const auto& weapon : gallery::g_weaponList) {
            if (ImGui::Selectable(weapon.second, weapon.first == gallery::g_weapon)) gallery::g_weapon = weapon.first;
        }
        ImGui::EndCombo();
    }
    const int weapon = gallery::g_mode == 0 ? gallery::g_weapon : 70;
    const int selected = astra::skins::choice(weapon);
    if (ImGui::Selectable(XS("Default"), selected == 0)) astra::skins::choose(weapon, 0);
    for (const auto& entry : skin_catalog::entries) {
        if (int(entry.weapon) != weapon) continue;
        std::string name;
        gallery::tileFor(entry, name);
        ImGui::PushID(entry.id);
        if (ImGui::Selectable(name.c_str(), selected == entry.id)) astra::skins::choose(weapon, entry.id);
        ImGui::PopID();
    }
}

static void pageSettings() {
    section(XS("Configuration"));
    if (buttonRow(XS("cfg.save"), XS("Save silent diagnostics"))) silent_diag::save();
    if (buttonRow(XS("cfg.reset"), XS("Reset settings"))) resetAll();
    ImGui::TextUnformatted(silent_diag::status);
    section(XS("Runtime"));
    const auto unity = g_unityBase.load(std::memory_order_acquire);
    kv(XS("Binary profile"), !unity ? XS("waiting for libunity") : g_offsetProfileReady.load() ? XS("1.1.0 ARM64") : XS("mismatch"));
    kv(XS("Aim hook"), astra::aim::g_normalHookReady.load() ? XS("ready") : XS("waiting"));
    kv(XS("Silent hook"), astra::aim::g_silentHookReady.load() ? XS("ready") : XS("waiting"));
    kv(XS("ESP hook"), astra::esp::g_playerUpdateHookReady.load() ? XS("ready") : XS("waiting"));
    ImGui::Text("%.1f FPS", ImGui::GetIO().Framerate);
}

static void frameShell() {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize(ImVec2(std::min(700.f, display.x * .9f), std::min(500.f, display.y * .85f)), ImGuiCond_FirstUseEver);
    bool open = true;
    if (ImGui::Begin(XS("Xevilight"), &open, ImGuiWindowFlags_NoSavedSettings)) {
        if (ImGui::BeginTabBar(XS("##tabs"))) {
            if (ImGui::BeginTabItem(XS("Rage"))) { pageRage(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem(XS("Visuals"))) { pageVisuals(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem(XS("Skins"))) { pageSkins(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem(XS("Settings"))) { pageSettings(); ImGui::EndTabItem(); }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
    if (!open) setMenuOpen(false);
}

static void drawWatermark() {
    ImGui::SetNextWindowPos(ImVec2(12.f, 12.f), ImGuiCond_Always);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove;
    if (ImGui::Begin(XS("##menu-launcher"), nullptr, flags)) {
        if (ImGui::Button(menuOpen() ? XS("Close menu") : XS("Open menu"))) setMenuOpen(!menuOpen());
    }
    ImGui::End();
}

static void resetAll() {
    astra_camera_test::reset();
    astra_arrows::reset();
    astra_esp_extra::reset();
    astra_night::publish(astra_night::Settings{});
    edit = astra::aim::Configuration{};
    g_b = KwB{};
    g_espBoxCol = ImVec4(.94f, .95f, .97f, 1.f);
    g_espHpCol = ImVec4(.36f, .87f, .65f, 1.f);
    g_espWepCol = ImVec4(.91f, .92f, .95f, 1.f);
    g_espAmmoCol = ImVec4(.68f, .72f, .79f, 1.f);
    g_dirty = true;
    g_reset = true;
}

static void commit() {
    if (!g_dirty) return;
    auto config = g_reset ? astra::aim::Configuration{} : astra::aim::configuration();
    config.normal = edit.normal;
    config.silent = edit.silent;
    config.silent.rcs = false;
    config.silent.noRecoil = false;
    config.autoFire = edit.autoFire;
    config.antiAim = edit.antiAim;
    config.pauseInMenu = edit.pauseInMenu;
    astra::aim::publish(config);
    g_dirty = false;
    g_reset = false;
}

void frame() {
    edit = astra::aim::configuration();
    g_dirty = false;
    g_reset = false;
    g_S = std::clamp(std::min(ImGui::GetIO().DisplaySize.x / 1920.f, ImGui::GetIO().DisplaySize.y / 1080.f),
                     .85f, 2.f);
    astra::aim::g_menuOpen.store(menuOpen(), std::memory_order_release);

    updateProjection();
    renderEsp();
    astra_arrows::render();
    renderFov(edit.normal, IM_COL32(240, 242, 248, 190));
    renderFov(edit.silent, IM_COL32(255, 96, 96, 190));
    if (menuOpen()) frameShell();
    drawWatermark();

    commit();
    astra::aim::g_menuOpen.store(menuOpen(), std::memory_order_release);
}

}  // namespace xevm

static void renderMinimalMenu() { xevm::frame(); }
