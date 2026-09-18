// main.cpp —— swarm 范例的窗口层：键盘、固定步、Phase UI、按武器/道具种类选贴图。
//
// 渲染策略：
//   - tile + 实体：camera 跟随玩家，色块 + 贴图（按 weapon_kind / pickup_kind 选贴图）
//   - HUD：HP/XP/Wave/Time/Weapon/Passives 列表
//   - Phase 覆盖：升级白闪 → LevelUp 抽卡 → Paused → Dead
//
// R 在 main 层独立处理（不通过 sim.step），保证 Dead 时也能重开。
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include <raylib.h>

#include "../common/harness.hpp"
#include "trogue/terrain.hpp"
#include "sim.hpp"

namespace {

constexpr int kWinW = 800;
constexpr int kWinH = 600;
constexpr float kShotSeconds = 2.0f;
constexpr float kDefaultSeconds = 5.0f;

swarm::Input read_input(const swarm::World& w, const Camera2D& cam) {
    swarm::Input in;
    if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) in.mx -= 1.0f;
    if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) in.mx += 1.0f;
    if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP)) in.my -= 1.0f;
    if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) in.my += 1.0f;
    in.dash = IsKeyPressed(KEY_LEFT_SHIFT) || IsKeyPressed(KEY_RIGHT_SHIFT);
    in.pause = IsKeyPressed(KEY_ESCAPE);
    // 鼠标世界坐标；右键按住 → 强制瞄准方向
    const Vector2 mp = GetMousePosition();
    const Vector2 world = GetScreenToWorld2D(mp, cam);
    const float dx = world.x - (w.player >= 0 ? w.pos[w.player].x : 0.0f);
    const float dy = world.y - (w.player >= 0 ? w.pos[w.player].y : 0.0f);
    const float dlen = std::sqrt(dx * dx + dy * dy);
    if (IsMouseButtonDown(MOUSE_RIGHT_BUTTON) && dlen > 1e-3f) {
        in.aim_x = dx / dlen;
        in.aim_y = dy / dlen;
    }
    if (IsKeyPressed(KEY_ONE))  in.card_pick = 1;
    if (IsKeyPressed(KEY_TWO))  in.card_pick = 2;
    if (IsKeyPressed(KEY_THREE)) in.card_pick = 3;
    return in;
}

// ── 贴图集合 ──
// 失败非零退出：缺哪张都说明 assets/ 不全，需要重新跑 import。
// 动画：每个 AnimationAsset 持有一个 AnimationPlayer（绑定 asset.view）。
// AnimationSet 不公开 textures 字段，所以同时存一份 GPU 贴图给 main 直接画。
//
// Pivot（视觉锚点）：每个 sprite 都附一份 alpha-bbox 中心，作为「内容中心」在
// cell 内的位置。draw 时把 pivot 钉在 entity 中心，而不是钉 cell 中心；这样
// 静态贴图与动画帧在视觉上对齐（首帧 bbox 设计时与静态贴图匹配），后续动画
// 帧的轻微位移（attack lunge / hurt recoil / dash 脚步）保留但不漂走。
struct Sprite { Texture2D tex{}; tg::Vec2 pivot{0.0f, 0.0f}; };
struct Anim {
    std::optional<tg::AnimationAsset> asset;  // optional 让 Sprites 可默认构造
    tg::AnimationPlayer player;
    Texture2D sheet{};             // 同 sprite sheet（与 JSON 的 textures[0] 对应）
    tg::Vec2 pivot{0.0f, 0.0f};    // 首帧 alpha-bbox 中心（cell 内坐标）
    bool loaded = false;
};
struct Sprites {
    Sprite hero, hero_dash, enemy, enemy_demon, boss;
    // 武器弹贴图（按 WeaponKind 选）
    Sprite bullet_lineshot, bullet_spread, bullet_radial, bullet_homing;
    // 道具贴图（按 PickupKind 选）
    Sprite orb;          // 默认蓝色水晶（向后兼容）
    Sprite orb_crystal;  // 替代绿水晶（保留选择）
    Sprite pickup_heal, pickup_speed, pickup_invuln;
    // 特效（近战武器命中时的瞬时贴图，本例 main 层简化不画）
    Sprite effect_slash, effect_bash;
    // 动画资源（tro-animations v1 JSON）
    Anim anim_hero_walk;
    Anim anim_hero_attack;
    Anim anim_hero_hurt;
    Anim anim_hero_die;
    Anim anim_hero_dash;
    Anim anim_goblin_walk;
    Anim anim_demon_hurt;
    Anim anim_boss_idle;
    // HUD 字体（系统 Liberation Sans Bold，覆盖 raylib 默认字体以支持更多字符）
    Font font{};
    bool ok = false;
};

