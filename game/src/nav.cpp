// nav.cpp —— 导航原语实现。
//
// A* 细节对齐原版 coordinates.lua:81-187：
//   - 8 向邻接、启发 chebyshev、对角步代价 1.414、pop 上限 1000；
//   - 邻格展开顺序与原版一致（先 4 正交后 4 斜向）——同代价路径的 tie-break
//     以入堆序为准（(f, seq) 最小堆），确定性仅要求自洽，不要求与 Lua
//     MinHeap 的并列弹出序逐位一致。
#include "nav.hpp"

#include <cstdint>
#include <map>
#include <queue>
#include <vector>

#include "trogue/scene.hpp"

namespace game::nav {

int chebyshev(int x1, int y1, int x2, int y2) {
    const int dx = x2 - x1 >= 0 ? x2 - x1 : x1 - x2;
    const int dy = y2 - y1 >= 0 ? y2 - y1 : y1 - y2;
    return dx > dy ? dx : dy;
}

bool has_line_of_sight(int x1, int y1, int x2, int y2,
                       const std::function<bool(int, int)>& is_solid) {
    if (!is_solid) return true;
    int dx = x2 - x1;
    int dy = y2 - y1;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    const int sx = x1 < x2 ? 1 : -1;
    const int sy = y1 < y2 ? 1 : -1;
    int err = dx - dy;

    int x = x1, y = y1;
    while (x != x2 || y != y2) {
        const int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x += sx;
        }
        if (e2 < dx) {
            err += dx;
            y += sy;
        }
        // 终点格不判定（对齐原版：贴墙目标仍可见）
        if ((x != x2 || y != y2) && is_solid(x, y)) return false;
    }
    return true;
}

namespace {

constexpr float kDiagCost = 1.414f;  // 对齐原版 diagonalCost
constexpr int kMaxIterations = 1000; // 对齐原版 maxIterations

int node_key(int x, int y, int w) { return y * w + x; }

}  // namespace

std::optional<TilePos> astar_step(
    const GameState& gs, TilePos from, TilePos goal,
    const std::function<bool(int, int)>& blocked_by_actor) {
    if (from == goal) return std::nullopt;
    if (gs.map_w <= 0 || gs.map_h <= 0) return std::nullopt;

    // 展开顺序：4 正交 → 4 斜向（对齐原版 directions 数组）
    static constexpr TilePos kDirs[8] = {
        {0, -1}, {0, 1}, {-1, 0}, {1, 0},
        {-1, -1}, {1, -1}, {-1, 1}, {1, 1},
    };
    const auto passable = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= gs.map_w || y >= gs.map_h) return false;
        if (tile_solid_terrain(gs, x, y)) return false;
        if (blocked_by_actor && blocked_by_actor(x, y)) return false;
        return true;
    };

    // A* 主循环（gScore/cameFrom 与原版同构；小根堆按 (f, seq) 懒删除）
    struct OpenEntry {
        float f;
        std::uint64_t seq;
        TilePos pos;
    };
    const auto cmp = [](const OpenEntry& a, const OpenEntry& b) {
        if (a.f != b.f) return a.f > b.f;
        return a.seq > b.seq;
    };
    std::priority_queue<OpenEntry, std::vector<OpenEntry>, decltype(cmp)> open(
        cmp);
    std::map<int, float> g_score;
    std::map<int, bool> closed;
    std::map<int, TilePos> came_from;
    std::uint64_t seq = 0;

    const int start_key = node_key(from.x, from.y, gs.map_w);
    g_score[start_key] = 0.0f;
    open.push({static_cast<float>(chebyshev(from.x, from.y, goal.x, goal.y)),
               seq++, from});

    for (int iterations = 0; iterations < kMaxIterations && !open.empty();
         ++iterations) {
        const TilePos current = open.top().pos;
        open.pop();
        const int ck = node_key(current.x, current.y, gs.map_w);
        if (closed[ck]) continue;  // 懒删除：堆里可能残留旧条目
        closed[ck] = true;

        if (current == goal) {
            // 回溯路径，返回起点后的第一格（原版 path[2]）
            TilePos cur = current;
            while (true) {
                const auto parent =
                    came_from.find(node_key(cur.x, cur.y, gs.map_w));
                if (parent == came_from.end()) return cur;  // cur == from
                if (parent->second == from) return cur;
                cur = parent->second;
            }
        }

        for (const auto& d : kDirs) {
            const int nx = current.x + d.x;
            const int ny = current.y + d.y;
            // 先判界内/可通行再算 key（界外坐标不产生越界键，消除读者疑虑）
            if (!passable(nx, ny)) continue;
            const int nk = node_key(nx, ny, gs.map_w);
            if (closed[nk]) continue;
            // 斜切约束（地形 + 战斗实体，见 nav.hpp 注释）：两正交邻格都被阻挡才禁止
            if (d.x != 0 && d.y != 0) {
                const bool adj1 =
                    tile_solid_terrain(gs, current.x + d.x, current.y) ||
                    (blocked_by_actor &&
                     blocked_by_actor(current.x + d.x, current.y));
                const bool adj2 =
                    tile_solid_terrain(gs, current.x, current.y + d.y) ||
                    (blocked_by_actor &&
                     blocked_by_actor(current.x, current.y + d.y));
                if (adj1 && adj2) continue;
            }
            const float step = (d.x != 0 && d.y != 0) ? kDiagCost : 1.0f;
            const float tentative = g_score[ck] + step;
            auto g_it = g_score.find(nk);
            if (g_it == g_score.end() || tentative < g_it->second) {
                g_score[nk] = tentative;
                came_from[nk] = current;
                open.push({tentative + static_cast<float>(chebyshev(
                                          nx, ny, goal.x, goal.y)),
                           seq++, TilePos{nx, ny}});
            }
        }
    }
    return std::nullopt;  // open 空 / 迭代耗尽（同原版返回 nil）
}

}  // namespace game::nav
