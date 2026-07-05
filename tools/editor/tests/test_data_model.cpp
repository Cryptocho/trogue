#include "../include/tileset.hpp"
#include <cassert>
#include <cstdio>
#include <string>

static int testsPassed = 0;
static int testsFailed = 0;

#define TEST(name) do { printf("  TEST: %s ... ", name);
#define END_TEST(passed) \
    if (passed) { printf("PASS\n"); testsPassed++; } \
    else { printf("FAIL\n"); testsFailed++; } \
} while(0)

static void testTileSetSource() {
    TEST("default values");
    TileSetSource s;
    END_TEST(s.name.empty() && s.texturePath.empty()
          && s.margins[0] == 0 && s.margins[1] == 0
          && s.separation[0] == 0 && s.separation[1] == 0
          && s.regionSize[0] == 16 && s.regionSize[1] == 16);

    TEST("field assignment");
    TileSetSource s2;
    s2.name = "test";
    s2.texturePath = "tileset.png";
    s2.margins = {1, 2};
    s2.separation = {3, 4};
    s2.regionSize = {32, 32};
    END_TEST(s2.name == "test"
          && s2.texturePath == "tileset.png"
          && s2.margins[0] == 1 && s2.margins[1] == 2
          && s2.separation[0] == 3 && s2.separation[1] == 4
          && s2.regionSize[0] == 32 && s2.regionSize[1] == 32);
}

static void testOcclusionRegion() {
    TEST("default values");
    OcclusionRegion r;
    END_TEST(r.x == 0 && r.y == 0 && r.w == 0 && r.h == 0 && r.zOrder == 0);

    TEST("field assignment");
    OcclusionRegion r2;
    r2.x = 10; r2.y = 20; r2.w = 30; r2.h = 40; r2.zOrder = 5;
    END_TEST(r2.x == 10 && r2.y == 20 && r2.w == 30 && r2.h == 40 && r2.zOrder == 5);
}

static void testTileData() {
    TEST("default values");
    TileData td;
    END_TEST(td.id == -1
          && td.sourceIndex == -1
          && td.atlasCoords[0] == 0 && td.atlasCoords[1] == 0
          && td.sizeInAtlas[0] == 1 && td.sizeInAtlas[1] == 1
          && td.textureOffset[0] == 0 && td.textureOffset[1] == 0
          && td.flipH == false && td.flipV == false && td.transpose == false
          && td.isWall == false
          && td.placementAnchor == "bottom"
          && td.occlusionRegions.empty());

    TEST("field assignment");
    TileData td2;
    td2.id = 5;
    td2.sourceIndex = 1;
    td2.atlasCoords = {3, 4};
    td2.sizeInAtlas = {2, 2};
    td2.textureOffset = {8, 8};
    td2.flipH = true;
    td2.flipV = true;
    td2.transpose = true;
    td2.isWall = true;
    td2.placementAnchor = "center";
    END_TEST(td2.id == 5 && td2.sourceIndex == 1
          && td2.atlasCoords[0] == 3 && td2.atlasCoords[1] == 4
          && td2.sizeInAtlas[0] == 2 && td2.sizeInAtlas[1] == 2
          && td2.textureOffset[0] == 8 && td2.textureOffset[1] == 8
          && td2.flipH && td2.flipV && td2.transpose
          && td2.isWall && td2.placementAnchor == "center");

    TEST("occlusion regions vector");
    TileData td3;
    td3.occlusionRegions.push_back({10, 20, 30, 40, 1});
    td3.occlusionRegions.push_back({50, 60, 70, 80, 2});
    END_TEST(td3.occlusionRegions.size() == 2
          && td3.occlusionRegions[0].x == 10
          && td3.occlusionRegions[0].zOrder == 1
          && td3.occlusionRegions[1].y == 60);
}

