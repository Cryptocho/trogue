// collision.cpp —— 静态 solid tile 层的碰撞几何原语实现。
//
// SceneAsset 重载通过 scene 的公共查询 API 逐层物化为 SolidGridView，不触碰
// SceneImpl 内部（无需 friend）。逐层处理是因为每个 solid 层有独立的
// origin/tile 网格，不能共用一次步进再映射；视图重载直接消费调用方的掩码。
//
// 确定性：无随机、无累加漂移；同输入逐位同输出。所有 double→整数的转换都先做
// 饱和钳制（含「极大但有限」坐标），不产生转换 UB。

#include "trogue/collision.hpp"

#include <algorithm>  // std::max / std::min / std::swap
#include <cmath>      // std::floor / std::ceil / std::fabs / std::isfinite / std::nextafter
#include <cstddef>
#include <limits>
#include <vector>

#include "raylib.h"  // CheckCollisionRecs（engine 内部直接调 raylib C API）

namespace tg {

namespace {

constexpr int kNudgeMax = 64;  // 贴边浮点修正的迭代上限（防御性）

bool finite2(float a, float b) { return std::isfinite(a) && std::isfinite(b); }

// double → long long 的饱和转换：超出范围时钳到边界（不产生转换 UB）。
long long to_ll_sat(double v) {
    if (!(v > static_cast<double>(std::numeric_limits<long long>::min())))
        return std::numeric_limits<long long>::min();
    // LLONG_MAX 转 double 会舍入为 2^63，必须用严格的可表示上界比较。
    if (v >= 0x1p63)
        return std::numeric_limits<long long>::max();
    return static_cast<long long>(v);
}

// 半开区间 [min_pos, min_pos+size) 沿某轴覆盖的 tile 索引范围 [i0, i1)（未 clamp）。
void tile_index_span(double min_pos, double size, double origin, double tile,
                     long long& i0, long long& i1) {
    i0 = to_ll_sat(std::floor((min_pos - origin) / tile));
    i1 = to_ll_sat(std::ceil((min_pos + size - origin) / tile));
}

bool valid_view(const SolidGridView& view) {
    const int stride = view.stride == 0 ? view.width : view.stride;
    return view.mask != nullptr && view.width > 0 && view.height > 0 &&
           view.tile_w > 0 && view.tile_h > 0 && stride >= view.width;
}

int view_stride(const SolidGridView& view) {
    return view.stride == 0 ? view.width : view.stride;
}

bool cell_occupied(const SolidGridView& view, long long cx, long long cy) {
    if (!valid_view(view) || cx < 0 || cy < 0 || cx >= view.width ||
        cy >= view.height)
        return false;
    return view.mask[static_cast<std::size_t>(cy) * view_stride(view) +
                      static_cast<std::size_t>(cx)] != 0;
}

std::vector<SolidGridView> materialize_views(
    const SceneAsset& asset, std::vector<std::vector<std::uint8_t>>& masks) {
    std::vector<SolidGridView> views;
    masks.reserve(static_cast<std::size_t>(asset.layer_count()));
    views.reserve(static_cast<std::size_t>(asset.layer_count()));
    const int tile_w = asset.tile_width();
    const int tile_h = asset.tile_height();
    for (int li = 0; li < asset.layer_count(); ++li) {
        const LayerInfo& info = asset.layer(li);
        if (!info.solid || info.width <= 0 || info.height <= 0) continue;
        masks.emplace_back(static_cast<std::size_t>(info.width) * info.height);
        std::vector<int> tiles(masks.back().size());
        tile_grid(asset, li, 0, 0, info.width, info.height, tiles.data());
        for (std::size_t i = 0; i < tiles.size(); ++i)
            masks.back()[i] = tiles[i] == -1 ? 0 : 1;
        views.push_back(SolidGridView{info.width, info.height, tile_w, tile_h,
                                      info.origin_x, info.origin_y,
                                      masks.back().data(), info.width, li});
    }
    return views;
}

}  // namespace

bool aabb_overlap(Rect a, Rect b) noexcept {
    if (!(a.w > 0.0f) || !(a.h > 0.0f) || !(b.w > 0.0f) || !(b.h > 0.0f))
        return false;  // 零/负/NaN 尺寸
    if (!finite2(a.x, a.y) || !finite2(b.x, b.y)) return false;
    // 委托 raylib：严格 < 判据（边界相接不算相交，与半开一致）。
    return CheckCollisionRecs(Rectangle{a.x, a.y, a.w, a.h},
                              Rectangle{b.x, b.y, b.w, b.h});
}

TileQueryResult is_solid_at(const SolidGridView* views, int count, Vec2 world) {
    if (count < 0 || (count > 0 && views == nullptr) ||
        !finite2(world.x, world.y))
        return TileQueryResult::error;
    for (int i = 0; i < count; ++i) {
        const SolidGridView& view = views[i];
        if (!valid_view(view)) continue;
        const long long tx = to_ll_sat(std::floor(
            (static_cast<double>(world.x) - view.origin_x) / view.tile_w));
        const long long ty = to_ll_sat(std::floor(
            (static_cast<double>(world.y) - view.origin_y) / view.tile_h));
        if (cell_occupied(view, tx, ty)) return TileQueryResult::solid;
    }
    return TileQueryResult::clear;
}

TileQueryResult rect_hits_solid(const SolidGridView* views, int count,
                                Rect world_rect) {
    if (count < 0 || (count > 0 && views == nullptr) ||
        !finite2(world_rect.x, world_rect.y) ||
        !finite2(world_rect.w, world_rect.h) || !(world_rect.w > 0.0f) ||
        !(world_rect.h > 0.0f))
        return TileQueryResult::error;
    for (int i = 0; i < count; ++i) {
        const SolidGridView& view = views[i];
        if (!valid_view(view)) continue;
        long long x0, x1, y0, y1;
        tile_index_span(world_rect.x, world_rect.w, view.origin_x, view.tile_w,
                        x0, x1);
        tile_index_span(world_rect.y, world_rect.h, view.origin_y, view.tile_h,
                        y0, y1);
        x0 = std::max<long long>(x0, 0);
        y0 = std::max<long long>(y0, 0);
        x1 = std::min<long long>(x1, view.width);
        y1 = std::min<long long>(y1, view.height);
        for (long long y = y0; y < y1; ++y)
            for (long long x = x0; x < x1; ++x)
                if (cell_occupied(view, x, y)) return TileQueryResult::solid;
    }
    return TileQueryResult::clear;
}

// ════════════════════ 线段 vs solid 层 ════════════════════

namespace {

struct LayerHit {
    bool exact = false;
    int tx = -1, ty = -1;
    float t = 0.0f;
};

// 单层网格步进：依次检查线段落在该层内的每个单元（含起、终点所属单元）。
// 半开语义：单元 [tx,tx+1)×[ty,ty+1)；落在层远边界上的点不属于该层。
LayerHit segment_hit_layer(const SolidGridView& view, Vec2 a, Vec2 b) {
    LayerHit hit;
    if (!valid_view(view)) return hit;
    const long long dim_w = view.width, dim_h = view.height;
    const double ox = static_cast<double>(view.origin_x);
    const double oy = static_cast<double>(view.origin_y);
    const double tw = static_cast<double>(view.tile_w);
    const double th = static_cast<double>(view.tile_h);

    const double ax = static_cast<double>(a.x), ay = static_cast<double>(a.y);
    const double dx = static_cast<double>(b.x) - ax;
    const double dy = static_cast<double>(b.y) - ay;

    // 层内单元查询助手（半开 floor；层外返回 false）。
    auto occupied_cell = [&](long long cx, long long cy, double t) -> bool {
        if (cx < 0 || cy < 0 || cx >= dim_w || cy >= dim_h) return false;
        if (!cell_occupied(view, cx, cy)) return false;
        hit.exact = true;
        hit.tx = static_cast<int>(cx);
        hit.ty = static_cast<int>(cy);
        hit.t = static_cast<float>(t);
        return true;
    };
    auto cell_x = [&](double p) { return to_ll_sat(std::floor((p - ox) / tw)); };
    auto cell_y = [&](double p) { return to_ll_sat(std::floor((p - oy) / th)); };

    // 退化线段（点查询）：按半开 floor 取所在单元，层外（含远边界）= 不命中。
    if (std::fabs(dx) < 1e-12 && std::fabs(dy) < 1e-12) {
        occupied_cell(cell_x(ax), cell_y(ay), 0.0);
        return hit;
    }

    // 线段 vs 层 AABB 的 slab 裁剪 → t ∈ [t_enter, t_exit] ⊂ [0,1]。
    double t_enter = 0.0, t_exit = 1.0;
    auto slab = [&](double p0, double d, double lo, double hi) -> bool {
        if (std::fabs(d) < 1e-12) return p0 >= lo && p0 <= hi;
        double t1 = (lo - p0) / d;
        double t2 = (hi - p0) / d;
        if (t1 > t2) std::swap(t1, t2);
        if (t1 > t_enter) t_enter = t1;
        if (t2 < t_exit) t_exit = t2;
        return t_enter <= t_exit;
    };
    if (!slab(ax, dx, ox, ox + static_cast<double>(dim_w) * tw)) return hit;
    if (!slab(ay, dy, oy, oy + static_cast<double>(dim_h) * th)) return hit;
    if (t_exit < t_enter) return hit;

    const int step_x = (dx > 0) ? 1 : (dx < 0 ? -1 : 0);
    const int step_y = (dy > 0) ? 1 : (dy < 0 ? -1 : 0);

    // 入口单元：对入口点做 floor，必要时按运动方向定向钳制（仅在向内时）。
    // 若入口落在层外且运动方向背离层 → 仅触及边界，不命中（半开）。
    // 先判是否与层有正长度交集：t_enter == t_exit（含均为 1）时交集为止点，
    // 退化为点查（不做向内钳制），避免把远边界上的端点夹进边缘格造成假命中。
    const double sx = ax + dx * t_enter, sy = ay + dy * t_enter;
    if (!(t_enter < t_exit - 1e-12)) {
        occupied_cell(cell_x(sx), cell_y(sy), t_enter);  // 层外自动返回不命中
        return hit;
    }
    auto entry_cell = [](long long c, int step, long long dim) -> long long {
        if (c < 0) return step > 0 ? 0 : -1;
        if (c >= dim) return step < 0 ? dim - 1 : -1;
        return c;
    };
    long long cx = entry_cell(cell_x(sx), step_x, dim_w);
    long long cy = entry_cell(cell_y(sy), step_y, dim_h);
    if (cx < 0 || cy < 0) return hit;  // 仅边界接触，无层内行段

    occupied_cell(cx, cy, t_enter);
    if (hit.exact) return hit;

    const double inf = std::numeric_limits<double>::infinity();
    const double t_delta_x = (step_x == 0) ? inf : tw / std::fabs(dx);
    const double t_delta_y = (step_y == 0) ? inf : th / std::fabs(dy);
    auto next_x = [&]() -> double {
        return (step_x > 0)
                   ? (ox + (static_cast<double>(cx) + 1.0) * tw - ax) / dx
                   : (ax - (ox + static_cast<double>(cx) * tw)) / (-dx);
    };
    auto next_y = [&]() -> double {
        return (step_y > 0)
                   ? (oy + (static_cast<double>(cy) + 1.0) * th - ay) / dy
                   : (ay - (oy + static_cast<double>(cy) * th)) / (-dy);
    };
    double t_max_x = (step_x == 0) ? inf : next_x();
    double t_max_y = (step_y == 0) ? inf : next_y();

    const long long guard = 2 * (dim_w + dim_h) + 16;
    for (long long i = 0; i <= guard; ++i) {
        const double t_next = std::min(t_max_x, t_max_y);
        // 只步进「在本层内有正长度」的单元：t_next 严格小于 t_exit 才跨入。
        // 终点恰在单元边界时（t_next == t_exit）不跨入邻格——半开语义下该点
        // 属于前一个单元；终点单元由尾部的半开 floor 补检。
        if (!(t_next < t_exit - 1e-12)) break;
        double t_cur;
        // 精确对角线（tie）双轴步进；否则步进较小者。
        if (t_max_x == t_max_y) {
            t_cur = t_max_x;
            cx += step_x;
            cy += step_y;
            t_max_x += t_delta_x;
            t_max_y += t_delta_y;
        } else if (t_max_x < t_max_y) {
            t_cur = t_max_x;
            cx += step_x;
            t_max_x += t_delta_x;
        } else {
            t_cur = t_max_y;
            cy += step_y;
            t_max_y += t_delta_y;
        }
        if (cx < 0 || cy < 0 || cx >= dim_w || cy >= dim_h) break;
        occupied_cell(cx, cy, t_cur);
        if (hit.exact) return hit;
    }

    // 终点单元（半开 floor）：步进可能停在边界前的单元，显式补检终点所属单元。
    const double ex = ax + dx * t_exit, ey = ay + dy * t_exit;
    occupied_cell(cell_x(ex), cell_y(ey), t_exit);
    return hit;
}

}  // namespace

TileHit segment_hits_solid(const SolidGridView* views, int count, Vec2 a,
                           Vec2 b) {
    TileHit out;
    if (count < 0 || (count > 0 && views == nullptr) ||
        !finite2(a.x, a.y) || !finite2(b.x, b.y)) {
        out.result = TileQueryResult::error;
        return out;
    }
    bool found = false;
    for (int i = 0; i < count; ++i) {
        const SolidGridView& view = views[i];
        const LayerHit h = segment_hit_layer(view, a, b);
        if (h.exact && (!found || h.t < out.t)) {
            found = true;
            out.layer = view.layer_id;
            out.tx = h.tx;
            out.ty = h.ty;
            out.t = h.t;
        }
    }
    out.result = found ? TileQueryResult::solid : TileQueryResult::clear;
    const float tt = out.t;
    out.point = Vec2{a.x + (b.x - a.x) * tt, a.y + (b.y - a.y) * tt};
    return out;
}

TileHit segment_hits_solid(const SceneAsset& asset, Vec2 a, Vec2 b) {
    std::vector<std::vector<std::uint8_t>> masks;
    const std::vector<SolidGridView> views = materialize_views(asset, masks);
    return segment_hits_solid(views.data(), static_cast<int>(views.size()), a, b);
}

// ════════════════════ 滑移解算 ════════════════════

namespace {

struct AxisResult {
    double min_pos = 0.0;
    bool blocked = false;
};

// 单轴解算：把 min_pos 沿 delta 移到与 solid 首次接触前（逐层取最紧约束）。
// is_x=true 沿 X 轴（cross 为 Y 的 [cross_min, cross_min+cross_size)），否则反之。
AxisResult solve_axis(const SolidGridView* views, int count, bool is_x,
                      double min_pos, double size, double delta,
                      double cross_min, double cross_size) {
    AxisResult res;
    res.min_pos = min_pos;
    const double delta_abs = std::fabs(delta);
    if (delta_abs == 0.0) return res;

    double allowed = delta_abs;  // 允许的最大位移（正值），逐层取最小
    for (int li = 0; li < count; ++li) {
        const SolidGridView& view = views[li];
        if (!valid_view(view)) continue;
        const double tile = static_cast<double>(is_x ? view.tile_w : view.tile_h);
        const double other_tile = static_cast<double>(is_x ? view.tile_h : view.tile_w);
        const double ox = static_cast<double>(view.origin_x);
        const double oy = static_cast<double>(view.origin_y);
        const double origin = is_x ? ox : oy;
        const long long dim = is_x ? view.width : view.height;
        const double cross_origin = is_x ? oy : ox;
        const long long cross_dim = is_x ? view.height : view.width;
        if (dim < 1 || cross_dim < 1) continue;

        // cross 轴候选单元区间（clamp 到层范围；层外无数据不阻挡）。
        long long j0, j1;
        tile_index_span(cross_min, cross_size, cross_origin, other_tile, j0, j1);
        j0 = std::max<long long>(j0, 0);
        j1 = std::min<long long>(j1, cross_dim);
        if (j1 <= j0) continue;

        auto occupied_at = [&](long long c, long long j) -> bool {
            const int cx = is_x ? static_cast<int>(c) : static_cast<int>(j);
            const int cy = is_x ? static_cast<int>(j) : static_cast<int>(c);
            return cell_occupied(view, cx, cy);
        };

        if (delta > 0.0) {
            const double lead = min_pos + size;  // 前缘（右/下）
            long long c0 = to_ll_sat(std::floor((lead - origin) / tile));
            c0 = std::max<long long>(c0, 0);
            for (long long c = c0; c < dim; ++c) {
                const double cell_left = origin + static_cast<double>(c) * tile;
                if (cell_left > lead + delta) break;
                bool solid = false;
                for (long long j = j0; j < j1; ++j)
                    if (occupied_at(c, j)) { solid = true; break; }
                if (solid) {
                    double m = cell_left - lead;
                    if (m < 0.0) m = 0.0;  // 前缘已在阻挡格内 → 0 位移
                    if (m < allowed) allowed = m;
                    break;
                }
            }
        } else {
            const double lead = min_pos;  // 前缘（左/上）
            long long c0 = to_ll_sat(std::floor((lead - origin) / tile));
            c0 = std::min<long long>(c0, dim - 1);
            for (long long c = c0; c >= 0; --c) {
                const double cell_right =
                    origin + static_cast<double>(c + 1) * tile;
                if (cell_right < lead + delta) break;
                bool solid = false;
                for (long long j = j0; j < j1; ++j)
                    if (occupied_at(c, j)) { solid = true; break; }
                if (solid) {
                    double m = lead - cell_right;
                    if (m < 0.0) m = 0.0;
                    if (m < allowed) allowed = m;
                    break;
                }
            }
        }
    }

    res.blocked = allowed < delta_abs;
    res.min_pos = min_pos + (delta > 0.0 ? allowed : -allowed);
    return res;
}

// 把某轴位置沿解算方向逐 ulp 内移，直到 rect 不再与 solid 重叠（浮点贴边修正）。
double nudge_clear(const SolidGridView* views, int count, double pos,
                   bool positive_dir, double other_pos, double w, double h,
                   bool is_x) {
    const double inf = std::numeric_limits<double>::infinity();
    for (int i = 0; i < kNudgeMax; ++i) {
        const Rect r = is_x ? Rect{static_cast<float>(pos),
                                   static_cast<float>(other_pos),
                                   static_cast<float>(w), static_cast<float>(h)}
                            : Rect{static_cast<float>(other_pos),
                                   static_cast<float>(pos),
                                   static_cast<float>(w), static_cast<float>(h)};
        if (rect_hits_solid(views, count, r) != TileQueryResult::solid) break;
        pos = positive_dir ? std::nextafter(pos, -inf) : std::nextafter(pos, inf);
    }
    return pos;
}

}  // namespace

SweepResult sweep_move(const SolidGridView* views, int count, Rect box,
                       Vec2 delta) {
    SweepResult out;
    out.box = box;
    if (count < 0 || (count > 0 && views == nullptr) ||
        !finite2(box.x, box.y) || !finite2(box.w, box.h) ||
        !finite2(delta.x, delta.y) || !(box.w > 0.0f) || !(box.h > 0.0f)) {
        out.result = TileQueryResult::error;
        return out;
    }

    const AxisResult rx =
        solve_axis(views, count, true, static_cast<double>(box.x),
                   static_cast<double>(box.w), static_cast<double>(delta.x),
                   static_cast<double>(box.y), static_cast<double>(box.h));
    out.blocked_x = rx.blocked;
    box.x = static_cast<float>(rx.min_pos);
    if (out.blocked_x) {
        box.x = static_cast<float>(nudge_clear(
            views, count, static_cast<double>(box.x), delta.x > 0.0,
            static_cast<double>(box.y), box.w, box.h, true));
    }

    const AxisResult ry =
        solve_axis(views, count, false, static_cast<double>(box.y),
                   static_cast<double>(box.h), static_cast<double>(delta.y),
                   static_cast<double>(box.x), static_cast<double>(box.w));
    out.blocked_y = ry.blocked;
    box.y = static_cast<float>(ry.min_pos);
    if (out.blocked_y) {
        box.y = static_cast<float>(nudge_clear(
            views, count, static_cast<double>(box.y), delta.y > 0.0,
            static_cast<double>(box.x), box.w, box.h, false));
    }

    out.box = box;
    out.result = (rx.blocked || ry.blocked) ? TileQueryResult::solid
                                            : TileQueryResult::clear;
    return out;
}

SweepResult sweep_move(const SceneAsset& asset, Rect box, Vec2 delta) {
    std::vector<std::vector<std::uint8_t>> masks;
    const std::vector<SolidGridView> views = materialize_views(asset, masks);
    return sweep_move(views.data(), static_cast<int>(views.size()), box, delta);
}

}  // namespace tg
