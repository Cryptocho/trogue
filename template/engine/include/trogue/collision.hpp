#pragma once
// collision.hpp —— solid tile 层的低层碰撞几何原语（静态地形 + 动态盒输入）。
//
// 边界（复刻 scene.hpp/render.hpp 句式）：引擎只回答「一个矩形/线段与**几何**的
// 关系」，不回答「谁和谁碰、碰后发生什么」。刚体物理/solver、动态-动态互推
// （链式承载的传递解算）、斜坡、分层碰撞矩阵、寻路（图搜索）均归 game。
// 单向平台是本文件的例外扩展：仅 tile 矩形形状的机制性穿越规则（见
// kinematic_step/sweep_move 的 one_way 重载），平台布局与下跳摘除归 game。
//
// 与 tile 查询同一套语义：只读 solid==true 的层；tiles!= -1 即阻挡；层矩形
// 外 = 无数据 = 不阻挡；像素→tile = floor((world - layer_origin)/tile_size)（每
// 层有独立 origin，可负）；矩形与 tile 相交用半开 [x,x+w)×[y,y+h)。
//
// 全部为自由函数 / 纯值类型（SolidGrid 为单个 RAII 掩码类），无 OOP 层级、
// 不暴露 raylib 类型、无随机（确定性）。

#include <cstdint>
#include <functional>  // std::function（SolidGrid::create 的阻挡谓词参数）
#include <string>
#include <vector>

#include "trogue/scene.hpp"  // SceneAsset
#include "trogue/types.hpp"  // Vec2 / Rect / TileQueryResult / ErrorOr / expected

