#pragma once
// config.hpp —— 版本、目录约定与全局常量（C++20 头文件常量，语义对齐历史 config.h）。
//
// 常量用 inline constexpr 表达（C++17 起 inline 变量，跨翻译单元单一定义）；
// TROGUE_DEBUG 宏由 CMake 注入（见 engine/CMakeLists.txt），IPC/Watcher 的桩化
// 开关不在本头判断，由 hotreload.hpp / ipc.hpp（分卷 5.5）按宏自行处理。

#include <cstddef>  // std::size_t

namespace tg {

// ── 版本 ──
inline constexpr int kVersionMajor = 0;
inline constexpr int kVersionMinor = 1;
inline constexpr int kVersionPatch = 0;

// 版本字符串（engine.cpp 定义，供日志/诊断/错误消息使用）。
// 骨架期唯一需要链接的 engine 符号：最小 consumer 以它验证链接成功。
const char* version_string() noexcept;

// ── 资产目录（相对进程工作目录，约定从项目根运行） ──
inline constexpr const char* kAssetsDir = "assets";
inline constexpr const char* kSceneDir = "assets/scenes";

// ── 通用限额 ──
inline constexpr int kNameMax = 64;       // 实体名等名称上限（含结尾 NUL 的字节数）
inline constexpr int kPathMax = 512;      // 资产相对路径上限

// 实体独立贴图缓存上限（render 模块懒加载，路径去重；分卷 5.3）
inline constexpr int kMaxSpriteTextures = 16;

// ── 场景结构限额（plan-5.2 §2.3，语义对齐既有 C 版） ──
inline constexpr int kLayerMax = 4;               // tilemap.layers ≤4
inline constexpr int kTilesetMax = 8;             // tilemap.tilesets 1..8
inline constexpr int kPaletteMax = 32;            // tilemap.palette 1..32
inline constexpr int kTileDimMax = 256;           // tile_width/height ∈ [1,256]
inline constexpr int kLayerDimMax = 4096;         // 层 width/height ∈ [1,4096]
inline constexpr int kTileSizeInAtlasMax = 4096;  // tro-tileset size_in_atlas 元素 ∈ [1,4096]（plan-8 §3.1）
inline constexpr int kTileOriginMax = 65536;      // tro-tileset texture_origin/y_sort_origin 绝对值上限（防御）

// ── payload 限额（plan-5.2 §2.6，多级、一常量一语义） ──
inline constexpr std::size_t kPayloadBytesMax = 256 * 1024;   // 单 payload 紧凑序列化字节
inline constexpr int kJsonDepthMax = 32;                      // 任一 JSON 嵌套深度
inline constexpr int kPayloadKeysMax = 1024;                  // 任一 object 键数
inline constexpr std::size_t kAssetPayloadBytesMax = 4 * 1024 * 1024;  // 资产累计

// ── 动画限额（plan-5.2 §2.6 / plan-5.4 §2） ──
inline constexpr int kAnimTexturesPerEntityMax = 32;   // 单实体 animations.textures
inline constexpr int kAnimClipsPerEntityMax = 64;      // 单实体 animations.animations
inline constexpr int kAnimFramesPerClipMax = 512;      // 单 clip frames
inline constexpr int kAssetAnimFramesMax = 4096;       // 资产所有 clip 帧合计

// ── IPC（tro-ipc v1，分卷 5.5） ──
inline constexpr int kIpcPortDefault = 48764;
inline constexpr int kIpcMaxClients = 8;
inline constexpr std::size_t kIpcLineMax = 64 * 1024;  // 单行上限（含 \r）

// ── 热重载（分卷 5.5） ──
inline constexpr int kWatchDebounceMs = 150;

}  // namespace tg