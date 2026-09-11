// anim_tween_test.cpp —— 动画播放器与 Tween 虚拟时钟单测。
//
// 无窗口、纯逻辑（不触 GPU）：给定 fps/loop/速度/seek 推进 → 帧索引序列正确；
// on_frame 在新帧触发一次；非 loop 播完 on_finish + done；Tween 缓动/延迟/
// 循环采样关键点符合 easing 值；cancel 停止后续；wait/done 协程完成后 resume。
// 播放器数据源：真实 soldier 资产（内嵌 7 动画 43 帧）+ 内联构造的动画集场景。
#include <cstdio>
#include <cstring>
#include <memory>  // std::unique_ptr
#include <string>

#include "trogue/trogue.hpp"
#include "test_util.hpp"

namespace {

using tg::AnimationPlayer;
using tg::SceneAsset;
using tg::TweenManager;
using tg::TweenSpec;

// ── 内联动画集场景（模拟 asset 内数据） ──
// 三 clip：two（fps=2、2 帧、非 loop）；loop3（fps=1、3 帧、loop）；
// empty（0 帧、非 loop —— 空帧 clip，schema 允许载入，播放视为即时完成）
const SceneAsset& inline_asset() {
    static SceneAsset asset = [] {
        static int seq = 0;
        const std::string path = "build/tmp_animset_" + std::to_string(seq++) + ".json";
        std::FILE* f = std::fopen(path.c_str(), "wb");
        std::fputs(R"({"format":"tro-scene","version":2,"tilemap":{"layers":[]},
            "entities":[{"id":"s","type":"x","x":0,"y":0,"w":16,"h":16,
                "animations":{"textures":["a.png","b.png"],
                    "animations":[
                        {"name":"two","fps":2.0,"loop":false,
                         "frames":[{"texture":0},{"texture":1}]},
                        {"name":"loop3","fps":1.0,"loop":true,
                         "frames":[{"texture":0},{"texture":1},{"texture":0}]},
                        {"name":"empty","fps":2.0,"loop":false,
                         "frames":[]}]}}]})",
                  f);
        std::fclose(f);
        auto r = SceneAsset::load(path);
        std::remove(path.c_str());
        if (!r) {
            std::fprintf(stderr, "inline anim set load failed: %s\n",
                         r.error().message.c_str());
            std::abort();
        }
        return std::move(*r);
    }();
    return asset;
}

// ── 空帧 clip：播放即时完成（回归：不得永不结束 / done 挂死） ──
bool test_player_empty_clip() {
    bool ok = true;
    const SceneAsset& asset = inline_asset();
    REQUIRE(asset.animation_set_count() == 1);
    const auto& set = asset.animation_set(0);
    REQUIRE(set.has_clip("empty"));

    // 空帧非 loop：首个 advance 即完成（on_finish 一次 + done 立即完成）
    AnimationPlayer p;
    p.bind(set);
    int finish_count = 0;
    p.on_finish([&] { ++finish_count; });
    CHECK(p.play("empty"));
    CHECK(p.playing());
    CHECK(!p.advance(0.1));      // 首拍即完成 → 返回 false
    CHECK(!p.playing());
    CHECK(finish_count == 1);
    CHECK(p.current_frame().has == false);  // 无帧可播 → 无视觉
    auto d = p.done();
    d.start();
    CHECK(d.done());             // 已终止 → done 立即完成（不挂死）

    // 空帧 loop 同样即时完成（无帧可播的循环无意义）
    AnimationPlayer p2;
    p2.bind(set);
    p2.looping(true);
    CHECK(p2.play("empty"));
    CHECK(p2.advance(1000.0) == false);
    CHECK(!p2.playing());
    // 再次推进（已停止）→ false，不二次触发 finish
    CHECK(!p2.advance(0.1));
    return ok;
}