static void testTileSet() {
    TEST("default values");
    TileSet ts;
    END_TEST(ts.name.empty()
          && ts.tileWidth == 16 && ts.tileHeight == 16
          && ts.sources.empty()
          && ts.tiles.empty());

    TEST("holding sources");
    TileSet ts2;
    ts2.sources.push_back({"ground", "ground.png", {0,0}, {2,2}, {16,16}});
    ts2.sources.push_back({"objects", "objects.png", {1,1}, {1,1}, {32,32}});
    END_TEST(ts2.sources.size() == 2
          && ts2.sources[0].name == "ground"
          && ts2.sources[1].regionSize[0] == 32);

    TEST("holding tiles");
    TileSet ts3;
    ts3.tiles.push_back({0, 0, {0,0}, {1,1}, {0,0}, false, false, false, false, "bottom", {}});
    ts3.tiles.push_back({1, 0, {1,0}, {1,1}, {0,0}, false, false, false, true, "bottom", {}});
    END_TEST(ts3.tiles.size() == 2
          && ts3.tiles[0].id == 0
          && ts3.tiles[1].isWall == true);
}

static void testMapCell() {
    TEST("default values");
    MapCell mc;
    END_TEST(mc.tileSetIndex == -1 && mc.tileId == -1);

    TEST("field assignment");
    MapCell mc2;
    mc2.tileSetIndex = 0;
    mc2.tileId = 42;
    END_TEST(mc2.tileSetIndex == 0 && mc2.tileId == 42);
}

static void testGameMap() {
    TEST("default values");
    GameMap gm;
    END_TEST(gm.name.empty()
          && gm.width == 60 && gm.height == 60
          && gm.cells.empty()
          && gm.tileSets.empty());

    TEST("sentinel defaults after resize");
    GameMap gm2;
    gm2.cells.resize(gm2.width * gm2.height);
    bool sentinelOk = true;
    for (int i = 0; i < gm2.width * gm2.height && sentinelOk; i++) {
        sentinelOk = (gm2.cells[i].tileSetIndex == -1 && gm2.cells[i].tileId == -1);
    }
    END_TEST(gm2.cells.size() == 3600 && sentinelOk);

    TEST("cells write and read");
    GameMap gm3;
    gm3.cells.resize(gm3.width * gm3.height);
    for (int i = 0; i < gm3.width * gm3.height; i++) {
        gm3.cells[i] = {0, i};
    }
    bool allCorrect = true;
    for (int i = 0; i < gm3.width * gm3.height && allCorrect; i++) {
        allCorrect = (gm3.cells[i].tileSetIndex == 0 && gm3.cells[i].tileId == i);
    }
    END_TEST(gm3.cells.size() == 3600 && allCorrect);

    TEST("holding tileSets");
    GameMap gm4;
    gm4.tileSets.push_back({"terrain", 16, 16, {}, {}});
    gm4.tileSets.push_back({"items", 16, 16, {}, {}});
    END_TEST(gm4.tileSets.size() == 2
          && gm4.tileSets[0].name == "terrain"
          && gm4.tileSets[1].name == "items");

    TEST("row-major access pattern");
    GameMap gm5;
    gm5.cells.resize(3 * 4);
    gm5.width = 4; gm5.height = 3;
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 4; col++) {
            gm5.cells[row * 4 + col] = {row, col};
        }
    }
    bool rowMajor = true;
    for (int row = 0; row < 3 && rowMajor; row++) {
        for (int col = 0; col < 4 && rowMajor; col++) {
            auto& c = gm5.cells[row * 4 + col];
            rowMajor = (c.tileSetIndex == row && c.tileId == col);
        }
    }
    END_TEST(rowMajor);
}

int main() {
    printf("=== Data Model Tests ===\n\n");

    printf("--- TileSetSource ---\n");
    testTileSetSource();

    printf("\n--- OcclusionRegion ---\n");
    testOcclusionRegion();

    printf("\n--- TileData ---\n");
    testTileData();

    printf("\n--- TileSet ---\n");
    testTileSet();

    printf("\n--- MapCell ---\n");
    testMapCell();

    printf("\n--- GameMap ---\n");
    testGameMap();

    printf("\n=== Results: %d passed, %d failed ===\n",
           testsPassed, testsFailed);

    return testsFailed > 0 ? 1 : 0;
}