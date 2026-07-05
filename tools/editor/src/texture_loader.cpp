#include "texture_loader.hpp"
#include <SDL3_image/SDL_image.h>
#include <filesystem>

static constexpr int MAX_ROOT_WALK = 10;

SDL_Texture* loadTexture(SDL_Renderer* renderer, const char* path) {
    SDL_Surface* surface = IMG_Load(path);
    if (!surface) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load texture: %s (%s)", path, SDL_GetError());
        return nullptr;
    }
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_DestroySurface(surface);
    if (!tex) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create texture from surface: %s (%s)", path, SDL_GetError());
        return nullptr;
    }
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
    return tex;
}

std::string resolveProjectRoot() {
    const char* basePath = SDL_GetBasePath();
    std::filesystem::path p(basePath ? basePath : ".");
    if (basePath) SDL_free((void*)basePath);
    for (int i = 0; i < MAX_ROOT_WALK; i++) {
        if (std::filesystem::exists(p / "src" / "assets")) {
            return p.string();
        }
        if (!p.has_parent_path() || p == p.parent_path()) break;
        p = p.parent_path();
    }
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not find project root (containing src/assets/)");
    return "";
}