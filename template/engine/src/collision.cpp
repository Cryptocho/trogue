// collision.cpp —— solid tile 层的碰撞几何原语实现（静态几何 + 动态盒输入）。
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

// ════════════════════ 运动学步进与混合扫掠（共享内核） ════════════════════

namespace {

// 单轴动态盒约束：沿 is_x 轴移动 (min_pos, size, delta) 时，与 cross 跨度
// 相交的 DynBox 对该轴允许的最大位移（无约束 → +inf；已重叠 → 0）。
double dyn_axis_gap(const DynBox& other, bool is_x, double min_pos,
                    double size, double delta, double cross_min,
                    double cross_size) {
    const Rect& ob = other.box;
    const double o_min = is_x ? static_cast<double>(ob.x)
                              : static_cast<double>(ob.y);
    const double o_size = is_x ? static_cast<double>(ob.w)
                               : static_cast<double>(ob.h);
    const double oc_min = is_x ? static_cast<double>(ob.y)
                               : static_cast<double>(ob.x);
    const double oc_size = is_x ? static_cast<double>(ob.h)
                                : static_cast<double>(ob.w);
    if (!(cross_min < oc_min + oc_size) || !(oc_min < cross_min + cross_size))
        return std::numeric_limits<double>::infinity();  // cross 不相交
    if (delta == 0.0) return std::numeric_limits<double>::infinity();
    if (delta > 0.0) {
        const double lead = min_pos + size;
        if (o_min <= lead) return 0.0;  // 已重叠 → 0 位移
        return o_min - lead;
    }
    const double o_trail = o_min + o_size;
    if (min_pos <= o_trail) return 0.0;
    return min_pos - o_trail;
}

// 单向平台的 Y 轴允许位移（规则 1/2：仅下落参与；ε 容差优先；抵达或越过均命中）。
double one_way_axis_gap(const Rect& plat, double min_pos, double size,
                        double delta, double cross_min, double cross_size) {
    if (delta <= 0.0) return std::numeric_limits<double>::infinity();
    const double plat_top = static_cast<double>(plat.y);
    const double old_bottom = min_pos + size;
    if (old_bottom > plat_top + static_cast<double>(kEpsilon))
        return std::numeric_limits<double>::infinity();  // 已在平台内 → 永不阻挡
    // cross（X）按当前（先 X 解算后的）跨度与平台半开相交
    if (!(cross_min < static_cast<double>(plat.x) +
                         static_cast<double>(plat.w)) ||
        !(static_cast<double>(plat.x) < cross_min + cross_size))
        return std::numeric_limits<double>::infinity();
    if (old_bottom + delta < plat_top)
        return std::numeric_limits<double>::infinity();  // 未抵达
    const double gap = plat_top - old_bottom;
    return gap > 0.0 ? gap : 0.0;
}

// 探地（静态层 + 单向平台）：探地矩形 [x, y+h, w, kKinematicProbe]。
bool kinematic_grounded(const SolidGridView* views, int count,
                        const Rect* one_way, int one_way_count, Rect box) {
    const Rect probe{box.x, box.y + box.h, box.w, kKinematicProbe};
    if (rect_hits_solid(views, count, probe) == TileQueryResult::solid)
        return true;
    for (int i = 0; i < one_way_count; ++i)
        if (aabb_overlap(probe, one_way[i])) return true;
    return false;
}

// 探墙（仅静态层；上下内缩防相邻地面误判）：+1 右 / -1 左 / 0 无（右优先）。
int kinematic_wall_dir(const SolidGridView* views, int count, Rect box) {
    const Rect right{box.x + box.w, box.y + kWallProbeInset, kKinematicProbe,
                     box.h - 2.0f * kWallProbeInset};
    if (rect_hits_solid(views, count, right) == TileQueryResult::solid)
        return 1;
    const Rect left{box.x - kKinematicProbe, box.y + kWallProbeInset,
                    kKinematicProbe, box.h - 2.0f * kWallProbeInset};
    if (rect_hits_solid(views, count, left) == TileQueryResult::solid)
        return -1;
    return 0;
}

// 混合内核：静态层 + 单向平台（仅 Y 下落）+ DynBox 的统一轴分离扫掠。
// hit_dyn_* 返回阻挡该轴的 DynBox 下标（-1 = 无动态阻挡；同为阻挡取小下标；
// 仅当动态约束确实截断位移时才报告——静态层不占下标）。
SweepResult sweep_core(const SolidGridView* views, int count,
                       const Rect* one_way, int one_way_count,
                       const DynBox* others, int other_count, Rect box,
                       Vec2 delta, int* hit_dyn_x, int* hit_dyn_y) {
    SweepResult out;
    out.box = box;
    if (hit_dyn_x) *hit_dyn_x = -1;
    if (hit_dyn_y) *hit_dyn_y = -1;
    if (count < 0 || (count > 0 && views == nullptr) ||
        !finite2(box.x, box.y) || !finite2(box.w, box.h) ||
        !finite2(delta.x, delta.y) || !(box.w > 0.0f) || !(box.h > 0.0f)) {
        out.result = TileQueryResult::error;
        return out;
    }
    for (int i = 0; i < other_count; ++i) {
        const DynBox& d = others[i];
        if (!finite2(d.box.x, d.box.y) || !finite2(d.box.w, d.box.h) ||
            !finite2(d.delta.x, d.delta.y) || !(d.box.w > 0.0f) ||
            !(d.box.h > 0.0f)) {
            out.result = TileQueryResult::error;
            return out;
        }
    }
    const bool has_dyn = others != nullptr && other_count > 0;
    const double inf = std::numeric_limits<double>::infinity();

    // ── X 轴：静态 + 动态盒（单向平台不参与横向）──
    double dyn_allow_x = inf;
    int dyn_first_x = -1;  // 阻挡者（gap < |delta|）中的最小下标
    if (has_dyn && delta.x != 0.0f) {
        const double delta_abs_x = std::fabs(static_cast<double>(delta.x));
        for (int i = 0; i < other_count; ++i) {
            const double g = dyn_axis_gap(others[i], true,
                                          static_cast<double>(box.x),
                                          static_cast<double>(box.w),
                                          static_cast<double>(delta.x),
                                          static_cast<double>(box.y),
                                          static_cast<double>(box.h));
            if (g < dyn_allow_x) dyn_allow_x = g;
            if (dyn_first_x < 0 && g < delta_abs_x) dyn_first_x = i;
        }
    }
    double dx = static_cast<double>(delta.x);
    bool dyn_blocked_x = std::fabs(dx) > dyn_allow_x;
    if (dyn_blocked_x) dx = dx > 0.0 ? dyn_allow_x : -dyn_allow_x;
    const AxisResult rx =
        solve_axis(views, count, true, static_cast<double>(box.x),
                   static_cast<double>(box.w), dx,
                   static_cast<double>(box.y), static_cast<double>(box.h));
    out.blocked_x = rx.blocked || dyn_blocked_x;
    box.x = static_cast<float>(rx.min_pos);
    if (rx.blocked) {
        box.x = static_cast<float>(nudge_clear(
            views, count, static_cast<double>(box.x), delta.x > 0.0,
            static_cast<double>(box.y), box.w, box.h, true));
    }
    if (hit_dyn_x) *hit_dyn_x = dyn_blocked_x ? dyn_first_x : -1;

    // ── Y 轴：动态盒 + 单向平台（仅下落）+ 静态层，取最紧 ──
    double dyn_allow_y = inf;
    double plat_allow = inf;
    int dyn_first_y = -1;
    if (has_dyn && delta.y != 0.0f) {
        const double delta_abs_y = std::fabs(static_cast<double>(delta.y));
        for (int i = 0; i < other_count; ++i) {
            const double g = dyn_axis_gap(others[i], false,
                                          static_cast<double>(box.y),
                                          static_cast<double>(box.h),
                                          static_cast<double>(delta.y),
                                          static_cast<double>(box.x),
                                          static_cast<double>(box.w));
            if (g < dyn_allow_y) dyn_allow_y = g;
            if (dyn_first_y < 0 && g < delta_abs_y) dyn_first_y = i;
        }
    }
    if (one_way != nullptr && delta.y > 0.0f) {
        for (int i = 0; i < one_way_count; ++i) {
            const double g = one_way_axis_gap(one_way[i],
                                              static_cast<double>(box.y),
                                              static_cast<double>(box.h),
                                              static_cast<double>(delta.y),
                                              static_cast<double>(box.x),
                                              static_cast<double>(box.w));
            if (g < plat_allow) plat_allow = g;
        }
    }
    double dy = static_cast<double>(delta.y);
    const double non_static_allow = std::min(dyn_allow_y, plat_allow);
    const bool dyn_blocked_y = std::fabs(dy) > dyn_allow_y;
    // 单向平台「抵达即命中」：gap == |delta| 也判阻挡（== 不隧道化），故用 >=。
    const bool plat_blocked = std::fabs(dy) >= plat_allow;
    if (std::fabs(dy) > non_static_allow) {
        dy = dy > 0.0 ? non_static_allow : -non_static_allow;
    }
    const AxisResult ry =
        solve_axis(views, count, false, static_cast<double>(box.y),
                   static_cast<double>(box.h), dy,
                   static_cast<double>(box.x), static_cast<double>(box.w));
    out.blocked_y = ry.blocked || dyn_blocked_y || plat_blocked;
    box.y = static_cast<float>(ry.min_pos);
    if (ry.blocked) {
        box.y = static_cast<float>(nudge_clear(
            views, count, static_cast<double>(box.y), delta.y > 0.0,
            static_cast<double>(box.x), box.w, box.h, false));
    }
    if (hit_dyn_y) *hit_dyn_y = dyn_blocked_y ? dyn_first_y : -1;

    out.box = box;
    out.result = (out.blocked_x || out.blocked_y) ? TileQueryResult::solid
                                                  : TileQueryResult::clear;
    return out;
}

}  // namespace

