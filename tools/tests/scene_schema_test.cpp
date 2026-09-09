// scene_schema_test.cpp —— SceneAsset 解析 schema 拒绝规则全集单测（plan-5.2 §6）。
//
// 每个 §2 拒绝规则各一个反例断言；另测正例（layers 缺省≡空数组、bare 尺寸 0、
// 三态模式、payload 限额边界、路径 grammar 反例）。所有用例通过公共 API
// SceneAsset::load 走完整文件加载路径（临时文件写于 CWD=CMAKE_SOURCE_DIR）。
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "trogue/trogue.hpp"
#include "test_util.hpp"

namespace {

using nlohmann::json;
using tg::SceneAsset;

// 写临时场景文件；返回相对 CWD 的路径（调用方负责删除）。
// pid+序号保证并发测试互不冲突。
int g_seq = 0;

std::string write_temp_scene2(const std::string& content) {
    const std::string path = "build/tmp_schema_" + std::to_string(g_seq++) + ".json";
    std::ofstream out(path, std::ios::binary);
    out << content;
    out.close();
    return path;
}

// 断言 JSON 文本 load 失败并匹配错误码
bool expect_reject(const std::string& label, const std::string& json_text,
                   tg::ErrorCode want) {
    const std::string path = write_temp_scene2(json_text);
    auto r = SceneAsset::load(path);
    std::remove(path.c_str());
    if (r) {
        ::tg_test::record_failure(__FILE__, __LINE__,
                                  label + ": 期望拒绝但成功");
        return false;
    }
    const auto& e = r.error();
    if (e.code != want) {
        ::tg_test::record_failure(
            __FILE__, __LINE__,
            label + ": 错误码不匹配 want=" +
                std::string(tg::error_code_name(want)) + " got=" +
                std::string(tg::error_code_name(e.code)) + " msg=" + e.message);
        return false;
    }
    ::tg_test::record_ok();
    return true;
}

// 断言 JSON 文本 load 成功
bool expect_ok(const std::string& label, const std::string& json_text) {
    const std::string path = write_temp_scene2(json_text);
    auto r = SceneAsset::load(path);
    std::remove(path.c_str());
    if (!r) {
        ::tg_test::record_failure(__FILE__, __LINE__,
                                  label + ": 期望成功但失败: " + r.error().message);
        return false;
    }
    ::tg_test::record_ok();
    return true;
}

// 生成合法 bare 场景
std::string bare_scene() {
    return R"({"format":"tro-scene","version":2,"tilemap":{"layers":[]},"entities":[]})";
}

// ════════════ §2 拒绝规则 ════════════

bool test_root_and_keys() {
    bool ok = true;
    // 根非 object
    ok &= expect_reject("根非 object", R"([1,2,3])", tg::ErrorCode::kSchemaViolation);
    // format 错误
    ok &= expect_reject("format 缺失", R"({"version":2,"tilemap":{}})",
                        tg::ErrorCode::kSchemaViolation);
    ok &= expect_reject("format 非 tro-scene",
                        R"({"format":"other","version":2,"tilemap":{}})",
                        tg::ErrorCode::kSchemaViolation);
    // version 错误
    ok &= expect_reject("version 非 int", R"({"format":"tro-scene","version":"2","tilemap":{}})",
                        tg::ErrorCode::kSchemaViolation);
    ok &= expect_reject("version != 2", R"({"format":"tro-scene","version":3,"tilemap":{}})",
                        tg::ErrorCode::kSchemaViolation);
    // tilemap 缺失 / 非 object
    ok &= expect_reject("tilemap 缺失", R"({"format":"tro-scene","version":2})",
                        tg::ErrorCode::kSchemaViolation);
    ok &= expect_reject("tilemap 非 object", R"({"format":"tro-scene","version":2,"tilemap":[]})",
                        tg::ErrorCode::kSchemaViolation);
    // 根未知键宽容：合法场景加未知根键应成功
    ok &= expect_ok("根未知键宽容（忽略+warning）",
                    R"({"format":"tro-scene","version":2,"tilemap":{"layers":[]},"future_key":123,"entities":[]})");
    return ok;
}

bool test_modes() {
    bool ok = true;
    // tilesets + palette 同现
    ok &= expect_reject("tilesets+palette 同现",
                        R"({"format":"tro-scene","version":2,"tilemap":{
                            "tile_width":16,"tile_height":16,
                            "tilesets":[{"name":"a","path":"tilesets/tile_set.json"}],
                            "palette":["#000000"]}})",
                        tg::ErrorCode::kSchemaViolation);
    // 无 tilesets/palette 但有非空层
    ok &= expect_reject(
        "无 tilesets/palette 但 layers 非空",
        R"({"format":"tro-scene","version":2,"tilemap":{
            "tile_width":16,"tile_height":16,
            "layers":[{"name":"g","width":1,"height":1,"tiles":[0]}]}})",
        tg::ErrorCode::kSchemaViolation);
    // bare 合法：layers 缺省 ≡ 空数组
    ok &= expect_ok("bare layers 缺省",
                    R"({"format":"tro-scene","version":2,"tilemap":{},"entities":[]})");
    ok &= expect_ok("bare layers 显式空数组", bare_scene());
    // 非 bare 缺 tile 尺寸
    ok &= expect_reject(
        "非 bare 缺 tile_width",
        R"({"format":"tro-scene","version":2,"tilemap":{
            "palette":["#000000"],
            "layers":[{"name":"g","width":1,"height":1,"tiles":[0]}]}})",
        tg::ErrorCode::kSchemaViolation);
    // 非 bare tile 尺寸越界
    ok &= expect_reject(
        "tile 尺寸越界 0",
        R"({"format":"tro-scene","version":2,"tilemap":{
            "tile_width":0,"tile_height":16,
            "palette":["#000000"],
            "layers":[{"name":"g","width":1,"height":1,"tiles":[0]}]}})",
        tg::ErrorCode::kSchemaViolation);
    ok &= expect_reject(
        "tile 尺寸越界 257",
        R"({"format":"tro-scene","version":2,"tilemap":{
            "tile_width":257,"tile_height":16,
            "palette":["#000000"],
            "layers":[{"name":"g","width":1,"height":1,"tiles":[0]}]}})",
        tg::ErrorCode::kSchemaViolation);
    return ok;
}