// 计算 (rx, ry, rw, rh) 区域内非透明像素的 bbox 中心，作为该帧的视觉 pivot。
// 全透明时退回 cell 几何中心。仅首帧调用一次（资源加载时）。
tg::Vec2 compute_alpha_pivot(const char* path, int rx, int ry, int rw, int rh) {
    Image img = LoadImage(path);
    tg::Vec2 p{static_cast<float>(rw) * 0.5f, static_cast<float>(rh) * 0.5f};
    if (img.data && img.width > 0) {
        int min_x = rw, min_y = rh, max_x = -1, max_y = -1;
        const auto* d = static_cast<const unsigned char*>(img.data);
        for (int dy = 0; dy < rh; ++dy) {
            const int py = ry + dy;
            if (py < 0 || py >= img.height) continue;
            for (int dx = 0; dx < rw; ++dx) {
                const int px = rx + dx;
                if (px < 0 || px >= img.width) continue;
                const unsigned char a = d[(py * img.width + px) * 4 + 3];
                if (a > 16) {
                    if (px < min_x) min_x = px;
                    if (py < min_y) min_y = py;
                    if (px > max_x) max_x = px;
                    if (py > max_y) max_y = py;
                }
            }
        }
        if (max_x >= 0) {
            p.x = (min_x + max_x + 1 - rx) * 0.5f;
            p.y = (min_y + max_y + 1 - ry) * 0.5f;
        }
    }
    UnloadImage(img);
    return p;
}
Sprites load_sprites() {
    Sprites s;
    auto load_sprite = [&](Sprite& sp, const char* path) -> bool {
        sp.tex = LoadTexture(path);
        if (sp.tex.id != 0) SetTextureFilter(sp.tex, TEXTURE_FILTER_POINT);
        sp.pivot = compute_alpha_pivot(path, 0, 0, sp.tex.width, sp.tex.height);
        return sp.tex.id != 0;
    };
    auto load_anim = [&](Anim& a, const char* json_path, const char* clip,
                         const char* sheet_path, int cell_size) -> bool {
        auto asset = tg::AnimationAsset::load(json_path);
        if (!asset) {
            std::fprintf(stderr, "[swarm] 动画加载失败: %s (%s)\n", json_path,
                         asset.error().message.c_str());
            return false;
        }
        a.asset = std::move(*asset);
        a.player.bind((*a.asset).view());
        if (!a.player.play(clip)) {
            std::fprintf(stderr, "[swarm] 动画 clip 缺失: %s -> %s\n", json_path, clip);
            return false;
        }
        a.sheet = LoadTexture(sheet_path);
        if (a.sheet.id != 0) SetTextureFilter(a.sheet, TEXTURE_FILTER_POINT);
        a.loaded = a.sheet.id != 0;
        // 取首帧 alpha-bbox 中心作为整段动画的 rest pivot
        a.pivot = compute_alpha_pivot(sheet_path, 0, 0, cell_size, cell_size);
        return a.loaded;
    };
    load_sprite(s.hero,    "assets/textures/pixellab/swarm_hero.png");
    load_sprite(s.hero_dash, "assets/textures/pixellab/swarm_hero_dash.png");
    load_sprite(s.enemy,   "assets/textures/pixellab/swarm_enemy.png");
    load_sprite(s.enemy_demon, "assets/textures/pixellab/swarm_enemy_demon.png");
    load_sprite(s.boss,    "assets/textures/pixellab/swarm_boss.png");
    load_sprite(s.bullet_lineshot, "assets/textures/pixellab/swarm_bullet_lineshot.png");
    load_sprite(s.bullet_spread,   "assets/textures/pixellab/swarm_bullet_spread.png");
    load_sprite(s.bullet_radial,   "assets/textures/pixellab/swarm_bullet_radial.png");
    load_sprite(s.bullet_homing,   "assets/textures/pixellab/swarm_bullet_homing.png");
    load_sprite(s.orb,         "assets/textures/pixellab/swarm_orb.png");
    load_sprite(s.orb_crystal, "assets/textures/pixellab/swarm_orb_crystal.png");
    load_sprite(s.pickup_heal,   "assets/textures/pixellab/swarm_pickup_heal.png");
    load_sprite(s.pickup_speed,  "assets/textures/pixellab/swarm_pickup_speed.png");
    load_sprite(s.pickup_invuln, "assets/textures/pixellab/swarm_pickup_invuln.png");
    load_sprite(s.effect_slash,  "assets/textures/pixellab/swarm_effect_slash.png");
    load_sprite(s.effect_bash,   "assets/textures/pixellab/swarm_effect_bash.png");
    // 动画资源（tro-animations v1 JSON）：加载失败 → 回退静态贴图（不退出）
    load_anim(s.anim_hero_walk,   "assets/animations/swarm_hero_walk.json",   "walk",
              "assets/textures/pixellab/swarm_hero_walk.png", 32);
    load_anim(s.anim_hero_attack, "assets/animations/swarm_hero_attack.json", "attack",
              "assets/textures/pixellab/swarm_hero_attack.png", 32);
    load_anim(s.anim_hero_hurt,   "assets/animations/swarm_hero_hurt.json",   "hurt",
              "assets/textures/pixellab/swarm_hero_hurt.png", 32);
    load_anim(s.anim_hero_die,    "assets/animations/swarm_hero_death.json",  "die",
              "assets/textures/pixellab/swarm_hero_death.png", 32);
    load_anim(s.anim_hero_dash,   "assets/animations/swarm_hero_dash.json",   "dash",
              "assets/textures/pixellab/swarm_hero_dash.png", 32);
    load_anim(s.anim_goblin_walk, "assets/animations/swarm_goblin_walk.json", "walk",
              "assets/textures/pixellab/swarm_goblin_walk.png", 32);
    load_anim(s.anim_demon_hurt,  "assets/animations/swarm_demon_hurt.json",  "hurt",
              "assets/textures/pixellab/swarm_demon_hurt.png", 32);
    load_anim(s.anim_boss_idle,   "assets/animations/swarm_boss_idle.json",  "idle",
              "assets/textures/pixellab/swarm_boss_idle.png", 64);
    // 系统字体：Liberation Sans Bold，覆盖 raylib 默认字体（默认字体很多 Unicode 字符
    // PixelLab 生成的 pixel font（warm orange arcade），加载 ttf 后用 POINT 滤波；
    // glyph_px=16 → DrawTextEx 用 16/20 之类字号刚好 1:1。注意：PixelLab TTF
    // 的 cmap 漏了一些 ASCII 符号（`[ ] # & < > @ \ ^ ` { | } ~` —— atlas 画了
    // 但 codepoint 表没收录），避开用它们。
    s.font = LoadFont("assets/fonts/swarm_hud.ttf");
    if (s.font.texture.id != 0) SetTextureFilter(s.font.texture, TEXTURE_FILTER_POINT);
    if (s.hero.tex.id == 0 || s.enemy.tex.id == 0 || s.boss.tex.id == 0 ||
        s.bullet_lineshot.tex.id == 0 || s.bullet_spread.tex.id == 0 ||
        s.bullet_radial.tex.id == 0 || s.bullet_homing.tex.id == 0 ||
        s.orb.tex.id == 0 || s.pickup_heal.tex.id == 0 || s.pickup_speed.tex.id == 0 ||
        s.pickup_invuln.tex.id == 0 || s.hero_dash.tex.id == 0) {
        std::fprintf(stderr, "[swarm] 资产加载失败：检查 assets/textures/pixellab/ 下 PNG\n");
        return s;
    }
    s.ok = true;
    return s;
}
void unload_sprites(const Sprites& s) {
    auto u = [](Texture2D t) { if (t.id) UnloadTexture(t); };
    u(s.hero.tex); u(s.hero_dash.tex);
    u(s.enemy.tex); u(s.enemy_demon.tex); u(s.boss.tex);
    u(s.bullet_lineshot.tex); u(s.bullet_spread.tex);
    u(s.bullet_radial.tex); u(s.bullet_homing.tex);
    u(s.orb.tex); u(s.orb_crystal.tex);
    u(s.pickup_heal.tex); u(s.pickup_speed.tex); u(s.pickup_invuln.tex);
    u(s.effect_slash.tex); u(s.effect_bash.tex);
    if (s.font.texture.id != 0) UnloadFont(s.font);
    // Anim 析构由 RAII 自动处理（AnimationAsset 持 data_，player 持 set view）
    (void)s;
}