namespace tg {

// ── 判据常量（钉死契约，调用方可依赖其数值） ──
inline constexpr float kEpsilon = 1e-3f;        // 贴边/穿越判据的统一容差
inline constexpr float kKinematicProbe = 1.0f;  // 探地/探墙探针厚度（px）
inline constexpr float kWallProbeInset = 2.0f;  // 探墙上下内缩（防相邻地面误判成墙）

// 非拥有的单层 solid 网格视图。mask 按行主序存储，非零元素表示阻挡。
// mask 的生命周期由调用方保证；视图本身不复制或释放它。
struct SolidGridView {
    int width = 0;
    int height = 0;
    int tile_w = 0;
    int tile_h = 0;
    int origin_x = 0;
    int origin_y = 0;
    const std::uint8_t* mask = nullptr;
    int stride = 0;  // 0 表示 width
    int layer_id = -1;
};

// ════════════════════ 几何谓词 ════════════════════

// 三态语义（本文件全部几何查询一致，调用方必须显式区分 error）：
//   error   = 参数非法（非有限坐标/非正尺寸/视图指针与计数不匹配）
//   clear   = 无阻挡
//   solid   = 命中阻挡
// **error ≠ 可通行**——只判 `== solid` 会把参数错误静默当成「没撞到」，调用方的
// 输入 bug 就变成「实体穿墙」这类难查的现象；需要「非阻挡」时应判 `!= solid`
// 并让 error 走错误分支。

// 轴对齐矩形相交：半开 [x,x+w)×[y,y+h)；仅边界相接（如 a.x+a.w == b.x）→ false。
// 任一 w/h <= 0（含 NaN）→ false。
bool aabb_overlap(Rect a, Rect b) noexcept;

// 以下查询接受调用方自持的、按 solid 层展开的视图数组。视图数组为空或
// 退化视图不产生阻挡；TileHit.layer 使用 SolidGridView::layer_id。
TileQueryResult is_solid_at(const SolidGridView* views, int count, Vec2 world);
TileQueryResult rect_hits_solid(const SolidGridView* views, int count,
                                Rect world_rect);

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
TileHit segment_hits_solid(const SolidGridView* views, int count, Vec2 a,
                           Vec2 b);

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
SweepResult sweep_move(const SolidGridView* views, int count, Rect box,
                       Vec2 delta);

// ════════════════════ 运动学步进：位移 + 探针 + 事件派生 ════════════════════

// 一步运动学事件（探针与事件派生的全部输出；手感/状态归 game）。
struct KinematicEvents {
    bool grounded = false;      // 步末贴地（探地探针命中）
    bool landed = false;        // 本步发生落地：grounded ∧ 原位同款探地未命中
    bool hit_ceiling = false;   // 本步向上被挡（blocked_y 且 delta.y < 0）
    bool hit_wall = false;      // 本步横向被挡（blocked_x）
    int wall_dir = 0;           // 步末贴墙方向：-1 左 / +1 右 / 0 无
};

// 运动学一步的结果：位移解算全量结果 + 事件派生。
struct KinematicResult {
    SweepResult sweep;   // 位移解算全量契约（box/blocked/result）同 sweep_move
    KinematicEvents ev;
};

// 动态实体的一步位移解算：sweep_move 之上收编「探针约定 + 事件派生」脚手架。
//
// 语义：
// - 位移：内部按 sweep_move 解算（轴分离、先 X 后 Y、不重叠不变式全部沿用）。
// - 探地（grounded）：对解算后 box 取 [x, y+h, w, kKinematicProbe] 探地矩形，
//   命中 solid **或 one_way 平台** → grounded。
// - landed 单条差分定义：landed = grounded(解算后) ∧ 对入参原 box 同款探地
//   未命中。一步内不产生二义：从空中跨到贴地仅当「原位探空 + 落位探实」。
// - 探墙：步末 box 左右各 kKinematicProbe 厚、上下内缩 kWallProbeInset
//   （防把相邻地面误判成墙）；右命中 wall_dir=+1，左命中 -1（右优先，确定性）。
//   探墙只看静态 solid 层，不含 one_way 平台（侧面永远穿过）。
// - 事件与速度积分、土狼/缓冲/可变跳高等手感完全无关；delta 由 game 算。
// - 参数非法（同 sweep_move 判据）→ sweep.result=error、sweep.box=入参原值、
//   全事件 false、wall_dir=0。
KinematicResult kinematic_step(const SolidGridView* views, int count,
                               Rect box, Vec2 delta);

// one_way 平台规则（钉死；平台是 game 自持矩形数组，布局/下跳摘除归 game）：
// 1. 仅 delta.y > 0（下落）参与阻挡；向上与横向永远穿过。
// 2. 单一阻挡谓词：入参 box 底边 ≤ plat.top + kEpsilon 且解算新底边（未加本
//    约束时）≥ plat.top（抵达或越过均命中，含 ==，不隧道化）→ 阻挡；底边
//    > plat.top + kEpsilon 即视为已在平台内部，无论位移多大都不阻挡
//    （**不可逆**：平台上移不会重新捕获，效果等同下跳穿越后不可逆）。
// 3. X 相交按先 X 解算后的 x 跨度与平台半开相交判定（与轴分离次序一致）。
// 4. 命中：box.y = plat.top - box.h，blocked_y = true（landed 差分照常成立）。
// 5. 探地把 one_way 一并纳入（站上平台即贴地）；探墙不含 one_way。
// 6. 与静态 solid 约束在同一 Y 轴解算中取最紧者（谁把停位提得更高谁生效）。
// one_way 数组为空时与无 one_way 重载逐位一致。
SweepResult sweep_move(const SolidGridView* views, int count,
                       const Rect* one_way, int one_way_count, Rect box,
                       Vec2 delta);
KinematicResult kinematic_step(const SolidGridView* views, int count,
                               const Rect* one_way, int one_way_count,
                               Rect box, Vec2 delta);
// 便捷重载：逐层物化 asset 的 solid 层后调用视图版（语义一致）。
KinematicResult kinematic_step(const SceneAsset& asset, const Rect* one_way,
                               int one_way_count, Rect box, Vec2 delta);

// ════════════════════ 探针查询：探地 ════════════════════
//
// 与 kinematic_step 内部的探地**同一语义**（同一份实现）：对 box 底边下方
// kKinematicProbe 厚、宽度 = box.w 的矩形做查询。
//
// 用途：`KinematicEvents::grounded` 只在**跑过一步之后**才有意义；spawn/reset/
// 传送之后要对任意 box 立即提问「脚下是否有支撑」，用本函数。注意
// `is_solid_at` 是**点查询**，不能替代探地矩形语义：底边中点落在 tile 边界或
// 紧邻空格的列上时，点查询为 clear 而探地矩形已命中。
//
// 返回三态（参数非法不伪装成「悬空」）：
//   solid = 脚下有支撑（命中 solid 层 **或** one_way 平台——探地语境的语义扩展，
//           因为两者都能站）
//   clear = 悬空
//   error = 参数非法（box 非有限/非正尺寸，或 views/one_way 指针与计数不匹配）；
//           参数非法先判，一律 error（即使 one_way 同时命中）
TileQueryResult probe_grounded(const SolidGridView* views, int count, Rect box);

TileQueryResult probe_grounded(const SolidGridView* views, int count,
                               const Rect* one_way, int one_way_count, Rect box);

// 便捷重载：逐层物化 asset 的 solid 层后查询（O(层数×格数)）。
// **逐帧查询请持 SolidGrid / 视图走上面的重载**；本重载适合一次性判断与测试。
TileQueryResult probe_grounded(const SceneAsset& asset, Rect box);

// ════════════════════ 最小轴脱出（depenetration） ════════════════════

struct OverlapResult {
    Rect box{0, 0, 0, 0};
    bool resolved = false;  // false = 参数非法；true = 后置条件成立（见下）
};

// 把与 solid 重叠的 box 沿最小轴一次推出到不重叠（「快速动态盒撞进静态几何」）。
//
// 语义：
// - 前置：box 与 solid 重叠（不重叠时直接返回 {box, true}）。
// - 对 X、Y 两轴分别计算把重叠清空所需的最小单轴位移（向两侧取更近者：沿该轴
//   把 box 移出所有相交 solid 单元；多层时对同方向取各层所需的最大推距——逐层
//   取最紧），取两轴中位移小者执行；相等取 X（确定性）。
// - 后置条件：resolved==true ⇒ rect_hits_solid(结果 box) 为 clear，且位移方向
//   唯一、值为上述最小可行推距。
// - **不保证项**：推距无上限（必要时可能横穿整片实心区才脱离层矩形），不做
//   迭代搜索最小化、不做旋转、不处理跨层折中；除参数非法外不设失败分支
//  ——有限 solid 层下单轴推进总能离开层矩形（= 无数据 = 不阻挡）。
// - 参数非法（同 rect_hits_solid 判据）→ {入参原值, false}。
OverlapResult resolve_overlap(const SolidGridView* views, int count, Rect box);
OverlapResult resolve_overlap(const SceneAsset& asset, Rect box);

// ════════════════════ 混合扫掠：动态实心盒 + 承载索引 ════════════════════

// 动态实心盒：位置 + 本步位移（速度积分归 game；各盒在本步内视为瞬时障碍）。
struct DynBox {
    Rect box{0, 0, 0, 0};
    Vec2 delta{0.0f, 0.0f};
};

struct MixedSweepResult {
    SweepResult sweep;       // 静态+动态合并的解算结果（契约同 sweep_move）
    int hit_dyn_x = -1;      // 阻挡 X 轴的 DynBox 下标（-1 = 无；同为阻挡取小下标）
    int hit_dyn_y = -1;
};

// 注意：hit_dyn_* 报告的是「该轴上截断位移的动态约束里下标最小者」——
// 当更紧的静态层/单向平台约束实际决定停位时，仍报告满足截断条件的最小
// 动态下标（它描述「本步有动态阻挡关系」，不描述「停位由谁决定」）。

// sweep_move 的混合扩展：阻挡集 = 静态 solid 层 ∪ 各 DynBox AABB。
//
// 语义：
// - 解算与 sweep_move 完全同构（轴分离、先 X 后 Y、贴边、不重叠不变式）；
//   其余 DynBox 在本步内视为瞬时 AABB 障碍（不参与彼此的本步位移——链式承载
//   由调用方按序多轮调用编排，引擎不做 solver）。
// - **承载语义收窄为索引报告**：hit_dyn_x/hit_dyn_y 只报告「谁挡的」；骑乘/
//   携带的位移增量由 game 用该下标自算——引擎不保存上一步关系、不推平台。
//   静态层与某 DynBox 同时阻挡同轴时，索引仍报告该 DynBox 下标（静态层不占
//   下标）；无动态阻挡者 → -1。
// - 起始即与某 DynBox 重叠：该方向解算为 0 位移（同 sweep_move 对静态重叠的
//   约定，不保证脱出；resolve_overlap 不扩 DynBox 版本）。
// - `sweep_move` 本体签名与语义不变；混合能力只走本函数。
// - 参数非法（box/delta 任一非有限或尺寸非正，含任一 DynBox）→ result=error。
MixedSweepResult sweep_move_mixed(const SolidGridView* views, int count,
                                  const DynBox* others, int other_count,
                                  Rect box, Vec2 delta);
// 便捷重载：逐层物化 asset 的 solid 层后调用视图版（语义一致）。
MixedSweepResult sweep_move_mixed(const SceneAsset& asset,
                                  const DynBox* others, int other_count,
                                  Rect box, Vec2 delta);

// ════════════════════ SolidGrid：与 asset solid 层同步的碰撞掩码 ════════════════════

// RAII 自持掩码，是「渲染 tile（SceneAsset）+ 碰撞真值（掩码）」双真相的同步
// 载体：set_tile 把两份写入合并为一次调用，任何失败路径下两份真值都不分叉。
// 掩码语义与既有物化一致：tiles != -1 → 1（掩码只记阻挡与否，不记 tile 值）。
//
// 两种来源：
//   load(asset, layer) —— 与某个资产的 solid 层绑定，支持 refresh/set_tile 的同步写，
//                         view().layer_id = 该层索引（可被 segment_hits_solid 原样报出）；
//   create(...)        —— 由谓词构造的独立掩码（无关联资产），view().layer_id == -1。
// create 出来的实例**不适用** refresh/set_tile：它们要么要求层号等于 layer_id
// （set_tile 会因 -1 永不匹配而报错），要么会整体重建掩码（refresh 会静默丢弃谓词
// 结果并改写 layer_id）——「层 1 是 solid 这类约定」不适用时，请改用本形态。
class SolidGrid {
public:
    SolidGrid() = default;
    SolidGrid(const SolidGrid&) = delete;             // 拷贝会使 view_.mask 悬垂，禁用
    SolidGrid& operator=(const SolidGrid&) = delete;
    SolidGrid(SolidGrid&& other) noexcept
        : mask_(std::move(other.mask_)), view_(other.view_) {
        view_.mask = mask_.data();
    }
    SolidGrid& operator=(SolidGrid&& other) noexcept {
        if (this != &other) {
            mask_ = std::move(other.mask_);
            view_ = other.view_;
            view_.mask = mask_.data();
        }
        return *this;
    }

