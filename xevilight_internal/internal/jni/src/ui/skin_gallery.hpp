
// ================================================================================
// Source code from @imguisource_bot
// Primary: t.me/ImGui_Source_FIXNEY (ImGui Source)
// Secondary: t.me/il2cpp_src (shit src 🧨)
// ================================================================================

#pragma once

#include "features/visuals/skin_changer.hpp"
#include "ui/xevilight/theme.hpp"
#include "ui/xevilight/widgets.hpp"

#include <GLES3/gl3.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <stb_image.h>

namespace xev::gallery {

extern "C" {
extern const unsigned char sg_index_start[], sg_index_end[], sg_pages_start[], sg_pages_end[];
extern const std::uint32_t sg_page_sizes[];
}

using namespace xev::ui;

struct Tile {
    int id;
    std::uint8_t kind, page;
    std::uint16_t x, y, w, h;
    std::string name;
};

inline std::vector<Tile> g_tiles;
inline std::unordered_map<int, std::uint8_t> g_skinWeapon;
inline std::vector<std::pair<int, const char*>> g_weaponList;
inline int g_mode = 0;            // 0 weapons, 1 knives
inline int g_weapon = 44;         // AKR by default
inline bool g_tried = false, g_ready = false;
inline std::uint16_t g_pageW = 0, g_pageH = 0;
inline std::uint32_t g_pageCount = 0;

struct PageSlot {
    GLuint texture = 0;
    std::uint64_t used = 0;
};
inline std::unordered_map<std::uint8_t, PageSlot> g_pageCache;
inline std::uint64_t g_useClock = 0;
inline std::unordered_map<int, const Tile*> g_tileById;
inline std::unordered_map<int, const Tile*> g_headerById;
inline std::unordered_map<std::string, const Tile*> g_tileByName;

inline const char* weaponLabel(int id) {
    switch (id) {
        case 11: return "G22";
        case 12: return "USP";
        case 13: return "P350";
        case 15: return "Deagle";
        case 16: return "Tec9";
        case 17: return "FiveSeven";
        case 18: return "Berettas";
        case 32: return "UMP45";
        case 33: return "AkimboUzi";
        case 34: return "MP7";
        case 35: return "P90";
        case 36: return "MP5";
        case 37: return "MAC10";
        case 42: return "VAL";
        case 43: return "M4A1";
        case 44: return "AKR";
        case 45: return "AKR12";
        case 46: return "M4";
        case 47: return "M16";
        case 48: return "FAMAS";
        case 49: return "FnFal";
        case 51: return "AWM";
        case 52: return "M40";
        case 53: return "M110";
        case 54: return "Mallard";
        case 62: return "SM1014";
        case 63: return "FabM";
        case 64: return "M60";
        case 65: return "SPAS";
        case 70: return "Knife";
        case 71: return "Hands";
        default: return "?";
    }
}

inline std::string displayName(const char* raw) {
    if (!raw) return {};
    std::string out;
    out.reserve(std::strlen(raw));
    bool escaped = false;
    for (const char* p = raw; *p; ++p) {
        if (*p == '\\') {
            escaped = !escaped;
            continue;
        }
        if (!escaped || *p != '"') out.push_back(*p);
        escaped = false;
    }
    return out;
}

inline bool load() {
    if (g_tried) return g_ready;
    g_tried = true;
    const auto* begin = sg_index_start;
    const std::size_t total = static_cast<std::size_t>(sg_index_end - sg_index_start);
    constexpr std::size_t kHeader = 18;
    if (total < kHeader) return false;

    std::uint32_t items = 0;
    std::memcpy(&g_pageCount, begin + 8, 2);
    std::memcpy(&g_pageW, begin + 10, 2);
    std::memcpy(&g_pageH, begin + 12, 2);
    std::memcpy(&items, begin + 14, 4);
    if (!g_pageCount || !g_pageW || !g_pageH || g_pageCount > 256) return false;
    if (items == 0 || items > 4096) return false;

    const std::size_t tilesEnd = kHeader + std::size_t(items) * 16;
    if (total < tilesEnd + 4) return false;
    std::uint32_t blobLen = 0;
    std::memcpy(&blobLen, begin + tilesEnd, 4);
    if (!blobLen || total < tilesEnd + 4 + blobLen) return false;
    const char* blob = reinterpret_cast<const char*>(begin + tilesEnd + 4);

    g_tiles.reserve(items);
    for (std::uint32_t i = 0; i < items; ++i) {
        const auto* p = begin + kHeader + std::size_t(i) * 16;
        Tile tile{};
        std::uint16_t nameOffset = 0;
        std::memcpy(&tile.id, p, 4);
        std::memcpy(&tile.kind, p + 4, 1);
        std::memcpy(&tile.page, p + 5, 1);
        std::memcpy(&tile.x, p + 6, 2);
        std::memcpy(&tile.y, p + 8, 2);
        std::memcpy(&tile.w, p + 10, 2);
        std::memcpy(&tile.h, p + 12, 2);
        std::memcpy(&nameOffset, p + 14, 2);
        if (nameOffset >= blobLen) return false;
        tile.name = displayName(blob + nameOffset);
        if (!tile.name.empty()) g_tiles.push_back(std::move(tile));
    }

    for (const auto& tile : g_tiles) {
        if (tile.kind == 0 || tile.kind == 1) {
            g_tileById.emplace(tile.id, &tile);
            g_tileByName.emplace(tile.name, &tile);
        } else if (tile.kind == 6) {
            g_headerById.emplace(tile.id, &tile);
        }
    }
    for (const auto& entry : skin_catalog::entries) g_skinWeapon.emplace(entry.id, entry.weapon);
    for (const auto& entry : skin_catalog::entries) {
        if (entry.weapon == 0 || entry.weapon == 70 || entry.weapon == 71) continue;
        bool seen = false;
        for (const auto& row : g_weaponList)
            if (row.first == entry.weapon) {
                seen = true;
                break;
            }
        if (!seen) g_weaponList.emplace_back(entry.weapon, weaponLabel(entry.weapon));
    }
    std::sort(g_weaponList.begin(), g_weaponList.end(),
              [](const auto& a, const auto& b) { return std::strcmp(a.second, b.second) < 0; });

    g_ready = true;
    return true;
}

inline void resetCache() { g_pageCache.clear(); }

inline void evictPages() {
    // Keep every page alive for the whole session. The draw list holds texture
    // ids until the frame is submitted, so deleting a page mid-frame turns
    // every earlier tile on that page black. Pages are decoded at half size
    // (256x128 RGB), so the full 96-page atlas is only ~9 MB of VRAM.
    while (g_pageCache.size() > g_pageCount) {
        auto oldest = g_pageCache.begin();
        for (auto it = g_pageCache.begin(); it != g_pageCache.end(); ++it)
            if (it->second.used < oldest->second.used) oldest = it;
        if (oldest->second.texture) glDeleteTextures(1, &oldest->second.texture);
        g_pageCache.erase(oldest);
    }
}

inline GLuint pageTexture(std::uint8_t page) {
    auto found = g_pageCache.find(page);
    if (found != g_pageCache.end()) {
        found->second.used = ++g_useClock;
        return found->second.texture;
    }
    if (page >= g_pageCount) return 0;

    std::size_t offset = 0;
    for (std::uint32_t i = 0; i < page; ++i) offset += sg_page_sizes[i];
    const std::uint32_t packed = sg_page_sizes[page];
    if (!packed || sg_pages_start + offset + packed > sg_pages_end) return 0;

    int width = 0, height = 0, channels = 0;
    unsigned char* pixels = stbi_load_from_memory(sg_pages_start + offset, static_cast<int>(packed), &width,
                                                  &height, &channels, 3);
    if (!pixels) return 0;

    GLuint texture = 0;
    glGenTextures(1, &texture);
    if (!texture) {
        stbi_image_free(pixels);
        return 0;
    }
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels);
    stbi_image_free(pixels);

