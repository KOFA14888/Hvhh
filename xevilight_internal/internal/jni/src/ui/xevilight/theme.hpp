
// ================================================================================
// t.me/imguisource_bot | t.me/ImGui_Source_FIXNEY (ImGui Source) | t.me/il2cpp_src (shit src 🧨)
// ================================================================================

#pragma once
#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace xev::ui {

// ── palette ─────────────────────────────────────────────────────────────────
inline constexpr ImU32 kPanel     = IM_COL32(  9,   9,  10, 255);
inline constexpr ImU32 kPanelTop  = IM_COL32( 13,  13,  15, 255);
inline constexpr ImU32 kEdge      = IM_COL32(255, 255, 255,  40);
inline constexpr ImU32 kEdgeSoft  = IM_COL32(255, 255, 255,  22);
inline constexpr ImU32 kHair      = IM_COL32(255, 255, 255,  16);
inline constexpr ImU32 kHairSoft  = IM_COL32(255, 255, 255,   8);
inline constexpr ImU32 kTitle     = IM_COL32(198, 199, 205, 255);
inline constexpr ImU32 kText      = IM_COL32(238, 239, 242, 255);
inline constexpr ImU32 kTextDim   = IM_COL32(142, 143, 150, 255);
inline constexpr ImU32 kTextFaint = IM_COL32( 94,  95, 101, 255);
inline constexpr ImU32 kTrack     = IM_COL32(255, 255, 255,  22);
inline constexpr ImU32 kTrackFill = IM_COL32(203, 210, 226, 255);
inline constexpr ImU32 kThumb     = IM_COL32(247, 248, 251, 255);
inline constexpr ImU32 kOnA       = IM_COL32(219, 226, 238, 255);
inline constexpr ImU32 kOnB       = IM_COL32(170, 182, 203, 255);
inline constexpr ImU32 kField     = IM_COL32(255, 255, 255,   7);
inline constexpr ImU32 kFieldEdge = IM_COL32(255, 255, 255,  48);
inline constexpr ImU32 kHover     = IM_COL32(255, 255, 255,  12);
inline constexpr ImU32 kActive    = IM_COL32(255, 255, 255, 255);
inline constexpr ImU32 kScrim     = IM_COL32(  0,   0,   0, 120);

inline ImU32 mix(ImU32 a, ImU32 b, float t) {
    t = std::clamp(t, 0.f, 1.f);
    const int ar = int(a & 0xFF), ag = int((a >> 8) & 0xFF), ab = int((a >> 16) & 0xFF), aa = int(a >> 24);
    const int br = int(b & 0xFF), bg = int((b >> 8) & 0xFF), bb = int((b >> 16) & 0xFF), ba = int(b >> 24);
    return IM_COL32(int(ar + (br - ar) * t), int(ag + (bg - ag) * t),
                    int(ab + (bb - ab) * t), int(aa + (ba - aa) * t));
}

inline ImU32 fade(ImU32 c, float f) { return mix(c & 0x00FFFFFF, c, std::clamp(f, 0.f, 1.f)); }

inline ImU32 rgb(float r, float g, float b, float a = 1.f) {
    return IM_COL32(int(std::clamp(r, 0.f, 1.f) * 255.f + .5f), int(std::clamp(g, 0.f, 1.f) * 255.f + .5f),
                    int(std::clamp(b, 0.f, 1.f) * 255.f + .5f), int(std::clamp(a, 0.f, 1.f) * 255.f + .5f));
}

// hsv with h in turns [0,1); keeps the picker and every accent on one curve.
inline void hsvToRgb(float h, float s, float v, float& r, float& g, float& b) {
    h = h - std::floor(h);
    const float i = std::floor(h * 6.f), f = h * 6.f - i;
    const float p = v * (1.f - s), q = v * (1.f - f * s), t = v * (1.f - (1.f - f) * s);
    switch (int(i) % 6) {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
    }
}

inline void rgbToHsv(float r, float g, float b, float& h, float& s, float& v) {
    const float hi = std::max({r, g, b}), lo = std::min({r, g, b}), d = hi - lo;
    v = hi;
    s = hi <= 1e-6f ? 0.f : d / hi;
    if (d <= 1e-6f) { h = 0.f; return; }
    if (hi == r) h = (g - b) / d + (g < b ? 6.f : 0.f);
    else if (hi == g) h = (b - r) / d + 2.f;
    else h = (r - g) / d + 4.f;
    h /= 6.f;
}

inline ImU32 colorU32(const ImVec4& c) { return ImGui::ColorConvertFloat4ToU32(c); }

