// game_core.hpp —— 回合制核心（纯逻辑，无窗口/无渲染依赖）。
//
// 玩家移动（8 向 + 斜切约束）→ 敌方回合 → 回合 +1；敌 AI/战斗接入：
//   - Actor 增加 Hp/AiState/冷却（按原型门控：仅 goblin 参与战斗）；
//   - 移动裁决泛化为 try_move（任意 actor，成功发 MoveSucceeded）；
//   - GameSystems 可空重载：main/IPC 走完整敌方阶段（AI），旧测试走静止
//     语义（零改动）；GameOver 相位收尾不推进回合。
//
// 设计约束：
//   - 不依赖渲染/窗口/输入：可直接进入 tools/tests 无窗口单测；
//   - 只消费 trogue/scene.hpp 的 tile 查询（tg::is_solid_at）与
//     trogue/animation.hpp 的动画集名查询（纯声明）；
//   - GameState 是 actor 表 + 回合状态的唯一所有权，main.cpp 只读快照；
//   - 引擎公共 API 不动，本模块全部是 game 层代码。

#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "trogue/scene.hpp"

namespace game {

class EventBus;    // event_bus.hpp（实现文件 include）
class RuleEngine;  // rules.hpp
struct AiSystem;   // ai.hpp

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
// **动画执行用引擎 `tg::TweenManager`（不重复造轮子）**：main 在玩家/敌人移动
// 成功后以 add_vec2 + Easing::quad_out 驱动视觉位置（引擎 quad_out = 1-(1-t)²
// = 2t-t²，与原版 tween_system.lua 的 easeOutQuad(t) = -t*(t-2) 完全一致），
// 播完精确落格（无浮点残差 → 静止时与网格严格对齐、无像素抖动）。
constexpr float kMoveDuration = 0.12f;  // 对齐原版 MOVE_DURATION

// ── 原型门控：AI/战斗资格的唯一入口 ──
//
// 对齐原版「AI 只遍历带 Actor+AIState 的实体」（ai.lua:41）：demo.json 的
// coin 等惰性实体不参与战斗（无 hp/无 AI/不发事件），保留占格阻挡行为。
bool is_combat_archetype(std::string_view type);  // 战斗原型仅 "goblin"

// ── 实体（game 自有 Actor，非引擎实体） ──

struct Hp {
    int cur = 0;
    int max = 0;
};

// AI 三态（对齐原版 components/ai_state.lua + systems/ai.lua 状态机）
enum class AiPhase {
    idle,
    alerted,
    chasing,
};
const char* ai_state_name(AiPhase s);  // "idle"/"alerted"/"chasing"（快照/wire 用）

struct AiState {
    AiPhase state = AiPhase::idle;
    int alerted_turn = 0;   // 进入 alerted 的回合（gs.turn_count 口径）
    bool has_target = false;
    TilePos target;         // 最后一次看见玩家的位置（丢失视线后的记忆点）
};

// 原型默认 HP（game 层原型表；tro-scene descriptor 不携带数值）
constexpr int kPlayerMaxHp = 100;  // 对齐原版 entities.lua player
constexpr int kGoblinMaxHp = 25;   // 对齐原版 entities.lua goblin

struct Actor {
    std::string id;
    std::string type;
    TilePos pos;             // 逻辑位置（tile 格，0-based）
    bool is_player = false;  // 玩家（接受输入的实体）
    tg::Color color{255, 255, 255, 255};
    int z = 0;               // 视觉层级提示（descriptor 透传，渲染排序用）
    tg::SpriteDesc sprite;   // 视觉快照（无贴图时 has==false，渲染为色块）
    float rotation = 0.0f;   // 初始朝向提示（descriptor 透传，度）
    nlohmann::json props;    // descriptor 自由透传（game 自行解释）
    int anim_set = -1;       // 动画集索引（-1=无动画；名=entity id 映射）
    // ── 战斗/AI 状态（惰性实体一律 nullopt/空） ──
    std::optional<Hp> hp;                 // nullopt = 无 hp（不参与战斗）
    std::map<std::string, int> cooldowns; // 能力冷却（仅 >0 才登记）
    AiState ai;                           // 仅战斗原型有效
};

// ── 回合阶段（GameOver：玩家 hp≤0，收尾不推进回合） ──

enum class Phase {
    PlayerTurn,
    EnemyTurn,
    GameOver,
};

// ── 行动结果 ──

enum class ActionResult {
    Invalid,  // 参数非法（(0,0)/越界/非玩家回合（GameSystems 版））
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
    std::vector<std::string> pending_despawn;  // 延迟销毁（对齐原版 ShouldDespawn，
                                               // 收尾统一清除，避免 emit 中 erase）

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

// ── 系统上下文：main/IPC 的完整敌方阶段入口 ──
//
// sys 为空（nullptr / 缺省重载）时敌方阶段退化为静止语义（无 AI、无事件）——
// 单一代码路径，既有测试零改动。指针由 main 持有并保证存活期 ≥ 调用。
struct GameSystems {
    EventBus* bus = nullptr;
    RuleEngine* rules = nullptr;
    AiSystem* ai = nullptr;
};

// ── 导入与初始化 ──

// 从 SceneAsset 导入 actors：player → hp 100/100；goblin → hp 25/25 + AI；
// 其余 → 惰性实体（无 hp/AI）。并读取层尺寸作为地图边界。asset 生命周期由
// 调用方（main）保证 ≥ GameState。热重载按原型表重建（AI 状态复位 idle）。
void import_scene(GameState& gs, const tg::SceneAsset& asset);

// ── 只读查询（供渲染/测试使用，不改状态） ──

bool tile_is_solid(const GameState& gs, int tx, int ty);
// 地形 solid 判定（仅 solid 层数据；界外/层矩形外 = 无数据 = 不阻挡）。
// 与 tile_is_solid 的差异：后者界外返回 true（移动边界语义）；本函数
// 对齐引擎 tg::is_solid_at 原语义，供导航/视野使用。
bool tile_solid_terrain(const GameState& gs, int tx, int ty);
// 指定实体是否占住该格（排除 exclude_id，如移动者自身）
bool tile_has_entity(const GameState& gs, int tx, int ty,
                     const std::string& exclude_id);
// 斜向是否可走：仅当两个相邻正交格都被阻挡时才禁止（对齐原版
// coordinates.lua:canDiagonalMove 的 `not (adj1 and adj2)`）
bool can_diagonal_move(const GameState& gs, int from_x, int from_y, int dx,
                       int dy, const std::string& exclude_id);

// ── 移动裁决（任意 actor；成功发 MoveSucceeded） ──
//
// 与玩家裁决同一套规则（地形 solid / 实体互斥 / 斜切切角）。bus 非空时
// 成功 emit `MoveSucceeded{entity, from:[gx,gy], to:[gx,gy]}`；失败无事件。
ActionResult try_move(GameState& gs, EventBus* bus, const std::string& actor_id,
                      int dx, int dy);

// ── 玩家行动（穷尽结算：敌回合 → 回合 +1 → 回 PlayerTurn） ──
//
// M6 兼容签名（既有测试原样调用）：敌方阶段 = 静止 + 回合 +1。
ActionResult player_move(GameState& gs, int dx, int dy);
ActionResult player_wait(GameState& gs);
// GameSystems 版（main/IPC 用）：完整敌方阶段（AI + 事件）；入口带 phase
// 守卫（非 PlayerTurn，含 GameOver → Invalid）。
ActionResult player_move(GameState& gs, GameSystems& sys, int dx, int dy);
ActionResult player_wait(GameState& gs, GameSystems& sys);

// 敌方回合收尾：sys 非空 → AI 行动；统一清除 pending_despawn；
// GameOver → 保持相位（不 +1、不发 TurnEnded）；否则 +1 回玩家回合并
// emit `TurnEnded{turn}`（sys->bus 非空时）。
void resolve_enemy_turn(GameState& gs, const GameSystems* sys = nullptr);

}  // namespace game