SweepResult sweep_move(const SolidGridView* views, int count,
                       const Rect* one_way, int one_way_count, Rect box,
                       Vec2 delta) {
    return sweep_core(views, count, one_way, one_way_count, nullptr, 0, box,
                      delta, nullptr, nullptr);
}

KinematicResult kinematic_step(const SolidGridView* views, int count,
                               Rect box, Vec2 delta) {
    return kinematic_step(views, count, nullptr, 0, box, delta);
}

KinematicResult kinematic_step(const SolidGridView* views, int count,
                               const Rect* one_way, int one_way_count,
                               Rect box, Vec2 delta) {
    KinematicResult out;
    out.sweep = sweep_core(views, count, one_way, one_way_count, nullptr, 0,
                           box, delta, nullptr, nullptr);
    if (out.sweep.result == TileQueryResult::error) return out;  // 全事件 false
    out.ev.grounded = kinematic_grounded(views, count, one_way, one_way_count,
                                         out.sweep.box);
    const bool was_ground =
        kinematic_grounded(views, count, one_way, one_way_count, box);
    out.ev.landed = out.ev.grounded && !was_ground;
    out.ev.hit_ceiling = out.sweep.blocked_y && delta.y < 0.0f;
    out.ev.hit_wall = out.sweep.blocked_x;
    out.ev.wall_dir = kinematic_wall_dir(views, count, out.sweep.box);
    return out;
}