// ── 帧序列：非 loop / loop / 无效名 ──
bool test_player_frame_sequence() {
    bool ok = true;
    const SceneAsset& asset = inline_asset();
    REQUIRE(asset.animation_set_count() == 1);
    const auto& set = asset.animation_set(0);

    AnimationPlayer p;
    p.bind(set);
    CHECK(p.valid());

    // two: fps=2、2 帧非 loop。t: 0→帧0；0.25→帧0；0.5→帧1；1.0 播完
    CHECK(p.play("two"));
    CHECK(p.frame_index() == 0);
    CHECK(p.advance(0.25));
    CHECK(p.frame_index() == 0);
    CHECK(p.advance(0.25));
    CHECK(p.frame_index() == 1);
    CHECK(!p.advance(0.5));      // t=1.0 越过 duration（非 loop）→ 完成 false
    CHECK(!p.playing());
    CHECK(p.frame_index() == 1); // 播完停在末帧（评审小问题①：不回卷首帧）

    // loop3：fps=1，3 帧循环
    AnimationPlayer p2;
    p2.bind(set);
    CHECK(p2.play("loop3"));
    CHECK(p2.frame_index() == 0);
    p2.advance(1.5);            // t=1.5 → 帧1
    CHECK(p2.frame_index() == 1);
    p2.advance(1.5);            // t=3.0 → fmod(3,3)=0 → 帧0（回卷）
    CHECK(p2.frame_index() == 0);

    // 无效名
    AnimationPlayer p3;
    p3.bind(set);
    CHECK(!p3.play("nope"));
    return ok;
}

// ── 事件 / done ──
bool test_player_events_and_done() {
    bool ok = true;
    const SceneAsset& asset = inline_asset();
    REQUIRE(asset.animation_set_count() == 1);
    const auto& set = asset.animation_set(0);

    AnimationPlayer p;
    p.bind(set);
    int frames_seen = 0, last = -1, finish_count = 0;
    p.on_frame([&](int idx) { ++frames_seen; last = idx; });
    p.on_finish([&] { ++finish_count; });

    CHECK(p.play("two"));
    p.advance(0.3);   // t=0.3 → 帧0（fps2：floor(0.6)=0）
    CHECK(frames_seen == 1 && last == 0);
    p.advance(0.3);   // t=0.6 → 帧1
    CHECK(frames_seen == 2 && last == 1);
    p.advance(0.1);   // t=0.7 仍帧1 → 不重复触发
    CHECK(frames_seen == 2);
    p.advance(1000);  // 播完
    CHECK(finish_count == 1);
    CHECK(!p.playing());

    // done()：播完已 set → 立即完成
    auto d1 = p.done();
    d1.start();
    CHECK(d1.done());

    // 播放中 done 挂起；stop → 完成
    AnimationPlayer p2;
    p2.bind(set);
    p2.play("loop3");
    auto d2 = p2.done();
    d2.start();
    CHECK(!d2.done());
    p2.stop();
    CHECK(d2.done());

    // 未绑定 → done 立即完成
    AnimationPlayer p3;
    auto d3 = p3.done();
    d3.start();
    CHECK(d3.done());
    return ok;
}

// ── 当前帧视觉描述 ──
bool test_player_current_frame() {
    bool ok = true;
    const SceneAsset& asset = inline_asset();
    REQUIRE(asset.animation_set_count() == 1);
    const auto& set = asset.animation_set(0);

    AnimationPlayer p;
    p.bind(set);
    CHECK(p.play("loop3"));
    auto fr = p.current_frame();
    CHECK(fr.has);
    CHECK(fr.asset_id == asset.asset_id());  // 归属填充
    CHECK(fr.texture == "a.png");
    CHECK(fr.region.w == 0);                 // 整图
    return ok;
}

// ── Tween：float 线性 ──
bool test_tween_float() {
    bool ok = true;
    TweenManager m;
    TweenSpec spec; spec.duration = 1.0;
    float last = -1;
    const auto id = m.add_float(0, 10, spec, [&](const TweenManager::Sample<float>& s) {
        last = s.value;
    });
    CHECK(m.alive(id));
    m.tick(0.5);
    CHECK(last > 4.99 && last < 5.01);   // 线性中值 5
    CHECK(m.alive(id));                   // 未完成
    m.tick(0.5);
    CHECK(last > 9.99 && last < 10.01);
    CHECK(!m.alive(id));                  // 完成移除
    return ok;
}

// ── Tween：delay + repeats ──
bool test_tween_delay_repeat() {
    bool ok = true;
    TweenManager m;
    TweenSpec spec; spec.duration = 1.0; spec.delay = 0.5; spec.repeats = 2;
    int updates = 0, completes = 0;
    m.add_float(0, 1, spec, [&](const TweenManager::Sample<float>&) { ++updates; },
                [&] { ++completes; });
    m.tick(0.4);           // delay 段：无采样
    CHECK(updates == 0);
    m.tick(0.3);           // t=0.7 → 采样段 0.2
    CHECK(updates == 1);
    // 推进到全部完成（repeats=2 → 共 3 段；每段完成回调一次）
    while (m.alive(1)) m.tick(1.0);
    CHECK(completes == 1);
    // updates = 1 次中间采样（t=0.2）+ 3 次段完成回调 = 4
    CHECK(updates == 4);
    return ok;
}