// ── typography ──────────────────────────────────────────────────────────────
// Poppins: geometric grotesk, semibold body + bold display. One family, two
// weights — hierarchy without a second voice.
namespace {
#include "../fonts/PoppinsSemiBold.h"
#include "../fonts/PoppinsBold.h"
}  // namespace

namespace fonts {
inline ImFont* body = nullptr;    // rows, labels — Poppins SemiBold 48 px raster
inline ImFont* small = nullptr;   // notes, fields — Poppins SemiBold 34 px raster
inline ImFont* strong = nullptr;  // title, tabs, numbers — Poppins Bold 60 px raster

inline void load(ImGuiIO& io) {
    io.Fonts->ClearFonts();
    ImFontConfig base;
    base.FontDataOwnedByAtlas = false;
    base.OversampleH = 1;
    base.OversampleV = 1;
    base.PixelSnapH = false;
    base.GlyphRanges = io.Fonts->GetGlyphRangesDefault();

    body = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(PoppinsSemiBold),
                                          int(sizeof PoppinsSemiBold), 48.f, &base);
    small = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(PoppinsSemiBold),
                                           int(sizeof PoppinsSemiBold), 34.f, &base);
    strong = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(PoppinsBold),
                                            int(sizeof PoppinsBold), 60.f, &base);
    if (!body) {
        io.Fonts->AddFontDefault();
        body = io.Fonts->Fonts[0];
    }
    if (!small) small = body;
    if (!strong) strong = body;
    io.FontDefault = small;
    io.FontGlobalScale = 1.f;
}
}  // namespace fonts

// ── ImGui style: only stock widgets outside the custom shell see this ───────
inline void applyStyle() {
    ImGuiStyle& st = ImGui::GetStyle();
    st = ImGuiStyle{};
    st.WindowPadding = ImVec2(10, 10);
    st.FramePadding = ImVec2(7, 5);
    st.ItemSpacing = ImVec2(7, 7);
    st.ItemInnerSpacing = ImVec2(6, 5);
    st.ScrollbarSize = 12.f;
    st.GrabMinSize = 14.f;
    st.WindowBorderSize = 1.f;
    st.ChildBorderSize = 1.f;
    st.FrameBorderSize = 1.f;
    st.WindowRounding = 4.f;
    st.ChildRounding = 4.f;
    st.FrameRounding = 5.f;
    st.PopupRounding = 4.f;
    st.ScrollbarRounding = 6.f;
    st.GrabRounding = 5.f;
    st.WindowTitleAlign = ImVec2(.5f, .5f);
    st.DisplaySafeAreaPadding = ImVec2(0, 0);

    ImVec4* c = st.Colors;
    c[ImGuiCol_Text] = ImVec4(.93f, .93f, .95f, 1.f);
    c[ImGuiCol_TextDisabled] = ImVec4(.55f, .55f, .59f, 1.f);
    c[ImGuiCol_WindowBg] = ImVec4(.04f, .04f, .045f, .98f);
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = ImVec4(.045f, .045f, .05f, .99f);
    c[ImGuiCol_Border] = ImVec4(1, 1, 1, .16f);
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = ImVec4(1, 1, 1, .05f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(1, 1, 1, .09f);
    c[ImGuiCol_FrameBgActive] = ImVec4(1, 1, 1, .12f);
    c[ImGuiCol_TitleBg] = c[ImGuiCol_WindowBg];
    c[ImGuiCol_TitleBgActive] = c[ImGuiCol_WindowBg];
    c[ImGuiCol_Button] = ImVec4(1, 1, 1, .06f);
    c[ImGuiCol_ButtonHovered] = ImVec4(1, 1, 1, .11f);
    c[ImGuiCol_ButtonActive] = ImVec4(1, 1, 1, .16f);
    c[ImGuiCol_Header] = ImVec4(1, 1, 1, .07f);
    c[ImGuiCol_HeaderHovered] = ImVec4(1, 1, 1, .11f);
    c[ImGuiCol_HeaderActive] = ImVec4(1, 1, 1, .15f);
    c[ImGuiCol_Separator] = ImVec4(1, 1, 1, .12f);
    c[ImGuiCol_SliderGrab] = ImVec4(.86f, .88f, .93f, 1.f);
    c[ImGuiCol_SliderGrabActive] = ImVec4(1, 1, 1, 1.f);
    c[ImGuiCol_CheckMark] = ImVec4(1, 1, 1, 1.f);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(1, 1, 1, .18f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(1, 1, 1, .30f);
    c[ImGuiCol_ScrollbarGrabActive] = ImVec4(1, 1, 1, .40f);
}

}  // namespace xev::ui
