// oop_client_smoke.cpp —— 面向对象风格 consumer smoke。
//
// 目的：以 game 层 OOP 视角消费**生产库 trogue_engine** 的纯公共 API，
// 证明无需私有头/测试库即可完成一个典型游戏循环片段——
//   ① import 场景 descriptor → 自建 Actor 对象；
//   ② tile-only 查询（is_solid_at / rect_hits_solid / tile_at）做移动碰撞；
//   ③ 绑定真实资产（soldier）动画集并播放（虚拟时钟推进）；
//   ④ TweenManager 虚拟时钟补间采样；
//   ⑤ 无窗口安全：不建窗口调用 render_scene → WindowUnavailable 不触 GPU；
//   ⑥ Ipc / Watcher：Debug 为真实现、Release 为 invalid 桩（形状不变）。
//
// 无窗口、不触 GPU（判定见 ⑤）；全部使用公共头 trogue.hpp。
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

// ── ① OOP 风格 Actor：自建对象模型，从场景 descriptor 快照导入 ──
// 仅演示「descriptor → game 对象」的复制语义；engine 不持有/管理这些对象。
class Actor {
public:
    std::string id;
    std::string type;
    float x = 0.0f, y = 0.0f;  // 左上角（像素）
    float w = 0.0f, h = 0.0f;
    tg::Color color;

    // 从 SceneEntity 值快照导入（通用 spawn descriptor → OOP 对象）。
    static Actor from_descriptor(const tg::SceneEntity& d) {
        Actor a;
        a.id = d.id;
        a.type = d.type;
        a.x = d.x; a.y = d.y; a.w = d.w; a.h = d.h;
        a.color = d.color;
        return a;
    }
};

// ── ①b 加载真实 demo.json（palette 模式，无 tileset JSON） ──
const SceneAsset& demo_asset() {
    static SceneAsset asset = [] {
        auto r = SceneAsset::load("assets/scenes/demo.json");
        if (!r) {
            std::fprintf(stderr, "[oop smoke] demo load failed: %s\n",
                         r.error().message.c_str());
            std::abort();
        }
        return std::move(*r);
    }();
    return asset;
}

// ── ② tile 查询 + ③ ④ 的断言体 ──
bool test_import_and_query() {
    bool ok = true;
    const SceneAsset& asset = demo_asset();

    // demo.json：40×24、palette 模式、2 层（ground 非 solid / walls solid）
    CHECK(asset.tile_width() == 16);
    CHECK(asset.tile_height() == 16);
    CHECK(asset.layer_count() == 2);
    CHECK(asset.entity_count() == 7);  // 含 tex_probe（独立贴图探针实体）

    // 层信息快照：walls 层 solid、palette（tileset_index<0、tileset_name 空）
    const auto& walls = asset.layer(1);
    CHECK(walls.name == "walls");
    CHECK(walls.solid);
    CHECK(walls.tileset_index == -1);
    CHECK(walls.tileset_name.empty());
    CHECK(walls.nonempty > 0);

    // import 全部 descriptor → OOP 对象（值复制，快照独立）
    std::vector<Actor> actors;
    actors.reserve(asset.entity_count());
    for (int i = 0; i < asset.entity_count(); ++i)
        actors.push_back(Actor::from_descriptor(asset.entity(i)));
    CHECK(actors.size() == 7);
    CHECK(actors[0].id == "player");
    CHECK(actors[0].type == "player");
    CHECK(actors[0].x == 48.0f);  // demo: player x=48
    // 快照独立性：改 actor 不改资产
    actors[0].x += 999.0f;
    CHECK(asset.entity(0).x == 48.0f);
    actors[0].x -= 999.0f;

    // tile-only 查询（只查 solid 层）：墙点 solid、室内 clear
    // demo walls 层：(0,0) 是墙（四周一圈），(tile 3,3)=(48,48) 为室内
    CHECK(is_solid_at(asset, {0, 0}) == TileQueryResult::solid);
    CHECK(is_solid_at(asset, {48, 48}) == TileQueryResult::clear);
    // 层矩形外 = 无数据 = 不阻挡
    CHECK(is_solid_at(asset, {-1000, -1000}) == TileQueryResult::clear);
    CHECK(is_solid_at(asset, {400 * 16, 400 * 16}) == TileQueryResult::clear);
    // 矩形命中墙 → solid
    CHECK(rect_hits_solid(asset, Rect{0, 0, 16, 16}) == TileQueryResult::solid);
    // 全室内矩形 → clear
    CHECK(rect_hits_solid(asset, Rect{32, 32, 16, 16}) == TileQueryResult::clear);
    // tile_at：室内 (48,48) → 层 0 有 tile、层 1 空；取层 0 值
    {
        int v = -999;
        CHECK(tile_at(asset, 0, {48, 48}, &v) == TileLookupResult::occupied);
        CHECK(v >= 0);
    }
    {
        int v = -999;
        // walls 层 (48,48) 空格 → empty 且 *out=-1
        CHECK(tile_at(asset, 1, {48, 48}, &v) == TileLookupResult::empty);
        CHECK(v == -1);
    }
    return ok;
}

