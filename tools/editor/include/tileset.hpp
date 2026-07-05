#pragma once
#include <string>
#include <vector>
#include <array>

struct TileSetSource {
    std::string name;
    std::string texturePath;
    std::array<int,2> margins = {0, 0};
    std::array<int,2> separation = {0, 0};
    std::array<int,2> regionSize = {16, 16};
};

struct OcclusionRegion {
    int x = 0, y = 0, w = 0, h = 0;
    int zOrder = 0;
};

struct TileData {
    int id = -1;
    int sourceIndex = -1;
    std::array<int,2> atlasCoords = {0, 0};
    std::array<int,2> sizeInAtlas = {1, 1};
    std::array<int,2> textureOffset = {0, 0};
    bool flipH = false, flipV = false, transpose = false;
    bool isWall = false;
    std::string placementAnchor = "bottom";
    std::vector<OcclusionRegion> occlusionRegions;
};

struct TileSet {
    std::string name;
    int tileWidth = 16, tileHeight = 16;
    std::vector<TileSetSource> sources;
    std::vector<TileData> tiles;
};

struct MapCell {
    int tileSetIndex = -1;
    int tileId = -1;
};

struct GameMap {
    std::string name;
    int width = 60, height = 60;
    std::vector<MapCell> cells;
    std::vector<TileSet> tileSets;
};