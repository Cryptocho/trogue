#pragma once
// scene_test_seams.hpp —— 测试 seam 的私有声明（TROGUE_TEST_SEAMS）。
//
// 仅在 TROGUE_BUILD_CONSUMER_SMOKES AND BUILD_TESTING 时由 trogue_engine_test
// 编译进库（engine/CMakeLists.txt）；生产 trogue_engine 不含这些符号。
// 测试目标链接 trogue_engine_test 并 include 本目录（engine/src/）即可调用。
// 符号白名单：
//   asset_test_seed_id / asset_test_current_id / asset_test_reset_id /
//   render_test_stats / render_test_reset_stats
// 注意：render 三段计数现已公共化（tg::RenderStats / tg::render_stats）；
// 上述 render_test_* seam 保留为薄转发，仅为既有测试零改动。

#include <cstdint>

#include "trogue/render.hpp"  // 公共 RenderStats（seam 转发用，单一事实源）

namespace tg::detail {

// ── asset_id seam ──

// 把全局 asset_id 计数器 seed 到给定值（用于断言单调递增与耗尽行为）。
// 仅测试可调：生产路径行为不变（从 1 开始）。
void asset_test_seed_id(std::uint64_t v);

// 读取当前计数（断言单调用）。
std::uint64_t asset_test_current_id();

// 恢复初始状态（下次分配从 1 开始）。测试收尾用，避免影响其它用例。
void asset_test_reset_id();

// ── render 三段计数 seam ──
// RenderStats/数值语义已公共化（tg::RenderStats / tg::render_stats）；
// 此处仅保留 seam 符号转发，使既有测试零改动。

using RenderStats = ::tg::RenderStats;

// 进程内单调累计（不随调用清零）；重置用 render_test_reset_stats。
RenderStats render_test_stats();

void render_test_reset_stats();

}  // namespace tg::detail