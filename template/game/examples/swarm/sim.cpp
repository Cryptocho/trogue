// sim.cpp —— 系统实现（纯逻辑，不 include raylib；只用引擎公共头）。
//
// 每步的固定顺序与理由：
//   state→steer→weapon→spawn→move→combat→pickup→buff→cull
// 1) state: Phase 转换（HP≤0 → Dead）；LevelUp 期间冻结 XP 累积。
// 2) steer: 玩家速度（含冲刺、aim_dir、Buff）；敌追踪；Boss 瞬移 AI。
// 3) weapon: 每把武器 cd 到 0 → fire_pattern 生成弹 / 近战命中。
// 4) spawn: 波次推进 + 普通敌按节奏 + 道具 + Boss。
// 5) move: sweep_move（引擎原语）。
// 6) combat: 子弹×敌命中 + 敌/Boss 撞玩家。
// 7) pickup: 经验球入账 + 道具应用 buff。
// 8) buff: speed_timer / invuln_timer / Pickup.ttl 衰减。
// 9) cull: swap-remove 死亡/过期。
#include "sim.hpp"

#include <algorithm>  // std::max / std::min
#include <cmath>
#include <cstdio>

namespace swarm {

// ── 几何与碰撞视图 ──
float half_of(std::uint8_t tag) noexcept {
    if (tag == kPlayer) return 6.0f;
    if (tag == kEnemy)  return 5.0f;
    if (tag == kBoss)   return 9.0f;
    if (tag == kBullet) return 2.0f;
    return 2.5f;  // kOrb / kPickup
}
tg::Rect rect_of(const World& w, int i) noexcept {
    const float h = half_of(w.tag[i]);
    return tg::Rect{w.pos[i].x - h, w.pos[i].y - h, h + h, h + h};
}

namespace {

std::vector<bool> g_obstacles;

void generate_obstacles() {
    g_obstacles.assign(static_cast<std::size_t>(kMapW * kMapH), false);
    // 障碍从 main.cpp 共享的 kMossObstacles 圆盘写入：tileset 的 upper（moss）
    // 那种 tile 在物理上也真挡路 → 视觉与 SolidGrid 同源。
    // 2026-09-17 更新：去掉 g_obstacles 表，改为程序化 is_moss_tile（sim 端和
    // main 端都用同一个 hash）；room_solid 现在只查 is_moss_tile，不再有 wall
    // margin —— 地图不再有外墙，玩家走到边缘之外照样走。
}

}  // namespace

bool is_moss_tile(int tx, int ty) noexcept {
    // 程序化 moss 生成：每 kMossChunkSize=32 tile 一格；格内 hash 决定本块
    // 是否含圆盘 + 圆盘中心 + 半径。wrap：负坐标 → 翻成 (tx + kMapW, ty + kMapH)
    // 让程序化分布可平铺（pseudo-tiling），而不是出地图左边界就消失。
    auto wrap = [](int v) {
        v %= kMapW;
        return v < 0 ? v + kMapW : v;
    };
    if (tx < 0) tx = wrap(tx);
    if (ty < 0) ty = wrap(ty);
    const int cx = wrap(static_cast<int>(tx / kMossChunkSize));
    const int cy = wrap(static_cast<int>(ty / kMossChunkSize));
    // 局部坐标（块内偏移）
    const int lx = tx - cx * kMossChunkSize;
    const int ly = ty - cy * kMossChunkSize;
    tg::Random h{kMossSeed ^ (static_cast<std::uint64_t>(cx) * 0x9E3779B97F4A7C15ULL)
                          ^ (static_cast<std::uint64_t>(cy) * 0xC6BC279692B5C323ULL)};
    if (h.next_int(0, 1) == 0) return false;  // ~50% 块不放 moss，留出通行通道
    const int dcx = h.next_int(-4, 4);
    const int dcy = h.next_int(-4, 4);
    const int r = h.next_int(4, kMossMaxRadius);
    const int ox = lx - (kMossChunkSize / 2 + dcx);
    const int oy = ly - (kMossChunkSize / 2 + dcy);
    return ox * ox + oy * oy <= r * r;
}

bool room_solid(int tx, int ty) noexcept {
    // 没有外墙 → 只看 moss。范围外视为"非 moss"（玩家可走出地图到虚空）。
    return is_moss_tile(tx, ty);
}

// ── 升级规则 ──
int xp_need(int level) noexcept { return 2 + level; }
int bullet_dmg(int level) noexcept { return 1 + level / 10; }
float fire_interval(int level) noexcept {
    float iv = kFireInterval;
    for (int k = 0; k < (level / 5 + 1) / 2; ++k) iv *= 0.85f;
    return iv;
}
int player_max_hp(const World& w) noexcept {
    int mhp = w.base_max_hp;
    for (const auto& p : w.passives)
        if (p.kind == PassiveKind::HpUp) mhp = static_cast<int>(mhp * (1.0f + PassiveDef{}.per_lvl * p.level));
    return mhp;
}
int player_hp(const World& w) noexcept {
    return w.player >= 0 ? w.hp[w.player] : 0;
}
float player_pickup_radius(const World& w) noexcept {
    float r = kOrbRadius;
    for (const auto& p : w.passives)
        if (p.kind == PassiveKind::PickupUp) r *= (1.0f + 0.20f * p.level);
    return r;
}
bool is_dashing(const World& w) noexcept { return w.dash_timer > 0.0f; }
float dash_cooldown_remaining(const World& w) noexcept {
    return w.dash_cd > 0.0f ? w.dash_cd : 0.0f;
}

namespace {

// 追加一个实体：所有组件列同步 push（保持等长同序）。
int push_entity(World& w, std::uint8_t tag, tg::Vec2 p, tg::Vec2 v, int hp) {
    w.pos.push_back(p); w.vel.push_back(v); w.hp.push_back(hp); w.tag.push_back(tag);
    w.bullet.push_back(Bullet{}); w.oxp.push_back(Oxp{}); w.pickup.push_back(Pickup{});
    return count(w) - 1;
}

bool close(tg::Vec2 a, tg::Vec2 b, float r) noexcept {
    const float dx = a.x - b.x, dy = a.y - b.y;
    return dx * dx + dy * dy <= r * r;
}

void remove_at(World& w, int i) {
    const int last = count(w) - 1;
    // swap-remove 维护外部句柄：删 i 时，last 槽的内容会搬到 i（除非 i==last），
    // 因此任何指向 last 的句柄必须改成 i（指向被删槽 i 本身则改成 -1）。
    // 这里管两个长期句柄：player（cull 不删玩家，但防御性处理）和 boss_idx
    //（Boss 死亡后由 cull 回收）。
    auto fix_handle = [i, last](int& h) {
        if (h == last) h = (i == last) ? -1 : i;
        else if (h == i && i != last) h = -1;
    };
    fix_handle(w.player);
    fix_handle(w.boss_idx);
    if (i != last) {
        w.pos[i] = w.pos[last]; w.vel[i] = w.vel[last]; w.hp[i] = w.hp[last];
        w.tag[i] = w.tag[last]; w.bullet[i] = w.bullet[last];
        w.oxp[i] = w.oxp[last]; w.pickup[i] = w.pickup[last];
    }
    w.pos.pop_back(); w.vel.pop_back(); w.hp.pop_back(); w.tag.pop_back();
    w.bullet.pop_back(); w.oxp.pop_back(); w.pickup.pop_back();
}

void drop_orb(World& w, tg::Vec2 p, int value) {
    int idx = push_entity(w, kOrb, p, tg::Vec2{0.0f, 0.0f}, 0);
    w.oxp[idx].value = value;
}

void push_kill_fx(World& w, tg::Vec2 p) {
    int oldest = 0;
    float oldest_t = w.kill_fx[0].timer;
    for (int i = 1; i < kKillFxMax; ++i) {
        if (w.kill_fx[i].timer <= 0.0f) { oldest = i; oldest_t = -1.0f; break; }
        if (w.kill_fx[i].timer < oldest_t) { oldest = i; oldest_t = w.kill_fx[i].timer; }
    }
    w.kill_fx[oldest].pos = p;
    w.kill_fx[oldest].timer = kKillFxLife;
}

int nearest_enemy(const World& w) {
    if (w.player < 0) return -1;
    const tg::Vec2 p = w.pos[w.player];
    int best = -1;
    float best_d2 = 0.0f;
    for (int i = 0; i < count(w); ++i) {
        const std::uint8_t t = w.tag[i];
        if ((t != kEnemy && t != kBoss) || w.hp[i] <= 0) continue;
        const float dx = w.pos[i].x - p.x, dy = w.pos[i].y - p.y;
        const float d2 = dx * dx + dy * dy;
        if (best < 0 || d2 < best_d2) { best = i; best_d2 = d2; }
    }
    return best;
}


void fire_weapon(World& w, int wi, const tg::Vec2& aim) {
    const auto& inst = w.weapons[static_cast<std::size_t>(wi)];
    const auto& def = kWeaponDefs[static_cast<int>(inst.kind)];
    const int dmg = static_cast<int>(def.base_dmg + def.dmg_per_lvl * (inst.level - 1));
    if (def.is_melee) return;  // 近战由 weapon_system 直接调用 combat hit
    const float speed = def.speed;
    const float life = def.life;
    auto spawn_bullet = [&](tg::Vec2 dir, float speed_mul_v) {
        if (std::sqrt(dir.x * dir.x + dir.y * dir.y) < 1e-4f) return;
        const float d = std::sqrt(dir.x * dir.x + dir.y * dir.y);
        const int b = push_entity(w, kBullet, w.pos[w.player],
                                  tg::Vec2{dir.x / d * speed * speed_mul_v,
                                           dir.y / d * speed * speed_mul_v},
                                  0);
        w.bullet[b].dmg = dmg;
        w.bullet[b].life = life;
        w.bullet[b].weapon = static_cast<std::int8_t>(inst.kind);
    };
    switch (inst.kind) {
        case WeaponKind::LineShot: {
            spawn_bullet(aim, 1.0f);
            break;
        }
        case WeaponKind::SpreadShot: {
            // ±20° 扇形 3 弹
            const float ang = std::atan2(aim.y, aim.x);
            const float spread = 20.0f * 3.14159265f / 180.0f;
            spawn_bullet(tg::Vec2{std::cos(ang - spread), std::sin(ang - spread)}, 1.0f);
            spawn_bullet(aim, 1.0f);
            spawn_bullet(tg::Vec2{std::cos(ang + spread), std::sin(ang + spread)}, 1.0f);
            break;
        }
        case WeaponKind::RadialBurst: {
            // 8 方向全射
            for (int k = 0; k < 8; ++k) {
                const float a = k * (2.0f * 3.14159265f / 8.0f);
                spawn_bullet(tg::Vec2{std::cos(a), std::sin(a)}, 1.0f);
            }
            break;
        }
        case WeaponKind::HomingShot: {
            spawn_bullet(aim, 1.0f);
            break;
        }
        default: break;
    }
}

// 应用武器等级 → cd
float weapon_cd(const WeaponInst& inst) {
    const auto& def = kWeaponDefs[static_cast<int>(inst.kind)];
    float cd = def.base_cd;
    for (int k = 1; k < inst.level; ++k) cd *= def.cd_mul_per_lvl;
    return cd;
}

}  // namespace

float weapon_cd_of(const WeaponInst& inst) noexcept {
    const auto& def = kWeaponDefs[static_cast<int>(inst.kind)];
    float cd = def.base_cd;
    for (int k = 1; k < inst.level; ++k) cd *= def.cd_mul_per_lvl;
    return cd;
}

void reset_round(World& w) {
    w.pos.clear(); w.vel.clear(); w.hp.clear(); w.tag.clear();
    w.bullet.clear(); w.oxp.clear(); w.pickup.clear();
    w.player = -1; w.fire_cd = 0.0f; w.invuln_timer = 0.0f; w.spawn_cd = kSpawnInterval;
    w.dash_timer = 0.0f; w.dash_cd = 0.0f;
    w.boss_idx = -1; w.boss_hp = 0.0f; w.boss_teleport_cd = 0.0f;
    w.levelup_flash_timer = 0.0f;
    w.weapons.clear(); w.passives.clear();
    w.speed_mul = 1.0f; w.speed_timer = 0.0f;
    for (int i = 0; i < kKillFxMax; ++i) w.kill_fx[i].timer = 0.0f;
    w.wave_clear_msg = 0;
    w.wave_clear_msg_timer = 0.0f;
    w.player_anim = World::PlayerAnim::Idle;
    w.player_anim_timer = 0.0f;
    w.boss_anim = World::BossAnim::Idle;
    w.boss_anim_timer = 0.0f;

    const tg::Vec2 c{(kMapW * 0.5f) * kTile, (kMapH * 0.5f) * kTile};
    w.player = push_entity(w, kPlayer, c, tg::Vec2{0.0f, 0.0f}, 100);
    w.hp[w.player] = player_max_hp(w);
    // 起始武器：LineShot
    WeaponInst start; start.kind = kStartingWeapon; start.level = 1;
    start.cd_remaining = 0.0f;
    w.weapons.push_back(start);

    if (g_obstacles.empty()) generate_obstacles();
}

void steer_system(World& w, const Input& in) {
    // 冲刺计时（与 phase 无关，但 Paused/Dead 在 step() 整体跳过）
    if (w.dash_cd > 0.0f) w.dash_cd = std::max(0.0f, w.dash_cd - kDt);
    if (w.dash_timer > 0.0f) w.dash_timer = std::max(0.0f, w.dash_timer - kDt);
    if (in.dash && w.dash_cd <= 0.0f && w.dash_timer <= 0.0f) {
        w.dash_timer = kDashDuration;
        w.dash_cd = kDashCooldown;
        ++w.dashes;
    }
    // Boss 瞬移 AI
    if (w.boss_idx >= 0) {
        w.boss_teleport_cd -= kDt;
        if (w.boss_teleport_cd <= 0.0f && w.player >= 0) {
            w.boss_teleport_cd = kBossTeleportInterval;
            // 瞬移触发 → Boss Attack 动画（0.6s 内播放 attack clip）
            w.boss_anim = World::BossAnim::Attack;
            w.boss_anim_timer = std::max(w.boss_anim_timer, 0.6f);
            // 瞬移到玩家附近 8 tile 的随机方向
            const float ang = static_cast<float>(w.rng.next_double() * 2.0 * 3.14159265);
            const float dist = 8.0f * kTile;
            tg::Vec2 np{w.pos[w.player].x + std::cos(ang) * dist,
                        w.pos[w.player].y + std::sin(ang) * dist};
            // 找一个不与墙重叠的位置（最多 8 次尝试）
            for (int t = 0; t < 8; ++t) {
                const int tx = static_cast<int>(np.x / kTile);
                const int ty = static_cast<int>(np.y / kTile);
                if (!room_solid(tx, ty)) break;
                np.x = w.pos[w.player].x + std::cos(ang + t * 0.3f) * dist;
                np.y = w.pos[w.player].y + std::sin(ang + t * 0.3f) * dist;
            }
            w.pos[w.boss_idx] = np;
        }
    }

    // 玩家速度
    const float len = std::sqrt(in.mx * in.mx + in.my * in.my);
    const tg::Vec2 dir = len > 1e-4f ? tg::Vec2{in.mx / len, in.my / len} : tg::Vec2{};
    // 玩家动画状态：移动 → Walk（main 层据此切 walk clip）
    // 优先级：Die > Hurt > Attack > Walk > Idle（由 buff_system 把临时状态归 Idle）
    if (w.player >= 0 && w.player_anim != World::PlayerAnim::Die) {
        const float vx = w.vel[w.player].x;
        const float vy = w.vel[w.player].y;
        const bool moving = (vx * vx + vy * vy) > 25.0f;  // >5 px/s
        if (moving) {
            w.player_anim = World::PlayerAnim::Walk;
            w.player_anim_timer = 0.0f;  // walk 是 loop，timer 仅用于"何时停止"
        } else if (w.player_anim == World::PlayerAnim::Walk) {
            w.player_anim = World::PlayerAnim::Idle;
        }
    }
    float speed_mul_total = w.speed_mul;
    for (const auto& p : w.passives)
        if (p.kind == PassiveKind::SpeedUp) speed_mul_total *= (1.0f + 0.10f * p.level);
    const float speed = kPlayerSpeed * (w.dash_timer > 0.0f ? kDashSpeedMul : 1.0f) * speed_mul_total;
    for (int i = 0; i < count(w); ++i) {
        if (w.tag[i] == kPlayer) {
            w.vel[i] = tg::Vec2{dir.x * speed, dir.y * speed};
            continue;
        }
        if (w.tag[i] != kEnemy || w.player < 0) continue;
        const float dx = w.pos[w.player].x - w.pos[i].x, dy = w.pos[w.player].y - w.pos[i].y;
        const float d = std::sqrt(dx * dx + dy * dy);
        w.vel[i] = d > 1e-4f ? tg::Vec2{dx / d * kEnemySpeed, dy / d * kEnemySpeed} : tg::Vec2{};
    }
}

void move_system(World& w, const tg::SolidGridView* views, int nviews, float dt) {
    for (int i = 0; i < count(w); ++i) {
        const float h = half_of(w.tag[i]);
        const tg::SweepResult r = tg::sweep_move(
            views, nviews, rect_of(w, i), tg::Vec2{w.vel[i].x * dt, w.vel[i].y * dt});
        w.pos[i] = tg::Vec2{r.box.x + h, r.box.y + h};
        if (w.tag[i] == kBullet && (r.blocked_x || r.blocked_y)) w.bullet[i].life = 0.0f;
    }
}

void weapon_system(World& w, float /*dt*/, const Input& in) {
    if (w.player < 0) return;
    // 跟踪本步是否有武器开火（开火 → 玩家 Attack 动画）
    bool fired_this_step = false;
    // 决定瞄准方向：aim 非零 → 玩家→鼠标；否则 → 最近敌
    tg::Vec2 aim = {};
    {
        const float al = std::sqrt(in.aim_x * in.aim_x + in.aim_y * in.aim_y);
        if (al > 1e-4f) {
            aim = tg::Vec2{in.aim_x / al, in.aim_y / al};
        } else {
            const int t = nearest_enemy(w);
            if (t >= 0) {
                const float dx = w.pos[t].x - w.pos[w.player].x;
                const float dy = w.pos[t].y - w.pos[w.player].y;
                const float d = std::sqrt(dx * dx + dy * dy);
                if (d > 1e-4f) aim = tg::Vec2{dx / d, dy / d};
            }
        }
    }
    for (size_t wi = 0; wi < w.weapons.size(); ++wi) {
        auto& inst = w.weapons[wi];
        if (inst.cd_remaining > 0.0f) {
            inst.cd_remaining = std::max(0.0f, inst.cd_remaining - kDt);
            continue;
        }
        const auto& def = kWeaponDefs[static_cast<int>(inst.kind)];
        if (def.is_melee) {
            // 近战命中：ArcSlash 24px / ShieldBash 16px
            const float radius = (inst.kind == WeaponKind::ArcSlash) ? 24.0f : 16.0f;
            const int dmg = static_cast<int>(def.base_dmg + def.dmg_per_lvl * (inst.level - 1));
            for (int e = 0; e < count(w); ++e) {
                const std::uint8_t t = w.tag[e];
                if ((t != kEnemy && t != kBoss) || w.hp[e] <= 0) continue;
                if (!close(w.pos[e], w.pos[w.player], radius)) continue;
                const int before = w.hp[e];
                w.hp[e] = before - dmg;
                if (t == kBoss) w.boss_hp -= dmg;
                if (before > 0 && w.hp[e] <= 0) {
                    ++w.kills;
                    if (t == kEnemy) drop_orb(w, w.pos[e], 1);
                }
                if (inst.kind == WeaponKind::ShieldBash && w.player >= 0) {
                    const float dx = w.pos[e].x - w.pos[w.player].x;
                    const float dy = w.pos[e].y - w.pos[w.player].y;
                    const float d = std::sqrt(dx * dx + dy * dy);
                    if (d > 1e-4f) {
                        w.vel[e] = tg::Vec2{dx / d * 200.0f, dy / d * 200.0f};
                    }
                    // ShieldBash 触发短无敌
                    w.invuln_timer = std::max(w.invuln_timer, 0.4f);
                }
            }
        } else {
            // 远程武器：只有在有瞄准方向时才真发弹（fire_weapon 内部对 aim={0,0} 早退）
            const bool will_fire = (aim.x != 0.0f || aim.y != 0.0f);
            if (will_fire) {
                fire_weapon(w, static_cast<int>(wi), aim);
                fired_this_step = true;
            }
        }
        inst.cd_remaining = weapon_cd(inst);
    }
    if (fired_this_step && w.player >= 0) {
        w.player_anim = World::PlayerAnim::Attack;
        // 攻击动画覆盖持续时长（按帧时长的小倍数算，让动画能完整播放几帧）
        w.player_anim_timer = std::max(w.player_anim_timer, 0.30f);
    }
}

void spawn_system(World& w, float dt) {
    // 波次推进
    w.wave_timer -= dt;
    if (w.wave_timer <= 0.0f) {
        if (w.wave == 0) {
            // 准备阶段结束 → 第 1 波
            w.wave = 1;
            w.wave_timer = kWaveInterval;
        } else {
            w.wave += 1;
            w.wave_timer = kWaveInterval;
            // 波次清空提示：仅 wave >= 2 弹（首次进入 wave 1 不弹）
            w.wave_clear_msg = w.wave;
            w.wave_clear_msg_timer = kWaveClearMsgLife;
        }
    }

    // 准备阶段不刷怪
    if (w.wave == 0) return;

    // 普通敌按节奏刷（密度随 wave 增长，封顶每波 40）
    w.spawn_cd -= dt;
    if (w.spawn_cd <= 0.0f) {
        w.spawn_cd = kSpawnInterval;
        // 该波应刷数量：min(8 + wave*2, 40)。已刷的累计到目标就不再追加。
        const int wave_target = std::min(8 + w.wave * 2, 40);
        int enemies_now = 0;
        for (int i = 0; i < count(w); ++i)
            if (w.tag[i] == kEnemy || w.tag[i] == kBoss) ++enemies_now;
        // 一拍刷 1 个（节拍不变）；密度靠同波时长内累计多次刷到达成
        if (enemies_now < kMaxEnemies && w.spawned < wave_target * w.wave) {
            // 地图无外墙 → 沿四边 1-tile 偏移随机刷一个（玩家视野里能看到
            // 敌从地平线走来；不在 moss 上就再补一次 stack）。
            tg::Vec2 p{};
            for (int tries = 0; tries < 8; ++tries) {
                const int side = w.rng.next_int(0, 3);
                const bool vertical = side >= 2;
                const int along = w.rng.next_int(0,
                                                  vertical ? kMapH : kMapW);
                const int tx = vertical
                                   ? (side == 2 ? 0 : kMapW - 1) : along;
                const int ty = vertical
                                   ? along
                                   : (side == 0 ? 0 : kMapH - 1);
                p = tg::Vec2{(tx + 0.5f) * kTile, (ty + 0.5f) * kTile};
                if (!room_solid(tx, ty)) break;
            }
            push_entity(w, kEnemy, p, tg::Vec2{0.0f, 0.0f}, kEnemyHp);
            ++w.spawned;
        }
    }

    // Boss 触发：每 kWaveBossEvery 波（4/8/12…）开局第 5 秒刷
    if (w.boss_idx < 0 && w.wave > 0 && w.wave % kWaveBossEvery == 0 &&
        w.wave_timer <= kWaveInterval - 5.0f) {
        // 找玩家视野内边缘作为 Boss 出生点（边角都试 8 次避 moss）
        tg::Vec2 p{};
        for (int tries = 0; tries < 8; ++tries) {
            const int side = w.rng.next_int(0, 3);
            const bool vertical = side >= 2;
            const int along = w.rng.next_int(0, vertical ? kMapH : kMapW);
            const int tx = vertical ? (side == 2 ? 0 : kMapW - 1) : along;
            const int ty = vertical ? along : (side == 0 ? 0 : kMapH - 1);
            p = tg::Vec2{(tx + 0.5f) * kTile, (ty + 0.5f) * kTile};
            if (!room_solid(tx, ty)) break;
        }
        w.boss_idx = push_entity(w, kBoss, p, tg::Vec2{0.0f, 0.0f}, static_cast<int>(kBossHp));
        w.boss_hp = kBossHp;
        w.boss_teleport_cd = kBossTeleportInterval;
    }

    // 道具刷：每 15-20 秒 1 个（随机间隔），由 w.pickup_step 控制（reset 时归 0）
    w.pickup_step += 1;
    if (w.pickup_step >= 900 + static_cast<int>(w.rng.next_int(0, 300))) {
        w.pickup_step = 0;
        if (w.player >= 0) {
            // 在玩家附近 8-16 tile 范围内刷
            const float ang = static_cast<float>(w.rng.next_double() * 2.0 * 3.14159265);
            const float dist = (8.0f + static_cast<float>(w.rng.next_double() * 8.0)) * kTile;
            tg::Vec2 np{w.pos[w.player].x + std::cos(ang) * dist,
                        w.pos[w.player].y + std::sin(ang) * dist};
            // 找一个不与墙重叠的位置
            for (int t = 0; t < 8; ++t) {
                const int tx = static_cast<int>(np.x / kTile);
                const int ty = static_cast<int>(np.y / kTile);
                if (!room_solid(tx, ty)) break;
                np.x = w.pos[w.player].x + std::cos(ang + t * 0.4f) * dist;
                np.y = w.pos[w.player].y + std::sin(ang + t * 0.4f) * dist;
            }
            const int pk = w.rng.next_int(0, 99);
            PickupKind kind = PickupKind::Heal;
            if (pk < 60) kind = PickupKind::Heal;
            else if (pk < 90) kind = PickupKind::SpeedBuff;
            else kind = PickupKind::InvulnBuff;
            const int idx = push_entity(w, kPickup, np, tg::Vec2{0.0f, 0.0f}, 0);
            w.pickup[idx].kind = kind;
            w.pickup[idx].value = 25.0f;
            w.pickup[idx].ttl = kPickupTtl;
        }
    }
}

void combat_system(World& w, float dt) {
    const int n = count(w);
    // 子弹 × 敌人/Boss：命中即扣血、子弹消失
    for (int b = 0; b < n; ++b) {
        if (w.tag[b] != kBullet || w.bullet[b].life <= 0.0f) continue;
        for (int e = 0; e < n; ++e) {
            const std::uint8_t t = w.tag[e];
            if ((t != kEnemy && t != kBoss) || w.hp[e] <= 0) continue;
            if (!close(w.pos[b], w.pos[e], kHitDist)) continue;
            const int before = w.hp[e];
            w.hp[e] = before - w.bullet[b].dmg;
            if (t == kBoss) w.boss_hp -= w.bullet[b].dmg;
            w.bullet[b].life = 0.0f;
            if (before > 0 && w.hp[e] <= 0) {
                ++w.kills;
                if (t == kEnemy) drop_orb(w, w.pos[e], 1);
                push_kill_fx(w, w.pos[e]);
            }
            break;
        }
    }
    // 敌 × 玩家：接触扣血 + 无敌帧
    if (w.invuln_timer > 0.0f) w.invuln_timer -= dt;
    if (w.player >= 0 && w.invuln_timer <= 0.0f && w.dash_timer <= 0.0f) {
        for (int e = 0; e < count(w); ++e) {
            const std::uint8_t t = w.tag[e];
            if ((t != kEnemy && t != kBoss) || w.hp[e] <= 0) continue;
            if (!close(w.pos[e], w.pos[w.player], kTouchDist)) continue;
            int dmg = kEnemyDmg;
            if (t == kBoss) dmg = static_cast<int>(player_max_hp(w) * kBossTouchRatio);
            w.hp[w.player] -= dmg;
            w.invuln_timer = kInvuln;
            // 玩家受击 → Hurt 动画（覆盖时长按 invuln 帧长度）
            w.player_anim = World::PlayerAnim::Hurt;
            w.player_anim_timer = std::max(w.player_anim_timer, kInvuln * 0.6f);
            // Boss 撞后将 Boss 推离玩家 4 tile
            if (t == kBoss) {
                const float dx = w.pos[e].x - w.pos[w.player].x;
                const float dy = w.pos[e].y - w.pos[w.player].y;
                const float d = std::sqrt(dx * dx + dy * dy);
                if (d > 1e-4f) {
                    w.pos[e] = tg::Vec2{w.pos[w.player].x + dx / d * 4.0f * kTile,
                                        w.pos[w.player].y + dy / d * 4.0f * kTile};
                }
            }
            break;
        }
    }
    // Boss 击杀 → 双倍经验球 + Heal 道具
    if (w.boss_idx >= 0 && w.boss_hp <= 0.0f) {
        const tg::Vec2 bp = w.pos[w.boss_idx];
        // 标记 Boss 待回收
        w.hp[w.boss_idx] = 0;
        // 5 颗大 orb
        for (int k = 0; k < 5; ++k) {
            const float ang = k * (2.0f * 3.14159265f / 5.0f);
            drop_orb(w, tg::Vec2{bp.x + std::cos(ang) * 24.0f,
                                 bp.y + std::sin(ang) * 24.0f}, 2);
        }
        // 1 个 Heal 道具
        const int idx = push_entity(w, kPickup, bp, tg::Vec2{0.0f, 0.0f}, 0);
        w.pickup[idx].kind = PickupKind::Heal;
        w.pickup[idx].value = 25.0f;
        w.pickup[idx].ttl = kPickupTtl;
        w.boss_idx = -1;
        w.boss_hp = 0.0f;
    }
    // 死亡重置：kills/pickups/level/deaths 是元进度
    if (w.player >= 0 && w.hp[w.player] <= 0) {
        ++w.deaths;
        reset_round(w);
    }
}

void pickup_system(World& w) {
    if (w.player < 0) return;
    const float pickup_r = player_pickup_radius(w);
    // orb 磁场：远距离的 orb 朝玩家方向加速，让站立玩家也能拾到远处掉落的经验球
    // （子弹从远处击杀 → orb 远离玩家 → 没磁吸就永远捡不到）。磁吸强度按距离
    // 线性，>2×pickup_r 时无吸力（防止远处 orb 全部瞬移过来）。
    for (int i = 0; i < count(w); ++i) {
        if (w.tag[i] != kOrb || w.oxp[i].taken) continue;
        const float dx = w.pos[w.player].x - w.pos[i].x;
        const float dy = w.pos[w.player].y - w.pos[i].y;
        const float d = std::sqrt(dx * dx + dy * dy);
        if (d < pickup_r) {
            w.oxp[i].taken = true;
            w.xp += w.oxp[i].value;
            ++w.pickups;
        } else if (d < 600.0f && d > 1e-3f) {
            // 磁场吸力：恒定强磁吸（接近 pickup_r 时不会因公式 (d-pickup_r)*K 衰减
            // 而卡在边缘——玩家站立也能稳定拾到远处击杀留下的 orb）
            const float pull = 600.0f;
            w.vel[i].x = dx / d * pull;
            w.vel[i].y = dy / d * pull;
        }
    }
    // 道具：直接落在玩家身上才拾取（不磁吸——避免远处道具乱飞）
    for (int i = 0; i < count(w); ++i) {
        if (w.tag[i] != kPickup) continue;
        if (!close(w.pos[i], w.pos[w.player], pickup_r)) continue;
        switch (w.pickup[i].kind) {
            case PickupKind::Heal:
                w.hp[w.player] = std::min(player_max_hp(w),
                                          w.hp[w.player] + static_cast<int>(w.pickup[i].value));
                break;
            case PickupKind::SpeedBuff:
                w.speed_mul = kSpeedBuffMul;
                w.speed_timer = kSpeedBuffDuration;
                break;
            case PickupKind::InvulnBuff:
                w.invuln_timer = std::max(w.invuln_timer, kInvulnBuffDuration);
                break;
        }
        w.pickup[i].ttl = 0.0f;
        ++w.pickups;
    }
    // 升级链（LevelUp 期间冻结 XP 累积）
    if (w.phase != Phase::LevelUp) {
        while (w.xp >= xp_need(w.level)) {
            w.xp -= xp_need(w.level);
            w.level += 1;
            // 升级自动回血 20% max_hp
            w.hp[w.player] = std::min(player_max_hp(w),
                                      w.hp[w.player] +
                                          static_cast<int>(player_max_hp(w) * kLevelHealRatio));
            w.levelup_flash_timer = 0.2f;
            offer_levelup_cards(w);
            w.phase = Phase::LevelUp;
        }
    }
}

void buff_system(World& w, float dt) {
    if (w.speed_timer > 0.0f) {
        w.speed_timer = std::max(0.0f, w.speed_timer - dt);
        if (w.speed_timer <= 0.0f) w.speed_mul = 1.0f;
    }
    if (w.levelup_flash_timer > 0.0f)
        w.levelup_flash_timer = std::max(0.0f, w.levelup_flash_timer - dt);
    // Pickup ttl 衰减
    for (int i = 0; i < count(w); ++i)
        if (w.tag[i] == kPickup)
            w.pickup[i].ttl = std::max(0.0f, w.pickup[i].ttl - dt);
    // 击杀爆点衰减
    for (int i = 0; i < kKillFxMax; ++i)
        if (w.kill_fx[i].timer > 0.0f)
            w.kill_fx[i].timer = std::max(0.0f, w.kill_fx[i].timer - dt);
    // 波次清空提示衰减
    if (w.wave_clear_msg_timer > 0.0f)
        w.wave_clear_msg_timer = std::max(0.0f, w.wave_clear_msg_timer - dt);
    // 玩家 / Boss 动画状态计时器衰减（非 loop clip 播放完毕后回 Idle）
    if (w.player_anim_timer > 0.0f) {
        w.player_anim_timer = std::max(0.0f, w.player_anim_timer - dt);
        if (w.player_anim_timer == 0.0f &&
            (w.player_anim == World::PlayerAnim::Attack ||
             w.player_anim == World::PlayerAnim::Hurt)) {
            w.player_anim = World::PlayerAnim::Idle;
        }
    }
    if (w.boss_anim_timer > 0.0f) {
        w.boss_anim_timer = std::max(0.0f, w.boss_anim_timer - dt);
        if (w.boss_anim_timer == 0.0f && w.boss_anim == World::BossAnim::Attack) {
            w.boss_anim = World::BossAnim::Idle;
        }
    }
}

void cull_system(World& w) {
    for (int i = count(w) - 1; i >= 0; --i) {
        const std::uint8_t t = w.tag[i];
        const bool dead =
            (t == kEnemy && w.hp[i] <= 0) ||
            (t == kBoss && w.hp[i] <= 0) ||
            (t == kBullet && w.bullet[i].life <= 0.0f) ||
            (t == kOrb && w.oxp[i].taken) ||
            (t == kPickup && w.pickup[i].ttl <= 0.0f);
        if (dead) remove_at(w, i);
    }
}

void offer_levelup_cards(World& w) {
    int n_weapon = 0, n_passive = 0;
    {
        const float r = static_cast<float>(w.rng.next_double());
        if (r < 0.6f) { n_weapon = 3; n_passive = 0; }
        else if (r < 0.85f) { n_weapon = 2; n_passive = 1; }
        else { n_weapon = 1; n_passive = 2; }
    }
    bool used_weapon[kWeaponCount] = {};
    bool used_passive[kPassiveCount] = {};
    auto weighted_pick = [&](bool is_weapon) -> int {
        std::vector<int> pool;
        std::vector<float> weights;
        const int n = is_weapon ? kWeaponCount : kPassiveCount;
        const bool* used = is_weapon ? used_weapon : used_passive;
        for (int k = 0; k < n; ++k) {
            if (used[k]) continue;
            bool owned = false;
            int cur_lvl = 0;
            if (is_weapon) {
                for (const auto& wi : w.weapons)
                    if (static_cast<int>(wi.kind) == k) { owned = true; cur_lvl = wi.level; break; }
            } else {
                for (const auto& pi : w.passives)
                    if (static_cast<int>(pi.kind) == k) { owned = true; cur_lvl = pi.level; break; }
            }
            if (owned && cur_lvl >= kLevelMax) continue;
            pool.push_back(k);
            weights.push_back(owned ? 1.0f : 3.0f);
        }
        if (pool.empty()) return -1;
        float total = 0.0f;
        for (float ww : weights) total += ww;
        float r = static_cast<float>(w.rng.next_double() * total);
        for (size_t i = 0; i < pool.size(); ++i) {
            r -= weights[i];
            if (r <= 0.0f) return pool[i];
        }
        return pool.back();
    };
    auto fill_card = [&](int slot, bool is_weapon, int kind) {
        w.lvlup_cards[static_cast<size_t>(slot)].is_weapon = is_weapon;
        w.lvlup_cards[static_cast<size_t>(slot)].kind = static_cast<std::uint8_t>(kind);
        w.lvlup_cards[static_cast<size_t>(slot)].name =
            is_weapon ? kWeaponDefs[kind].name : kPassiveDefs[kind].name;
        int cur_lvl = 0;
        if (is_weapon) {
            for (const auto& wi : w.weapons)
                if (static_cast<int>(wi.kind) == kind) { cur_lvl = wi.level; break; }
        } else {
            for (const auto& pi : w.passives)
                if (static_cast<int>(pi.kind) == kind) { cur_lvl = pi.level; break; }
        }
        w.lvlup_cards[static_cast<size_t>(slot)].current_level = cur_lvl;
        if (is_weapon) used_weapon[kind] = true; else used_passive[kind] = true;
    };
    int filled = 0;
    while (n_weapon > 0 && filled < kCardChoices) {
        const int k = weighted_pick(true);
        if (k < 0) break;
        fill_card(filled, true, k);
        ++filled;
        --n_weapon;
    }
    while (n_passive > 0 && filled < kCardChoices) {
        const int k = weighted_pick(false);
        if (k < 0) break;
        fill_card(filled, false, k);
        ++filled;
        --n_passive;
    }
    while (filled < kCardChoices) {
        const int k = weighted_pick(true);
        if (k < 0) {
            const int kp = weighted_pick(false);
            if (kp < 0) break;
            fill_card(filled, false, kp);
        } else {
            fill_card(filled, true, k);
        }
        ++filled;
    }
}

void step(World& w, const Input& in, const tg::SolidGridView* views, int nviews,
          float dt) {
    if (in.restart) { reset_round(w); w.phase = Phase::Playing; }
    // Paused / Dead 期间 step 整体跳过（除 LevelUp 期间 step 仍推进但 XP 冻结）
    if (w.phase == Phase::Paused || w.phase == Phase::Dead) {
        w.steps += 1;  // 步数仍累计（与 Paused 计时分离由 main 决定）
        return;
    }
    steer_system(w, in);
    weapon_system(w, dt, in);
    spawn_system(w, dt);
    move_system(w, views, nviews, dt);
    combat_system(w, dt);
    pickup_system(w);
    buff_system(w, dt);
    cull_system(w);
    // LevelUp 期间若玩家选了卡 → 退出 LevelUp
    if (w.phase == Phase::LevelUp && in.card_pick >= 1 && in.card_pick <= kCardChoices) {
        const int pick = in.card_pick - 1;
        const CardOffer& co = w.lvlup_cards[static_cast<size_t>(pick)];
        if (co.is_weapon) {
            bool found = false;
            for (auto& wi : w.weapons)
                if (static_cast<int>(wi.kind) == co.kind) { wi.level = std::min(kLevelMax, wi.level + 1); found = true; break; }
            if (!found) {
                WeaponInst nw; nw.kind = static_cast<WeaponKind>(co.kind); nw.level = 1;
                nw.cd_remaining = 0.0f;
                w.weapons.push_back(nw);
            }
        } else {
            bool found = false;
            for (auto& pi : w.passives)
                if (static_cast<int>(pi.kind) == co.kind) { pi.level = std::min(kLevelMax, pi.level + 1); found = true; break; }
            if (!found) {
                PassiveInst np; np.kind = static_cast<PassiveKind>(co.kind); np.level = 1;
                w.passives.push_back(np);
            }
        }
        w.phase = Phase::Playing;
    }
    w.steps += 1;
}

int push_test_pickup_helper(World& w, tg::Vec2 p, PickupKind kind) {
    const int idx = push_entity(w, kPickup, p, tg::Vec2{0.0f, 0.0f}, 0);
    w.pickup[idx].kind = kind;
    w.pickup[idx].value = 25.0f;
    w.pickup[idx].ttl = swarm::kPickupTtl;
    return idx;
}

std::string summary(const World& w) {
    char buf[320];
    std::snprintf(buf, sizeof buf,
                  "steps=%lld spawned=%d kills=%d pickups=%d level=%d phase=%d wave=%d "
                  "player_hp=%d deaths=%d dashes=%d weapons=%zu passives=%zu "
                  "boss=%d boss_hp=%.0f seed=%llu",
                  static_cast<long long>(w.steps), w.spawned, w.kills, w.pickups,
                  w.level, static_cast<int>(w.phase), w.wave, player_hp(w),
                  w.deaths, w.dashes, w.weapons.size(), w.passives.size(),
                  w.boss_idx >= 0 ? 1 : 0, w.boss_hp,
                  static_cast<unsigned long long>(kSeed));
    return std::string(buf);
}

}  // namespace swarm