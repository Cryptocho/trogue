#pragma once
#include "tileset.hpp"
#include <SDL3/SDL.h>
#include <imgui.h>

struct EditorState {
    TileSet tileSet;
    SDL_Texture* atlasTexture = nullptr;
    float atlasZoom = 1.0f;
    ImVec2 atlasPan = {0, 0};
    bool atlasPanInitialized = false;
    int selectedTileId = -1;
    int hoveredCol = -1;
    int hoveredRow = -1;
    GameMap map;
    float mapZoom = 1.0f;
    ImVec2 mapPan = {0, 0};
    int hoveredMapCol = -1;
    int hoveredMapRow = -1;
};