    g_pageCache[page] = {texture, ++g_useClock};
    evictPages();
    return texture;
}

// ── widgets ─────────────────────────────────────────────────────────────────
inline bool chip(const ImVec2& at, float w, float h, const char* label, bool active, ImGuiID id) {
    const ImRect rect(at, ImVec2(at.x + w, at.y + h));
    const bool over = hover(rect);
    if (active) L.dl->AddRectFilled(rect.Min, rect.Max, IM_COL32(226, 229, 236, 245), px(4.f));
    else if (over) L.dl->AddRectFilled(rect.Min, rect.Max, kHover, px(4.f));
    L.dl->AddRect(rect.Min, rect.Max, active ? IM_COL32(255, 255, 255, 190) : kEdgeSoft, px(4.f), 0, px(1.f));
    textC(px(9.f), rect.GetCenter().x, rect.GetCenter().y,
          active ? IM_COL32(12, 12, 14, 255) : (over ? kText : kTextDim), label, fonts::body);
    return tap(rect, id);
}

inline float chipRow(float top, const std::vector<std::pair<std::string, bool>>& items,
                     const std::function<void(int)>& press, const char* prefix) {
    const float h = px(26.f), gapX = px(6.f), gapY = px(6.f);
    const float width = L.x1 - L.x0;
    float x = 0.f, y = 0.f;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const float w = textWidth(items[i].first.c_str(), px(9.f), fonts::body) + px(18.f);
        if (x > 0.f && x + w > width) {
            x = 0.f;
            y += h + gapY;
        }
        ImGui::PushID(prefix);
        ImGui::PushID(static_cast<int>(i));
        if (chip(ImVec2(L.x0 + x, top + y), w, h, items[i].first.c_str(), items[i].second,
                 ImGui::GetID(XS("chip"))))
            press(static_cast<int>(i));
        ImGui::PopID();
        ImGui::PopID();
        x += w + gapX;
    }
    return y + h;
}

