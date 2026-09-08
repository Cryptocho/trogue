// util/path_check.cpp —— 资产相对路径 grammar 实现（plan-5.2 §2.1）。
#include "util/path_check.hpp"

#include <string>

#include "util/json_check.hpp"  // is_valid_utf8

namespace tg::detail {

bool is_safe_relative_path(std::string_view path) {
    if (path.empty()) return false;
    if (!is_valid_utf8(path)) return false;
    if (path.find('\0') != std::string_view::npos) return false;
    if (path.front() == '/') return false;      // 不以 / 开头
    if (path.back() == '/') return false;       // 无尾随空组件

    // Windows drive 前缀（词法拒绝：C:/、C:\、\\server）
    if (path.size() >= 2 && path[1] == ':') return false;
    if (path.size() >= 2 && path[0] == '\\' && path[1] == '\\') return false;

    std::size_t i = 0;
    while (i < path.size()) {
        // 组件边界：上一个 / 之后
        const std::size_t seg_start = (i == 0) ? 0 : i + 1;
        std::size_t seg_end = path.find('/', seg_start);
        if (seg_end == std::string_view::npos) seg_end = path.size();

        if (seg_start == seg_end) return false;  // // 空组件
        const std::string_view seg = path.substr(seg_start, seg_end - seg_start);
        if (seg == "." || seg == "..") return false;  // . / ..
        if (seg.find('\\') != std::string_view::npos) return false;  // 反斜杠

        if (seg_end == path.size()) break;
        i = seg_end;
    }
    return true;
}

}  // namespace tg::detail