// ── ③ 动画：绑定真实 soldier 资产（bare + 内嵌 7 动画 43 帧） ──
const SceneAsset& soldier_asset() {
    static SceneAsset asset = [] {
        auto r =
            SceneAsset::load("assets/scenes/soldier_animated_sprite_2d.json");
        if (!r) {
            std::fprintf(stderr, "[oop smoke] soldier load failed: %s\n",
                         r.error().message.c_str());
            std::abort();
        }
        return std::move(*r);
    }();
    return asset;
}

bool test_animation_playback() {
    bool ok = true;
    const SceneAsset& soldier = soldier_asset();
    REQUIRE(soldier.animation_set_count() == 1);
    const auto& set = soldier.animation_set(0);
    CHECK(set.has_clip("walk"));
    CHECK(set.has_clip("dead") == false);

    AnimationPlayer p;
    p.bind(set);
    CHECK(p.valid());
    CHECK(p.play("walk"));  // fps=7、8 帧、loop
    // 虚拟时钟推进：一秒多应跨越若干帧
    p.advance(10.0);  // 播 long
    CHECK(p.playing());              // loop 不结束
    CHECK(p.frame_index() >= 0 && p.frame_index() < 8);
    // 当前帧视觉描述归属校验
    auto fr = p.current_frame();
    CHECK(fr.has);
    CHECK(fr.asset_id == soldier.asset_id());
    CHECK(!fr.texture.empty());
    // 停止 → 播放停止；current_frame 冻结在停止前帧（不再推进）= 视觉仍在
    p.stop();
    CHECK(!p.playing());
    auto stopped = p.current_frame();
    CHECK(stopped.has);  // 冻结帧仍可绘制
    CHECK(stopped.asset_id == soldier.asset_id());
    // 未绑定播放器的 current_frame 才无视觉
    AnimationPlayer unbound;
    CHECK(!unbound.current_frame().has);
    return ok;
}

// ── ④ Tween 虚拟时钟采样 ──
bool test_tween_sampling() {
    bool ok = true;
    TweenManager m;
    TweenSpec spec; spec.duration = 1.0;
    float last = -1.0f;
    const auto id = m.add_float(0.0f, 10.0f, spec,
                                [&](const TweenManager::Sample<float>& s) {
                                    last = s.value;
                                });
    m.tick(0.5);
    CHECK(last > 4.99f && last < 5.01f);  // 线性 0→10 中点
    m.tick(0.5);
    CHECK(last > 9.99f && last < 10.01f);
    CHECK(!m.alive(id));  // 完成
    return ok;
}

// ── ⑤ 无窗口渲染安全：不建窗口（无 InitWindow） ──
bool test_no_window_render_safe() {
    bool ok = true;
    const SceneAsset& asset = demo_asset();
    // 3 段校验内的第 2 段：IsWindowReady()==false → WindowUnavailable
    // （无窗口程序不建窗口，绝不触 GPU）
    CHECK(render_scene(asset) == RenderResult::WindowUnavailable);
    const auto sp = asset.entity(0).sprite;
    // 无 sprite（demo 实体无贴图）→ Invalid（参数校验优先，不触窗口）
    CHECK(render_sprite(asset, sp, {0, 0}) == RenderResult::Invalid);
    return ok;
}

// ── ⑥ Ipc / Watcher：Debug 真实现 / Release 桩 ──
bool test_ipc_watcher() {
    bool ok = true;
    // Debug：快建一个 Ipc 再析构（valid 即可，转录行为由 watcher_ipc_test 覆盖）
    const std::uint16_t port = 49200 + (::getpid() % 400);
    {
        auto ipc = tg::Ipc::create(port);
#ifdef TROGUE_DEBUG
        CHECK(ipc.valid());
        ipc.clear_handler();  // no-op 安全
        ipc.poll();           // 无连接轮询安全
#else
        CHECK(!ipc.valid());  // Release 桩
        ipc.poll();           // no-op
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
    test_import_and_query();
    test_animation_playback();
    test_tween_sampling();
    test_no_window_render_safe();
    test_ipc_watcher();
    std::printf("[oop smoke] checks=%d failures=%d\n", ::tg_test::g_checks,
                ::tg_test::g_failures);
    return ::tg_test::g_failures == 0 ? 0 : 1;
}