// ── Tween：cancel / wait 协程 ──
bool test_tween_cancel_wait() {
    bool ok = true;
    TweenManager m;
    TweenSpec spec; spec.duration = 1.0;
    const auto id = m.add_float(0, 1, spec, [](const TweenManager::Sample<float>&) {});
    CHECK(m.alive(id));
    CHECK(m.cancel(id));
    CHECK(!m.alive(id));
    CHECK(!m.cancel(id));          // 已取消 → false

    // wait：未完成挂起 → cancel_all 后完成
    TweenManager m2;
    TweenSpec spec2; spec2.duration = 10.0;
    const auto id2 = m2.add_float(0, 1, spec2, [](const TweenManager::Sample<float>&) {});
    auto w = m2.wait(id2);
    w.start();
    CHECK(!w.done());
    m2.cancel_all();
    CHECK(w.done());

    // wait 不存在 id → 立即完成
    TweenManager m3;
    auto w2 = m3.wait(999);
    w2.start();
    CHECK(w2.done());

    // 生命周期：manager 销毁后已完成补间的 done 信号不悬垂（shared_ptr 持份）
    // （「宿主存活期标志」；ASan 下验证无 UAF）
    std::unique_ptr<TweenManager> m4 = std::make_unique<TweenManager>();
    TweenSpec spec4; spec4.duration = 1.0;
    const auto id4 = m4->add_float(0, 1, spec4,
                                   [](const TweenManager::Sample<float>&) {});
    auto w4 = m4->wait(id4);
    w4.start();
    m4->tick(2.0);   // 完成 → done set → 协程 resume 完成
    m4.reset();      // 销毁 manager：done 事件被 shared_ptr 持有，协程不受影响
    CHECK(w4.done());
    return ok;
}

// ── Tween 回调内再入 manager 安全（回归：两阶段 tick 不失效迭代器） ──
// on_update 里 add 新补间 + cancel_all：两阶段收集后统一执行，tick 不 UB。
bool test_tween_reentrant_callbacks() {
    bool ok = true;
    TweenManager m;
    TweenSpec spec; spec.duration = 0.5;
    bool reentered = false;
    int added_id = 0;
    // 补间 A：on_update 首次采样时再入 add 补间 B + cancel_all
    const auto id_a =
        m.add_float(0, 1, spec, [&](const TweenManager::Sample<float>&) {
            if (!reentered) {
                reentered = true;
                added_id = static_cast<int>(m.add_float(
                    0, 1, spec, [](const TweenManager::Sample<float>&) {}));
                m.cancel_all();  // 再入取消（含本补间；阶段 2 内安全）
            }
        });
    CHECK(id_a != 0);
    m.tick(0.5);  // 越过 duration → 完成分支收集 → 阶段 2 执行回调（再入）
    CHECK(reentered);                 // 回调在阶段 2 执行过
    CHECK(added_id != 0);             // 再入的 add 成功分配 id（无崩溃）
    CHECK(!m.alive(id_a));            // 原补间已取消/完成
    // 再入期间新加的补间 B 也被 cancel_all 取消 → 不再活跃
    // （无法直接拿到 B 的 id，取消后 tick 不应崩溃）
    m.tick(0.5);
    // cancel 后 tick 安全；再正常走一遍 add/完成验证状态机未损坏
    TweenManager m2;
    float last = -1;
    const auto id2 = m2.add_float(0, 10, spec, [&](const TweenManager::Sample<float>& s) {
        last = s.value;
    });
    m2.tick(1.0);
    CHECK(last > 9.99f && last < 10.01f);
    CHECK(!m2.alive(id2));
    return ok;
}

