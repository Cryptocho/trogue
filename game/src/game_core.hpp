// game_core.hpp —— 里程碑 6：回合制核心（纯逻辑，无窗口/无渲染依赖）。
//
// 移植自 trogue-orign（只读参考）的最小闭环：
//   玩家移动（8 向 + 斜切约束）→ 敌方回合（本里程碑为「静止」策略）→ 回合 +1。
//
// 设计约束（docs/plan-6.md §3.3）：
//   - 不依赖渲染/窗口/输入：可直接进入 tools/tests 无窗口单测；
//   - 只消费 trogue/scene.hpp 的 tile 查询（tg::is_solid_at）；
//   - GameState 是 actor 表 + 回合状态的唯一所有权，main.cpp 只读快照；
//   - 引擎公共 API 不动，本模块全部是 game 层代码。

#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "trogue/scene.hpp"

namespace game {

// ── 网格与方向（tile 粒度逻辑） ──

constexpr int kTileSize = 16;  // 与场景 tile 尺寸一致

struct TilePos {
    int x = 0;
    int y = 0;
    bool operator==(const TilePos& o) const { return x == o.x && y == o.y; }
};

// 移动方向（dx/dy ∈ -1..1）
struct Dir {
    int dx = 0;
    int dy = 0;
};

// ── 输入缓冲（对齐原版 trogue-orign/src/systems/input.lua 的 key buffer） ──
//
// 原版手感机制（input.lua:172-213）：
//   - 4 向键（左/右/上/下、a/d/w/s）进入缓冲队列（容量 2），并设置
//     keyBufferWindow（0.18s）倒计时；
//   - 0.18s 内快速追加第二个正交键 → 立即结算：两键合成一次对角移动
//     （dx/dy 各自 clamp 到 [-1,1]；若相互抵消如 [左,右] → 回落第一个键）；
//   - 时间窗超时（update 中递减）→ 结算队列中第一个键；
//   - 斜向键（q/e/z/c）不缓冲：清空队列并立即执行。
// 本结构是纯状态 + 自由函数，可无窗口单测；main.cpp 只喂按键、每帧 tick。

struct InputBuffer {
    Dir queue[2];
    int size = 0;
    float timer = 0.0f;                    // 剩余缓冲窗口（秒）
    static constexpr float kWindow = 0.18f;  // 对齐原版 keyBufferWindow
};

// 推入一个方向键；返回本次要立即执行的步（斜向键 / 双键合成 / 无=nullopt）。
// 返回非 nullopt 时调用方应立即交给 player_move。
std::optional<Dir> input_buffer_push(InputBuffer& buf, Dir d);
// 帧推进 dt 秒：窗口耗尽时结算队列；返回要执行的步（可能为 nullopt）。
std::optional<Dir> input_buffer_tick(InputBuffer& buf, float dt);
// 立即清空队列（如非玩家回合拒绝输入时）。
void input_buffer_flush(InputBuffer& buf);

// ── 移动动画时长 ──
//
// 对齐原版 trogue-orign/src/config.lua 的 MOVE_DURATION = 0.12（移动动画秒数）。
// **动画执行用引擎 `tg::TweenManager`（不重复造轮子）**：main 在玩家移动成功后
// 以 add_vec2 + Easing::quad_out 驱动视觉位置（引擎 quad_out = 1-(1-t)² = 2t-t²，
// 与原版 tween_system.lua 的 easeOutQuad(t) = -t*(t-2) 完全一致），
// 播完精确落格（无浮点残差 → 静止时与网格严格对齐、无像素抖动）。
constexpr float kMoveDuration = 0.12f;  // 对齐原版 MOVE_DURATION

// ── 实体（game 自有 Actor，非引擎实体） ──

struct Actor {
    std::string id;
    std::string type;
    TilePos pos;             // 逻辑位置（tile 格，0-based）
    bool is_player = false;  // 玩家（接受输入的实体）
    tg::Color color{255, 255, 255, 255};
    int z = 0;               // 视觉层级提示（descriptor 透传，渲染排序用）
    tg::SpriteDesc sprite;   // 视觉快照（无贴图时 has==false，渲染为色块）
};

// ── 回合阶段（为未来异步敌回合预留；当前同步结算不驻留 EnemyTurn） ──

enum class Phase {
    PlayerTurn,
    EnemyTurn,
};

// ── 行动结果 ──

enum class ActionResult {
    Invalid,  // 参数非法（(0,0)/越界）
    Blocked,  // 目标被地形/实体阻挡，回合不前进
    Moved,    // 移动成功（已结算敌回合并回合 +1）
    Waited,   // 等待成功（已结算敌回合并回合 +1）
};

// ── GameState：唯一所有权结构 ──

struct GameState {
    std::map<std::string, Actor> actors;  // key = id
    const tg::SceneAsset* asset = nullptr;  // 只读资产引用（main 拥有，swap 帧外）
    int map_w = 0;   // 地图尺寸（tile 格），来自 scene 层
    int map_h = 0;
    Phase phase = Phase::PlayerTurn;
    int turn_count = 1;

    // 便捷查询：玩家
    const Actor* player() const {
        for (const auto& [id, a] : actors)
            if (a.is_player) return &a;
        return nullptr;
    }
    Actor* player() {
        for (auto& [id, a] : actors)
            if (a.is_player) return &a;
        return nullptr;
    }
};

// ── 导入与初始化 ──

// 从 SceneAsset 导入 actors（type=="player" → 玩家；其余 type → 敌人），
// 并读取层尺寸作为地图边界。asset 生命周期由调用方（main）保证 ≥ GameState。
void import_scene(GameState& gs, const tg::SceneAsset& asset);

// ── 只读查询（供渲染/测试使用，不改状态） ──

bool tile_is_solid(const GameState& gs, int tx, int ty);
// 指定实体是否占住该格（排除 exclude_id，如移动者自身）
bool tile_has_entity(const GameState& gs, int tx, int ty,
                     const std::string& exclude_id);
// 斜向是否可走：仅当两个相邻正交格都被阻挡时才禁止（对齐原版
// coordinates.lua:canDiagonalMove 的 `not (adj1 and adj2)`）
bool can_diagonal_move(const GameState& gs, int from_x, int from_y, int dx,
                       int dy, const std::string& exclude_id);

// ── 玩家行动（穷尽结算：敌回合静止 → 回合 +1 → 回 PlayerTurn） ──

ActionResult player_move(GameState& gs, int dx, int dy);
ActionResult player_wait(GameState& gs);

// 敌方回合（本里程碑：静止策略，不动任何敌人）——独立暴露便于单测与未来扩展
void resolve_enemy_turn(GameState& gs);

}  // namespace game