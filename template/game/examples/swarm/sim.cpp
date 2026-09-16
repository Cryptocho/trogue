// sim.cpp —— 系统实现（纯逻辑，不 include raylib；只用引擎公共头）。
//
// 每步的固定顺序与理由：steer（意图→速度）→ move（积分+墙碰撞）→ spawn（刷怪/
// 开火）→ combat（命中结算）→ pickup（入账升级）→ cull（回收）。先动后结算，保证
// 「本步的命中」用的是本步位置；回收放最后，前面各系统才能安全地沿用旧下标循环。
#include "sim.hpp"

#include <cmath>   // std::sqrt（唯一数学函数；IEEE 正确舍入 → 跨平台逐位一致）
#include <cstdio>  // std::snprintf（摘要）

namespace swarm {

float half_of(std::uint8_t tag) noexcept {
    // 半边长（碰撞盒与渲染色块同源，不会错位）：玩家 6 / 敌人 5 / 子弹 2 / 球 2.5
    if (tag == kPlayer) return 6.0f;
    if (tag == kEnemy) return 5.0f;
    return tag == kBullet ? 2.0f : 2.5f;
}

tg::Rect rect_of(const World& w, int i) noexcept {
    const float h = half_of(w.tag[i]);
    return tg::Rect{w.pos[i].x - h, w.pos[i].y - h, h + h, h + h};
}

bool room_solid(int tx, int ty) noexcept {
    return tx <= 0 || ty <= 0 || tx >= kRoomW - 1 || ty >= kRoomH - 1;
}

int xp_need(int level) noexcept { return 3 + level; }
int bullet_dmg(int level) noexcept { return 1 + level / 10; }
int player_hp(const World& w) noexcept { return w.player >= 0 ? w.hp[w.player] : 0; }

float fire_interval(int level) noexcept {
    float iv = kFireInterval;
    for (int k = 0; k < (level / 5 + 1) / 2; ++k) iv *= 0.85f;  // 5、15、25… 级各减一次
    return iv;
}

namespace {

// 追加一个实体：六个组件列同步 push（保持等长同序 = 「下标即句柄」不变式）。
int push_entity(World& w, std::uint8_t tag, tg::Vec2 p, tg::Vec2 v, int hp) {
    w.pos.push_back(p); w.vel.push_back(v); w.hp.push_back(hp); w.tag.push_back(tag);
    w.bullet.push_back(Bullet{}); w.oxp.push_back(Oxp{});
    return count(w) - 1;
}

// 距离判据用平方比较：不开方、无浮点分支，逐位可复现。
bool close(tg::Vec2 a, tg::Vec2 b, float r) noexcept {
    const float dx = a.x - b.x, dy = a.y - b.y;
    return dx * dx + dy * dy <= r * r;
}

// O(1) 回收：尾元素换进空槽再整体 pop_back（调用方倒序遍历 → 不会漏检）。
// 不变式：玩家实体永不被回收（cull_system 只删敌人/子弹/经验球），因此下面的
// w.player 修正只需处理「别的实体被搬动时玩家本身挪位」这一种情形。
void remove_at(World& w, int i) {
    const int last = count(w) - 1;
    if (i != last) {  // 尾元素换进空槽（六列同步搬运，保持等长同序）
        w.pos[i] = w.pos[last]; w.vel[i] = w.vel[last]; w.hp[i] = w.hp[last];
        w.tag[i] = w.tag[last]; w.bullet[i] = w.bullet[last]; w.oxp[i] = w.oxp[last];
    }
    w.pos.pop_back(); w.vel.pop_back(); w.hp.pop_back(); w.tag.pop_back(); w.bullet.pop_back(); w.oxp.pop_back();
    if (w.player == last) w.player = (i == last) ? -1 : i;  // 玩家被搬动过就改指
}

void drop_orb(World& w, tg::Vec2 p) {
    w.oxp[push_entity(w, kOrb, p, tg::Vec2{0.0f, 0.0f}, 0)].value = 1;
}

// 最近敌人；距离相同取小下标（确定性平手规则）。
int nearest_enemy(const World& w) {
    if (w.player < 0) return -1;
    const tg::Vec2 p = w.pos[w.player];
    int best = -1;
    float best_d2 = 0.0f;
    for (int i = 0; i < count(w); ++i) {
        if (w.tag[i] != kEnemy || w.hp[i] <= 0) continue;
        const float dx = w.pos[i].x - p.x, dy = w.pos[i].y - p.y;
        const float d2 = dx * dx + dy * dy;
        if (best < 0 || d2 < best_d2) { best = i; best_d2 = d2; }
    }
    return best;
}

}  // namespace

void reset_round(World& w) {
    w.pos.clear(); w.vel.clear(); w.hp.clear(); w.tag.clear();
    w.bullet.clear(); w.oxp.clear();
    // 留一个完整刷怪间隔：玩家不会开局即被贴脸（也给拾取链一点缓冲）
    w.player = -1; w.fire_cd = 0.0f; w.invuln = 0.0f; w.spawn_cd = kSpawnInterval;
    const tg::Vec2 c{(kRoomW * 0.5f) * kTile, (kRoomH * 0.5f) * kTile};
    w.player = push_entity(w, kPlayer, c, tg::Vec2{0.0f, 0.0f}, kPlayerHp);
}

void steer_system(World& w, const Input& in) {
    const float len = std::sqrt(in.mx * in.mx + in.my * in.my);
    const tg::Vec2 dir = len > 1e-4f ? tg::Vec2{in.mx / len, in.my / len} : tg::Vec2{};
    for (int i = 0; i < count(w); ++i) {
        if (w.tag[i] == kPlayer) {  // 斜向经归一化，不加速
            w.vel[i] = tg::Vec2{dir.x * kPlayerSpeed, dir.y * kPlayerSpeed};
            continue;
        }
        if (w.tag[i] != kEnemy || w.player < 0) continue;  // 子弹方向发射时定好，不转向
        // 每步重取朝玩家的单位方向：空房间无障碍，直线追踪即最优（不做寻路）
        const float dx = w.pos[w.player].x - w.pos[i].x, dy = w.pos[w.player].y - w.pos[i].y;
        const float d = std::sqrt(dx * dx + dy * dy);
        w.vel[i] = d > 1e-4f ? tg::Vec2{dx / d * kEnemySpeed, dy / d * kEnemySpeed} : tg::Vec2{};
    }
}

void move_system(World& w, const tg::SolidGridView* views, int nviews, float dt) {
    for (int i = 0; i < count(w); ++i) {
        // 位移交给引擎的 swept 解算（轴分离、贴边不重叠、大 delta 不穿透）：本层只把
        // 速度积成 delta、把解算结果换回中心坐标，不手写扫掠/回退（与 platformer 同一
        // 边界：机制性几何原语归引擎，速度积分与规则归 game）。
        const float h = half_of(w.tag[i]);
        const tg::SweepResult r = tg::sweep_move(
            views, nviews, rect_of(w, i), tg::Vec2{w.vel[i].x * dt, w.vel[i].y * dt});
        w.pos[i] = tg::Vec2{r.box.x + h, r.box.y + h};
        // 子弹撞墙即消（寿命清零 → cull_system 回收）
        if (w.tag[i] == kBullet && (r.blocked_x || r.blocked_y)) w.bullet[i].life = 0.0f;
    }
}

void spawn_system(World& w, float dt) {
    w.spawn_cd -= dt;
    if (w.spawn_cd <= 0.0f) {
        w.spawn_cd = kSpawnInterval;  // 达上限时本拍空转，下一拍再试（节奏不漂）
        int enemies = 0;
        for (int i = 0; i < count(w); ++i) if (w.tag[i] == kEnemy) ++enemies;
        if (enemies < kMaxEnemies) {
            // 边缘刷怪：四条内边界等概率，落点由引擎 RNG 抽取（同 seed 同落点）
            const int side = w.rng.next_int(0, 3);
            const bool vertical = side >= 2;  // 2/3 = 左/右列，0/1 = 上/下行
            const int along = w.rng.next_int(1, (vertical ? kRoomH : kRoomW) - 2);
            const int tx = vertical ? (side == 2 ? 1 : kRoomW - 2) : along;
            const int ty = vertical ? along : (side == 0 ? 1 : kRoomH - 2);
            const tg::Vec2 p{(tx + 0.5f) * kTile, (ty + 0.5f) * kTile};
            push_entity(w, kEnemy, p, tg::Vec2{0.0f, 0.0f}, kEnemyHp);
            w.spawned++;
        }
    }
    // 自动开火：每 fire_interval 秒朝**最近**敌人射一发；无目标则本步不开火
    w.fire_cd -= dt;
    if (w.fire_cd > 0.0f || w.player < 0) return;
    const int target = nearest_enemy(w);
    if (target < 0) return;
    w.fire_cd = fire_interval(w.level);
    const tg::Vec2 p = w.pos[w.player];
    const float dx = w.pos[target].x - p.x, dy = w.pos[target].y - p.y;
    const float d = std::sqrt(dx * dx + dy * dy);
    if (d < 1e-4f) return;  // 与目标重合：方向未定义，放弃本发（不消耗随机数）
    const tg::Vec2 v{dx / d * kBulletSpeed, dy / d * kBulletSpeed};
    const int b = push_entity(w, kBullet, p, v, 0);
    w.bullet[b].dmg = bullet_dmg(w.level); w.bullet[b].life = kBulletLife;
}

void combat_system(World& w, float dt) {
    const int n = count(w);
    // 子弹 × 敌人：命中即扣血、子弹消失；血量由正变非正才算一次击杀并掉经验球
    for (int b = 0; b < n; ++b) {
        if (w.tag[b] != kBullet || w.bullet[b].life <= 0.0f) continue;
        for (int e = 0; e < n; ++e) {
            if (w.tag[e] != kEnemy || w.hp[e] <= 0) continue;  // 已死待回收的不挡弹
            if (!close(w.pos[b], w.pos[e], kHitDist)) continue;
            const int before = w.hp[e];
            w.hp[e] = before - w.bullet[b].dmg; w.bullet[b].life = 0.0f;
            // push_back 只追加：本步循环用的 n 仍然有效，新球下步才参与
            if (before > 0 && w.hp[e] <= 0) { w.kills++; drop_orb(w, w.pos[e]); }
            break;
        }
    }
    // 敌人 × 玩家：接触扣血 + 无敌帧（一步最多挨一次，避免多敌同帧叠扣）
    if (w.invuln > 0.0f) w.invuln -= dt;
    if (w.player >= 0 && w.invuln <= 0.0f) {
        for (int e = 0; e < count(w); ++e) {
            if (w.tag[e] != kEnemy || w.hp[e] <= 0) continue;
            if (!close(w.pos[e], w.pos[w.player], kTouchDist)) continue;
            w.hp[w.player] -= kEnemyDmg; w.invuln = kInvuln;
            break;
        }
    }
    // 死亡重置：kills/pickups/level/deaths 是元进度，跨重开保留（rng 也不重置）
    if (w.player >= 0 && w.hp[w.player] <= 0) { w.deaths++; reset_round(w); }
}

void pickup_system(World& w) {
    if (w.player < 0) return;
    for (int i = 0; i < count(w); ++i) {
        if (w.tag[i] != kOrb || w.oxp[i].taken) continue;
        if (!close(w.pos[i], w.pos[w.player], kOrbRadius)) continue;
        w.oxp[i].taken = true;  // 只打标记：下标搬运统一交给 cull_system
        w.xp += w.oxp[i].value; w.pickups++;
        while (w.xp >= xp_need(w.level)) { w.xp -= xp_need(w.level); w.level++; }
    }
}

void cull_system(World& w) {
    for (int i = count(w) - 1; i >= 0; --i) {  // 倒序：swap-remove 不会漏检元素
        const bool dead = (w.tag[i] == kEnemy && w.hp[i] <= 0) || (w.tag[i] == kBullet && w.bullet[i].life <= 0.0f) || (w.tag[i] == kOrb && w.oxp[i].taken);
        if (dead) remove_at(w, i);
    }
}

void step(World& w, const Input& in, const tg::SolidGridView* views, int nviews, float dt) {
    if (in.restart) reset_round(w);
    steer_system(w, in);
    move_system(w, views, nviews, dt);
    spawn_system(w, dt);
    combat_system(w, dt);
    pickup_system(w);
    cull_system(w);
    w.steps++;
}

std::string summary(const World& w) {
    char buf[192];
    std::snprintf(buf, sizeof buf,
                  "steps=%lld spawned=%d kills=%d pickups=%d level=%d player_hp=%d deaths=%d seed=%llu",
                  static_cast<long long>(w.steps), w.spawned, w.kills, w.pickups,
                  w.level, player_hp(w), w.deaths, static_cast<unsigned long long>(kSeed));
    return std::string(buf);
}

}  // namespace swarm