// ── 暂停查询与冻结语义（viewer 的 toggle 依赖 paused() 单一事实源） ──
bool test_player_pause() {
    bool ok = true;
    const SceneAsset& asset = inline_asset();
    REQUIRE(asset.animation_set_count() == 1);
    const auto& set = asset.animation_set(0);

    // 初值 / pause / resume / play 清除
    AnimationPlayer p;
    p.bind(set);
    CHECK(!p.paused());
    CHECK(p.play("loop3"));
    CHECK(!p.paused());
    p.pause();
    CHECK(p.paused());
    CHECK(p.playing());            // 暂停中仍处「播放状态」（时间挂起）
    p.resume();
    CHECK(!p.paused());
    p.pause();
    CHECK(p.play("two"));          // play() 清除暂停（切 clip 即恢复播放，引擎语义）
    CHECK(!p.paused());

    // 冻结回归钉：pause 后 advance() 挂起（返回 true）但时间不推进 →
    // frame_index 不变、current_frame 恒有效且内容不变
    AnimationPlayer p2;
    p2.bind(set);
    CHECK(p2.play("loop3"));       // fps=1 → 1s/帧
    p2.advance(1.5);               // t=1.5 → 帧1
    CHECK(p2.frame_index() == 1);
    p2.pause();
    const auto frozen = p2.current_frame();
    CHECK(frozen.has);
    for (int i = 0; i < 5; ++i) {
        CHECK(p2.advance(0.5));    // 挂起：true（仍在播放状态）
        CHECK(p2.frame_index() == 1);
        const auto f = p2.current_frame();
        CHECK(f.has);
        CHECK(f.texture == frozen.texture);
    }
    // 冻结期不积累时间：resume 后 t=3.0 → fmod 回卷帧0（若冻结期误积累，
    // fmod 后必落非 0 帧——此断言即回归钉）
    p2.resume();
    p2.advance(1.5);
    CHECK(p2.frame_index() == 0);
    return ok;
}

// ── loop 覆盖跨 play() 粘连回归（B1）──
// 显式 looping() 覆盖仅作用于当前 clip；play() 切换 clip 必须复位为
// clip 自身 loop，否则上一段的覆盖会粘到下一段（非循环动画永不完成、
// 循环动画播完即停）。
bool test_player_loop_override_switch() {
    bool ok = true;
    const SceneAsset& asset = inline_asset();
    REQUIRE(asset.animation_set_count() == 1);
    const auto& set = asset.animation_set(0);

    // ① 过覆盖 false 不得粘到「自身 loop=true」的 clip
    AnimationPlayer p;
    p.bind(set);
    CHECK(p.play("loop3"));       // 自身 loop=true
    CHECK(p.looping(false));      // 覆盖为不循环（仅本 clip）
    p.advance(1000);              // 覆盖生效：播完
    CHECK(!p.playing());

    CHECK(p.play("two"));         // 非循环 clip
    p.advance(1000);
    CHECK(!p.playing());

    CHECK(p.play("loop3"));       // 重新播循环 clip，未显式 looping()
    CHECK(p.playing());
    p.advance(3.5);               // 越过一个 3s 周期
    CHECK(p.playing());           // 修复前：粘住的 false → 停在末帧（回归钉）

    // ② 过覆盖 true 不得粘到「自身 loop=false」的 clip
    AnimationPlayer q;
    q.bind(set);
    CHECK(q.play("loop3"));
    CHECK(q.looping(true));
    q.advance(0.5);
    CHECK(q.play("two"));         // 切到非循环 clip
    q.advance(1000);
    CHECK(!q.playing());          // 修复前：粘住的 true → 永不完成

    // ③ restart_if_same=false 且同名在播：不清覆盖（同一 clip 继续）
    AnimationPlayer r;
    r.bind(set);
    CHECK(r.play("loop3"));
    CHECK(r.looping(false));
    CHECK(r.play("loop3", /*restart_if_same=*/false));  // 同名继续
    r.advance(1000);
    CHECK(!r.playing());          // 覆盖 false 保留（仅真正切 clip 才复位）
    return ok;
}

}  // namespace

int main() {
    test_player_frame_sequence();
    test_player_events_and_done();
    test_player_current_frame();
    test_player_empty_clip();
    test_player_pause();
    test_player_loop_override_switch();
    test_tween_float();
    test_tween_delay_repeat();
    test_tween_cancel_wait();
    test_tween_reentrant_callbacks();
    std::printf("[anim/tween test] checks=%d failures=%d\n", ::tg_test::g_checks,
                ::tg_test::g_failures);
    return ::tg_test::g_failures == 0 ? 0 : 1;
}