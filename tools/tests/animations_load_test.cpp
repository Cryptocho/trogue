// animations_load_test.cpp —— 独立 tro-animations v1 加载与生命周期回归。
#include <cstdio>
#include <string>
#include <utility>

#include "trogue/trogue.hpp"
#include "test_util.hpp"

namespace {

bool test_load_json_and_view_lifetime() {
    bool ok = true;
    const auto text = R"({
        "format":"tro-animations", "version":1,
        "textures":["textures/a.png"],
        "animations":[{"name":"idle","fps":4,"loop":true,
                        "frames":[{"texture":0,"offset":[1,-2]}]}]
    })";
    auto loaded = tg::AnimationAsset::load_json(text, "memory_anim");
    REQUIRE(loaded.has_value());
    tg::AnimationAsset asset = std::move(*loaded);
    CHECK(asset.name() == "memory_anim");
    const tg::AnimationSet& view = asset.view();
    CHECK(&view == &asset.view());
    CHECK(view.name() == "memory_anim");
    CHECK(view.clip_count() == 1);
    CHECK(view.has_clip("idle"));

    tg::AnimationPlayer player;
    player.bind(view);
    CHECK(player.play("idle"));
    const auto frame = player.current_frame();
    CHECK(frame.has);
    CHECK(frame.asset_id == 0);
    CHECK(frame.texture == "textures/a.png");
    CHECK(frame.offset.x == 1 && frame.offset.y == -2);

    tg::AnimationAsset moved = std::move(asset);
    CHECK(moved.view().name() == "memory_anim");
    CHECK(moved.view().has_clip("idle"));
    return ok;
}

bool test_outer_format_and_shared_validation() {
    bool ok = true;
    const auto valid = R"({"format":"tro-animations","version":1,
        "textures":["a.png"],"animations":[]})";
    const auto bad_format = R"({"format":"tro-scene","version":1,
        "textures":[],"animations":[]})";
    const auto bad_version = R"({"format":"tro-animations","version":2,
        "textures":[],"animations":[]})";
    const auto bad_frame = R"({"format":"tro-animations","version":1,
        "textures":["a.png"],"animations":[{"name":"x","fps":1,
        "frames":[{"texture":1}]}]})";
    const auto duplicate = R"({"format":"tro-animations","version":1,
        "textures":["a.png"],"animations":[{"name":"x","fps":1,"frames":[]},
        {"name":"x","fps":1,"frames":[]}]})";

    CHECK(tg::AnimationAsset::load_json(valid, "valid").has_value());
    CHECK(!tg::AnimationAsset::load_json(bad_format, "bad_format").has_value());
    CHECK(!tg::AnimationAsset::load_json(bad_version, "bad_version").has_value());
    CHECK(!tg::AnimationAsset::load_json(bad_frame, "bad_frame").has_value());
    CHECK(!tg::AnimationAsset::load_json(duplicate, "duplicate").has_value());
    return ok;
}

bool test_load_file() {
    bool ok = true;
    const char* path = "build/animations_load_test.json";
    std::FILE* file = std::fopen(path, "wb");
    REQUIRE(file != nullptr);
    const char* text = R"({"format":"tro-animations","version":1,
        "textures":["textures/a.png"],"animations":[{"name":"walk",
        "fps":8,"loop":true,"frames":[{"texture":0}]}]})";
    std::fputs(text, file);
    std::fclose(file);

    auto loaded = tg::AnimationAsset::load(path);
    std::remove(path);
    REQUIRE(loaded.has_value());
    CHECK(loaded->name() == "animations_load_test.json");
    CHECK(loaded->view().has_clip("walk"));

    auto real = tg::AnimationAsset::load("assets/animations/pxlab_soldier.json");
    REQUIRE(real.has_value());
    CHECK(real->view().clip_count() > 0);
    CHECK(real->view().name() == "pxlab_soldier.json");
    return ok;
}

}  // namespace

int main() {
    test_load_json_and_view_lifetime();
    test_outer_format_and_shared_validation();
    test_load_file();
    std::fprintf(stderr, "animations_load_test: %d checks, %d failures\n",
                 tg_test::g_checks, tg_test::g_failures);
    return tg_test::g_failures == 0 ? 0 : 1;
}