// ════════════════════ 最小轴脱出 ════════════════════

namespace {

constexpr int kResolveMaxIter = 65536;  // 跳格扫描的防御上限（每跳至少越过 1 列）

// 沿某轴正/负方向推进，直到 box 不再与 solid 重叠；返回所需推距（像素，
// 正方向为正、负方向为负）。每跳越过当前相交的最远/最近 occupied 单元
//（逐层取最紧约束），确定性、必终止；不做全局最小化。
// 防御分支（迭代上限/无进展）仅理论上可达：有限层内每跳至少越过 1 列。
double push_axis_clear(const SolidGridView* views, int count, bool is_x,
                       double min_pos, double size, double cross_min,
                       double cross_size, bool positive_dir, double other_pos,
                       double w, double h) {
    const double inf = std::numeric_limits<double>::infinity();
    double p = 0.0;
    for (int iter = 0; iter < kResolveMaxIter; ++iter) {
        const Rect r = is_x ? Rect{static_cast<float>(min_pos + p),
                                   static_cast<float>(other_pos),
                                   static_cast<float>(w),
                                   static_cast<float>(h)}
                            : Rect{static_cast<float>(other_pos),
                                   static_cast<float>(min_pos + p),
                                   static_cast<float>(w),
                                   static_cast<float>(h)};
        if (rect_hits_solid(views, count, r) != TileQueryResult::solid) break;
        // 下一跳：越过当前相交单元。正方向需满足所有层的 ≥ 约束（取最大），
        // 负方向需满足全部 ≤ 约束（取最小）。
        double next = positive_dir ? -inf : inf;
        for (int li = 0; li < count; ++li) {
            const SolidGridView& view = views[li];
            if (!valid_view(view)) continue;
            const double tile =
                static_cast<double>(is_x ? view.tile_w : view.tile_h);
            const double other_tile =
                static_cast<double>(is_x ? view.tile_h : view.tile_w);
            const double origin =
                static_cast<double>(is_x ? view.origin_x : view.origin_y);
            const double cross_origin =
                static_cast<double>(is_x ? view.origin_y : view.origin_x);
            const long long dim = is_x ? view.width : view.height;
            const long long cross_dim = is_x ? view.height : view.width;
            if (dim < 1 || cross_dim < 1) continue;
            long long j0, j1;
            tile_index_span(cross_min, cross_size, cross_origin, other_tile,
                            j0, j1);
            j0 = std::max<long long>(j0, 0);
            j1 = std::min<long long>(j1, cross_dim);
            if (j1 <= j0) continue;
            const double cur_min = is_x ? r.x : r.y;
            long long c0 = to_ll_sat(std::floor((cur_min - origin) / tile));
            long long c1 = to_ll_sat(std::ceil((cur_min + size - origin) / tile));
            c0 = std::max<long long>(c0, 0);
            c1 = std::min<long long>(c1, dim);
            long long far = -1;   // 正方向：最远 occupied 列
            long long near = -1;  // 负方向：最近 occupied 列（最左）
            for (long long c = c0; c < c1; ++c) {
                bool occupied = false;
                for (long long j = j0; j < j1; ++j) {
                    const int cx = is_x ? static_cast<int>(c) : static_cast<int>(j);
                    const int cy = is_x ? static_cast<int>(j) : static_cast<int>(c);
                    if (cell_occupied(view, cx, cy)) {
                        occupied = true;
                        break;
                    }
                }
                if (!occupied) continue;
                if (far < 0 || c > far) far = c;
                if (near < 0 || c < near) near = c;
            }
            if (far < 0) continue;
            const double cand =
                positive_dir
                    ? (origin + static_cast<double>(far + 1) * tile) - min_pos
                    : (origin + static_cast<double>(near) * tile) - size - min_pos;
            next = positive_dir ? std::max(next, cand) : std::min(next, cand);
        }
        if (next == inf || next == -inf) break;              // 防御：无目标
        if (positive_dir ? next <= p : next >= p) break;     // 无进展（防御）
        p = next;
    }
    return p;
}

}  // namespace