inline std::string fitName(const std::string& text, float size, float maxWidth) {
    if (textWidth(text.c_str(), size, fonts::body) <= maxWidth) return text;
    std::string out = text;
    while (!out.empty() && textWidth((out + "...").c_str(), size, fonts::body) > maxWidth) out.pop_back();
    return out + "...";
}

inline std::string stripVariantSuffix(std::string name) {
    // Two spellings are used by the catalogue: `AKR "Name" StatTrack` and
    // `AKR "Name ST"` (suffix inside the closing quote).
    const std::string stat = " StatTrack";
    if (name.size() > stat.size() && name.compare(name.size() - stat.size(), stat.size(), stat) == 0)
        name.resize(name.size() - stat.size());
    if (name.size() >= 4 && name.back() == '"') {
        const std::size_t quote = name.size() - 1;
        if (quote >= 3 && name.compare(quote - 3, 3, " ST") == 0) name.erase(quote - 3, 3);
    }
    return name;
}

// Catalogue ids do not always have their own atlas tile (StatTrack variants
// reuse the base art). Resolve by id, then by exact name, then by the name
// with the StatTrack suffix removed, so a row is never left with a blank card.
inline const Tile* tileFor(const skin_catalog::entry& entry, std::string& nameOut) {
    nameOut = displayName(entry.name);
    if (const auto it = g_tileById.find(entry.id); it != g_tileById.end()) return it->second;
    if (const auto it = g_tileByName.find(nameOut); it != g_tileByName.end()) return it->second;
    const std::string base = stripVariantSuffix(nameOut);
    if (base != nameOut) {
        if (const auto it = g_tileByName.find(base); it != g_tileByName.end()) return it->second;
    }
    return nullptr;
}

inline bool drawArt(const Tile* tile, const ImVec2& p0, const ImVec2& p1, float rounding) {
    if (!tile || !g_pageW || !g_pageH) return false;
    const GLuint texture = pageTexture(tile->page);
    if (!texture) return false;
    const float bw = p1.x - p0.x, bh = p1.y - p0.y;
    if (bw <= 1.f || bh <= 1.f) return false;
    const float scale = std::min(bw / float(tile->w), bh / float(tile->h));
    const float dw = float(tile->w) * scale, dh = float(tile->h) * scale;
    const ImVec2 a(p0.x + (bw - dw) * .5f, p0.y + (bh - dh) * .5f);
    const ImVec2 b(a.x + dw, a.y + dh);
    const ImVec2 uv0(float(tile->x) / g_pageW, float(tile->y) / g_pageH);
    const ImVec2 uv1(float(tile->x + tile->w) / g_pageW, float(tile->y + tile->h) / g_pageH);
    L.dl->AddImageRounded(static_cast<ImTextureID>(texture), a, b, uv0, uv1,
                          IM_COL32(255, 255, 255, 255), rounding);
    return true;
}

