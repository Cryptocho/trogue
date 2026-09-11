#pragma once
// collision.hpp —— 静态 solid tile 层的低层碰撞几何原语。
//
// 边界（复刻 scene.hpp/render.hpp 句式）：引擎只回答「一个矩形/线段与**静态
// 地形几何**的关系」，不回答「谁和谁碰、碰后发生什么」。动态实体之间的碰撞
// 规则、刚体物理/solver、单向平台/斜坡、分层碰撞矩阵、寻路（图搜索）均归 game。
//
// 与 tile 查询同一套语义：只读 solid==true 的层；tiles!= -1 即阻挡；层矩形
// 外 = 无数据 = 不阻挡；像素→tile = floor((world - layer_origin)/tile_size)（每
// 层有独立 origin，可负）；矩形与 tile 相交用半开 [x,x+w)×[y,y+h)。
//
// 全部为自由函数 / 纯值类型，无 OOP 层级、不暴露 raylib 类型、无随机（确定性）。

#include "trogue/scene.hpp"  // SceneAsset
#include "trogue/types.hpp"  // Vec2 / Rect / TileQueryResult

namespace tg {

// ════════════════════ 几何谓词 ════════════════════

// 轴对齐矩形相交：半开 [x,x+w)×[y,y+h)；仅边界相接（如 a.x+a.w == b.x）→ false。
// 任一 w/h <= 0（含 NaN）→ false。
bool aabb_overlap(Rect a, Rect b) noexcept;

// ════════════════════ 线段 vs solid tile 层（视线 / 射线） ════════════════════

// 线段命中描述；result==solid 表命中，clear 表未命中，error 表参数非法
//（此时 layer/tx/ty/point/t 无意义）。
struct TileHit {
    TileQueryResult result = TileQueryResult::clear;
    int layer = -1;          // 命中的 solid 层索引（hit 时有效）
    int tx = -1, ty = -1;    // 命中单元在该层内的 tile 坐标（hit 时有效）
    Vec2 point{0.0f, 0.0f};  // 命中点（世界像素）
    float t = 0.0f;          // 沿 a→b 的参数（0=a，1=b）
};

// 判定线段 [a,b] 与 solid 几何是否相交，返回沿 a→b 的**首个**命中。
//
// 语义：
// - 闭区间参数：起/终点所在单元都参与判定（起点在 solid → t==0）。
// - 单元按半开 [tx,tx+1)×[ty,ty+1) 定义（与 rect_hits_solid 一致）；线段恰好
//   穿过单元角点时按行进方向步进对角单元，仅被点接触的相邻单元不枚举。
// - 遍历用 Amanatides–Woo 网格步进：精确对角线上双轴同时步进（不会漏掉线段
//   以正长度穿过的单元）。逐层步进（各层 origin 可不同），先对线段与层 AABB
//   做裁剪，无交集则跳过该层。
// - 首个命中按 t 递增；同 t 取层索引小者，再同取 (ty,tx) 小者（确定性）。
// - 端点豁免（如「视线终点不判定」）是调用方策略：需要时自行回缩 b 或先判端点。
// - a/b 任一坐标非有限 → error。
TileHit segment_hits_solid(const SceneAsset& asset, Vec2 a, Vec2 b);

// ════════════════════ 轴对齐矩形对 solid 层的滑移解算（swept） ════════════════════

// 滑移解算结果；result==error 时 box 为入参原值。
struct SweepResult {
    Rect box{0, 0, 0, 0};      // 解算后的位置
    bool blocked_x = false;    // X 轴是否被阻挡（位移被截断）
    bool blocked_y = false;
    TileQueryResult result = TileQueryResult::clear;  // error / clear / solid
};

// 把 box 沿 delta 移动，逐轴解算到与 solid **首次接触前**的位置（可沿墙滑动）。
//
// 语义：
// - 轴分离、先 X 后 Y：X 轴按**原始 y 跨度**解算；Y 轴以**解算后的 x** 解算。
//   任一轴被阻挡则该轴停住、另一轴照常位移（自然沿墙滑动）。
// - swept（连续）：结果是「前缘恰好停在首个 solid 单元边界（不重叠）」的位置，
//   大 delta 不穿透、小 delta 与单步试探一致。
// - **核心不变式**：解算后 box 与 solid **不重叠**（`rect_hits_solid(resolved)` 为
//   clear）。停位使被阻挡轴的前缘与 solid 边界相距不超过约 1e-3 px（浮点精度内
//   的「贴住但不重叠」）。「再朝该方向走 1px 即再被阻挡」仅在「该轴为最后解算轴
//   或另一轴本次无位移」时成立（轴分离语义下斜向不保证，非缺陷）。
// - 逐层取最紧约束（层矩形外不阻挡）。
// - **调用方不变式**：入参 box 应与 solid 不重叠。起始即重叠时该方向解算为 0
//   位移（不保证脱出），但不会崩溃或越界。
// - 参数非法（box/delta 含非有限值，或 box.w<=0 / box.h<=0）→ result=error，
//   返回 box=入参原值。
SweepResult sweep_move(const SceneAsset& asset, Rect box, Vec2 delta);

}  // namespace tg
