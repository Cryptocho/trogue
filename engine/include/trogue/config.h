#ifndef TROGUE_CONFIG_H
#define TROGUE_CONFIG_H

// ── 版本 ──
#define TROGUE_VERSION_MAJOR 0
#define TROGUE_VERSION_MINOR 1
#define TROGUE_VERSION_PATCH 0
#define TROGUE_VERSION      "0.1.0"

// ── 资产目录（相对进程工作目录，约定从项目根运行） ──
#define TROGUE_ASSETS_DIR "assets"
#define TROGUE_SCENE_DIR "assets/scenes"

// ── 通用限额 ──
#define TROGUE_NAME_MAX 64
#define TROGUE_PATH_MAX 512

// 实体独立贴图缓存上限（render 模块懒加载，路径去重）
#define TROGUE_MAX_SPRITE_TEXTURES 16

// ── IPC（tro-ipc v1）──
#define TROGUE_IPC_PORT_DEFAULT 48764
#define TROGUE_IPC_MAX_CLIENTS  8
#define TROGUE_IPC_LINE_MAX     (64 * 1024)

// ── 热重载 ──
#define TROGUE_WATCH_DEBOUNCE_MS 150

// TROGUE_DEBUG 由 CMake 定义；开启时编译 IPC 与热重载的实际实现，
// 关闭时两个子系统编译为 no-op 桩（API 保持不变）。
#ifndef TROGUE_DEBUG
#define TROGUE_IPC_ENABLED        0
#define TROGUE_HOTRELOAD_ENABLED  0
#else
#define TROGUE_IPC_ENABLED        1
#define TROGUE_HOTRELOAD_ENABLED  1
#endif

#endif // TROGUE_CONFIG_H
