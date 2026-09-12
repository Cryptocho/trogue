#include "ui_font.hpp"

#include <cstdlib>
#include <fstream>
#include <nlohmann/json.hpp>
#include "raylib.h"

namespace game {

bool UiFont::load(std::string_view atlas_path, std::string_view metrics_path) {
    unload();
    std::ifstream in{std::string(metrics_path)};
    if (!in) return false;
    nlohmann::json metrics;
    try { in >> metrics; } catch (...) { return false; }
    const auto glyphs = metrics.find("glyphs");
    if (glyphs == metrics.end() || !glyphs->is_object()) return false;

    Font candidate{};
    candidate.baseSize = metrics.value("font_size", 16);
    candidate.glyphCount = static_cast<int>(glyphs->size());
    candidate.glyphPadding = 0;
    candidate.texture = LoadTexture(std::string(atlas_path).c_str());
    if (!IsTextureValid(candidate.texture) || candidate.glyphCount <= 0) {
        if (IsTextureValid(candidate.texture)) UnloadTexture(candidate.texture);
        return false;
    }
    candidate.recs = static_cast<Rectangle*>(
        std::calloc(static_cast<std::size_t>(candidate.glyphCount), sizeof(Rectangle)));
    candidate.glyphs = static_cast<GlyphInfo*>(
        std::calloc(static_cast<std::size_t>(candidate.glyphCount), sizeof(GlyphInfo)));
    if (!candidate.recs || !candidate.glyphs) {
        std::free(candidate.recs);
        std::free(candidate.glyphs);
        UnloadTexture(candidate.texture);
        return false;
    }
    int index = 0;
    for (const auto& item : glyphs->items()) {
        const auto& region = item.value().at("region");
        if (!region.is_array() || region.size() != 4 || !region[0].is_number_integer() ||
            !region[1].is_number_integer() || !region[2].is_number_integer() ||
            !region[3].is_number_integer()) {
            UnloadFont(candidate);
            return false;
        }
        int bytes = 0;
        const int codepoint = GetCodepoint(item.key().c_str(), &bytes);
        candidate.recs[index] = Rectangle{static_cast<float>(region[0].get<int>()),
                                          static_cast<float>(region[1].get<int>()),
                                          static_cast<float>(region[2].get<int>()),
                                          static_cast<float>(region[3].get<int>())};
        candidate.glyphs[index].value = codepoint;
        candidate.glyphs[index].advanceX =
            static_cast<int>(item.value().value("advance", region[2].get<int>()));
        ++index;
    }
    font = candidate;
    return IsFontValid(font) && font.baseSize > 0;
}

void UiFont::unload() {
    if (IsFontValid(font)) UnloadFont(font);
    font = Font{};
}

bool UiFont::valid() const { return IsFontValid(font) && font.baseSize > 0; }

}  // namespace game
