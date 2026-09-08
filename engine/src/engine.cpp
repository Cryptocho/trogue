// engine.cpp —— trogue_engine 库的基础符号承载。
//
// 承载两个全局基础符号：
//   tg::version_string()   —— 版本字符串（consumer 以它识别引擎版本）
//   tg::error_code_name()  —— types.hpp 的 Error::diagnostics() 依赖
//
// 各模块实现（scene_asset.cpp / render.cpp / animation.cpp / tween.cpp /
// hotreload.cpp / ipc.cpp + util/）见 engine/CMakeLists.txt ENGINE_SOURCES。
#include "trogue/config.hpp"
#include "trogue/types.hpp"

namespace tg {

const char* version_string() noexcept {
    // 与 kVersionMajor/Minor/Patch 保持一致（未来若由构建系统注入可改此处）
    return "0.1.0";
}

const char* error_code_name(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::kInvalidArgument:   return "invalid_argument";
        case ErrorCode::kNotFound:          return "not_found";
        case ErrorCode::kParseError:        return "parse_error";
        case ErrorCode::kSchemaViolation:   return "schema_violation";
        case ErrorCode::kIoError:           return "io_error";
        case ErrorCode::kResourceExhausted: return "resource_exhausted";
        case ErrorCode::kNotSupported:      return "not_supported";
        case ErrorCode::kInternal:          return "internal";
    }
    return "?";
}

}  // namespace tg