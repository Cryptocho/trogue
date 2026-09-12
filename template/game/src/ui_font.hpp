#pragma once

#include <string_view>
#include "raylib.h"

namespace game {

struct UiFont {
    Font font{};
    bool load(std::string_view atlas_path, std::string_view metrics_path);
    void unload();
    bool valid() const;
};

}  // namespace game