bool test_tilesets() {
    bool ok = true;
    // tilesets 空数组
    ok &= expect_reject("tilesets 空数组",
                        R"({"format":"tro-scene","version":2,"tilemap":{
                            "tile_width":16,"tile_height":16,"tilesets":[]}})",
                        tg::ErrorCode::kSchemaViolation);
    // tilesets 超 8
    std::string many;
    for (int i = 0; i < 9; ++i) {
        many += (i ? "," : "") + std::string(R"({"name":"t)") + std::to_string(i) +
                R"(","path":"tilesets/tile_set.json"})";
    }
    ok &= expect_reject("tilesets > 8",
                        R"({"format":"tro-scene","version":2,"tilemap":{
                            "tile_width":16,"tile_height":16,"tilesets":[)" +
                            many + R"(]}})",
                        tg::ErrorCode::kSchemaViolation);
    // tileset 未知键
    ok &= expect_reject("tileset 未知键",
                        R"({"format":"tro-scene","version":2,"tilemap":{
                            "tile_width":16,"tile_height":16,
                            "tilesets":[{"name":"a","path":"tilesets/tile_set.json","extra":1}]}})",
                        tg::ErrorCode::kSchemaViolation);
    // tileset name 缺失 / path 缺失
    ok &= expect_reject("tileset 缺 path",
                        R"({"format":"tro-scene","version":2,"tilemap":{
                            "tile_width":16,"tile_height":16,
                            "tilesets":[{"name":"a"}]}})",
                        tg::ErrorCode::kSchemaViolation);
    // 引用不存在文件
    ok &= expect_reject("tileset 文件不存在",
                        R"({"format":"tro-scene","version":2,"tilemap":{
                            "tile_width":16,"tile_height":16,
                            "tilesets":[{"name":"a","path":"tilesets/nope.json"}]}})",
                        tg::ErrorCode::kIoError);
    // 合法图集引用（真实 tile_set.json）
    ok &= expect_ok("图集 tileset 引用真实文件",
                    R"({"format":"tro-scene","version":2,"tilemap":{
                        "tile_width":16,"tile_height":16,
                        "tilesets":[{"name":"ts","path":"tilesets/tile_set.json"}],
                        "layers":[{"name":"g","width":2,"height":1,"tileset":"ts",
                                   "tiles":[0,1]}]},"entities":[]})");
    return ok;
}

