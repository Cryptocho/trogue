#pragma once
// swarm/sim.hpp —— 类幸存者范例的**纯逻辑层**（ECS 风格 + Phase 状态机）。
//
// 数据/逻辑分层：本头与 sim.cpp 都不 include raylib，输入只是一个普通 struct、
// 时间只有固定步 dt。sim_test 在**无窗口、无 GL**的环境里重放同一份规则——
// 「同 seed 同输入 → 逐位一致」这类回归不必依赖显示环境。
//
// ECS 风格：行为按**系统**（自由函数）切分、数据按**组件列**（并置数组）切分。
// 实体 = 数组下标；swap-remove 做 O(1) 回收。tag 是唯一分类组件，其它组件列
// 仅在对应 tag 下有意义（Bullet/Pickup 用默认值填充，但语义仅在 tag 匹配时启用）。
//
// Phase 状态机：playing / levelup / paused / dead 四态，main 层只读取、sim 层
// 推进转换。Paused/Dead 时 step 整体跳过；LevelUp 时 step 仍推进但 XP 累积冻结。
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "trogue/collision.hpp"
#include "trogue/random.hpp"
#include "trogue/types.hpp"

namespace swarm {

// ── 基础常量 ──
inline constexpr std::uint64_t kSeed = 20260916ULL;
inline constexpr float kDt = 1.0f / 60.0f;                  // 固定步长（秒）
inline constexpr int kTile = 16;
inline constexpr int kMapW = 512, kMapH = 512;              // 512*16=8192 px；4MB 资产上限允许的最大方阵（≈ 1MB JSON）
inline constexpr int kMossChunkSize = 32;                   // moss 圆盘的 hash 网格粒度
inline constexpr int kMossMaxRadius = 10;                   // 单个圆盘最大半径（tile）
inline constexpr int kMaxEnemies = 200;                     // 同屏敌上限
inline constexpr int kEnemyHp = 5, kEnemyDmg = 10;
inline constexpr int kPlayerHp = 100;
inline constexpr float kPlayerSpeed = 150.0f, kEnemySpeed = 45.0f, kBulletSpeed = 320.0f;
inline constexpr float kFireInterval = 0.5f, kSpawnInterval = 0.9f, kInvuln = 0.6f;
inline constexpr float kBulletLife = 1.4f, kOrbRadius = 48.0f;
inline constexpr float kHitDist = 7.0f, kTouchDist = 11.0f;
inline constexpr float kDashDuration = 0.18f, kDashCooldown = 1.2f, kDashSpeedMul = 2.4f;
inline constexpr float kBossHp = 25.0f;                     // Boss 总血量
inline constexpr float kBossTeleportInterval = 1.5f;        // Boss 瞬移间隔
inline constexpr float kBossTouchRatio = 0.20f;             // Boss 撞玩家扣 max_hp 的 20%
inline constexpr float kWaveInterval = 25.0f;               // 每 25 秒一波
inline constexpr float kPrepTime = 3.0f;                    // 开局 3 秒准备阶段
inline constexpr float kPickupTtl = 15.0f;                  // 道具在场上存活秒数
inline constexpr float kSpeedBuffDuration = 5.0f;           // 速度包加成时长
inline constexpr float kInvulnBuffDuration = 3.0f;          // 无敌药水时长
inline constexpr float kSpeedBuffMul = 1.5f;                // 速度包加速倍率
inline constexpr float kLevelHealRatio = 0.20f;            // 升级自动回血比例
inline constexpr int kWaveBossEvery = 4;                    // 每 4 波一个 Boss
inline constexpr int kLevelMax = 5;                         // 武器/被动封顶等级
inline constexpr int kCardChoices = 3;                      // 抽卡三选一

// ── 标签 ──
enum Tag : std::uint8_t { kPlayer, kEnemy, kBullet, kOrb, kPickup, kBoss };

// Phase 状态机
enum class Phase : std::uint8_t { Playing, LevelUp, Paused, Dead };

// 武器池（4 远程 + 2 近战）。M5a。
enum class WeaponKind : std::uint8_t {
    LineShot = 0,  // 直线单弹
    SpreadShot,    // 扇形 3 弹
    RadialBurst,   // 8 方向全射
    HomingShot,    // 追踪单弹
    ArcSlash,      // 近战弧斩
    ShieldBash,    // 近战推开 + 短无敌
    kCount,
};
inline constexpr int kWeaponCount = static_cast<int>(WeaponKind::kCount);

// 武器定义（编译期常量）
struct WeaponDef {
    const char* name;
    float base_dmg;
    float speed;       // 子弹速度（近战 = 0）
    float life;        // 子弹寿命（近战 = 0）
    float base_cd;     // 基础冷却
    int dmg_per_lvl;
    float cd_mul_per_lvl;
    bool is_melee;
};
inline constexpr WeaponDef kWeaponDefs[kWeaponCount] = {
    {"LineShot",      3.0f, 320.0f, 0.5f, 0.50f, 1, 0.85f, false},
    {"SpreadShot",    2.0f, 280.0f, 0.5f, 0.80f, 1, 0.90f, false},
    {"RadialBurst",   2.0f, 260.0f, 0.5f, 1.50f, 1, 0.95f, false},
    {"HomingShot",    5.0f, 220.0f, 1.0f, 1.20f, 2, 0.90f, false},
    {"ArcSlash",      6.0f,   0.0f, 0.0f, 0.60f, 2, 0.85f, true },
    {"ShieldBash",    4.0f,   0.0f, 0.0f, 1.50f, 2, 0.90f, true },
};
// 起始武器：LineShot
inline constexpr WeaponKind kStartingWeapon = WeaponKind::LineShot;

// 被动池（3 种）
enum class PassiveKind : std::uint8_t { SpeedUp, HpUp, PickupUp, kCount };
inline constexpr int kPassiveCount = static_cast<int>(PassiveKind::kCount);
struct PassiveDef {
    const char* name;
    float per_lvl;     // 每级加成
};
inline constexpr PassiveDef kPassiveDefs[kPassiveCount] = {
    {"SpeedUp",   0.10f},  // +10% 玩家速度 / 级
    {"HpUp",      0.15f},  // +15% max_hp / 级
    {"PickupUp",  0.20f},  // +20% 拾取半径 / 级
};

// 玩家身上的武器实例（M5a）
struct WeaponInst {
    WeaponKind kind = WeaponKind::LineShot;
    int level = 1;
    float cd_remaining = 0.0f;
};
// 玩家身上的被动实例
struct PassiveInst {
    PassiveKind kind;
    int level = 1;
};

// 道具种类（M4）
enum class PickupKind : std::uint8_t { Heal, SpeedBuff, InvulnBuff };
struct Pickup {
    PickupKind kind = PickupKind::Heal;
    float value = 25.0f;     // Heal = 25HP；SpeedBuff = 0（buff 时长从常量取）
    float ttl = kPickupTtl;  // 场上剩余秒数
};

// 抽卡（M6）
struct CardOffer {
    bool is_weapon = true;       // true = weapon / false = passive
    std::uint8_t kind = 0;       // WeaponKind 或 PassiveKind
    int current_level = 0;       // 玩家当前等级（0 = NEW）
    const char* name = "";
};

// 击杀爆点（死亡位置 + 衰减计时，main 层画扩张圆环做击杀反馈）
struct KillFx { tg::Vec2 pos{0.0f, 0.0f}; float timer = 0.0f; };
inline constexpr int kKillFxMax = 16;
inline constexpr float kKillFxLife = 0.30f;  // 爆点存活 0.3s
inline constexpr float kWaveClearMsgLife = 1.5f;  // 波次清空提示 1.5s

// 一帧的玩家意图
struct Input {
    float mx = 0.0f, my = 0.0f;  // 移动方向
    float aim_x = 0.0f, aim_y = 0.0f;  // 鼠标世界坐标（零向量 = 自动射最近）
    bool restart = false;        // R 重开（仅 main 层响应，sim 内不读）
    bool dash = false;           // Shift 冲刺边沿
    bool pause = false;          // Esc 暂停边沿
    int card_pick = -1;          // 1/2/3 选卡（-1 = 不选）
};
struct Bullet {
    int dmg = 1;
    float life = 0.0f;
    std::int8_t weapon = -1;     // WeaponKind（近战 = -1）
};
struct Oxp { int value = 1; bool taken = false; };

// ECS 世界
struct World {
    std::vector<tg::Vec2> pos, vel;
    std::vector<int> hp;
    std::vector<std::uint8_t> tag;
    std::vector<Bullet> bullet;
    std::vector<Oxp> oxp;
    std::vector<Pickup> pickup;   // 仅 kPickup 标签下有意义

