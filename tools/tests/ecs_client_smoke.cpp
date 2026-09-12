// ecs_client_smoke.cpp —— 极简 ECS 风格 consumer smoke。
//
// 目的：用与 oop_client_smoke 完全同一套 engine 公共 API（trogue.hpp），
// 以组件-系统风格消费资产，证明 engine 与使用者对象模型无关：
//   ① import 场景 descriptor → 实体表 + Position/Visual 组件；
//   ② 系统（movement/query 系统函数）跑 tile 查询做碰撞与过滤；
//   ③ 动画系统：绑定真实资产动画集，组件驱动播放；
//   ④ 补间系统：TweenManager 虚拟时钟推进组件值；
//   ⑤ 无窗口渲染安全 + ⑥ Ipc/Watcher Debug/Release 桩 —— 与 OOP smoke 相同。
//
// 模型无关性要点：这里组件是扁平数组/索引布局，对象是系统处理的数据行，
// 没有任何 OOP 类层次；engine API 面保持零差异。
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <unistd.h>  // getpid（Ipc 端口偏移）

#include "trogue/trogue.hpp"
#include "test_util.hpp"

namespace {

using tg::AnimationPlayer;
using tg::SceneAsset;
using tg::TweenManager;
using tg::TweenSpec;
using tg::Rect;
using tg::RenderResult;
using tg::TileLookupResult;
using tg::TileQueryResult;
using tg::Vec2;

// ── 极简 ECS：注册表 = 实体 id 表 + 并置组件数组（索引对齐） ──
// 组件即纯数据（Position / Visual / Clip）；系统是自由函数。
struct Position {
    float x = 0.0f, y = 0.0f;
};
struct Visual {
    tg::Color color{255, 255, 255, 255};
    tg::SpriteDesc sprite;  // descriptor 快照（显式绘制输入）
};
struct ActorTag {
    std::string id;
    std::string type;
};
struct Tagged {
    bool has = false;
    ActorTag tag;
};

struct World {
    std::vector<std::string> ids;
    std::vector<Position> pos;      // 必有
    std::vector<Visual> vis;         // 必有
    std::vector<Tagged> tag;         // 可有（tag==nullptr 语义：has=false）
};

// 组件添加（ECUS 惯用：逐组件 attach）
static std::size_t spawn(World& w, const tg::SceneEntity& d) {
    w.ids.push_back(d.id);
    w.pos.push_back(Position{d.x, d.y});
    w.vis.push_back(Visual{d.color, d.sprite});
    w.tag.push_back(Tagged{true, ActorTag{d.id, d.type}});
    return w.ids.size() - 1;
}

static const SceneAsset& demo_asset() {
    static SceneAsset asset = [] {
        auto r = SceneAsset::load("assets/scenes/demo.json");
        if (!r) {
            std::fprintf(stderr, "[ecs smoke] demo load failed: %s\n",
                         r.error().message.c_str());
            std::abort();
        }
        return std::move(*r);
    }();
    return asset;
}

static const SceneAsset& soldier_asset() {
    static SceneAsset asset = [] {
        auto r =
            SceneAsset::load("assets/scenes/soldier_animated_sprite_2d.json");
        if (!r) {
            std::fprintf(stderr, "[ecs smoke] soldier load failed: %s\n",
                         r.error().message.c_str());
            std::abort();
        }
        return std::move(*r);
    }();
    return asset;
}

// ── ① import + ② 系统查询 ──
bool test_import_and_systems() {
    bool ok = true;
    World w;
    const SceneAsset& asset = demo_asset();
    for (int i = 0; i < asset.entity_count(); ++i)
        spawn(w, asset.entity(i));
    CHECK(w.ids.size() == 7);  // 含 tex_probe（独立贴图探针实体）
    CHECK(w.tag[0].tag.id == "player");
    CHECK(w.tag[0].tag.type == "player");

    // 移动系统：以玩家中心点试探 —— 墙点应被碰撞拒绝、室内可通过
    const Vec2 wall{0, 0}, open{48, 48};
    CHECK(is_solid_at(asset, wall) == TileQueryResult::solid);
    CHECK(is_solid_at(asset, open) == TileQueryResult::clear);

    // 碰撞移动：直接改组件（game 决策），engine 只承担查询
    w.pos[0].x = open.x; w.pos[0].y = open.y;
    CHECK(w.pos[0].x == 48.0f);

    // 组件过滤系统：按 type 过滤实体（descriptor.type 是 opaque 标识，
    // 由 game 的 tag 系统解释 —— engine 从不分支）
    int goblins = 0, coins = 0;
    for (std::size_t i = 0; i < w.ids.size(); ++i)
        if (w.tag[i].has && w.tag[i].tag.type == "goblin") ++goblins;
        else if (w.tag[i].tag.type == "coin") ++coins;
    CHECK(goblins == 3);
    CHECK(coins == 2);
    return ok;
}

// ── ③ 组件驱动的动画系统 ──
bool test_animation_system() {
    bool ok = true;
    const SceneAsset& soldier = soldier_asset();
    REQUIRE(soldier.animation_set_count() == 1);
    const auto& set = soldier.animation_set(0);

    // 动画组件（每实体一个播放器；这里给一名角色挂上）
    AnimationPlayer p;
    p.bind(set);
    CHECK(p.play("idle"));  // fps=5、6 帧、loop
    p.advance(10.0);
    CHECK(p.playing());
    CHECK(p.frame_index() >= 0 && p.frame_index() < 6);
    auto fr = p.current_frame();
    CHECK(fr.has);
    CHECK(fr.asset_id == soldier.asset_id());
    p.stop();
    return ok;
}

// ── ④ 补间系统：驱动组件值 ──
bool test_tween_system() {
    bool ok = true;
    TweenManager m;
    Position target{100, 200};
    TweenSpec spec; spec.duration = 1.0;
    const auto id = m.add_vec2(
        Vec2{0, 0}, Vec2{100, 200}, spec,
        [&](const TweenManager::Sample<Vec2>& s) {
            target.x = s.value.x; target.y = s.value.y;
        });
    m.tick(0.5);
    CHECK(target.x > 49.9f && target.x < 50.1f);
    CHECK(target.y > 99.9f && target.y < 100.1f);
    m.tick(0.5);
    CHECK(target.x > 99.9f && target.x < 100.1f);
    CHECK(target.y > 199.9f && target.y < 200.1f);
    CHECK(!m.alive(id));
    return ok;
}

// ── ⑤ 无窗口渲染安全 ──
bool test_no_window_render_safe() {
    bool ok = true;
    const SceneAsset& asset = demo_asset();
    CHECK(render_scene(asset) == RenderResult::WindowUnavailable);
    const auto sp = asset.entity(0).sprite;
    CHECK(render_sprite(asset, sp, {0, 0}) == RenderResult::Invalid);
    return ok;
}

// ── ⑥ Ipc / Watcher 桩行为 ──
bool test_ipc_watcher() {
    bool ok = true;
    const std::uint16_t port = 49400 + (::getpid() % 300);
    {
        auto ipc = tg::Ipc::create(port);
#ifdef TROGUE_DEBUG
        CHECK(ipc.valid());
        ipc.clear_handler();
        ipc.poll();
#else
        CHECK(!ipc.valid());
        ipc.poll();
#endif
    }
    {
        auto w = tg::Watcher::create("assets/scenes");
#ifdef TROGUE_DEBUG
        CHECK(w.valid());
#else
        CHECK(!w.valid());
        CHECK(!w.poll().has_value());
#endif
        w.shutdown();
    }
    return ok;
}

}  // namespace

int main() {
    test_import_and_systems();
    test_animation_system();
    test_tween_system();
    test_no_window_render_safe();
    test_ipc_watcher();
    std::printf("[ecs smoke] checks=%d failures=%d\n", ::tg_test::g_checks,
                ::tg_test::g_failures);
    return ::tg_test::g_failures == 0 ? 0 : 1;
}