// 同 palette_scene 但按自定义 layers 构造
std::string palette_scene(const std::string& layers_json) {
    return std::string(R"({"format":"tro-scene","version":2,"tilemap":{
        "tile_width":16,"tile_height":16,
        "palette":["#2a2d3a","#7f8ca3"],
        "layers":)") +
           layers_json + R"(},"entities":[]})";
}

bool test_palette_layers() {
    bool ok = true;
    // palette 空数组
    ok &= expect_reject("palette 空数组",
                        R"({"format":"tro-scene","version":2,"tilemap":{
                            "tile_width":16,"tile_height":16,"palette":[]}})",
                        tg::ErrorCode::kSchemaViolation);
    // palette 超 32
    std::string pal;
    for (int i = 0; i < 33; ++i) pal += (i ? "," : "") + std::string(R"("#000000")");
    ok &= expect_reject("palette > 32",
                        R"({"format":"tro-scene","version":2,"tilemap":{
                            "tile_width":16,"tile_height":16,"palette":[)" +
                            pal + R"(]}})",
                        tg::ErrorCode::kSchemaViolation);
    // palette 非法颜色
    ok &= expect_reject("palette 非法颜色",
                        R"({"format":"tro-scene","version":2,"tilemap":{
                            "tile_width":16,"tile_height":16,
                            "palette":["#12g" ]}})",
                        tg::ErrorCode::kSchemaViolation);
    ok &= expect_reject("palette 非串",
                        R"({"format":"tro-scene","version":2,"tilemap":{
                            "tile_width":16,"tile_height":16,"palette":[42]}})",
                        tg::ErrorCode::kSchemaViolation);
    // layers 超 4
    std::string layers;
    for (int i = 0; i < 5; ++i) {
        layers += (i ? "," : "") + std::string(R"({"name":"g)") + std::to_string(i) +
                  R"(","width":1,"height":1,"tiles":[0]})";
    }
    ok &= expect_reject("layers > 4", palette_scene("[" + layers + "]"),
                        tg::ErrorCode::kSchemaViolation);
    // layer 未知键
    ok &= expect_reject("layer 未知键", palette_scene(R"(
        [{"name":"g","width":1,"height":1,"tiles":[0],"junk":1}])"),
                        tg::ErrorCode::kSchemaViolation);
    // width/height 缺失 / 非 int / 越界
    ok &= expect_reject("layer 缺 height", palette_scene(R"(
        [{"name":"g","width":1,"tiles":[0]}])"),
                        tg::ErrorCode::kSchemaViolation);
    ok &= expect_reject("layer height 非 int", palette_scene(R"(
        [{"name":"g","width":1,"height":"1","tiles":[0]}])"),
                        tg::ErrorCode::kSchemaViolation);
    ok &= expect_reject("layer 尺寸越界 0", palette_scene(R"(
        [{"name":"g","width":0,"height":1,"tiles":[0]}])"),
                        tg::ErrorCode::kSchemaViolation);
    ok &= expect_reject("layer 尺寸越界 4097", palette_scene(R"(
        [{"name":"g","width":4097,"height":1,"tiles":[0,0]}])"),
                        tg::ErrorCode::kSchemaViolation);
    // origin 非法
    ok &= expect_reject("origin 非 [ox,oy]", palette_scene(R"(
        [{"name":"g","width":1,"height":1,"origin":[1],"tiles":[0]}])"),
                        tg::ErrorCode::kSchemaViolation);
    ok &= expect_reject("origin 非 int", palette_scene(R"(
        [{"name":"g","width":1,"height":1,"origin":[1.5,0],"tiles":[0]}])"),
                        tg::ErrorCode::kSchemaViolation);
    // solid 非 bool
    ok &= expect_reject("solid 非 bool", palette_scene(R"(
        [{"name":"g","width":1,"height":1,"solid":"true","tiles":[0]}])"),
                        tg::ErrorCode::kSchemaViolation);
    return ok;
}

