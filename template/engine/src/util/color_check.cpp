// util/color_check.cpp —— 颜色解析 detail 工具实现。
#include "util/color_check.hpp"

#include <cstdint>

namespace tg::detail {

namespace {

// 单字符十六进制 → 0..15；非法返回 -1。
int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

}  // namespace

std::optional<Color> parse_hex_color(std::string_view text) {
    if (text.size() != 7 && text.size() != 9) return std::nullopt;
    if (text.empty() || text[0] != '#') return std::nullopt;

    // 每两位十六进制一组，从 # 后第 1 位起
    Color c;
    const auto read_byte = [&](std::size_t offset, std::uint8_t& out) -> bool {
        const int hi = hex_digit(text[offset]);
        const int lo = hex_digit(text[offset + 1]);
        if (hi < 0 || lo < 0) return false;
        out = static_cast<std::uint8_t>((hi << 4) | lo);
        return true;
    };

    if (!read_byte(1, c.r) || !read_byte(3, c.g) || !read_byte(5, c.b)) {
        return std::nullopt;
    }
    if (text.size() == 9) {
        if (!read_byte(7, c.a)) return std::nullopt;
    }
    return c;
}

}  // namespace tg::detail