// ── 按武器/道具种类选贴图（缺图时退回默认）──
const Sprite& bullet_sprite_for(const Sprites& s, std::int8_t weapon) {
    switch (static_cast<swarm::WeaponKind>(weapon)) {
        case swarm::WeaponKind::SpreadShot:  return s.bullet_spread;
        case swarm::WeaponKind::RadialBurst: return s.bullet_radial;
        case swarm::WeaponKind::HomingShot:  return s.bullet_homing;
        case swarm::WeaponKind::LineShot:
        default:                              return s.bullet_lineshot;
    }
}
const Sprite& pickup_sprite_for(const Sprites& s, swarm::PickupKind kind) {
    switch (kind) {
        case swarm::PickupKind::Heal:       return s.pickup_heal;
        case swarm::PickupKind::SpeedBuff:  return s.pickup_speed;
        case swarm::PickupKind::InvulnBuff: return s.pickup_invuln;
    }
    return s.pickup_heal;
}

// 用自定义字体画文字（带回退到默认字体；按 point 缩放控制字号）
inline void draw_text(const Sprites& s, const char* txt, int x, int y, int size,
                     Color color) {
    if (s.font.texture.id != 0) {
        DrawTextEx(s.font, txt, {static_cast<float>(x), static_cast<float>(y)},
                   static_cast<float>(size), 1.0f, color);
    } else {
        DrawText(txt, x, y, size, color);
    }
}

// ── HUD ──
void draw_hud(const swarm::World& w, const Sprites& s, float fps) {
    const int hp = swarm::player_hp(w);
    const int max_hp = swarm::player_max_hp(w);
    draw_text(s, TextFormat("HP %d/%d", hp, max_hp), 8, 8, 18, RAYWHITE);
    DrawRectangle(8, 30, 200, 8, DARKGRAY);
    DrawRectangle(8, 30, static_cast<int>(200 * hp / std::max(1, max_hp)), 8,
                  hp > max_hp / 2 ? GREEN : (hp > max_hp / 4 ? YELLOW : RED));
    const int xp_need_now = swarm::xp_need(w.level);
    draw_text(s, TextFormat("Lv %d  XP %d/%d", w.level, w.xp, xp_need_now), 8, 46, 14, GOLD);
    DrawRectangle(8, 64, 200, 6, DARKGRAY);
    DrawRectangle(8, 64, static_cast<int>(200 * w.xp / std::max(1, xp_need_now)), 6, GOLD);
    const float t_sec = w.steps * swarm::kDt;
    const int t_min = static_cast<int>(t_sec) / 60;
    const int t_sec_int = static_cast<int>(t_sec) % 60;
    const char* phase_str = (w.phase == swarm::Phase::Paused) ? " PAUSED"
                          : (w.phase == swarm::Phase::Dead)   ? " DEFEATED"
                          : (w.phase == swarm::Phase::LevelUp) ? " LEVEL UP"
                          : "";
    draw_text(s, TextFormat("Wave %d  Time %d:%02d%s", w.wave, t_min, t_sec_int, phase_str),
             8, kWinH - 56, 14, RAYWHITE);
    draw_text(s, TextFormat("Kills %d  Pickups %d  Deaths %d  Dashes %d",
                        w.kills, w.pickups, w.deaths, w.dashes),
             8, kWinH - 36, 12, LIGHTGRAY);
    // 帧率：右上角比 12px 灰色底栏显眼；附带 ms 帧时方便看卡点
    //  >120 fps → 绿；60~120 → 黄；<60 → 红
    const Color fps_color = fps > 120.0f ? GREEN
                          : fps >= 60.0f ? YELLOW
                          : RED;
    draw_text(s, TextFormat("FPS %.0f  %.1fms", fps, 1000.0f / std::max(fps, 1.0f)),
             kWinW - 130, kWinH - 18, 12, fps_color);
    // 右上 武器列表（图标 + 等级 + CD）
    int wx = kWinW - 220;
    draw_text(s, "Weapons", wx, 8, 12, LIGHTGRAY);
    int wy = 22;
    for (size_t i = 0; i < w.weapons.size() && i < 6; ++i) {
        const auto& wi = w.weapons[i];
        const auto& def = swarm::kWeaponDefs[static_cast<int>(wi.kind)];
        const Sprite& icon = bullet_sprite_for(s, static_cast<std::int8_t>(wi.kind));
        DrawTexturePro(icon.tex,
                       {0, 0, static_cast<float>(icon.tex.width), static_cast<float>(icon.tex.height)},
                       {static_cast<float>(wx), static_cast<float>(wy),
                        icon.tex.width * 0.5f, icon.tex.height * 0.5f},
                       {0, 0}, 0.0f, WHITE);
        const bool ready = wi.cd_remaining <= 0.0f;
        // 用 ASCII "..." 替代 ellipsis —— Liberation Sans Bold 不含 U+2026 字形
        draw_text(s, TextFormat("%s Lv%d%s", def.name, wi.level, ready ? "" : "..."),
                 wx + 24, wy, 12, ready ? RAYWHITE : GRAY);
        wy += 18;
    }
    if (!w.passives.empty()) {
        draw_text(s, "Passives", wx, wy + 2, 12, LIGHTGRAY);
        wy += 16;
        for (size_t i = 0; i < w.passives.size() && i < 3; ++i) {
            const auto& pi = w.passives[i];
            const auto& def = swarm::kPassiveDefs[static_cast<int>(pi.kind)];
            draw_text(s, TextFormat("%s Lv%d", def.name, pi.level), wx, wy, 12, RAYWHITE);
            wy += 14;
        }
    }
    if (w.boss_idx < 0 && w.wave > 0 && w.wave % swarm::kWaveBossEvery == 0 &&
        w.wave_timer > swarm::kWaveInterval - 5.0f) {
        const int secs_to_boss = static_cast<int>(swarm::kWaveInterval - 5.0f - w.wave_timer);
        if (secs_to_boss >= 0)
            draw_text(s, TextFormat("Boss in %d...", secs_to_boss),
                     kWinW / 2 - 50, 8, 18, RED);
    }
}