bool test_tiles_array() {
    bool ok = true;
    // tiles 长度 ≠ width*height
    ok &= expect_reject("tiles 长度不符", palette_scene(R"(
        [{"name":"g","width":2,"height":2,"tiles":[0,1,2]}])"),
                        tg::ErrorCode::kSchemaViolation);
    // tiles 值域越界（palette 2 色 → 值 ≤1 或 -1）
    ok &= expect_reject("tiles 值域越界 2", palette_scene(R"(
        [{"name":"g","width":1,"height":1,"tiles":[2]}])"),
                        tg::ErrorCode::kSchemaViolation);
    ok &= expect_reject("tiles 值域越界 -2", palette_scene(R"(
        [{"name":"g","width":1,"height":1,"tiles":[-2]}])"),
                        tg::ErrorCode::kSchemaViolation);
    ok &= expect_reject("tiles 元素非 int", palette_scene(R"(
        [{"name":"g","width":1,"height":1,"tiles":[1.5]}])"),
                        tg::ErrorCode::kSchemaViolation);
    // palette 模式层带 tileset 字段
    ok &= expect_reject("palette 层带 tileset", palette_scene(R"(
        [{"name":"g","width":1,"height":1,"tileset":"ts","tiles":[0]}])"),
                        tg::ErrorCode::kSchemaViolation);
    // 图集模式层缺 tileset 引用
    ok &= expect_reject(
        "图集层缺 tileset",
        R"({"format":"tro-scene","version":2,"tilemap":{
            "tile_width":16,"tile_height":16,
            "tilesets":[{"name":"ts","path":"tilesets/tile_set.json"}],
            "layers":[{"name":"g","width":1,"height":1,"tiles":[0]}]}})",
        tg::ErrorCode::kSchemaViolation);
    // 图集模式层引用不存在的 tileset name
    ok &= expect_reject(
        "图集层引用未知 tileset",
        R"({"format":"tro-scene","version":2,"tilemap":{
            "tile_width":16,"tile_height":16,
            "tilesets":[{"name":"ts","path":"tilesets/tile_set.json"}],
            "layers":[{"name":"g","width":1,"height":1,"tileset":"nope","tiles":[0]}]}})",
        tg::ErrorCode::kSchemaViolation);
    // 图集 tiles 值越界（tile_set.json count=16 → 值 16 越界）
    ok &= expect_reject(
        "图集 tiles 值越界 16",
        R"({"format":"tro-scene","version":2,"tilemap":{
            "tile_width":16,"tile_height":16,
            "tilesets":[{"name":"ts","path":"tilesets/tile_set.json"}],
            "layers":[{"name":"g","width":1,"height":1,"tileset":"ts","tiles":[16]}]}})",
        tg::ErrorCode::kSchemaViolation);
    return ok;
}

bool test_entities() {
    bool ok = true;
    auto scene_with = [](const std::string& entity_json) {
        return R"({"format":"tro-scene","version":2,"tilemap":{"layers":[]},"entities":[)" +
               entity_json + R"(]})";
    };
    // entity 未知键
    ok &= expect_reject("entity 未知键",
                        scene_with(R"({"id":"a","junk":1})"),
                        tg::ErrorCode::kSchemaViolation);
    // id 缺失 / 空 / 超长 / 重复
    ok &= expect_reject("entity 缺 id", scene_with(R"({"type":"x"})"),
                        tg::ErrorCode::kSchemaViolation);
    ok &= expect_reject("entity id 空", scene_with(R"({"id":""})"),
                        tg::ErrorCode::kSchemaViolation);
    std::string long_id(64, 'a');  // 恰好 kNameMax → 应拒绝（长度须 < 上限）
    ok &= expect_reject("entity id 超长", scene_with(R"({"id":")" + long_id + R"("})"),
                        tg::ErrorCode::kSchemaViolation);
    ok &= expect_reject("entity id 重复",
                        R"({"format":"tro-scene","version":2,"tilemap":{"layers":[]},
                            "entities":[{"id":"a"},{"id":"a"}]})",
                        tg::ErrorCode::kSchemaViolation);
    // type 显式空串
    ok &= expect_reject("entity type 空", scene_with(R"({"id":"a","type":""})"),
                        tg::ErrorCode::kSchemaViolation);
    // x/y/w/h 数值
    ok &= expect_reject("entity x 非有限", scene_with(R"({"id":"a","x":1e39})"),
                        tg::ErrorCode::kSchemaViolation);
    ok &= expect_reject("entity w<=0", scene_with(R"({"id":"a","w":0})"),
                        tg::ErrorCode::kSchemaViolation);
    ok &= expect_reject("entity z 越界", scene_with(R"({"id":"a","z":2.2e9})"),
                        tg::ErrorCode::kSchemaViolation);
    // color
    ok &= expect_reject("entity color 非法", scene_with(R"({"id":"a","color":"red"})"),
                        tg::ErrorCode::kSchemaViolation);
    return ok;
}

