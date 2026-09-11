#pragma once
// types.hpp —— 基础值类型与统一错误载体。
//
// 值类型 = 纯数据（public 字段、无 getter/setter），可复制/移动，不持有资源；
// 错误 = tl::expected<T, Error>，公共 API 以 tg::ErrorOr<T> 返回可预期失败，
// 不跨 API 抛裸异常（异常仅在调用约定被违反时用作程序错误信号，如 coro.hpp）。
//
// ErrorCode 是开放枚举：各模块按需要追加成员（追加不破坏现有代码）。

#include <cstdint>   // std::uint8_t / std::uint32_t / std::uint64_t
#include <string>
#include <utility>   // std::move
#include <tl/expected.hpp>

namespace tg {

// ════════════════════ 错误载体 ════════════════════

enum class ErrorCode {
    kInvalidArgument,    // 调用参数非法（越界/空指针/非法模式组合）
    kNotFound,           // 查询未命中（如 tile 查询无结果）
    kParseError,         // JSON/文本解析失败
    kSchemaViolation,    // 资产格式/限额/组合规则拒绝
    kIoError,            // 文件/套接字/inotify 等系统 I/O 失败
    kResourceExhausted,  // 资源耗尽（id 回绕、缓存上限等）
    kNotSupported,       // 平台/配置不支持（如非 Linux 的 watcher、Release 桩）
    kInternal,           // 兜底：不应发生的内部错误
};

// 诊断用枚举名（无匹配返回 "?"；仅用于日志/错误消息，不做控制流）
const char* error_code_name(ErrorCode code) noexcept;

// 基础错误类型：code + 人类可读消息（中文），各模块可用具名别名或子类型
struct Error {
    ErrorCode code = ErrorCode::kInternal;
    std::string message;

    Error() = default;
    Error(ErrorCode c, std::string msg) : code(c), message(std::move(msg)) {}

    // 诊断文本："[code] message"（日志友好）
    [[nodiscard]] std::string diagnostics() const {
        return std::string("[") + error_code_name(code) + "] " + message;
    }
};

// 公共别名：不使用 std::expected（C++23 才有，本工程定 C++20）
template <class T, class E>
using expected = tl::expected<T, E>;

template <class T>
using ErrorOr = tl::expected<T, Error>;

// ════════════════════ 基础值类型 ════════════════════

// 二维向量/坐标（像素空间，y 向下）
struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;

    bool operator==(const Vec2& o) const noexcept { return x == o.x && y == o.y; }
};

// 2D 轴对齐矩形（x/y = 左上角，w/h = 宽高，约定 w/h 非负）
struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    bool operator==(const Rect& o) const noexcept {
        return x == o.x && y == o.y && w == o.w && h == o.h;
    }
};

// RGBA 颜色（0..255；资产解析 `#rrggbb`/`#rrggbbaa` 时填入，缺省白色）
struct Color {
    std::uint8_t r = 255;
    std::uint8_t g = 255;
    std::uint8_t b = 255;
    std::uint8_t a = 255;

    bool operator==(const Color& o) const noexcept {
        return r == o.r && g == o.g && b == o.b && a == o.a;
    }
};

// ════════════════════ 渲染结果 ════════════════════

// 绘制原语返回值：供无窗口测试与 game 调试观测；正常 game 通常忽略。
enum class RenderResult {
    Drawn,             // 实际完成绘制
    Invalid,           // 参数/归属失败（asset 无效、sprite.asset_id 不匹配、
                       //   index/tile/region 非法、路径不合法）
    WindowUnavailable, // 窗口未就绪（不绘制、不记日志）
    TextureMissing,    // 贴图缺失/加载失败，该次跳过（每路径一次错误日志）
};

}  // namespace tg