#pragma once
// util/path_check.hpp —— 资产相对路径 grammar（plan-5.2 §2.1）。
//
// schema 内资源路径（tileset path / sprite·animation texture）与外部 scene
// 路径统一过 is_safe_relative_path()，禁止各模块自写路径判定。
// 规则：/ 分隔、无空组件、无 . / ..、不以 / 开头、无 drive 前缀、无 //、
// 无 embedded NUL、UTF-8 合法；外部 scene 路径另加长度 < kPathMax（调用方校验）。

#include <string_view>

namespace tg::detail {

// 词法校验（不做符号链接解析、不做文件系统访问）。合法返回 true。
bool is_safe_relative_path(std::string_view path);

}  // namespace tg::detail