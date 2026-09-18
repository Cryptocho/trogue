// render_test.cpp —— render 无窗口安全断言（经 seam 三段计数）。
//
// 断言（链接 trogue_engine_test，TROGUE_TEST_SEAMS）：
//   ① 传非法参数/归属不匹配/路径非法 → param_failures 增、window_checks /
//      texture_attempts 不变（段① 在窗口检查前失败）；
//   ② 合法调用但未建窗口 → window_checks 增、texture_attempts==0（段②不执行段③）；
//   ③ draw_rect 非法矩形 → param_failures 增；合法矩形未建窗口 → window_checks 增。
// 本机无窗口（不 InitWindow），render_* 应全程安全 no-op 不崩、不触碰 GPU。
//
// 视口裁剪（render_scene(asset, viewport)）：
//   段①   viewport 非有限 / w<=0 / h<=0 → param_failures 增、Invalid
//   段①'  CPU-only 计数（视口与层交集内的非空格 tile）→ 累加 culled_tiles
//         在段②之前已完成；无窗口单测因此能稳定断言 culled_tiles
//   段②   窗口检查：未就绪 → WindowUnavailable（culled_tiles 已稳定）
//   段③   视口内 tile 绘制（裁剪路径）/ 全层 tile 绘制（非裁剪路径）
#include <limits>
#include <cmath>     // std::nan / std::numeric_limits
#include <cstdio>
#include <optional>
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