bool test_sprite() {
    bool ok = true;
    auto scene_with = [](const std::string& entity_json) {
        return R"({"format":"tro-scene","version":2,"tilemap":{"layers":[]},"entities":[)" +
               entity_json + R"(]})";
    };
    // 双形态互斥（图集形态键白名单此时应拒绝）
    ok &= expect_reject(
        "sprite 双形态互斥",
        scene_with(R"({"id":"a","sprite":{"tileset":"x","tile":0,"texture":"t.png"}})"),
        tg::ErrorCode::kSchemaViolation);
    // 图集形态缺 tile
    ok &= expect_reject("sprite 图集缺 tile",
                        scene_with(R"({"id":"a","sprite":{"tileset":"x"}})"),
                        tg::ErrorCode::kSchemaViolation);
    // 图集引用不存在 tileset（bare 场景无 tilesets）
    ok &= expect_reject("sprite 图集引用未知 tileset",
                        scene_with(R"({"id":"a","sprite":{"tileset":"nope","tile":0}})"),
                        tg::ErrorCode::kSchemaViolation);
    // 独立贴图纹理路径非法
    ok &= expect_reject("sprite texture 路径非法",
                        scene_with(R"({"id":"a","sprite":{"texture":"../evil.png"}})"),
                        tg::ErrorCode::kSchemaViolation);
    ok &= expect_reject("sprite texture 绝对路径",
                        scene_with(R"({"id":"a","sprite":{"texture":"/etc/passwd"}})"),
                        tg::ErrorCode::kSchemaViolation);
    // region 数值非法
    ok &= expect_reject("sprite region w<=0",
                        scene_with(R"({"id":"a","sprite":{"texture":"t.png","region":[0,0,0,8]}})"),
                        tg::ErrorCode::kSchemaViolation);
    ok &= expect_reject("sprite region x<0",
                        scene_with(R"({"id":"a","sprite":{"texture":"t.png","region":[-1,0,8,8]}})"),
                        tg::ErrorCode::kSchemaViolation);
    ok &= expect_reject("sprite region 非 4 元",
                        scene_with(R"({"id":"a","sprite":{"texture":"t.png","region":[0,0,8]}})"),
                        tg::ErrorCode::kSchemaViolation);
    // offset 数值非法
    ok &= expect_reject("sprite offset 非有限",
                        scene_with(R"({"id":"a","sprite":{"texture":"t.png","offset":[1e39,0]}})"),
                        tg::ErrorCode::kSchemaViolation);
    return ok;
}

