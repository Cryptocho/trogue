#pragma once
// util/json_check.hpp —— JSON 结构校验 detail 工具。
//
// 语义对齐 C 版定案、仅换载体（jansson → nlohmann），禁止因换库放宽：
//   - 所有 number 有限（finite），转 float 后仍有限，坐标/尺寸绝对值 ≤ FLT_MAX/4
//   - 深度 ≤ kJsonDepthMax；任一 object 键数 ≤ kPayloadKeysMax
//   - 文本内嵌 NUL（\u0000）一律拒绝
//   - 序列化字节按紧凑 dump() 长度计量（≤ 各 payload 上限，由调用方传入）
//
// 这些函数只做「判定 + 带路径的诊断消息」，不修改调用方数据、不抛异常。

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace tg::detail {

// 深度优先遍历：返回最大嵌套深度（root 计 1）。超限提前停（不保证精确值）。
int json_max_depth(const nlohmann::json& v, int depth, int limit);

// 任一 object 键数 > limit → 返回首个越界 object 的路径（诊断用）；合法返回 nullopt。
std::optional<std::string> json_any_object_too_many_keys(
    const nlohmann::json& v, int limit, std::string_view path = "");

// 任一 string 内嵌 NUL（\u0000 解析产物）→ 返回首个命中路径；合法返回 nullopt。
std::optional<std::string> json_any_embedded_nul(
    const nlohmann::json& v, std::string_view path = "");

// 文本是否为合法 UTF-8（nlohmann 解析保证大部分，但裸字节/代理对残留需复核）。
bool is_valid_utf8(std::string_view s);

// 数值规则：有限且转 float 后仍有限，绝对值 ≤ FLT_MAX/4。
bool is_finite_number_ok(double d);

// 把 JSON number 读成 int；非法（非 number / 非有限 / 不在 int 范围）返回 nullopt。
std::optional<int> json_as_int(const nlohmann::json& v);

// 把 JSON number 读成 float；非法（非 number / 非有限 / 超 FLT_MAX/4）返回 nullopt。
std::optional<float> json_as_finite_float(const nlohmann::json& v);

}  // namespace tg::detail