    // 从 asset 第 layer 层物化掩码；层越界或非 solid 层 → error（不创建）。
    // 「哪几层是 solid」请用 SceneAsset::solid_layer_indices() 查，不要硬编码层号。
    static ErrorOr<SolidGrid> load(const SceneAsset& asset, int layer);

    // 由谓词构造掩码（非资产来源：程序生成房间、测试夹具等）。
    // blocked(tx, ty) 返回该格是否阻挡；tx/ty 为层局部 tile 坐标（0 <= tx < width）。
    // 校验：width/height/tile_w/tile_h <= 0 或**任一超过 kLayerDimMax** → kInvalidArgument；
    //       blocked 为空（未设置）→ kInvalidArgument（不把 bad_function_call 抛出去）。
    // origin_x/origin_y 不校验（可为负，与场景层 origin 语义一致）。
    // 产物：view() 的 width/height/tile_w/tile_h 同入参、stride == width、layer_id == -1。
    static ErrorOr<SolidGrid> create(int width, int height, int tile_w, int tile_h,
                                     const std::function<bool(int tx, int ty)>& blocked,
                                     int origin_x = 0, int origin_y = 0);

    // 整层重物化：O(w·h)（大地图高频改格用 set_tile，勿整层 refresh）。
    // 层不存在或非 solid 层 → error，旧掩码保留。
    expected<void, Error> refresh(const SceneAsset& asset, int layer);

    // 单格同步：委托 asset.set_tile_at 写资产内存态，成功后同步本掩码该格。
    // asset 写失败（值域/坐标错）→ error，掩码零修改。
    // 注意：asset 为非 const 引用（set_tile_at 是受限可变窗口）。
    expected<void, Error> set_tile(SceneAsset& asset, int layer, int tx, int ty,
                                   int value);

    // 掩码视图；load/refresh 出来的实例 layer_id = 该层索引；create 出来的实例 layer_id = -1
    // （非资产来源，会被 segment_hits_solid 原样报进 TileHit.layer，调用方不能据此区分
    //  「层索引」与「占位」）。移动赋值后源对象的 view() 失效；本对象的 view() 始终指向自身缓冲区。
    const SolidGridView& view() const { return view_; }

private:
    std::vector<std::uint8_t> mask_;
    SolidGridView view_;
};

}  // namespace tg