bool test_animations() {
    bool ok = true;
    auto scene_anim = [](const std::string& anim_json) {
        return std::string(R"({"format":"tro-scene","version":2,"tilemap":{"layers":[]},
            "entities":[{"id":"a","animations":)") +
               anim_json + R"(}]})";
    };
    // null / 非 object
    ok &= expect_reject("animations null", scene_anim("null"),
                        tg::ErrorCode::kSchemaViolation);
    ok &= expect_reject("animations 非 object", scene_anim("[1]"),
                        tg::ErrorCode::kSchemaViolation);
    // 未知键
    ok &= expect_reject("animations 未知键",
                        scene_anim(R"({"textures":[],"animations":[],"x":1})"),
                        tg::ErrorCode::kSchemaViolation);
    // 缺 textures 或 animations
    ok &= expect_reject("animations 缺 textures", scene_anim(R"({"animations":[]})"),
                        tg::ErrorCode::kSchemaViolation);
    ok &= expect_reject("animations 缺 animations", scene_anim(R"({"textures":[]})"),
                        tg::ErrorCode::kSchemaViolation);
    // textures 路径非法
    ok &= expect_reject("animations texture 路径非法",
                        scene_anim(R"({"textures":["../x.png"],"animations":[]})"),
                        tg::ErrorCode::kSchemaViolation);
    // clip 未知键
    ok &= expect_reject("clip 未知键",
                        scene_anim(R"({"textures":["a.png"],
                            "animations":[{"name":"idle","fps":6,"frames":[],
                                           "junk":1}]})"),
                        tg::ErrorCode::kSchemaViolation);
    // clip name 重复
    ok &= expect_reject("clip name 重复",
                        scene_anim(R"({"textures":["a.png"],
                            "animations":[{"name":"idle","fps":6,"frames":[]},
                                          {"name":"idle","fps":6,"frames":[]}]})"),
                        tg::ErrorCode::kSchemaViolation);
    // fps 非正
    ok &= expect_reject("clip fps<=0",
                        scene_anim(R"({"textures":["a.png"],
                            "animations":[{"name":"idle","fps":0,"frames":[]}]})"),
                        tg::ErrorCode::kSchemaViolation);
    ok &= expect_reject("clip fps 非有限",
                        scene_anim(R"({"textures":["a.png"],
                            "animations":[{"name":"idle","fps":1e39,"frames":[]}]})"),
                        tg::ErrorCode::kSchemaViolation);
    // frames 缺省
    ok &= expect_reject("clip 缺 frames",
                        scene_anim(R"({"textures":["a.png"],
                            "animations":[{"name":"idle","fps":6}]})"),
                        tg::ErrorCode::kSchemaViolation);
    // frame texture 索引越界
    ok &= expect_reject("frame texture 越界",
                        scene_anim(R"({"textures":["a.png"],
                            "animations":[{"name":"idle","fps":6,
                                           "frames":[{"texture":1}]}]})"),
                        tg::ErrorCode::kSchemaViolation);
    // frame 未知键 / texture 缺失
    ok &= expect_reject("frame 缺 texture",
                        scene_anim(R"({"textures":["a.png"],
                            "animations":[{"name":"idle","fps":6,
                                           "frames":[{"region":[0,0,8,8]}]}]})"),
                        tg::ErrorCode::kSchemaViolation);
    ok &= expect_reject("frame 未知键",
                        scene_anim(R"({"textures":["a.png"],
                            "animations":[{"name":"idle","fps":6,
                                           "frames":[{"texture":0,"junk":1}]}]})"),
                        tg::ErrorCode::kSchemaViolation);
    // 空 textures + 非空 clips → 帧引用越界
    ok &= expect_reject("空 textures 非空 clips",
                        scene_anim(R"({"textures":[],
                            "animations":[{"name":"idle","fps":6,
                                           "frames":[{"texture":0}]}]})"),
                        tg::ErrorCode::kSchemaViolation);
    // 合法内嵌动画（帧 offset/region 全规则通过）
    ok &= expect_ok("合法内嵌动画",
                    scene_anim(R"({"textures":["a.png"],
                        "animations":[{"name":"idle","fps":6,"loop":true,
                                       "frames":[{"texture":0,
                                                  "region":[0,0,16,16],
                                                  "offset":[1,-2]}]}]})"));
    return ok;
}

bool test_payload_limits() {
    bool ok = true;
    // 深度超限：tilemap 内嵌套 40 层（> kJsonDepthMax=32），用 nlohmann 构造防拼接错
    {
        json root = {
            {"format", "tro-scene"}, {"version", 2}, {"entities", json::array()},
        };
        json tm = json::object();
        json deep = 0;
        for (int i = 0; i < 40; ++i) deep = json::array({deep});
        tm["a"] = deep;
        root["tilemap"] = tm;
        std::string text = root.dump();
        ok &= expect_reject("深度超限", text, tg::ErrorCode::kSchemaViolation);
    }
    // 键数超限：tilemap 内构造 1025 键对象（> kPayloadKeysMax=1024）
    {
        json root = {
            {"format", "tro-scene"}, {"version", 2}, {"entities", json::array()},
        };
        json tm = json::object();
        for (int i = 0; i < 1025; ++i) tm["k" + std::to_string(i)] = i;
        root["tilemap"] = tm;
        std::string text = root.dump();
        ok &= expect_reject("键数超限", text, tg::ErrorCode::kSchemaViolation);
    }

    // embedded NUL：name 里 \u0000
    ok &= expect_reject("文本内嵌 NUL",
                        R"({"format":"tro-scene","version":2,"tilemap":{"layers":[]},
                            "entities":[{"id":"a\u0000b"}]})",
                        tg::ErrorCode::kSchemaViolation);
    return ok;
}

