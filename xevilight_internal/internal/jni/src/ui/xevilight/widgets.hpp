
// ================================================================================
// >> TG BOT: @imguisource_bot
// >> CHANNEL: @ImGui_Source_FIXNEY (ImGui Source)
// >> BACKUP: @il2cpp_src (shit src 🧨)
// ================================================================================

#pragma once
#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "ui/xevilight/theme.hpp"

namespace xev::ui {

struct Layout {
    ImDrawList* dl = nullptr;
    float s = 1.f;               // design-unit -> pixel scale
    float x0 = 0.f, x1 = 0.f;    // content column, screen space
    float y = 0.f;               // cursor, screen space
    ImVec2 clipMin{}, clipMax{}; // active interaction/draw clip
    ImVec2 panelMin{}, panelMax{};
    ImVec2 mouse{};
    bool windowHovered = false, down = false, pressed = false, released = false;
    bool claimed = false, dragging = false, interactive = true;
    ImGuiID pressId = 0, sliderId = 0;
    ImVec2 pressPos{};
    float dt = 0.f;
};
inline Layout L;

inline float px(float v) { return v * L.s; }

// ── text ────────────────────────────────────────────────────────────────────
inline ImFont* face(ImFont* f) { return f ? f : fonts::body; }

inline float textWidth(const char* t, float size, ImFont* f = nullptr) {
    if (!t || !*t) return 0.f;
    return face(f)->CalcTextSizeA(size, 100000.f, 0.f, t).x;
}

inline void text(float size, ImVec2 at, ImU32 col, const char* t, ImFont* f = nullptr) {
    if (L.dl && t && *t) L.dl->AddText(face(f), size, at, col, t);
}

inline void textOn(ImDrawList* dl, float size, ImVec2 at, ImU32 col, const char* t, ImFont* f = nullptr) {
    if (dl && t && *t) dl->AddText(face(f), size, at, col, t);
}

inline float lineY(float size, float cy) { return cy - size * .545f; }
inline void textL(float size, float x, float cy, ImU32 col, const char* t, ImFont* f = nullptr) {
    text(size, ImVec2(x, lineY(size, cy)), col, t, f);
}
inline void textR(float size, float right, float cy, ImU32 col, const char* t, ImFont* f = nullptr) {
    text(size, ImVec2(right - textWidth(t, size, f), lineY(size, cy)), col, t, f);
}
inline void textC(float size, float cx, float cy, ImU32 col, const char* t, ImFont* f = nullptr) {
    text(size, ImVec2(cx - textWidth(t, size, f) * .5f, lineY(size, cy)), col, t, f);
}

inline void textTracked(float size, float cx, float cy, ImU32 col, const char* t, float track,
                        ImFont* f = nullptr) {
    if (!t || !*t) return;
    const float total = textWidth(t, size, f) + track * float(std::strlen(t) - 1);
    float x = cx - total * .5f;
    char glyph[2] = {0, 0};
    for (const char* p = t; *p; ++p) {
        glyph[0] = *p;
        text(size, ImVec2(x, lineY(size, cy)), col, glyph, f);
        x += textWidth(glyph, size, f) + track;
    }
}

// ── input ───────────────────────────────────────────────────────────────────
inline bool pointerInside(const ImRect& r) {
    const ImVec2& m = L.mouse;
    return m.x >= L.clipMin.x && m.x <= L.clipMax.x && m.y >= L.clipMin.y && m.y <= L.clipMax.y &&
           r.Contains(m);
}

inline bool hover(const ImRect& r) { return L.windowHovered && L.interactive && pointerInside(r); }

// Tap semantics: press arms the control, release inside fires it. A drag that
// starts on a control never fires it — it scrolls instead.
inline bool tap(const ImRect& r, ImGuiID id) {
    if (!L.windowHovered || !L.interactive) return false;
    const bool over = pointerInside(r);
    if (over && L.pressed && !L.claimed) {
        L.pressId = id;
        L.pressPos = L.mouse;
        L.claimed = true;
    }
    if (L.pressId == id && L.released) {
        const bool fired = !L.dragging && over;
        L.pressId = 0;
        L.claimed = true;
        return fired;
    }
    return false;
}

inline float& animate(ImGuiID id, float initial) {
    return *ImGui::GetStateStorage()->GetFloatRef(id, initial);
}

inline void toward(float& v, float target, float speed) {
    v += (target - v) * std::min(1.f, L.dt * speed);
}

inline void spacer(float units) { L.y += px(units); }

// ── rows ────────────────────────────────────────────────────────────────────
inline void section(const char* label) {
    L.y += px(6.f);
    char upper[64];
    std::size_t n = 0;
    for (const char* p = label; *p && n + 1 < sizeof upper; ++p) upper[n++] = char(std::toupper(static_cast<unsigned char>(*p)));
    upper[n] = 0;
    const float size = px(10.5f), cy = L.y + px(10.f);
    textL(size, L.x0, cy, kTextFaint, upper);
    const float lx = L.x0 + textWidth(upper, size) + px(12.f);
    if (lx < L.x1) L.dl->AddLine(ImVec2(lx, cy + .5f), ImVec2(L.x1, cy + .5f), kHairSoft, px(1.f));
    L.y += px(18.f);
}

inline void paragraph(const char* t, ImU32 col = kTextDim, float units = 11.5f) {
    if (!t || !*t) return;
    const float size = px(units), lineH = size * 1.32f;
    const float width = L.x1 - L.x0;
    char line[512];
    std::size_t used = 0;
    const char* word = t;
    while (*word) {
        while (*word == ' ') ++word;
        const char* end = word;
        while (*end && *end != ' ') ++end;
        char token[128];
        const std::size_t len = std::min<std::size_t>(std::size_t(end - word), sizeof token - 1);
        std::memcpy(token, word, len);
        token[len] = 0;
        char probe[640];
        if (used == 0) std::snprintf(probe, sizeof probe, XS("%s"), token);
        else std::snprintf(probe, sizeof probe, XS("%s %s"), line, token);
        if (textWidth(probe, size) > width && used > 0) {
            text(size, ImVec2(L.x0, L.y), col, line);
            L.y += lineH;
            used = 0;
            std::snprintf(line, sizeof line, XS("%s"), token);
            used = std::strlen(line);
        } else {
            std::snprintf(line, sizeof line, XS("%s"), probe);
            used = std::strlen(line);
        }
        word = end;
    }
    if (used > 0) {
        text(size, ImVec2(L.x0, L.y), col, line);
        L.y += lineH;
    }
    L.y += px(4.f);
}

inline void kv(const char* key, const char* value) {
    const float cy = L.y + px(11.f);
    textL(px(11.5f), L.x0, cy, kTextFaint, key);
    textR(px(11.5f), L.x1, cy, kTextDim, value);
    L.y += px(22.f);
}

inline void divider() {
    L.dl->AddLine(ImVec2(L.x0, L.y + .5f), ImVec2(L.x1, L.y + .5f), kHairSoft, px(1.f));
    L.y += px(6.f);
}

inline bool toggle(const char* id, bool& value, const char* label) {
    const float rowH = px(38.f), cy = L.y + rowH * .5f;
    const ImRect row(L.x0, L.y, L.x1, L.y + rowH);
    const ImGuiID uid = ImGui::GetID(id);
    const bool over = hover(row);

    bool changed = false;
    if (tap(row, uid)) {
        value = !value;
        changed = true;
    }

    float& t = animate(uid ^ 0x9E3779B9u, value ? 1.f : 0.f);
    toward(t, value ? 1.f : 0.f, 16.f);

    const float w = px(42.f), h = px(21.f);
    const ImRect sw(L.x0, cy - h * .5f, L.x0 + w, cy + h * .5f);
    const float alpha = t;

    // Track: dark glass when off, lit lavender when on.
    L.dl->AddRectFilled(sw.Min, sw.Max, mix(IM_COL32(255, 255, 255, 14), kOnA, alpha), h * .5f);
    if (alpha > .01f) {
        L.dl->AddRectFilledMultiColor(ImVec2(sw.Min.x + h * .5f, sw.Min.y),
                                      ImVec2(sw.Max.x - h * .5f, sw.Max.y),
                                      fade(kOnA, alpha), fade(kOnA, alpha),
                                      fade(kOnB, alpha), fade(kOnB, alpha));
    }
    L.dl->AddRect(sw.Min, sw.Max,
                  mix(over ? IM_COL32(255, 255, 255, 64) : IM_COL32(255, 255, 255, 40),
                      IM_COL32(255, 255, 255, 26), alpha),
                  h * .5f, 0, px(1.f));

    const float pad = px(2.5f), r = h * .5f - pad;
    const float kx = mix(sw.Min.x + pad + r, sw.Max.x - pad - r, alpha);
    L.dl->AddCircleFilled(ImVec2(kx, cy + px(1.25f)), r, IM_COL32(0, 0, 0, 65), 32);
    L.dl->AddCircleFilled(ImVec2(kx, cy), r, mix(IM_COL32(146, 148, 155, 255), IM_COL32(255, 255, 255, 255), alpha), 32);

    textL(px(14.5f), L.x0 + px(52.f), cy, over ? IM_COL32(255, 255, 255, 255) : kText, label);
    L.y = row.Max.y;
    return changed;
}

inline bool sliderF(const char* id, const char* label, float& value, float lo, float hi, const char* fmt,
                    bool integer = false) {
    const float rowH = px(38.f), cy = L.y + rowH * .5f;
    const ImRect row(L.x0, L.y, L.x1, L.y + rowH);
    const ImGuiID uid = ImGui::GetID(id);
    const bool over = hover(row) || L.sliderId == uid;

    const float boxW = px(56.f), boxH = px(23.f);
    const ImRect box(L.x1 - boxW, cy - boxH * .5f, L.x1, cy + boxH * .5f);
    const float tx0 = L.x0 + (L.x1 - L.x0) * .30f;
    const float tx1 = box.Min.x - px(20.f);

    bool changed = false;
    if (L.windowHovered && L.interactive && L.pressed && !L.claimed && row.Contains(L.mouse) &&
        L.mouse.x >= tx0 - px(16.f)) {
        L.sliderId = uid;
        L.claimed = true;
    }
    if (L.sliderId == uid) {
        if (!L.down) {
            L.sliderId = 0;
        } else {
            float t = (L.mouse.x - tx0) / std::max(1.f, tx1 - tx0);
            t = std::clamp(t, 0.f, 1.f);
            float next = lo + t * (hi - lo);
            if (integer) next = std::round(next);
            if (std::fabs(next - value) > 1e-6f) {
                value = next;
                changed = true;
            }
        }
    }

    const float t = std::clamp((value - lo) / std::max(1e-6f, hi - lo), 0.f, 1.f);
    const float thumbX = tx0 + (tx1 - tx0) * t;
    const float th = px(4.f);

    L.dl->AddRectFilled(ImVec2(tx0, cy - th * .5f), ImVec2(tx1, cy + th * .5f), kTrack, th * .5f);
    if (thumbX > tx0) L.dl->AddRectFilled(ImVec2(tx0, cy - th * .5f), ImVec2(thumbX, cy + th * .5f), kTrackFill, th * .5f);
    L.dl->AddCircleFilled(ImVec2(thumbX, cy), px(11.f), IM_COL32(203, 210, 226, 34), 32);
    L.dl->AddCircleFilled(ImVec2(thumbX, cy + px(1.f)), px(8.5f), IM_COL32(0, 0, 0, 60), 32);
    L.dl->AddCircleFilled(ImVec2(thumbX, cy), px(8.f), kThumb, 32);

    textL(px(14.5f), L.x0, cy, over ? IM_COL32(255, 255, 255, 255) : kText, label);

    char buffer[48];
    if (integer) std::snprintf(buffer, sizeof buffer, fmt, int(std::lround(value)));
    else std::snprintf(buffer, sizeof buffer, fmt, double(value));
    L.dl->AddRectFilled(box.Min, box.Max, kField, px(6.f));
    L.dl->AddRect(box.Min, box.Max, over ? IM_COL32(255, 255, 255, 78) : kFieldEdge, px(6.f), 0, px(1.f));
    textC(px(13.f), box.GetCenter().x, cy, kText, buffer, fonts::strong);

    L.y = row.Max.y;
    return changed;
}

inline bool slider(const char* id, const char* label, float& value, float lo, float hi, const char* fmt) {
    return sliderF(id, label, value, lo, hi, fmt, false);
}

inline bool sliderInt(const char* id, const char* label, int& value, int lo, int hi, const char* fmt) {
    float proxy = float(value);
    const bool changed = sliderF(id, label, proxy, float(lo), float(hi), fmt, true);
    value = int(std::lround(proxy));
    return changed;
}

// ── popups (drawn inside the panel, above content) ──────────────────────────
enum class PopupKind { None, Select, Color };

struct Popup {
    PopupKind kind = PopupKind::None;
    ImRect anchor{}, box{};
    int* value = nullptr;
    const char* const* items = nullptr;
    int count = 0;
    ImVec4* color = nullptr;
    bool* dirty = nullptr;
    float hue = 0.f, sat = 0.f, val = 1.f;
    int drag = 0;
    ImRect svRect{}, hueRect{}, alphaRect{};
};
inline Popup P;

inline bool popupOpen() { return P.kind != PopupKind::None; }
inline void closePopup() { P.kind = PopupKind::None; P.value = nullptr; P.color = nullptr; P.drag = 0; }

inline void openSelect(const ImRect& anchor, int* value, const char* const* items, int count, bool* dirty) {
    P.kind = PopupKind::Select;
    P.anchor = anchor;
    P.value = value;
    P.items = items;
    P.count = count;
    P.dirty = dirty;
}

inline void openColor(const ImRect& anchor, ImVec4* color, bool* dirty) {
    P.kind = PopupKind::Color;
    P.anchor = anchor;
    P.color = color;
    P.dirty = dirty;
    P.drag = 0;
    rgbToHsv(color->x, color->y, color->z, P.hue, P.sat, P.val);
}

inline void selectRow(const char* id, const char* label, int& value, const char* const* items, int count,
                      bool* dirty) {
    const float rowH = px(38.f), cy = L.y + rowH * .5f;
    const ImRect row(L.x0, L.y, L.x1, L.y + rowH);
    const ImGuiID uid = ImGui::GetID(id);
    const ImRect field(L.x1 - px(132.f), cy - px(12.5f), L.x1, cy + px(12.5f));
    const bool over = hover(row) || hover(field);

    textL(px(14.5f), L.x0, cy, over ? IM_COL32(255, 255, 255, 255) : kText, label);

    L.dl->AddRectFilled(field.Min, field.Max, kField, px(5.f));
    L.dl->AddRect(field.Min, field.Max, over ? IM_COL32(255, 255, 255, 78) : kFieldEdge, px(6.f), 0, px(1.f));
    const char* current = (items && value >= 0 && value < count) ? items[value] : XS("");
    textL(px(13.f), field.Min.x + px(10.f), cy, kText, current);

    const float cxx = field.Max.x - px(15.f);
    L.dl->AddLine(ImVec2(cxx - px(4.f), cy - px(2.f)), ImVec2(cxx, cy + px(3.f)), kTextDim, px(1.4f));
    L.dl->AddLine(ImVec2(cxx, cy + px(3.f)), ImVec2(cxx + px(4.f), cy - px(2.f)), kTextDim, px(1.4f));

    if (tap(row, uid)) openSelect(field, &value, items, count, dirty);
    L.y = row.Max.y;
}

inline void colorRow(const char* id, const char* label, ImVec4& color, bool* dirty) {
    const float rowH = px(38.f), cy = L.y + rowH * .5f;
    const ImRect row(L.x0, L.y, L.x1, L.y + rowH);
    const ImGuiID uid = ImGui::GetID(id);
    const ImRect swatch(L.x1 - px(42.f), cy - px(9.f), L.x1, cy + px(9.f));
    const bool over = hover(row);

    textL(px(14.5f), L.x0, cy, over ? IM_COL32(255, 255, 255, 255) : kText, label);
    L.dl->AddRectFilled(swatch.Min, swatch.Max, colorU32(color), px(4.f));
    L.dl->AddRect(swatch.Min, swatch.Max, over ? IM_COL32(255, 255, 255, 120) : IM_COL32(255, 255, 255, 54),
                  px(5.f), 0, px(1.f));

    if (tap(row, uid)) openColor(swatch, &color, dirty);
    L.y = row.Max.y;
}

inline bool buttonRow(const char* id, const char* label) {
    const float h = px(32.f);
    const ImRect row(L.x0, L.y, L.x1, L.y + h);
    const ImGuiID uid = ImGui::GetID(id);
    const bool over = hover(row);
    const bool fired = tap(row, uid);

    L.dl->AddRectFilled(row.Min, row.Max, over ? kHover : kField, px(7.f));
    L.dl->AddRect(row.Min, row.Max, over ? IM_COL32(255, 255, 255, 88) : kFieldEdge, px(7.f), 0, px(1.f));
    textC(px(13.5f), row.GetCenter().x, row.GetCenter().y, over ? IM_COL32(255, 255, 255, 255) : kText, label);
    L.y = row.Max.y + px(4.f);
    return fired;
}

inline bool chips(const char* id, std::uint32_t& mask, const char* const* names, int count) {
    const float h = px(24.f), gap = px(5.f), size = px(12.5f);
    const float maxRight = L.x1;
    bool changed = false;
    float x = L.x0;
    L.y += px(2.f);
    for (int i = 0; i < count; ++i) {
        const float w = textWidth(names[i], size) + px(20.f);
        if (x + w > maxRight && x > L.x0) {
            x = L.x0;
            L.y += h + gap;
        }
        const ImRect pill(x, L.y, x + w, L.y + h);
        const ImGuiID uid = ImGui::GetID(id) + ImGuiID(i + 1);
        const bool on = (mask & (1u << i)) != 0;
        const bool over = hover(pill);
        if (tap(pill, uid)) {
            mask ^= 1u << i;
            changed = true;
        }
        if (on) {
            L.dl->AddRectFilled(pill.Min, pill.Max, kActive, h * .5f);
            textC(size, pill.GetCenter().x, pill.GetCenter().y, IM_COL32(10, 10, 11, 255), names[i]);
        } else {
            L.dl->AddRectFilled(pill.Min, pill.Max, over ? kHover : IM_COL32(0, 0, 0, 0), h * .5f);
            L.dl->AddRect(pill.Min, pill.Max, over ? IM_COL32(255, 255, 255, 70) : IM_COL32(255, 255, 255, 34),
                          h * .5f, 0, px(1.f));
            textC(size, pill.GetCenter().x, pill.GetCenter().y, kTextDim, names[i]);
        }
        x += w + gap;
    }
    L.y += h + px(6.f);
    return changed;
}

// ── popup rendering ─────────────────────────────────────────────────────────
inline void drawPopup() {
    if (!popupOpen()) return;
    ImDrawList* dl = L.dl;
    dl->PushClipRect(L.panelMin, L.panelMax, true);
    dl->AddRectFilled(L.panelMin, L.panelMax, kScrim);

    // Pages are already drawn; the popup owns the pointer from here.
    L.interactive = true;
    L.claimed = false;
    L.clipMin = L.panelMin;
    L.clipMax = L.panelMax;

    if (P.kind == PopupKind::Select) {
        const float padV = px(6.f), rowH = px(32.f);
        float w = P.anchor.GetWidth();
        for (int i = 0; i < P.count; ++i) w = std::max(w, textWidth(P.items[i], px(14.f)) + px(36.f));
        w = std::min(w, L.panelMax.x - L.panelMin.x - px(24.f));
        const float h = padV * 2.f + rowH * float(P.count);
        float x = std::min(P.anchor.Min.x, L.panelMax.x - px(12.f) - w);
        float y = P.anchor.Max.y + px(8.f);
        if (y + h > L.panelMax.y - px(12.f)) y = P.anchor.Min.y - h - px(8.f);
        P.box = ImRect(ImVec2(x, y), ImVec2(x + w, y + h));

        dl->AddRectFilled(P.box.Min, P.box.Max, IM_COL32(16, 16, 18, 253), px(7.f));
        dl->AddRect(P.box.Min, P.box.Max, IM_COL32(255, 255, 255, 52), px(7.f), 0, px(1.f));

        for (int i = 0; i < P.count; ++i) {
            const ImRect item(P.box.Min.x + px(4.f), y + padV + rowH * i, P.box.Max.x - px(4.f),
                              y + padV + rowH * (i + 1));
            const bool selected = P.value && *P.value == i;
            const bool over = hover(item);
            if (over) dl->AddRectFilled(item.Min, item.Max, kHover, px(5.f));
            textL(px(13.5f), item.Min.x + px(12.f), item.GetCenter().y,
                  selected ? IM_COL32(255, 255, 255, 255) : kTextDim, P.items[i]);
            if (selected) dl->AddCircleFilled(ImVec2(item.Max.x - px(14.f), item.GetCenter().y), px(3.f),
                                              kActive, 16);
            const ImGuiID uid = ImGui::GetID(P.items[i]) ^ ImGuiID(i * 7919 + 13);
            if (tap(item, uid)) {
                if (P.value) *P.value = i;
                if (P.dirty) *P.dirty = true;
                closePopup();
                dl->PopClipRect();
                return;
            }
        }
        if (L.windowHovered && L.pressed && !P.box.Contains(L.mouse)) closePopup();
    } else {
        const float pad = px(10.f), svH = px(104.f), barH = px(10.f);
        const float w = px(210.f);
        const float h = pad * 2.f + svH + px(10.f) + barH + px(8.f) + barH + px(12.f) + px(26.f);
        float x = std::min(P.anchor.Min.x - w + P.anchor.GetWidth(), L.panelMax.x - px(12.f) - w);
        x = std::max(x, L.panelMin.x + px(12.f));
        float y = P.anchor.Max.y + px(8.f);
        if (y + h > L.panelMax.y - px(12.f)) y = P.anchor.Min.y - h - px(8.f);
        y = std::max(y, L.panelMin.y + px(12.f));
        P.box = ImRect(ImVec2(x, y), ImVec2(x + w, y + h));

        dl->AddRectFilled(P.box.Min, P.box.Max, IM_COL32(16, 16, 18, 253), px(7.f));
        dl->AddRect(P.box.Min, P.box.Max, IM_COL32(255, 255, 255, 52), px(7.f), 0, px(1.f));

        P.svRect = ImRect(ImVec2(x + pad, y + pad), ImVec2(x + w - pad, y + pad + svH));
        P.hueRect = ImRect(ImVec2(x + pad, P.svRect.Max.y + px(10.f)),
                           ImVec2(x + w - pad, P.svRect.Max.y + px(10.f) + barH));
        P.alphaRect = ImRect(ImVec2(x + pad, P.hueRect.Max.y + px(8.f)),
                             ImVec2(x + w - pad, P.hueRect.Max.y + px(8.f) + barH));

        // saturation / value field
        for (int i = 0; i < 40; ++i) {
            const float t0 = float(i) / 40.f, t1 = float(i + 1) / 40.f;
            float r0, g0, b0, r1, g1, b1;
            hsvToRgb(P.hue, 0.f, 1.f - t0, r0, g0, b0);
            hsvToRgb(P.hue, 1.f, 1.f - t0, r1, g1, b1);
            dl->AddRectFilledMultiColor(ImVec2(P.svRect.Min.x, P.svRect.Min.y + P.svRect.GetHeight() * t0),
                                        ImVec2(P.svRect.Max.x, P.svRect.Min.y + P.svRect.GetHeight() * t1),
                                        rgb(r0, g0, b0), rgb(r1, g1, b1), rgb(r1, g1, b1), rgb(r0, g0, b0));
        }
        // hue strip
        for (int i = 0; i < 6; ++i) {
            float r0, g0, b0, r1, g1, b1;
            hsvToRgb(float(i) / 6.f, 1.f, 1.f, r0, g0, b0);
            hsvToRgb(float(i + 1) / 6.f, 1.f, 1.f, r1, g1, b1);
            dl->AddRectFilledMultiColor(ImVec2(P.hueRect.Min.x + P.hueRect.GetWidth() * (i / 6.f), P.hueRect.Min.y),
                                        ImVec2(P.hueRect.Min.x + P.hueRect.GetWidth() * ((i + 1) / 6.f), P.hueRect.Max.y),
                                        rgb(r0, g0, b0), rgb(r1, g1, b1), rgb(r1, g1, b1), rgb(r0, g0, b0));
        }
        // alpha strip
        const ImU32 solid = rgb(P.color->x, P.color->y, P.color->z, 1.f);
        dl->AddRectFilledMultiColor(P.alphaRect.Min, P.alphaRect.Max, solid & 0x00FFFFFF,
                                    solid & 0x00FFFFFF, solid, solid);

        const ImVec2 svCursor(P.svRect.Min.x + P.svRect.GetWidth() * P.sat,
                              P.svRect.Min.y + P.svRect.GetHeight() * (1.f - P.val));
        dl->AddCircle(svCursor, px(7.f), IM_COL32(0, 0, 0, 130), 24, px(2.f));
        dl->AddCircle(svCursor, px(6.f), IM_COL32(255, 255, 255, 240), 24, px(1.5f));
        const float hueX = P.hueRect.Min.x + P.hueRect.GetWidth() * P.hue;
        dl->AddRectFilled(ImVec2(hueX - px(2.f), P.hueRect.Min.y - px(2.f)),
                          ImVec2(hueX + px(2.f), P.hueRect.Max.y + px(2.f)), IM_COL32(255, 255, 255, 240), px(2.f));

        const float alphaX = P.alphaRect.Min.x + P.alphaRect.GetWidth() * P.color->w;
        dl->AddRectFilled(ImVec2(alphaX - px(2.f), P.alphaRect.Min.y - px(2.f)),
                          ImVec2(alphaX + px(2.f), P.alphaRect.Max.y + px(2.f)), IM_COL32(255, 255, 255, 240), px(2.f));

        // footer: hex + commit
        const float fy = P.alphaRect.Max.y + px(13.f);
        char hex[32];
        std::snprintf(hex, sizeof hex, XS("#%02X%02X%02X"),
                      int(P.color->x * 255.f + .5f), int(P.color->y * 255.f + .5f), int(P.color->z * 255.f + .5f));
        textL(px(13.f), x + pad, fy + px(9.f), kTextDim, hex);
        const ImRect done(ImVec2(P.box.Max.x - pad - px(54.f), fy), ImVec2(P.box.Max.x - pad, fy + px(22.f)));
        const bool doneOver = hover(done);
        dl->AddRectFilled(done.Min, done.Max, doneOver ? kHover : kField, px(5.f));
        dl->AddRect(done.Min, done.Max, kFieldEdge, px(5.f), 0, px(1.f));
        textC(px(13.f), done.GetCenter().x, done.GetCenter().y, kText, XS("Done"));
        const ImGuiID doneId = ImGui::GetID(XS("##color-done"));
        if (tap(done, doneId)) {
            closePopup();
            dl->PopClipRect();
            return;
        }

        // input
        if (L.windowHovered && L.pressed && !L.claimed) {
            if (P.svRect.Contains(L.mouse)) P.drag = 1;
            else if (P.hueRect.Contains(L.mouse)) P.drag = 2;
            else if (P.alphaRect.Contains(L.mouse)) P.drag = 3;
        }
        if (P.drag && !L.down) P.drag = 0;
        if (P.drag) {
            if (P.drag == 1) {
                P.sat = std::clamp((L.mouse.x - P.svRect.Min.x) / P.svRect.GetWidth(), 0.f, 1.f);
                P.val = std::clamp(1.f - (L.mouse.y - P.svRect.Min.y) / P.svRect.GetHeight(), 0.f, 1.f);
            } else if (P.drag == 2) {
                P.hue = std::clamp((L.mouse.x - P.hueRect.Min.x) / P.hueRect.GetWidth(), 0.f, .9999f);
            } else {
                P.color->w = std::clamp((L.mouse.x - P.alphaRect.Min.x) / P.alphaRect.GetWidth(), 0.f, 1.f);
            }
            float r, g, b;
            hsvToRgb(P.hue, P.sat, P.val, r, g, b);
            P.color->x = r;
            P.color->y = g;
            P.color->z = b;
            if (P.dirty) *P.dirty = true;
            L.claimed = true;
        }
        if (L.windowHovered && L.pressed && !P.box.Contains(L.mouse)) closePopup();
    }
    dl->PopClipRect();
}

}  // namespace xev::ui