OverlapResult resolve_overlap(const SolidGridView* views, int count,
                              Rect box) {
    OverlapResult out;
    out.box = box;
    if (count < 0 || (count > 0 && views == nullptr) ||
        !finite2(box.x, box.y) || !finite2(box.w, box.h) ||
        !(box.w > 0.0f) || !(box.h > 0.0f)) {
        out.resolved = false;  // 参数非法
        return out;
    }
    if (rect_hits_solid(views, count, box) != TileQueryResult::solid) {
        out.resolved = true;  // 前置不成立：本就不重叠
        return out;
    }
    // 四个方向各算一次清空推距，取位移小者；相等取 X（确定性）。
    const double bx = static_cast<double>(box.x);
    const double by = static_cast<double>(box.y);
    const double bw = static_cast<double>(box.w);
    const double bh = static_cast<double>(box.h);
    const double px_pos = push_axis_clear(views, count, true, bx, bw, by, bh,
                                          true, by, bw, bh);
    const double px_neg = push_axis_clear(views, count, true, bx, bw, by, bh,
                                          false, by, bw, bh);
    const double py_pos = push_axis_clear(views, count, false, by, bh, bx, bw,
                                          true, bx, bw, bh);
    const double py_neg = push_axis_clear(views, count, false, by, bh, bx, bw,
                                          false, bx, bw, bh);
    bool is_x = true;
    bool positive = true;
    double best = px_pos;
    if (std::fabs(px_neg) < std::fabs(best)) {
        best = px_neg;
        positive = false;
    }
    if (std::fabs(py_pos) < std::fabs(best)) {
        best = py_pos;
        is_x = false;
        positive = true;
    }
    if (std::fabs(py_neg) < std::fabs(best)) {
        best = py_neg;
        is_x = false;
        positive = false;
    }
    double x = bx;
    double y = by;
    if (is_x)
        x = bx + best;
    else
        y = by + best;
    // 浮点贴边修正：nudge_clear 向位移反方向内移，故传 !positive 使其沿推入
    // 方向再清一点（复用 sweep 的 ulp 修正）。
    if (is_x)
        x = nudge_clear(views, count, x, !positive, y, box.w, box.h, true);
    else
        y = nudge_clear(views, count, y, !positive, x, box.w, box.h, false);
    out.box = Rect{static_cast<float>(x), static_cast<float>(y), box.w, box.h};
    out.resolved =
        rect_hits_solid(views, count, out.box) != TileQueryResult::solid;
    return out;
}