void draw_flash(const swarm::World& w) {
    if (w.levelup_flash_timer <= 0.0f) return;
    const float a = w.levelup_flash_timer / 0.2f;
    const unsigned char alpha = static_cast<unsigned char>(a * 255);
    DrawRectangle(0, 0, kWinW, kWinH, ::Color{255, 255, 255, alpha});
}

void draw_levelup(const swarm::World& w, const Sprites& s) {
    const int card_w = 200, card_h = 240;
    const int gap = 20;
    const int total_w = card_w * 3 + gap * 2;
    const int x0 = (kWinW - total_w) / 2;
    const int y = kWinH / 2 - card_h / 2;
    DrawRectangle(0, 0, kWinW, kWinH, ::Color{0, 0, 0, 180});
    draw_text(s, "LEVEL UP  -  Press 1 / 2 / 3",
             kWinW / 2 - 130, y - 36, 22, GOLD);
    for (int i = 0; i < swarm::kCardChoices; ++i) {
        const int cx = x0 + i * (card_w + gap);
        const auto& co = w.lvlup_cards[static_cast<size_t>(i)];
        DrawRectangle(cx, y, card_w, card_h, ::Color{30, 30, 50, 255});
        DrawRectangleLines(cx, y, card_w, card_h, GOLD);
        // 用 `(1)` 而非 `[1]` —— swarm_hud.ttf 的 cmap 不含 `[` `]`（PixelLab atlas
        // 画了但 TTF 没收录 codepoint），避免 raylib fallback 渲染成 `?`。
        draw_text(s, TextFormat("(%d)", i + 1), cx + 8, y + 8, 18, GOLD);
        // 图标区（武器显示对应弹贴图，被动显示对应道具贴图）
        const Sprite* icon = nullptr;
        if (co.is_weapon) {
            icon = &bullet_sprite_for(s, static_cast<std::int8_t>(co.kind));
        } else {
            // 被动按 kind 选对应图标（SpeedUp→SpeedBuff / HpUp→Heal / PickupUp→InvulnBuff）
            switch (static_cast<swarm::PassiveKind>(co.kind)) {
                case swarm::PassiveKind::SpeedUp:  icon = &s.pickup_speed; break;
                case swarm::PassiveKind::HpUp:     icon = &s.pickup_heal; break;
                case swarm::PassiveKind::PickupUp: icon = &s.pickup_invuln; break;
                default: break;
            }
        }
        if (icon && icon->tex.id != 0) {
            DrawTexturePro(icon->tex,
                {0, 0, static_cast<float>(icon->tex.width), static_cast<float>(icon->tex.height)},
                {cx + card_w * 0.5f - 28.0f, static_cast<float>(y + 40),
                 56.0f, 56.0f},
                {0, 0}, 0.0f, WHITE);
        }
        draw_text(s, co.name ? co.name : "?",
                 cx + 8, y + 110, 18, RAYWHITE);
        draw_text(s, co.is_weapon ? "Weapon" : "Passive",
                 cx + 8, y + 132, 12, LIGHTGRAY);
        if (co.current_level > 0) {
            draw_text(s, TextFormat("UPGRADE (Lv%d -> Lv%d)", co.current_level, co.current_level + 1),
                     cx + 8, y + 156, 11, GREEN);
        } else {
            draw_text(s, "NEW", cx + 8, y + 156, 11, GOLD);
        }
        // 描述（一行简短）
        if (co.is_weapon) {
            const auto& def = swarm::kWeaponDefs[co.kind];
            const int dps = static_cast<int>(def.base_dmg / def.base_cd);
            draw_text(s, TextFormat("dmg %d  cd %.2fs", dps, def.base_cd),
                     cx + 8, y + 180, 11, LIGHTGRAY);
        } else {
            const auto& def = swarm::kPassiveDefs[co.kind];
            draw_text(s, TextFormat("+%d%% per level", static_cast<int>(def.per_lvl * 100)),
                     cx + 8, y + 180, 11, LIGHTGRAY);
        }
    }
}

void draw_paused(const Sprites& s) {
    DrawRectangle(0, 0, kWinW, kWinH, ::Color{0, 0, 0, 160});
    draw_text(s, "PAUSED", kWinW / 2 - 60, kWinH / 2 - 30, 36, RAYWHITE);
    draw_text(s, "Esc to resume  -  R restart  -  Q quit",
             kWinW / 2 - 140, kWinH / 2 + 20, 14, LIGHTGRAY);
}

