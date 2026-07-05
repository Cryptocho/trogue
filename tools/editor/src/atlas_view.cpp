#include "atlas_view.hpp"
#include "texture_loader.hpp"
#include <algorithm>
#include <cmath>

static void screenToAtlasCoords(EditorState& state, ImVec2 screenPos, ImVec2 canvasOrigin,
                                 int& outCol, int& outRow) {
    outCol = -1;
    outRow = -1;
    if (!state.atlasTexture || state.tileSet.sources.empty()) return;

    float texX = (screenPos.x - canvasOrigin.x - state.atlasPan.x) / state.atlasZoom;
    float texY = (screenPos.y - canvasOrigin.y - state.atlasPan.y) / state.atlasZoom;

    const auto& src = state.tileSet.sources[0];
    int stepX = src.regionSize[0] + src.separation[0];
    int stepY = src.regionSize[1] + src.separation[1];

    float adjX = texX - static_cast<float>(src.margins[0]);
    float adjY = texY - static_cast<float>(src.margins[1]);
    if (adjX < 0 || adjY < 0) return;

    outCol = static_cast<int>(adjX / stepX);
    outRow = static_cast<int>(adjY / stepY);

    float texW, texH;
    SDL_GetTextureSize(state.atlasTexture, &texW, &texH);
    int tilesAcross = (static_cast<int>(texW) - src.margins[0] + stepX - 1) / stepX;
    int tilesDown  = (static_cast<int>(texH) - src.margins[1] + stepY - 1) / stepY;

    if (outCol >= tilesAcross || outRow >= tilesDown) {
        outCol = -1;
        outRow = -1;
    }
}

static void drawGrid(EditorState& state, ImDrawList* drawList, ImVec2 topLeft,
                     float scaledW, float scaledH, float texW, float texH) {
    if (state.tileSet.sources.empty()) return;
    const auto& src = state.tileSet.sources[0];
    int stepX = src.regionSize[0] + src.separation[0];
    int stepY = src.regionSize[1] + src.separation[1];

    int tilesAcross = (static_cast<int>(texW) - src.margins[0] + stepX - 1) / stepX;
    int tilesDown   = (static_cast<int>(texH) - src.margins[1] + stepY - 1) / stepY;

    ImU32 gridColor = IM_COL32(128, 128, 128, 76);

    for (int col = 0; col <= tilesAcross; ++col) {
        float x = static_cast<float>(src.margins[0] + col * stepX) * state.atlasZoom;
        float sx = topLeft.x + x;
        drawList->AddLine(ImVec2(sx, topLeft.y), ImVec2(sx, topLeft.y + scaledH), gridColor);
    }
    for (int row = 0; row <= tilesDown; ++row) {
        float y = static_cast<float>(src.margins[1] + row * stepY) * state.atlasZoom;
        float sy = topLeft.y + y;
        drawList->AddLine(ImVec2(topLeft.x, sy), ImVec2(topLeft.x + scaledW, sy), gridColor);
    }
}

void drawAtlasView(EditorState& state) {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("Atlas View", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBringToFrontOnFocus);

    if (!state.atlasTexture) {
        ImGui::TextColored(ImVec4(1, 0, 0, 1), "No texture loaded");
        state.hoveredCol = -1;
        state.hoveredRow = -1;
        ImGui::End();
        return;
    }

    float texW, texH;
    SDL_GetTextureSize(state.atlasTexture, &texW, &texH);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImVec2 avail = ImGui::GetContentRegionAvail();

    float scaledW = texW * state.atlasZoom;
    float scaledH = texH * state.atlasZoom;

    if (!state.atlasPanInitialized) {
        state.atlasPan.x = (avail.x - scaledW) * 0.5f;
        state.atlasPan.y = (avail.y - scaledH) * 0.5f;
        state.atlasPanInitialized = true;
    }

    ImVec2 canvasSize = ImVec2(
        std::max(avail.x, scaledW),
        std::max(avail.y, scaledH)
    );

    ImGui::InvisibleButton("atlasCanvas", canvasSize);
    bool hovered = ImGui::IsItemHovered();

    // --- Step 6: Zoom (mouse wheel centered on cursor) ---
    if (hovered) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (std::fabs(wheel) > 0.0f) {
            ImVec2 mousePos = ImGui::GetMousePos();
            ImVec2 relMouse = ImVec2(mousePos.x - origin.x, mousePos.y - origin.y);
            float texPointX = (relMouse.x - state.atlasPan.x) / state.atlasZoom;
            float texPointY = (relMouse.y - state.atlasPan.y) / state.atlasZoom;

            state.atlasZoom += wheel * 0.1f;
            state.atlasZoom = std::max(0.25f, std::min(16.0f, state.atlasZoom));

            state.atlasPan.x = relMouse.x - texPointX * state.atlasZoom;
            state.atlasPan.y = relMouse.y - texPointY * state.atlasZoom;
        }
    }

    // --- Step 6: Pan (middle mouse drag) ---
    if (ImGui::IsMouseDown(ImGuiMouseButton_Middle) && hovered) {
        state.atlasPan.x += ImGui::GetIO().MouseDelta.x;
        state.atlasPan.y += ImGui::GetIO().MouseDelta.y;
    }

    ImVec2 topLeft = ImVec2(origin.x + state.atlasPan.x, origin.y + state.atlasPan.y);
    ImVec2 bottomRight = ImVec2(topLeft.x + scaledW, topLeft.y + scaledH);

    // --- Step 4: Draw texture ---
    drawList->AddImage(toImTextureID(state.atlasTexture), topLeft, bottomRight);

    // --- Step 7: Grid overlay ---
    drawGrid(state, drawList, topLeft, scaledW, scaledH, texW, texH);

    // --- Step 8: Hover coordinate ---
    if (hovered) {
        ImVec2 mousePos = ImGui::GetMousePos();
        screenToAtlasCoords(state, mousePos, origin, state.hoveredCol, state.hoveredRow);
    } else {
        state.hoveredCol = -1;
        state.hoveredRow = -1;
    }

    if (state.hoveredCol >= 0 && state.hoveredRow >= 0) {
        ImGui::Text("Tile: (%d, %d)", state.hoveredCol, state.hoveredRow);
    }

    ImGui::End();
}