inline void drawTile(const Tile* tile, const Tile* fallback, const ImVec2& at, float w, float h,
                     const char* label, bool selected, bool isDefault) {
    const ImRect box(at, ImVec2(at.x + w, at.y + h));
    const bool over = hover(box);
    const ImU32 top = over ? IM_COL32(255, 255, 255, 16) : IM_COL32(255, 255, 255, 8);
    const ImU32 bottom = over ? IM_COL32(255, 255, 255, 8) : IM_COL32(255, 255, 255, 3);
    L.dl->AddRectFilledMultiColor(box.Min, box.Max, top, top, bottom, bottom);
    L.dl->AddRect(box.Min, box.Max,
                  selected ? IM_COL32(238, 240, 245, 235) : (over ? kEdge : kEdgeSoft), px(5.f), 0,
                  px(selected ? 1.6f : 1.f));

    const float pad = px(4.f);
    const ImVec2 artMin(box.Min.x + pad, box.Min.y + pad);
    const ImVec2 artMax(box.Max.x - pad, box.Max.y - px(18.f));
    const Tile* art = tile ? tile : fallback;
    if (!drawArt(art, artMin, artMax, px(3.f))) {
        textC(px(16.f), box.GetCenter().x, (artMin.y + artMax.y) * .5f,
              IM_COL32(255, 255, 255, 52), isDefault ? XS("DEFAULT") : XS("..."), fonts::strong);
    } else if (!tile) {
        // no dedicated thumbnail: dim the weapon plate so the fallback reads as one
        L.dl->AddRectFilled(artMin, artMax, IM_COL32(0, 0, 0, 92), px(3.f));
    }
    if (selected) {
        L.dl->AddCircleFilled(ImVec2(box.Max.x - px(8.f), box.Min.y + px(8.f)), px(3.5f),
                              IM_COL32(240, 242, 247, 245), 12);
    }

    const float nameSize = px(8.f);
    const std::string shown = fitName(label ? label : "", nameSize, w - px(6.f));
    textC(nameSize, box.GetCenter().x, box.Max.y - px(8.5f), selected ? kText : kTextDim, shown.c_str(),
          fonts::body);
}

inline void drawFeatured(const Tile* tile, const Tile* fallback, const char* name, const char* weaponName,
                         bool isDefault) {
    const float w = L.x1 - L.x0;
    const float h = px(92.f);
    const ImRect box(ImVec2(L.x0, L.y), ImVec2(L.x1, L.y + h));
    L.dl->AddRectFilledMultiColor(box.Min, box.Max, IM_COL32(255, 255, 255, 13),
                                  IM_COL32(255, 255, 255, 10), IM_COL32(255, 255, 255, 4),
                                  IM_COL32(255, 255, 255, 3));
    L.dl->AddRect(box.Min, box.Max, kEdgeSoft, px(6.f), 0, px(1.f));

    const float pad = px(7.f);
    const float artW = std::min(w * .34f, h - pad * 2.f);
    const ImRect art(ImVec2(box.Min.x + pad, box.Min.y + pad),
                     ImVec2(box.Min.x + pad + artW, box.Max.y - pad));
    L.dl->AddRectFilled(art.Min, art.Max, IM_COL32(0, 0, 0, 80), px(4.f));
    const Tile* a = tile ? tile : fallback;
    if (!drawArt(a, art.Min, art.Max, px(4.f))) {
        textC(px(19.f), art.GetCenter().x, art.GetCenter().y, IM_COL32(255, 255, 255, 50),
              isDefault ? XS("DEFAULT") : XS("..."), fonts::strong);
    } else if (!tile) {
        L.dl->AddRectFilled(art.Min, art.Max, IM_COL32(0, 0, 0, 80), px(4.f));
    }

    const float textX = art.Max.x + px(12.f);
    const float right = box.Max.x - px(10.f);
    textOn(L.dl, px(8.f), ImVec2(textX, lineY(px(8.f), box.Min.y + px(21.f))), kTextDim, weaponName,
           fonts::body);
    const float nameSize = px(15.f);
    const std::string shown = fitName(name, nameSize, right - textX);
    textOn(L.dl, nameSize, ImVec2(textX, lineY(nameSize, box.Min.y + px(45.f))), kText, shown.c_str(),
           fonts::strong);

    const char* state = isDefault ? XS("FACTORY") : XS("EQUIPPED");
    const float badgeW = textWidth(state, px(8.f), fonts::strong) + px(14.f);
    const ImRect badge(ImVec2(textX, box.Max.y - px(24.f)),
                       ImVec2(textX + badgeW, box.Max.y - px(10.f)));
    L.dl->AddRectFilled(badge.Min, badge.Max, IM_COL32(255, 255, 255, 12), px(8.f));
    L.dl->AddRect(badge.Min, badge.Max, kEdgeSoft, px(8.f), 0, px(1.f));
    textC(px(8.f), badge.GetCenter().x, badge.GetCenter().y, kText, state, fonts::strong);
}

