#pragma once
#include <SDL3/SDL.h>
#include <imgui.h>
#include <cstdint>
#include <string>

SDL_Texture* loadTexture(SDL_Renderer* renderer, const char* path);
inline ImTextureID toImTextureID(SDL_Texture* tex) {
    return (ImTextureID)(intptr_t)tex;
}
std::string resolveProjectRoot();