    tg::Random rng{kSeed};

    std::int64_t steps = 0;
    int player = -1;
    int spawned = 0, kills = 0, pickups = 0, deaths = 0, dashes = 0;
    int level = 1, xp = 0;

    // Phase 状态机
    Phase phase = Phase::Playing;
    float levelup_flash_timer = 0.0f;  // 升级白闪倒计时（独立计时器，不入 phase）
    std::array<CardOffer, kCardChoices> lvlup_cards{};

    // 波次系统
    int wave = 0;                  // 0 = 准备阶段；>=1 = 当前波次
    float wave_timer = kPrepTime;  // 当前波剩余秒（准备阶段倒计时 / 波间倒计时）
    int boss_idx = -1;             // Boss 实体下标
    float boss_hp = 0.0f;
    float boss_teleport_cd = 0.0f;

    // Buff 系统
    float speed_mul = 1.0f;
    float speed_timer = 0.0f;
    float invuln_timer = 0.0f;       // 共用无敌（冲刺 / 受伤 / ShieldBash / InvulnBuff）

    // 武器与被动（M5a）
    std::vector<WeaponInst> weapons;
    std::vector<PassiveInst> passives;

    // 计时器（cd）
    float fire_cd = 0.0f;       // 基础 LineShot 兼容（实际以 weapons[i].cd_remaining 推进）
    float spawn_cd = 0.0f;
    float dash_timer = 0.0f, dash_cd = 0.0f;
    int pickup_step = 0;        // 道具刷节拍器（自持 World 状态，reset 时归 0）