// ── panel ───────────────────────────────────────────────────────────────────
inline void panel() {
    if (!load()) {
        paragraph(XS("Gallery atlas failed to load."), kTextFaint);
        return;
    }

    section(XS("Skin changer"));
    bool on = astra::skins::enabled.load(std::memory_order_relaxed);
    if (toggle(XS("skin.enabled"), on, XS("Enable skin changer")))
        astra::skins::enabled.store(on, std::memory_order_relaxed);
    const std::string state = astra::skins::status();
    kv(XS("State"), state.c_str());
    kv(XS("Equipped"), weaponLabel(astra::skins::currentWeapon.load(std::memory_order_relaxed)));
    paragraph(XS("Presentation cache only — inventory, ownership and the network WeaponSkinId stay untouched."),
              kTextFaint, 10.5f);

    section(XS("Gallery"));
    std::vector<std::pair<std::string, bool>> modes = {
        {XS("Weapons"), g_mode == 0},
        {XS("Knives"), g_mode == 1},
    };
    float y = chipRow(L.y, modes, [&](int index) { g_mode = index; }, XS("gal.mode"));
    L.y += y + px(6.f);

    if (g_mode == 0) {
        std::vector<std::pair<std::string, bool>> weapons;
        weapons.reserve(g_weaponList.size());
        for (const auto& row : g_weaponList) weapons.emplace_back(row.second, row.first == g_weapon);
        y = chipRow(L.y, weapons, [&](int index) { g_weapon = g_weaponList[std::size_t(index)].first; },
                    XS("gal.weapon"));
        L.y += y + px(8.f);
    }

    const int weapon = g_mode == 0 ? g_weapon : 70;
    const int selected = astra::skins::choice(weapon);

    struct Row {
        const Tile* tile;
        std::string name;
        int id;
    };
    std::vector<Row> rows;
    rows.reserve(64);
    const int wanted = g_mode == 0 ? weapon : 70;
    for (const auto& entry : skin_catalog::entries) {
        if (int(entry.weapon) != wanted) continue;
        std::string name;
        const Tile* tile = tileFor(entry, name);
        rows.push_back({tile, std::move(name), entry.id});
    }
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.name < b.name; });

    const Tile* header = nullptr;
    if (const auto it = g_headerById.find(weapon); it != g_headerById.end()) header = it->second;

    const Tile* selectedTile = nullptr;
    std::string selectedName = XS("Default");
    if (selected != 0) {
        for (const auto& row : rows)
            if (row.id == selected) {
                selectedTile = row.tile;
                selectedName = row.name;
                break;
            }
    }
    drawFeatured(selectedTile, header, selectedName.c_str(),
                 g_mode == 0 ? weaponLabel(weapon) : XS("Knife"), selected == 0);
    L.y += px(100.f);

    const float gap = px(7.f);
    const int columns = 4;
    const float cellW = (L.x1 - L.x0 - gap * float(columns - 1)) / float(columns);
    const float cellH = cellW * .56f + px(22.f);

    int index = 0;
    const int total = static_cast<int>(rows.size()) + 1;
    const int lineCount = (total + columns - 1) / columns;
    for (int line = 0; line < lineCount; ++line) {
        const float top = L.y + float(line) * (cellH + gap);
        for (int column = 0; column < columns; ++column, ++index) {
            if (index >= total) break;
            const float left = L.x0 + float(column) * (cellW + gap);
            ImGui::PushID(weapon);
            ImGui::PushID(index);
            const ImGuiID id = ImGui::GetID(XS("gal.tile"));
            if (index == 0) {
                drawTile(nullptr, header, ImVec2(left, top), cellW, cellH, XS("Default"), selected == 0, true);
                if (tap(ImRect(ImVec2(left, top), ImVec2(left + cellW, top + cellH)), id))
                    astra::skins::choose(weapon, 0);
            } else {
                const Row& row = rows[std::size_t(index - 1)];
                drawTile(row.tile, header, ImVec2(left, top), cellW, cellH, row.name.c_str(),
                         selected == row.id, false);
                if (tap(ImRect(ImVec2(left, top), ImVec2(left + cellW, top + cellH)), id))
                    astra::skins::choose(weapon, row.id);
            }
            ImGui::PopID();
            ImGui::PopID();
        }
    }
    L.y += float(lineCount) * (cellH + gap);
    if (total <= 1) paragraph(XS("No catalogued skins for this weapon."), kTextFaint);
}

}  // namespace xev::gallery