bool test_positive_regressions() {
    bool ok = true;
    // 真实资产回归（公共路径）
    ok &= expect_ok("demo.json（palette 模式）",
                    [] {
                        std::ifstream in("assets/scenes/demo.json", std::ios::binary);
                        return std::string((std::istreambuf_iterator<char>(in)),
                                           std::istreambuf_iterator<char>());
                    }());
    ok &= expect_ok("soldier.json（bare + 内嵌动画）",
                    [] {
                        std::ifstream in("assets/scenes/soldier_animated_sprite_2d.json",
                                         std::ios::binary);
                        return std::string((std::istreambuf_iterator<char>(in)),
                                           std::istreambuf_iterator<char>());
                    }());
    ok &= expect_ok("test.json（图集模式，引真实 tilesets/test{,_1}.json）",
                    [] {
                        std::ifstream in("assets/scenes/test.json", std::ios::binary);
                        return std::string((std::istreambuf_iterator<char>(in)),
                                           std::istreambuf_iterator<char>());
                    }());
    return ok;
}

// ── 图集 col/row 元数据（评审修复①；plan-5.3 §5 布局契约） ──
// tileset path 相对 assets/ 解析（scene_asset.cpp 约定），临时 tileset 写
// assets/ 根下、场景写 build/；tiles[].col/row 决定图集内矩形位置（非顺序
// 排列也须精确解析），缺失 col/row 必须拒绝载入。
bool test_atlas_col_row_meta() {
    bool ok = true;
    const std::string ts_path = "assets/__tmp_meta_ts.json";
    const std::string bad_ts_path = "assets/__tmp_meta_bad.json";
    const std::string sc_path = "build/tmp_meta_scene.json";
    const std::string bad_sc_path = "build/tmp_meta_bad_scene.json";
    const auto write = [](const std::string& p, const char* content) {
        std::ofstream f(p, std::ios::binary);
        f << content;
    };

    // 合法 tileset：id=0 在 (col=3,row=2)，id=1 在 (col=0,row=0)（非顺序演示）
    write(ts_path, R"({"format":"tro-tileset","version":2,
        "texture":"textures/floor.png","tile_width":16,"tile_height":16,
        "columns":16,"rows":16,
        "tiles":[{"id":0,"col":3,"row":2},{"id":1,"col":0,"row":0}]})");
    write(sc_path, R"({"format":"tro-scene","version":2,"tilemap":{
        "tile_width":16,"tile_height":16,
        "tilesets":[{"name":"ts","path":"__tmp_meta_ts.json"}],
        "layers":[{"name":"g","width":2,"height":1,"solid":false,"tileset":"ts",
                   "tiles":[0,1]}]},
        "entities":[]})");
    // 加载成功即说明 col/row 解析接受（矩形表在私有元数据，公共 API 不能直读；
    // 位置正确性另由带窗口截图验证，此处为解析接受/拒绝门禁）
    auto r = SceneAsset::load(sc_path);
    if (!r) {
        ::tg_test::record_failure(__FILE__, __LINE__,
                                  "atlas col/row scene load failed: " +
                                      r.error().message);
    }
    std::remove(sc_path.c_str());
    std::remove(ts_path.c_str());

    // 缺 col 的 tileset → 拒绝
    write(bad_ts_path, R"({"format":"tro-tileset","version":2,"texture":"textures/floor.png",
        "tile_width":16,"tile_height":16,"columns":16,"rows":16,
        "tiles":[{"id":0}]})");
    write(bad_sc_path, R"({"format":"tro-scene","version":2,"tilemap":{
        "tile_width":16,"tile_height":16,
        "tilesets":[{"name":"ts","path":"__tmp_meta_bad.json"}],
        "layers":[{"name":"g","width":1,"height":1,"solid":false,"tileset":"ts",
                   "tiles":[0]}]},
        "entities":[]})");
    {
        std::ifstream in(bad_sc_path, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        ok &= expect_reject("tileset 缺 col（拒载）", text,
                            tg::ErrorCode::kSchemaViolation);
    }
    std::remove(bad_sc_path.c_str());
    std::remove(bad_ts_path.c_str());
    return ok;
}