    // ── 玩家动画状态机（main 层读 → 选 AnimationPlayer 的 clip）──
    enum class PlayerAnim : std::uint8_t { Idle, Walk, Attack, Hurt, Die };
    PlayerAnim player_anim = PlayerAnim::Idle;
    float player_anim_timer = 0.0f;  // clip 剩余时长（决定非 loop 何时回 Idle）

    // ── Boss 动画状态机 ──
    enum class BossAnim : std::uint8_t { Idle, Attack };
    BossAnim boss_anim = BossAnim::Idle;
    float boss_anim_timer = 0.0f;

    // 击杀爆点环形缓冲（推进时衰减；timer<=0 时槽位空闲 → push_kill_fx 覆盖）
    KillFx kill_fx[kKillFxMax]{};

    // 波次清空提示（main 层弹"Wave N cleared!"）
    int wave_clear_msg = 0;            // >0 时绘制文字并按步衰减
    float wave_clear_msg_timer = 0.0f;

    // 最大血量缓存（HpUp 被动影响）
    int base_max_hp = 100;
};

// ── 大地图几何 ──
// 程序化 moss 生成：tileset 双 terrain（lower=stone / upper=mossy grass）里
// upper 这一支承担「场景障碍」。每 kMossChunkSize=32 tile 一格，每格用确定性
// hash 决定本块是否有 moss 圆盘 + 圆盘中心 + 半径。无墙：玩家可以走出地图
// 边缘，视觉与物理同源（main.cpp 同一函数）→ autotile 沿地形边界自然过渡。
inline constexpr std::uint64_t kMossSeed = 0x6d6f73735f73677aULL;  // "moss_sgz"

// 单 tile 是否处于 moss 圆盘（upper terrain）内。确定性 hash → 同 seed 复现。
bool is_moss_tile(int tx, int ty) noexcept;

// 兼容老 API：仅返回 moss 状态（已去掉 wall margin；地图无外墙）。
bool room_solid(int tx, int ty) noexcept;

// ── 世界查询小工具 ──
inline int count(const World& w) noexcept { return static_cast<int>(w.pos.size()); }
float half_of(std::uint8_t tag) noexcept;
tg::Rect rect_of(const World& w, int i) noexcept;
int player_hp(const World& w) noexcept;
int player_max_hp(const World& w) noexcept;
float player_pickup_radius(const World& w) noexcept;

// ── 升级规则（经验球 → 等级 → 战力）──
int xp_need(int level) noexcept;
int bullet_dmg(int level) noexcept;
float fire_interval(int level) noexcept;

// ── 冲刺状态查询 ──
bool is_dashing(const World& w) noexcept;
float dash_cooldown_remaining(const World& w) noexcept;

// ── 武器 CD 查询（窗口层 HUD 用）──
float weapon_cd_of(const WeaponInst& inst) noexcept;

// ── 系统（自由函数；step 按固定顺序调用，顺序即语义）──
void steer_system(World& w, const Input& in);
void move_system(World& w, const tg::SolidGridView* views, int nviews, float dt);
void spawn_system(World& w, float dt);   // 波次推进 + 刷怪 + Boss + 道具
void weapon_system(World& w, float dt);  // 每把武器 cd + fire_pattern
void combat_system(World& w, float dt);  // 命中 / 撞玩家 / 死亡
void pickup_system(World& w);            // 经验球 + 道具拾取
void buff_system(World& w, float dt);    // 各种 buff timer 衰减
void cull_system(World& w);              // 回收
void step(World& w, const Input& in, const tg::SolidGridView* views, int nviews,
          float dt);
void reset_round(World& w);  // 清场重建玩家

// 升级瞬间自动调用：根据当前 weapons/passives 状态生成 3 张候选卡（60% 武器 /
// 40% 被动；已持有 = 1× 权重，未持有 = 3× 权重）。调用后 phase = LevelUp。
void offer_levelup_cards(World& w);

// ── 测试辅助（M4/M5 测试断言用）──
// 在指定位置 push 一个 kPickup 实体（kind=Heal/Speed/Invuln），返回下标。
// 仅在测试代码中使用，避免测试 inline 重复实现 push_entity 私有细节。
int push_test_pickup_helper(World& w, tg::Vec2 p, PickupKind kind);

// ── 一行 key=value 摘要 ──
std::string summary(const World& w);

}  // namespace swarm