void draw_defeated(const swarm::World& w, const Sprites& s) {
    const float t_sec = w.steps * swarm::kDt;
    const int t_min = static_cast<int>(t_sec) / 60;
    const int t_sec_int = static_cast<int>(t_sec) % 60;
    DrawRectangle(0, 0, kWinW, kWinH, ::Color{0, 0, 0, 200});
    draw_text(s, "DEFEATED", kWinW / 2 - 110, kWinH / 2 - 80, 48, RED);
    draw_text(s, TextFormat("Survived %d:%02d  -  Wave %d  -  Kills %d  -  Lv %d",
                        t_min, t_sec_int, w.wave, w.kills, w.level),
             kWinW / 2 - 200, kWinH / 2 - 20, 18, RAYWHITE);
    draw_text(s, "Press R to restart",
             kWinW / 2 - 80, kWinH / 2 + 30, 16, GOLD);
}

// Boss 头顶血条：宽度 80px，固定在 boss 实体上方。boss_hp/kBossHp 比例缩放。
// 只在 boss 存在时画；用相机屏幕坐标（BeginMode2D 之外）。
void draw_boss_hp_bar(const swarm::World& w) {
    if (w.boss_idx < 0) return;
    const auto& p = w.pos[w.boss_idx];
    const float cx = p.x;
    const float cy = p.y - 28.0f;  // 头顶 28px
    // camera 已经把世界坐标映射成屏幕坐标，但 boss_hp_bar 在 BeginMode2D 外画，
    // 这里手动做同样的变换。
    const float sx = cx - w.pos[w.player >= 0 ? w.player : 0].x + kWinW * 0.5f;
    const float sy = cy - w.pos[w.player >= 0 ? w.player : 0].y + kWinH * 0.5f;
    const float bar_w = 80.0f;
    DrawRectangle(static_cast<int>(sx - bar_w * 0.5f),
                  static_cast<int>(sy - 4), static_cast<int>(bar_w), 6, DARKGRAY);
    const float pct = std::max(0.0f, std::min(1.0f,
                                w.boss_hp / static_cast<float>(swarm::kBossHp)));
    DrawRectangle(static_cast<int>(sx - bar_w * 0.5f),
                  static_cast<int>(sy - 4),
                  static_cast<int>(bar_w * pct), 6,
                  pct > 0.5f ? RED : (pct > 0.25f ? ORANGE : YELLOW));
}

// 敌人贴图：按实体索引在两种贴图间交替（确定性，无 RNG 依赖）。
const Sprite& enemy_sprite_for(const Sprites& s, int idx) {
    return (idx & 1) ? s.enemy_demon : s.enemy;
}

// 击杀爆点：死亡位置画一个扩张中的彩色圆环，alpha 随时间衰减。
// 在 BeginMode2D 之后调用（世界坐标）。
void draw_kill_fx(const swarm::World& w) {
    for (int i = 0; i < swarm::kKillFxMax; ++i) {
        const auto& fx = w.kill_fx[i];
        if (fx.timer <= 0.0f) continue;
        const float t = 1.0f - fx.timer / swarm::kKillFxLife;  // 0 → 1
        const float radius = 6.0f + t * 22.0f;  // 6px → 28px 扩张
        const unsigned char alpha = static_cast<unsigned char>((1.0f - t) * 220.0f);
        DrawCircleLinesV({fx.pos.x, fx.pos.y}, radius,
                          ::Color{255, 230, 120, alpha});
    }
}

// 视觉锚点对齐：以 pivot 为内容中心，把 src 矩形贴到 screen 上，使 pivot 落在
// center。统一用于静态贴图与动画帧；scale 由调用方传入（boss=1.6，否则 1.0）。
// DrawTexturePro + origin=(0,0)：源矩形从 src 拉伸到 dst（dst.w/h = src.w/h * scale），
// dst 左上 = center - pivot*scale，使 pivot 在源中的位置精确映射到 center。
void draw_anchored(Texture2D tex, int src_x, int src_y, int src_w, int src_h,
                   tg::Vec2 pivot, tg::Vec2 center, float scale, float rot,
                   Color tint) {
    DrawTexturePro(tex,
        ::Rectangle{static_cast<float>(src_x), static_cast<float>(src_y),
                    static_cast<float>(src_w), static_cast<float>(src_h)},
        ::Rectangle{center.x - pivot.x * scale, center.y - pivot.y * scale,
                    static_cast<float>(src_w) * scale,
                    static_cast<float>(src_h) * scale},
        ::Vector2{0.0f, 0.0f}, rot, tint);
}