// 构造一个 100×100 全非空格 palette 场景（视口裁剪裁剪比例与不交测试用）。
// 全 1 = 10000 瓦全画，origin=(0,0)，tile_w=tile_h=16。
tg::ErrorOr<SceneAsset> make_large_palette_asset() {
    static int seq = 0;
    const std::string path =
        "build/tmp_render_large_" + std::to_string(seq++) + ".json";
    {
        std::FILE* f = std::fopen(path.c_str(), "wb");
        std::fputs("{\"format\":\"tro-scene\",\"version\":2,\"tilemap\":{"
                   "\"tile_width\":16,\"tile_height\":16,"
                   "\"palette\":[\"#2a2d3a\",\"#7f8ca3\"],"
                   "\"layers\":[{\"name\":\"g\",\"width\":100,\"height\":100,"
                   "\"solid\":true,\"tiles\":[",
                   f);
        // 行主序 100×100 = 10000 瓦，全部 tile=1（palette[1]）
        for (int i = 0; i < 10000; ++i) {
            if (i > 0) std::fputc(',', f);
            std::fputc('1', f);
        }
        std::fputs("]}]},\"meta\":{\"name\":\"large_palette\"}}", f);
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

// flip/rotation 增量参数：非有限 rotation 段①拒绝；其余值无窗口时走段②。
bool test_flip_rotation_params() {
    bool ok = true;
    render_test_reset_stats();

    auto asset_or = make_asset();
    REQUIRE(asset_or.has_value());

    SpriteDesc sp;
    sp.has = true;
    sp.asset_id = asset_or->asset_id();
    sp.texture = "textures/x.png";
    sp.flip_x = true;
    sp.flip_y = true;

    // 非有限 rotation → 段① Invalid（窗口检查前）
    CHECK(render_sprite(*asset_or, sp, tg::Vec2{0, 0},
                        tg::Color{255, 255, 255, 255}, tg::Vec2{1, 1},
                        std::numeric_limits<float>::infinity()) ==
          RenderResult::Invalid);
    // 负 scale 维持现状 = Invalid（翻转只走显式 flip 字段，负 scale 不参与）
    CHECK(render_sprite(*asset_or, sp, tg::Vec2{0, 0},
                        tg::Color{255, 255, 255, 255}, tg::Vec2{-1, 1},
                        45.0f) == RenderResult::Invalid);

    const RenderStats after_param = render_test_stats();
    CHECK(after_param.param_failures == 2);
    CHECK(after_param.window_checks == 0);

    // flip + 合法 rotation、无窗口 → 段②（参数全过，窗口检查执行）
    CHECK(render_sprite(*asset_or, sp, tg::Vec2{0, 0},
                        tg::Color{255, 255, 255, 255}, tg::Vec2{1, 1},
                        45.0f) == RenderResult::WindowUnavailable);
    // 仅 flip_x（单 flip 位）同样走段②（fast-path 判据按位判断）
    SpriteDesc sp_x = sp;
    sp_x.flip_y = false;
    CHECK(render_sprite(*asset_or, sp_x, tg::Vec2{0, 0}) ==
          RenderResult::WindowUnavailable);
    const RenderStats after_win = render_test_stats();
    CHECK(after_win.window_checks == 2);
    CHECK(after_win.param_failures == 2);
    CHECK(after_win.texture_attempts == 0);
    return ok;
}

// 视口裁剪比例：100×100 全非空格 palette 层，vp=(0,0,160,160) →
// tx0=0, tx1=ceil(160/16)=10；ty 同理；视口内 10×10=100 瓦；
// culled = nonempty(10000) - drawn_in_vp(100) = 9900。
// 段①' 在段② 之前累加 culled_tiles → 无窗口单测也能断言。
bool test_viewport_cull_ratio() {
    bool ok = true;
    render_test_reset_stats();
    auto asset_or = make_large_palette_asset();
    REQUIRE(asset_or.has_value());
    const RenderStats before = render_test_stats();

    const tg::Rect vp{0.0f, 0.0f, 160.0f, 160.0f};
    // 无窗口 → WindowUnavailable；但 culled_tiles 已在段①' 累加
    CHECK(render_scene(*asset_or, vp) == RenderResult::WindowUnavailable);

    const RenderStats after = render_test_stats();
    CHECK(after.culled_tiles == before.culled_tiles + 9900);
    CHECK(after.param_failures == before.param_failures);  // 段① 合法
    CHECK(after.window_checks == before.window_checks + 1);  // 段② 跑过一次
    CHECK(after.texture_attempts == before.texture_attempts);  // palette 不触纹理加载
    return ok;
}

// viewport 非法路径：NaN / 负宽 / +∞ 三件套 → 段① 拒绝。
bool test_viewport_invalid_params() {
    bool ok = true;
    render_test_reset_stats();
    auto asset_or = make_asset();
    REQUIRE(asset_or.has_value());
    const RenderStats before = render_test_stats();

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    CHECK(render_scene(*asset_or, tg::Rect{nan, 0.0f, 100.0f, 100.0f}) ==
          RenderResult::Invalid);
    CHECK(render_scene(*asset_or, tg::Rect{0.0f, 0.0f, -1.0f, 100.0f}) ==
          RenderResult::Invalid);
    CHECK(render_scene(*asset_or, tg::Rect{0.0f, 0.0f, 100.0f, inf}) ==
          RenderResult::Invalid);

    const RenderStats after = render_test_stats();
    CHECK(after.param_failures == before.param_failures + 3);
    CHECK(after.window_checks == before.window_checks);  // 段① 拒绝不触段②
    CHECK(after.culled_tiles == before.culled_tiles);    // 段①' 未跑
    return ok;
}

// nullopt 等价：旧 overload `render_scene(asset)` 与新 overload `render_scene(asset, nullopt)`
// 在三段计数上完全等价——两次调用各自的"前后差值"逐字段相等。
bool test_viewport_nullopt_equivalence() {
    bool ok = true;
    render_test_reset_stats();
    auto asset_or = make_asset();
    REQUIRE(asset_or.has_value());

    const RenderStats before = render_test_stats();

    // 旧 overload
    CHECK(render_scene(*asset_or) == RenderResult::WindowUnavailable);
    const RenderStats after_old = render_test_stats();

    // 新 overload + nullopt
    CHECK(render_scene(*asset_or, std::nullopt) == RenderResult::WindowUnavailable);
    const RenderStats after_new = render_test_stats();

    // 两次调用的差值：window_checks +1、texture_attempts +0、culled_tiles +0、param_failures +0
    CHECK(after_new.window_checks - after_old.window_checks ==
          after_old.window_checks - before.window_checks);
    CHECK(after_new.texture_attempts - after_old.texture_attempts ==
          after_old.texture_attempts - before.texture_attempts);
    CHECK(after_new.culled_tiles - after_old.culled_tiles ==
          after_old.culled_tiles - before.culled_tiles);
    CHECK(after_new.param_failures - after_old.param_failures ==
          after_old.param_failures - before.param_failures);
    return ok;
}

// 视口与层不交：vp 完全在 100×100 层外 → 整层 skip；culled_tiles == nonempty。
// 无窗口下返回值 = WindowUnavailable（段② 失败），但段①' 已累加 culled_tiles。
bool test_viewport_disjoint_from_layer() {
    bool ok = true;
    render_test_reset_stats();
    auto asset_or = make_large_palette_asset();
    REQUIRE(asset_or.has_value());
    const RenderStats before = render_test_stats();

    const tg::Rect vp{-1000.0f, -1000.0f, 10.0f, 10.0f};
    // tx0 = floor(-1000/16) = -63 → clamp 0；tx1 = ceil(-990/16) = -61 → min(-61, 100) = -61；
    // 0 ≥ -61 → 整层 skip → culled = 10000
    CHECK(render_scene(*asset_or, vp) == RenderResult::WindowUnavailable);

    const RenderStats after = render_test_stats();
    CHECK(after.culled_tiles == before.culled_tiles + 10000);
    CHECK(after.param_failures == before.param_failures);
    return ok;
}

}  // namespace

int main() {
    test_param_failures_precede_window();
    test_window_unavailable_no_draw();
    test_reload_texture_contract();
    test_flip_rotation_params();
    test_viewport_cull_ratio();
    test_viewport_invalid_params();
    test_viewport_nullopt_equivalence();
    test_viewport_disjoint_from_layer();
    // 清理独立贴图缓存（未加载任何东西，幂等）
    tg::shutdown_render();
    std::printf("[render test] checks=%d failures=%d\n", ::tg_test::g_checks,
                ::tg_test::g_failures);
    return ::tg_test::g_failures == 0 ? 0 : 1;
}