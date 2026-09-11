// util/json_check.cpp —— JSON 结构校验 detail 工具实现。
#include "util/json_check.hpp"

#include <cmath>    // std::isfinite / std::floor
#include <cstdint>  // std::uint32_t
#include <limits>   // std::numeric_limits
#include <vector>

namespace tg::detail {

int json_max_depth(const nlohmann::json& v, int depth, int limit) {
    if (depth > limit) return depth;  // 超限提前停
    int child_max = depth;
    if (v.is_object() || v.is_array()) {
        for (const auto& item : v.items()) {
            const int d = json_max_depth(item.value(), depth + 1, limit);
            if (d > child_max) child_max = d;
            if (child_max > limit) break;
        }
    }
    return child_max;
}

std::optional<std::string> json_any_object_too_many_keys(
    const nlohmann::json& v, int limit, std::string_view path) {
    if (v.is_object()) {
        if (static_cast<int>(v.size()) > limit) {
            return std::string(path.empty() ? "<root>" : std::string(path));
        }
        for (const auto& item : v.items()) {
            const std::string child_path =
                path.empty() ? item.key() : (std::string(path) + "." + item.key());
            if (auto bad = json_any_object_too_many_keys(item.value(), limit, child_path)) {
                return bad;
            }
        }
    } else if (v.is_array()) {
        for (std::size_t i = 0; i < v.size(); ++i) {
            const std::string child_path =
                path.empty() ? "[" + std::to_string(i) + "]"
                             : (std::string(path) + "[" + std::to_string(i) + "]");
            if (auto bad = json_any_object_too_many_keys(v[i], limit, child_path)) {
                return bad;
            }
        }
    }
    return std::nullopt;
}

std::optional<std::string> json_any_embedded_nul(
    const nlohmann::json& v, std::string_view path) {
    if (v.is_string()) {
        const std::string& s = v.get_ref<const std::string&>();
        if (s.find('\0') != std::string::npos) {
            return std::string(path.empty() ? "<root>" : std::string(path));
        }
    } else if (v.is_object() || v.is_array()) {
        if (v.is_object()) {
            for (const auto& item : v.items()) {
                const std::string child_path =
                    path.empty() ? item.key() : (std::string(path) + "." + item.key());
                if (auto bad = json_any_embedded_nul(item.value(), child_path)) {
                    return bad;
                }
            }
        } else {
            for (std::size_t i = 0; i < v.size(); ++i) {
                const std::string child_path =
                    path.empty() ? "[" + std::to_string(i) + "]"
                                 : (std::string(path) + "[" + std::to_string(i) + "]");
                if (auto bad = json_any_embedded_nul(v[i], child_path)) {
                    return bad;
                }
            }
        }
    }
    return std::nullopt;
}

bool is_valid_utf8(std::string_view s) {
    // 严格 UTF-8 校验：拒绝 overlong/代理对/超 U+10FFFF/孤立续字节。
    std::size_t i = 0;
    const auto bytes = reinterpret_cast<const unsigned char*>(s.data());
    const auto len = s.size();
    while (i < len) {
        const unsigned char b = bytes[i];
        if (b < 0x80) { ++i; continue; }
        int cont = 0;
        std::uint32_t cp = 0;
        if ((b & 0xE0) == 0xC0) { cont = 1; cp = b & 0x1F; }
        else if ((b & 0xF0) == 0xE0) { cont = 2; cp = b & 0x0F; }
        else if ((b & 0xF8) == 0xF0) { cont = 3; cp = b & 0x07; }
        else { return false; }
        if (i + static_cast<std::size_t>(cont) >= len) return false;
        for (int k = 1; k <= cont; ++k) {
            const unsigned char c = bytes[i + static_cast<std::size_t>(k)];
            if ((c & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (c & 0x3F);
        }
        // overlong / 代理区 / 超界
        if (cont == 1 && cp < 0x80) return false;
        if (cont == 2 && cp < 0x800) return false;
        if (cont == 3 && cp < 0x10000) return false;
        if (cp >= 0xD800 && cp <= 0xDFFF) return false;
        if (cp > 0x10FFFF) return false;
        i += static_cast<std::size_t>(cont) + 1;
    }
    return true;
}

bool is_finite_number_ok(double d) {
    if (!std::isfinite(d)) return false;
    const float f = static_cast<float>(d);
    if (!std::isfinite(f)) return false;
    constexpr float kMaxAbs = std::numeric_limits<float>::max() / 4.0f;
    return std::fabs(static_cast<double>(f)) <= static_cast<double>(kMaxAbs);
}

std::optional<int> json_as_int(const nlohmann::json& v) {
    if (!v.is_number()) return std::nullopt;
    const double d = v.get<double>();
    if (!is_finite_number_ok(d)) return std::nullopt;
    // 语义收紧点：origin 等要求「int 可表示整数值」，非整数（如 1.5）拒绝，
    // 避免把小数当整数静默截断（C 版定案：origin 两项须有限且 int 可表示）。
    if (d != std::floor(d)) return std::nullopt;
    const double lo = static_cast<double>(std::numeric_limits<int>::min());
    const double hi = static_cast<double>(std::numeric_limits<int>::max());
    if (d < lo || d > hi) return std::nullopt;
    return static_cast<int>(d);
}

std::optional<float> json_as_finite_float(const nlohmann::json& v) {
    if (!v.is_number()) return std::nullopt;
    const double d = v.get<double>();
    if (!is_finite_number_ok(d)) return std::nullopt;
    return static_cast<float>(d);
}

}  // namespace tg::detail