// 用 AnimationPlayer 画当前帧：若 playing → 用 current_frame() 的 region；
// 失败回退用贴图尺寸。统一处理 hero/boss 等动画实体的绘制。
// 同步 player/boss/enemy 的 AnimationPlayer 状态：按 sim 的 *_anim 字段决定播哪个 clip。
// 仅在窗口循环里调用（每帧 real_dt）。
// AnimationPlayer::advance/play/stop 不是 const → Sprites 必须非 const
void update_animations(Sprites& s, swarm::World& w, float real_dt) {
    // 先 advance 所有 player（统一时间步）
    if (s.anim_hero_walk.loaded)   s.anim_hero_walk.player.advance(real_dt);
    if (s.anim_hero_attack.loaded) s.anim_hero_attack.player.advance(real_dt);
    if (s.anim_hero_hurt.loaded)   s.anim_hero_hurt.player.advance(real_dt);
    if (s.anim_hero_die.loaded)    s.anim_hero_die.player.advance(real_dt);
    if (s.anim_hero_dash.loaded)   s.anim_hero_dash.player.advance(real_dt);
    if (s.anim_goblin_walk.loaded) s.anim_goblin_walk.player.advance(real_dt);
    if (s.anim_demon_hurt.loaded)  s.anim_demon_hurt.player.advance(real_dt);
    if (s.anim_boss_idle.loaded)   s.anim_boss_idle.player.advance(real_dt);

    // Player clip 切换：按 sim 状态优先级（Die > Attack/Hurt > Walk > Idle）
    if (w.player >= 0) {
        // 决策：phase 优先 → 其次 sim.player_anim → 最后用速度兜底 walk
        auto switch_to = [&](Anim& a, const char* clip) {
            if (!a.loaded) return;
            const int idx = a.player.frame_index();
            // 已在播同名 clip 且没到末帧 → 不重播（避免每次 tick 重置）
            if (a.player.playing() && idx >= 0) return;
            a.player.play(clip);
        };
        // 死亡 → 一次性 die；其余 attack/hurt 优先级高
        if (w.phase == swarm::Phase::Dead) {
            switch_to(s.anim_hero_die, "die");
            if (s.anim_hero_walk.loaded)   s.anim_hero_walk.player.stop();
            if (s.anim_hero_attack.loaded) s.anim_hero_attack.player.stop();
            if (s.anim_hero_hurt.loaded)   s.anim_hero_hurt.player.stop();
        } else if (w.player_anim == swarm::World::PlayerAnim::Hurt) {
            switch_to(s.anim_hero_hurt, "hurt");
        } else if (w.player_anim == swarm::World::PlayerAnim::Attack) {
            switch_to(s.anim_hero_attack, "attack");
        } else {
            // 非特殊状态：玩家速度 ≠ 0 → walk；否则空闲（不动 = 显式 stop 让帧停在 0）
            const float vx = w.vel[w.player].x;
            const float vy = w.vel[w.player].y;
            const bool moving = (vx * vx + vy * vy) > 25.0f;  // >5 px/s
            if (moving) switch_to(s.anim_hero_walk, "walk");
            else if (s.anim_hero_walk.loaded) s.anim_hero_walk.player.stop();
        }
    }
    // Boss 永远 idle（plan 中 boss 没特殊 attack clip 视觉）
    if (w.boss_idx >= 0 && s.anim_boss_idle.loaded) {
        if (!s.anim_boss_idle.player.playing() ||
            s.anim_boss_idle.player.frame_index() == 0) {
            s.anim_boss_idle.player.play("idle");
        }
    }
}

// 波次清空横幅：屏幕中央顶部弹"Wave N cleared!"
void draw_wave_clear_msg(const swarm::World& w, const Sprites& s) {
    if (w.wave_clear_msg_timer <= 0.0f) return;
    const float t = w.wave_clear_msg_timer / swarm::kWaveClearMsgLife;
    const unsigned char alpha = static_cast<unsigned char>(std::min(255.0f, t * 2.0f * 255.0f));
    const int y_off = static_cast<int>((1.0f - t) * -20.0f);  // 上滑入场
    draw_text(s, TextFormat("Wave %d cleared!", w.wave_clear_msg),
             kWinW / 2 - 110, 50 + y_off, 26,
             ::Color{255, 230, 120, alpha});
}

}  // namespace

// 前向声明：update_animations 定义在匿名 namespace 里，main 在外面要用
namespace { void update_animations(Sprites& s, swarm::World& w, float real_dt); }