// tro-tileset 只增可选字段（plan-8 §3.1）：size_in_atlas / texture_origin /
// y_sort_origin。正例（全缺省=旧格式 / 部分带 / 全带）可载；非法类型/长度/值域
// 逐项拒绝（每条校验规则一个反例，含防御性上限）。经公共 SceneAsset::load 完整
// 路径；region/origin 数值正确性公共 API 不直读私有表，由运行期截图验收兜底。
bool test_tileset_visual_fields() {
    bool ok = true;
    const auto write = [](const std::string& p, const std::string& content) {
        std::ofstream f(p, std::ios::binary);
        f << content;
    };
    const std::string ts_path = "assets/__tmp_vis_ts.json";
    // 场景模板：引用 assets/__tmp_vis_ts.json 的 1×1 图集层（tileset 路径相对 assets/）
    const std::string scene_json =
        R"({"format":"tro-scene","version":2,"tilemap":{)"
        R"("tile_width":16,"tile_height":16,)"
        R"("tilesets":[{"name":"ts","path":"__tmp_vis_ts.json"}],)"
        R"("layers":[{"name":"g","width":1,"height":1,"solid":false,)"
        R"("tileset":"ts","tiles":[0]}]},"entities":[]})";
    const auto tileset_with = [](const std::string& tile_obj) {
        return R"({"format":"tro-tileset","version":2,)"
               R"("texture":"textures/floor.png","tile_width":16,"tile_height":16,)"
               R"("columns":16,"rows":16,"tiles":[)" + tile_obj + "]}";
    };
    struct Case {
        const char* label;
        const char* tile;  // tiles[0] JSON 片段
        bool want_ok;
    };
    const Case cases[] = {
        {"visual 旧格式全缺省", R"({"id":0,"col":10,"row":10})", true},
        {"visual 三字段全带",
         R"({"id":0,"col":10,"row":10,"size_in_atlas":[3,5],)"
         R"("texture_origin":[-2,30],"y_sort_origin":-4})",
         true},
        {"visual 部分带 size_in_atlas",
         R"({"id":0,"col":1,"row":1,"size_in_atlas":[2,2]})", true},
        {"visual 零原点可写",
         R"({"id":0,"col":1,"row":1,"texture_origin":[0,0]})", true},
        {"visual size 非数组", R"({"id":0,"col":1,"row":1,"size_in_atlas":3})", false},
        {"visual size 长度 1", R"({"id":0,"col":1,"row":1,"size_in_atlas":[3]})", false},
        {"visual size 非 int",
         R"({"id":0,"col":1,"row":1,"size_in_atlas":[2.5,1]})", false},
        {"visual size 零", R"({"id":0,"col":1,"row":1,"size_in_atlas":[0,1]})", false},
        {"visual size 超上限",
         R"({"id":0,"col":1,"row":1,"size_in_atlas":[4097,1]})", false},
        {"visual origin 非数组", R"({"id":0,"col":1,"row":1,"texture_origin":5})", false},
        {"visual origin 非 int",
         R"({"id":0,"col":1,"row":1,"texture_origin":[-2,3.5]})", false},
        {"visual origin 正向超限",
         R"({"id":0,"col":1,"row":1,"texture_origin":[70000,0]})", false},
        {"visual origin 负向超限",
         R"({"id":0,"col":1,"row":1,"texture_origin":[0,-70000]})", false},
        {"visual ysort 非 int",
         R"({"id":0,"col":1,"row":1,"y_sort_origin":"x"})", false},
        {"visual ysort 超上限",
         R"({"id":0,"col":1,"row":1,"y_sort_origin":70000})", false},
    };
    for (const auto& c : cases) {
        write(ts_path, tileset_with(c.tile));
        if (c.want_ok)
            ok &= expect_ok(c.label, scene_json);
        else
            ok &= expect_reject(c.label, scene_json,
                                tg::ErrorCode::kSchemaViolation);
    }
    std::remove(ts_path.c_str());
    return ok;
}

}  // namespace

// 注意：build/ 目录必须存在（写临时文件用）。ctest working dir = 项目根。
int main() {
    std::filesystem::create_directories("build");  // CWD=项目根；ctest WORKING_DIRECTORY 保证

    test_root_and_keys();
    test_modes();
    test_tilesets();
    test_palette_layers();
    test_tiles_array();
    test_entities();
    test_sprite();
    test_animations();
    test_payload_limits();
    test_positive_regressions();
    test_atlas_col_row_meta();
    test_tileset_visual_fields();

    std::printf("[schema test] checks=%d failures=%d\n", ::tg_test::g_checks,
                ::tg_test::g_failures);
    return ::tg_test::g_failures == 0 ? 0 : 1;
}