#pragma once
// swarm/sim.hpp —— 类幸存者范例的**纯逻辑层**（ECS 风格）。
//
// 为什么逻辑与渲染彻底分层：本头与 sim.cpp 都不 include raylib，输入只是一个
// 普通 struct、时间只有固定步 dt。于是 sim_test 能在**无窗口、无 GL** 的环境里
// 重放同一份规则——「同 seed 同输入 → 逐位一致」这类回归不必依赖显示环境。
//
// 为什么用 ECS 而不是对象层次：这一局同时存在上百个同质敌人/子弹/经验球，
// 行为按**系统**（自由函数）切分、数据按**组件列**（并置数组）切分。每个系统只
// 遍历自己真正需要的列，没有虚函数、没有继承层次。实体就是这些数组的下标，
// 用 swap-remove 做 O(1) 回收（死亡/命中即从列里消失，不留空洞）。
//
// 数据布局：pos/vel/hp/tag/bullet/oxp 六个**等长同序**数组，下标即实体句柄；
// tag 是唯一分类组件（玩家/敌人/子弹/经验球），bullet/oxp 只在对应 tag 下有意义。
#include <cstdint>
#include <string>
#include <vector>

#include "trogue/collision.hpp"
#include "trogue/random.hpp"
#include "trogue/types.hpp"

namespace swarm {

// ── 常量（玩法调参集中在此；kSeed 会回显进摘要，便于对照复现）──
inline constexpr std::uint64_t kSeed = 20260916ULL;
inline constexpr float kDt = 1.0f / 60.0f;                  // 固定步长（秒）
inline constexpr int kTile = 16, kRoomW = 40, kRoomH = 25;  // tile 像素 / 房间格数
inline constexpr int kMaxEnemies = 200;                     // 同屏敌上限（性能护栏）
inline constexpr int kEnemyHp = 3, kEnemyDmg = 10, kPlayerHp = 100;
inline constexpr float kPlayerSpeed = 150.0f, kEnemySpeed = 45.0f, kBulletSpeed = 320.0f;
inline constexpr float kFireInterval = 0.5f, kSpawnInterval = 0.9f, kInvuln = 0.6f;
inline constexpr float kBulletLife = 1.4f, kOrbRadius = 16.0f;  // 秒 / 拾取半径(px)
inline constexpr float kHitDist = 7.0f, kTouchDist = 11.0f;     // 子弹×敌 / 敌×玩家

// 实体种类（Tag 组件）。
enum Tag : std::uint8_t { kPlayer, kEnemy, kBullet, kOrb };

// 一帧的玩家意图（方向分量；steer_system 负责归一化）+ 重开边沿。
struct Input { float mx = 0.0f, my = 0.0f; bool restart = false; };
// Bullet 组件：伤害 + 剩余寿命（寿命<=0 即撞墙或命中，由 cull_system 回收）。
struct Bullet { int dmg = 1; float life = 0.0f; };
// Oxp 组件：经验值 + 拾取标记（只打标记、不就地搬下标，见 pickup_system）。
struct Oxp { int value = 1; bool taken = false; };

// ECS 世界：统计量跨「死亡重置」保留（元进度），rng 也不重置——随机流因此是整局
// 连续的一条序列，重放同一输入序列即得到逐位相同的摘要。
struct World {
    std::vector<tg::Vec2> pos, vel;  // 中心坐标（px）/ 速度（px/s）
    std::vector<int> hp;             // 生命（玩家/敌人）
    std::vector<std::uint8_t> tag;   // Tag 组件
    std::vector<Bullet> bullet;      // Bullet 组件
    std::vector<Oxp> oxp;            // Oxp 组件

    tg::Random rng{kSeed};  // 唯一随机源（只用引擎 tg::Random；无 rand/time）
    std::int64_t steps = 0; // 已执行固定步数
    int player = -1;        // 玩家实体下标（死亡重置后重建）
    int spawned = 0, kills = 0, pickups = 0, deaths = 0;  // 累计统计
    int level = 1, xp = 0;
    float fire_cd = 0.0f, spawn_cd = 0.0f, invuln = 0.0f;  // 秒
};

// ── 房间几何：渲染用的 '#' 环与碰撞掩码同源，画的墙就是挡的墙 ──
bool room_solid(int tx, int ty) noexcept;
// 碰撞视图由调用方用 SolidGrid::create(kRoomW, kRoomH, kTile, kTile, room_solid)
// 构造（本文件不提供掩码工厂：SolidGrid 是 RAII 自持掩码，比裸 buffer + 视图更省心）。

// ── 世界查询小工具（系统与渲染共用同一坐标约定）──
inline int count(const World& w) noexcept { return static_cast<int>(w.pos.size()); }
float half_of(std::uint8_t tag) noexcept;          // 半边长：碰撞盒与色块同源
tg::Rect rect_of(const World& w, int i) noexcept;  // 实体 AABB（墙碰撞用）
int player_hp(const World& w) noexcept;

// ── 升级规则（经验球 → 等级 → 战力）──
// 升到 5/15/25…（5 的奇数倍）走射速阶梯：开火间隔 ×0.85；升到 10/20/30…（5 的
// 偶数倍）走伤害阶梯：子弹伤害 +1。两阶梯交替，等级越高成长越温和。
int xp_need(int level) noexcept;         // 升入下一级所需经验 = 3 + level
int bullet_dmg(int level) noexcept;
float fire_interval(int level) noexcept; // 秒

// ── 系统（自由函数；step 按固定顺序调用，顺序即语义）──
void steer_system(World& w, const Input& in);  // 意图 → 速度（子弹方向不在此改）
void move_system(World& w, const tg::SolidGridView* views, int nviews, float dt);
void spawn_system(World& w, float dt);   // 定时刷怪 + 自动开火
void combat_system(World& w, float dt);  // 子弹×敌人、敌人×玩家、死亡重置
void pickup_system(World& w);            // 经验球 → 经验 → 升级
void cull_system(World& w);              // 回收死亡/过期实体（swap-remove）
void step(World& w, const Input& in, const tg::SolidGridView* views, int nviews,
          float dt);
void reset_round(World& w);  // 清场重建玩家（保留 kills/pickups/level/deaths）
// 一行 key=value 摘要（字段固定，供 headless 验收与逐位回归对比）
std::string summary(const World& w);

}  // namespace swarm