int main(int argc, char** argv) {
    const hp::Args args = hp::parse_args(
        argc, argv, "用法: swarm [--headless] [--seconds <N>] [--shot <path>]");
    if (!args.valid || args.help) return args.valid ? 0 : 2;
    if (args.headless) SetTraceLogLevel(LOG_ERROR);

    tg::SceneSpec spec;
    spec.name = "swarm";
    spec.tile_width = swarm::kTile;
    spec.tile_height = swarm::kTile;
    spec.has_background = true;
    spec.background = "#0a0c14";
    // 双网格 autotile tileset（PixelLab tileset15：lower=stone / upper=mossy grass，
    // 16 个角组合 + 圆弧过渡边界）。视觉与物理同源：每个 cell 的 4 个顶点按
    // is_moss_tile 程序化判断 → dual_grid pick 出正确 tile id。moss 圆斑在
    // 边缘 → autotile 自动切到过渡 tile，圆滑过渡。
    spec.tilesets.push_back({"swarm_autotile", "tilesets/swarm_autotile.json"});
    // 顶点网格 (kMapH+1) × (kMapW+1)：每个顶点 0=stone 或 1=moss。
    // cell (x, y) 的四角 = 顶点 (y, x), (y, x+1), (y+1, x), (y+1, x+1)。
    std::vector<std::vector<int>> vertex_grid(swarm::kMapH + 1,
                                             std::vector<int>(swarm::kMapW + 1, 0));
    for (int vy = 0; vy <= swarm::kMapH; ++vy) {
        for (int vx = 0; vx <= swarm::kMapW; ++vx) {
            // 顶点格中心 = (vx-0.5, vy-0.5) 落在 (vx-1, vy-1) tile 内
            // → 用那个 tile 的 is_moss_tile 判定（双线性插值都用同一 hash
            // 函数保证一致，避免顶点接缝出现锯齿状）。
            const int tx = vx - 1;
            const int ty = vy - 1;
            vertex_grid[vy][vx] = swarm::is_moss_tile(tx, ty) ? 1 : 0;
        }
    }
    auto dg_table = tg::load_dual_grid_table("tilesets/swarm_autotile.json");
    if (!dg_table) {
        std::fprintf(stderr, "[swarm] dual_grid 加载失败: %s\n",
                     dg_table.error().message.c_str());
        return 1;
    }
    {
        tg::SceneLayerSpec ground;
        ground.name = "ground";
        ground.width = swarm::kMapW;
        ground.height = swarm::kMapH;
        ground.tileset = "swarm_autotile";  // 引用图集 → 进入 atlas 模式（双网格表）
        ground.tiles.assign(static_cast<std::size_t>(swarm::kMapW * swarm::kMapH), 0);
        for (int y = 0; y < swarm::kMapH; ++y) {
            for (int x = 0; x < swarm::kMapW; ++x) {
                const std::size_t idx = static_cast<std::size_t>(y) * swarm::kMapW + x;
                const std::array<int, 4> corners = {
                    vertex_grid[y][x],
                    vertex_grid[y][x + 1],
                    vertex_grid[y + 1][x],
                    vertex_grid[y + 1][x + 1],
                };
                const auto picked = tg::pick_dual_grid_tile(*dg_table, corners);
                ground.tiles[idx] = picked ? *picked : 0;  // 兜底 = 纯 stone
            }
        }
        spec.layers.push_back(std::move(ground));
    }
    // 墙体层（碰撞仅）：tile 全部填 -1 → 渲染透明；真正的视觉障碍由 ground 层
    // 的 moss 斑块承担（同一份 kMossObstacles 写入 SolidGrid，所以玩家既看不
    // 见墙、也走不过边界）。保留 `solid=true` 让 SolidGrid::create 仍按
    // room_solid 把外圈 + 内部 moss 圆盘视为 solid。
    {
        tg::SceneLayerSpec walls;
        walls.name = "walls";
        walls.width = swarm::kMapW;
        walls.height = swarm::kMapH;
        walls.tileset = "swarm_autotile";
        walls.solid = true;
        walls.tiles.assign(static_cast<std::size_t>(swarm::kMapW * swarm::kMapH), -1);
        // 全部保持 -1（透明）：渲染层不画任何瓦片，视觉只来自 ground 层的
        // moss 圆斑。
        spec.layers.push_back(std::move(walls));
    }
    // 把 moss 斑块同步到 sim 的内部障碍：玩家和敌都绕不过去 → tileset 的
    // 两种 tile（stone / moss）都进入同一份 SolidGrid 视图，自动演示
    // 「同一图集 + dual_grid 选择器」对玩法可见的视觉/物理一致性。
    // sim 端在构造前会自己读 kMossObstacles 写入 g_obstacles，所以这里不重复。
    auto scene_or = tg::SceneAsset::create(spec, "swarm");
    if (!scene_or) {
        std::fprintf(stderr, "[swarm] 场景构造失败: %s\n", scene_or.error().message.c_str());
        return 1;
    }
    tg::SceneAsset scene = std::move(*scene_or);
    auto grid_or = tg::SolidGrid::create(swarm::kMapW, swarm::kMapH, swarm::kTile,
                                         swarm::kTile, swarm::room_solid);
    if (!grid_or) {
        std::fprintf(stderr, "[swarm] 掩码构造失败: %s\n", grid_or.error().message.c_str());
        return 1;
    }
    const tg::SolidGrid& grid = *grid_or;
    const tg::SolidGridView& view = grid.view();

    swarm::World world;
    swarm::reset_round(world);
    hp::open_window("swarm", kWinW, kWinH, args.headless);
    SetTargetFPS(60);

    Sprites sprites = load_sprites();
    if (!sprites.ok) return 1;

    Camera2D cam{};
    cam.target = {0.0f, 0.0f};
    cam.offset = {kWinW * 0.5f, kWinH * 0.5f};
    cam.rotation = 0.0f;
    cam.zoom = 1.0f;

    auto draw_world = [&]() {
        if (world.player >= 0) {
            cam.target = {world.pos[world.player].x, world.pos[world.player].y};
        }
        // 视口 Rect：从 Camera2D 反推世界坐标可见区域。cam.offset 是屏幕中心，
        // cam.zoom 是缩放——逆缩放后乘半屏尺寸得世界半视口半径。
        const float half_w = (kWinW * 0.5f) / cam.zoom;
        const float half_h = (kWinH * 0.5f) / cam.zoom;
        const tg::Rect vp{cam.target.x - half_w, cam.target.y - half_h,
                          half_w * 2.0f, half_h * 2.0f};
        BeginMode2D(cam);
        tg::render_scene(scene, vp);
        draw_kill_fx(world);
        for (int i = 0; i < swarm::count(world); ++i) {
            const tg::Rect r = swarm::rect_of(world, i);
            const std::uint8_t t = world.tag[i];
            const Sprite* sp = nullptr;
            const Anim* anim = nullptr;  // 优先用动画
            switch (t) {
                case swarm::kPlayer:
                    sp = &sprites.hero;
                    // 玩家动画优先（dash/attack/hurt/die/walk 已在 update_animations 切）
                    if (swarm::is_dashing(world) && sprites.anim_hero_dash.loaded)
                        anim = &sprites.anim_hero_dash;
                    else if (world.player_anim == swarm::World::PlayerAnim::Die && sprites.anim_hero_die.loaded)
                        anim = &sprites.anim_hero_die;
                    else if (world.player_anim == swarm::World::PlayerAnim::Hurt && sprites.anim_hero_hurt.loaded)
                        anim = &sprites.anim_hero_hurt;
                    else if (world.player_anim == swarm::World::PlayerAnim::Attack && sprites.anim_hero_attack.loaded)
                        anim = &sprites.anim_hero_attack;
                    else if (world.player_anim == swarm::World::PlayerAnim::Walk && sprites.anim_hero_walk.loaded)
                        anim = &sprites.anim_hero_walk;
                    break;
                case swarm::kEnemy:
                    sp = &enemy_sprite_for(sprites, i);
                    // 敌人动画：死亡前一直 walk 循环（goblin walk）
                    if (sprites.anim_goblin_walk.loaded) anim = &sprites.anim_goblin_walk;
                    break;
                case swarm::kBoss:
                    sp = &sprites.boss;
                    if (sprites.anim_boss_idle.loaded) anim = &sprites.anim_boss_idle;
                    break;
                case swarm::kBullet:
                    sp = &bullet_sprite_for(sprites, world.bullet[i].weapon);
                    break;
                case swarm::kOrb:
                    sp = &sprites.orb;
                    break;
                case swarm::kPickup:
                    sp = &pickup_sprite_for(sprites, world.pickup[i].kind);
                    break;
                default: break;
            }
            const float scale = (t == swarm::kBoss) ? 1.6f : 1.0f;
            // 受击 / 冲刺 tint
            Color tint = WHITE;
            if (t == swarm::kPlayer && world.invuln_timer > 0.0f &&
                world.invuln_timer < swarm::kInvuln &&
                (world.player_anim != swarm::World::PlayerAnim::Hurt || anim == nullptr)) {
                const float blink = static_cast<int>(world.invuln_timer * 20.0f) % 2;
                tint = blink ? Color{255, 80, 80, 255} : WHITE;
            }
            if (t == swarm::kPlayer && swarm::is_dashing(world) && anim == nullptr) {
                tint = Color{220, 240, 255, 255};
            }
            // 子弹按速度方向旋转
            float rot = 0.0f;
            if (t == swarm::kBullet) {
                const tg::Vec2 v = world.vel[i];
                if (v.x != 0.0f || v.y != 0.0f) {
                    rot = std::atan2(v.y, v.x) * 180.0f / 3.14159265f;
                }
            }
            const float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.5f;
            if (anim != nullptr && anim->loaded && anim->player.playing()) {
                // 动画帧：用 anim.sheet（与 JSON textures[0] 同源）+ fr.region 画子矩形，
                // 以 anim.pivot（首帧 bbox 中心）作锚 → 内容中心稳定钉在 (cx, cy)
                const tg::SpriteDesc fr = anim->player.current_frame();
                if (fr.has) {
                    draw_anchored(anim->sheet,
                                  fr.region.x, fr.region.y,
                                  fr.region.w, fr.region.h,
                                  anim->pivot,
                                  {cx, cy}, scale, 0.0f, tint);
                    continue;
                }
            }
            // 静态贴图回退：同理用 sp.pivot 钉内容中心
            if (sp != nullptr && sp->tex.id != 0) {
                draw_anchored(sp->tex, 0, 0, sp->tex.width, sp->tex.height,
                              sp->pivot, {cx, cy}, scale, rot, tint);
            }
        }
        EndMode2D();
    };

    const float secs = args.seconds > 0.0 ? static_cast<float>(args.seconds) : kDefaultSeconds;
    const long long shot_steps = static_cast<long long>(kShotSeconds / swarm::kDt + 0.5);
    const long long target = static_cast<long long>(secs / swarm::kDt + 0.5);

    if (args.headless) {
        // --shot --seconds N 优先 N（展示进度）；仅 --shot 默认 2s
        long long n;
        if (args.shot.empty()) n = target;
        else if (args.seconds > 0.0) n = target;
        else n = shot_steps;
        // 默认走右（让 walk 动画可见；其它动画依赖战斗自然触发）
        // 默认走右（让 walk 动画可见；其它动画依赖战斗自然触发）
        swarm::Input demo_in{}; demo_in.mx = 1.0f;
        for (long long i = 0; i < n; ++i) swarm::step(world, demo_in, &view, 1, swarm::kDt);
    } else {
        tg::StepClock clock{swarm::kDt, 5};
        const long long stop_at = args.shot.empty()
            ? (args.seconds > 0.0 ? target : -1) : shot_steps;
        while (!WindowShouldClose()) {
            const float real_dt = GetFrameTime();
            const bool r_pressed = IsKeyPressed(KEY_R);
            if (r_pressed && world.phase != swarm::Phase::Paused) {
                swarm::reset_round(world);
                world.phase = swarm::Phase::Playing;
            }
            swarm::Input in = read_input(world, cam);
            in.restart = false;
            if (in.pause) {
                if (world.phase == swarm::Phase::Playing ||
                    world.phase == swarm::Phase::LevelUp) {
                    world.phase = swarm::Phase::Paused;
                } else if (world.phase == swarm::Phase::Paused) {
                    world.phase = swarm::Phase::Playing;
                }
                in.pause = false;
            }
            const bool advance = (world.phase != swarm::Phase::Paused &&
                                  world.phase != swarm::Phase::Dead);
            if (advance) {
                const tg::StepClock::Tick t = clock.tick(real_dt);
                for (int s = 0; s < t.steps; ++s)
                    swarm::step(world, in, &view, 1, swarm::kDt);
            }
            // 推进 AnimationPlayer（用真实帧时长，便于视觉平滑）
            update_animations(sprites, world, real_dt);
            BeginDrawing();
            ClearBackground(BLACK);
            draw_world();
            draw_boss_hp_bar(world);
            draw_hud(world, sprites, GetFPS());
            draw_flash(world);
            draw_wave_clear_msg(world, sprites);
            if (world.phase == swarm::Phase::LevelUp) draw_levelup(world, sprites);
            if (world.phase == swarm::Phase::Paused)  draw_paused(sprites);
            if (world.phase == swarm::Phase::Dead)    draw_defeated(world, sprites);
            EndDrawing();
            if (stop_at >= 0 && static_cast<long long>(world.steps) >= stop_at) break;
        }
    }

    if (!args.shot.empty()) {
        if (!hp::capture_frame_png(args.shot, kWinW, kWinH, [&]() {
            ClearBackground(BLACK);
            // --shot 模式无窗口循环；推进 0.5s 让 walk 跑过几帧
            update_animations(sprites, world, 0.5f);
            draw_world();
            draw_boss_hp_bar(world);
            draw_hud(world, sprites, 60.0f);
            draw_flash(world);
            if (world.phase == swarm::Phase::LevelUp) draw_levelup(world, sprites);
            if (world.phase == swarm::Phase::Paused)  draw_paused(sprites);
            if (world.phase == swarm::Phase::Dead)    draw_defeated(world, sprites);
        })) {
            unload_sprites(sprites);
            {
                tg::SceneAsset tmp = std::move(scene);
                (void)tmp;
            }
            tg::shutdown_render();
            CloseWindow();
            return 1;
        }
    } else {
        std::printf("%s\n", swarm::summary(world).c_str());
    }
    unload_sprites(sprites);
    // scene 是栈变量，析构发生在 main return 时——若先 CloseWindow/shutdown_render
    // 会让 SceneImpl::~SceneImpl() 在 GL 销毁后调 UnloadTexture 而段错。
    // 解决：把 scene 移进一个块让它先于 CloseWindow 析构。
    {
        tg::SceneAsset tmp = std::move(scene);
        (void)tmp;
    }
    tg::shutdown_render();
    CloseWindow();
    return 0;
}