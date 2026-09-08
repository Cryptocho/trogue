#pragma once
// util/color_check.hpp —— 颜色解析 detail 工具（plan-5.2 §2.4）。
//
// 语义对齐 C 版 tg_parse_hex_color：仅接受 #rrggbb / #rrggbbaa（# 必须存在）；
// 非法 → 拒绝（不静默回退、不清零默认色）。

#include <optional>
#include <string_view>

#include "trogue/types.hpp"  // tg::Color

namespace tg::detail {

// 解析 `#rrggbb` 或 `#rrggbbaa`。非法返回 nullopt。
std::optional<Color> parse_hex_color(std::string_view text);

}  // namespace tg::detail