// render_test.cpp —— render 无窗口安全断言（经 seam 三段计数）。
//
// 断言（链接 trogue_engine_test，TROGUE_TEST_SEAMS）：
//   ① 传非法参数/归属不匹配/路径非法 → param_failures 增、window_checks /
//      texture_attempts 不变（段① 在窗口检查前失败）；
//   ② 合法调用但未建窗口 → window_checks 增、texture_attempts==0（段②不执行段③）；
//   ③ draw_rect 非法矩形 → param_failures 增；合法矩形未建窗口 → window_checks 增。
// 本机无窗口（不 InitWindow），render_* 应全程安全 no-op 不崩、不触碰 GPU。
#include <cstdio>
#include <string>

#include "trogue/trogue.hpp"
#include "scene_test_seams.hpp"  // seam：render_test_stats/reset
#include "test_util.hpp"

namespace {

using tg::RenderResult;
using tg::SceneAsset;
using tg::SpriteDesc;
using tg::detail::RenderStats;
using tg::detail::render_test_stats;
using tg::detail::render_test_reset_stats;

// 构造一个 minimal palette 场景（4×4，纯数据）
tg::ErrorOr<SceneAsset> make_asset() {
    // 写临时文件（CWD=项目根；build/ 已存在）
    static int seq = 0;
    const std::string path =
        "build/tmp_render_" + std::to_string(seq++) + ".json";
    {
        std::FILE* f = std::fopen(path.c_str(), "wb");
        std::fputs(R"({"format":"tro-scene","version":2,"tilemap":{
            "tile_width":16,"tile_height":16,
            "palette":["#2a2d3a","#7f8ca3"],
            "layers":[{"name":"g","width":4,"height":4,"solid":true,
                       "tiles":[1,-1,-1,-1,  -1,-1,-1,-1,  -1,-1,-1,-1,  -1,-1,-1,-1]}]},
            "entities":[{"id":"a","type":"x","x":0,"y":0,"w":16,"h":16}]})",
                    f);
        std::fclose(f);
    }
    auto r = SceneAsset::load(path);
    std::remove(path.c_str());
    return r;
}

bool test_param_failures_precede_window() {
    bool ok = true;
    render_test_reset_stats();

    auto asset_or = make_asset();
    REQUIRE(asset_or.has_value());
    const SceneAsset& a = *asset_or;

    const RenderStats before = render_test_stats();

    // ① 空 sprite → Invalid
    const SpriteDesc empty;
    CHECK(render_sprite(a, empty, tg::Vec2{0, 0}) == RenderResult::Invalid);
    // ① 归属不匹配（asset_id 不符）→ Invalid
    SpriteDesc wrong;
    wrong.has = true;
    wrong.asset_id = a.asset_id() + 12345;  // 不匹配
    wrong.texture = "textures/x.png";
    CHECK(render_sprite(a, wrong, tg::Vec2{0, 0}) == RenderResult::Invalid);
    // ① 路径不合法 → Invalid
    SpriteDesc bad_path;
    bad_path.has = true;
    bad_path.asset_id = a.asset_id();
    bad_path.texture = "../evil.png";
    CHECK(render_sprite(a, bad_path, tg::Vec2{0, 0}) == RenderResult::Invalid);
    // ① 非法缩放在窗口检查前失败
    SpriteDesc bad_scale = bad_path;
    bad_scale.texture = "textures/x.png";
    CHECK(render_sprite(a, bad_scale, tg::Vec2{0, 0},
                        tg::Color{255, 255, 255, 255}, tg::Vec2{0, -1}) ==
          RenderResult::Invalid);

    const RenderStats after_param = render_test_stats();
    CHECK(after_param.param_failures >= before.param_failures + 4);
    CHECK(after_param.window_checks == before.window_checks);       // 窗口检查未执行
    CHECK(after_param.texture_attempts == before.texture_attempts); // 未触达加载

    // draw_rect 非法矩形 → param_failures 增
    CHECK(draw_rect(tg::Rect{0, 0, -1, 4}, tg::Color{255, 0, 0}) == RenderResult::Invalid);
    const RenderStats after_rect = render_test_stats();
    CHECK(after_rect.param_failures == after_param.param_failures + 1);
    CHECK(after_rect.window_checks == after_param.window_checks);
    return ok;
}

bool test_window_unavailable_no_draw() {
    bool ok = true;
    render_test_reset_stats();

    auto asset_or = make_asset();
    REQUIRE(asset_or.has_value());

    // 未建窗口：render_scene / 合法 sprite / draw_rect 都走段②返回
    CHECK(render_scene(*asset_or) == RenderResult::WindowUnavailable);

    SpriteDesc sp;
    sp.has = true;
    sp.asset_id = asset_or->asset_id();
    sp.texture = "textures/x.png";
    CHECK(render_sprite(*asset_or, sp, tg::Vec2{8, 8}) == RenderResult::WindowUnavailable);
    CHECK(draw_rect(tg::Rect{0, 0, 4, 4}, tg::Color{0, 255, 0}) ==
          RenderResult::WindowUnavailable);

    const RenderStats after = render_test_stats();
    CHECK(after.window_checks == 3);           // 三次都执行了窗口检查
    CHECK(after.texture_attempts == 0);        // 段③未执行（无加载尝试）
    CHECK(after.param_failures == 0);          // 段①全部通过
    return ok;
}

bool test_reload_texture_contract() {
    bool ok = true;
    render_test_reset_stats();
    const RenderStats before = render_test_stats();

    // 合法路径（无窗口：缓存恒空）→ no-op true，stats 完全不变
    CHECK(tg::reload_texture("textures/anything.png") == true);
    const RenderStats after_ok = render_test_stats();
    CHECK(after_ok.param_failures == before.param_failures);
    CHECK(after_ok.window_checks == before.window_checks);
    CHECK(after_ok.texture_attempts == before.texture_attempts);

    // 非法路径 → false + 仅 param_failures 各 +1（window_checks/attempts 不变）
    CHECK(tg::reload_texture("") == false);
    CHECK(tg::reload_texture("../x.png") == false);
    CHECK(tg::reload_texture("textures/../x.png") == false);
    const RenderStats after_bad = render_test_stats();
    CHECK(after_bad.param_failures == after_ok.param_failures + 3);
    CHECK(after_bad.window_checks == after_ok.window_checks);
    CHECK(after_bad.texture_attempts == after_ok.texture_attempts);
    return ok;
}

}  // namespace

int main() {
    test_param_failures_precede_window();
    test_window_unavailable_no_draw();
    test_reload_texture_contract();
    // 清理独立贴图缓存（未加载任何东西，幂等）
    tg::shutdown_render();
    std::printf("[render test] checks=%d failures=%d\n", ::tg_test::g_checks,
                ::tg_test::g_failures);
    return ::tg_test::g_failures == 0 ? 0 : 1;
}