OverlapResult resolve_overlap(const SceneAsset& asset, Rect box) {
    std::vector<std::vector<std::uint8_t>> masks;
    const std::vector<SolidGridView> views = materialize_views(asset, masks);
    return resolve_overlap(views.data(), static_cast<int>(views.size()), box);
}

KinematicResult kinematic_step(const SceneAsset& asset, const Rect* one_way,
                               int one_way_count, Rect box, Vec2 delta) {
    std::vector<std::vector<std::uint8_t>> masks;
    const std::vector<SolidGridView> views = materialize_views(asset, masks);
    return kinematic_step(views.data(), static_cast<int>(views.size()),
                          one_way, one_way_count, box, delta);
}

MixedSweepResult sweep_move_mixed(const SceneAsset& asset,
                                  const DynBox* others, int other_count,
                                  Rect box, Vec2 delta) {
    std::vector<std::vector<std::uint8_t>> masks;
    const std::vector<SolidGridView> views = materialize_views(asset, masks);
    return sweep_move_mixed(views.data(), static_cast<int>(views.size()),
                            others, other_count, box, delta);
}

// ════════════════════ 混合扫掠 ════════════════════

MixedSweepResult sweep_move_mixed(const SolidGridView* views, int count,
                                  const DynBox* others, int other_count,
                                  Rect box, Vec2 delta) {
    MixedSweepResult out;
    out.sweep = sweep_core(views, count, nullptr, 0, others, other_count, box,
                           delta, &out.hit_dyn_x, &out.hit_dyn_y);
    return out;
}

// ════════════════════ SolidGrid ════════════════════

ErrorOr<SolidGrid> SolidGrid::load(const SceneAsset& asset, int layer) {
    SolidGrid grid;
    auto r = grid.refresh(asset, layer);
    if (!r) return tl::make_unexpected(std::move(r).error());
    return grid;
}

expected<void, Error> SolidGrid::refresh(const SceneAsset& asset, int layer) {
    if (layer < 0 || layer >= asset.layer_count())
        return tl::make_unexpected(Error{
            ErrorCode::kInvalidArgument,
            "SolidGrid: 层索引越界: " + std::to_string(layer)});
    const LayerInfo info = asset.layer(layer);
    if (!info.solid)
        return tl::make_unexpected(Error{
            ErrorCode::kInvalidArgument,
            "SolidGrid: 层 " + std::to_string(layer) + " 非 solid 层"});
    mask_.assign(static_cast<std::size_t>(info.width) * info.height, 0);
    std::vector<int> tiles(mask_.size());
    tile_grid(asset, layer, 0, 0, info.width, info.height, tiles.data());
    for (std::size_t i = 0; i < tiles.size(); ++i)
        mask_[i] = tiles[i] == -1 ? 0 : 1;
    view_ = SolidGridView{info.width,
                          info.height,
                          asset.tile_width(),
                          asset.tile_height(),
                          info.origin_x,
                          info.origin_y,
                          mask_.data(),
                          info.width,
                          layer};
    return {};
}

expected<void, Error> SolidGrid::set_tile(SceneAsset& asset, int layer,
                                          int tx, int ty, int value) {
    if (layer != view_.layer_id)
        return tl::make_unexpected(Error{
            ErrorCode::kInvalidArgument,
            "SolidGrid: 层 " + std::to_string(layer) +
                " 与掩码层 " + std::to_string(view_.layer_id) + " 不一致"});
    auto r = asset.set_tile_at(layer, tx, ty, value);
    if (!r) return tl::make_unexpected(std::move(r).error());
    const int stride = view_.stride == 0 ? view_.width : view_.stride;
    mask_[static_cast<std::size_t>(ty) * stride + tx] = value == -1 ? 0 : 1;
    return {};
}

}  // namespace tg
