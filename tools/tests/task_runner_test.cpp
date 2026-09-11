// task_runner_test.cpp —— TaskRunner 容器语义单测（纯公共 API）。
//
// 覆盖：push + pump_all 至完成 + 回收；多 task 并发推进；cancel_all 后不再推进；
// active_count 语义；与动画 done()/Tween wait() 协程联合（B1 回归：非循环动画
// 播完 done() 必须 resume，否则协程永久挂起）。
#include <cstdio>
#include <string>

#include "trogue/trogue.hpp"
#include "test_util.hpp"

namespace {

using tg::AnimationPlayer;
using tg::SceneAsset;
using tg::TaskRunner;
using tg::TweenManager;
using tg::TweenSpec;

// 单实体两 clip：nonloop（fps=2，2 帧，非 loop）；loop（fps=1，3 帧，loop）
const SceneAsset& inline_asset() {
    static SceneAsset asset = [] {
        const char* js = R"({"format":"tro-scene","version":2,
            "tilemap":{"layers":[]},
            "entities":[{"id":"s","type":"x","x":0,"y":0,"w":16,"h":16,
                "animations":{"textures":["a.png"],
                    "animations":[
                        {"name":"nonloop","fps":2.0,"loop":false,
                         "frames":[{"texture":0},{"texture":0}]},
                        {"name":"loop","fps":1.0,"loop":true,
                         "frames":[{"texture":0},{"texture":0},{"texture":0}]}]}}]})";
        auto r = SceneAsset::load_json(js, "<task_runner_test>");
        if (!r) {
            std::fprintf(stderr, "inline asset load failed: %s\n",
                         r.error().diagnostics().c_str());
            std::abort();
        }
        return std::move(*r);
    }();
    return asset;
}

// 协程在多个 pump 后才完成 → 验证 active_count 与分拍推进
tg::task<> staged(tg::single_consumer_event& ev, int& steps) {
    ++steps;
    co_await ev;
    ++steps;
}

bool test_push_pump_recycle() {
    bool ok = true;
    TaskRunner runner;
    CHECK(runner.active_count() == 0);

    tg::single_consumer_event ev;
    int steps = 0;
    runner.push(staged(ev, steps));
    CHECK(runner.active_count() == 1);

    runner.pump_all();               // 启动：执行到挂起点
    CHECK(steps == 1);
    CHECK(runner.active_count() == 1);

    runner.pump_all();               // 已挂起等事件：本拍不重复 resume
    CHECK(steps == 1);
    CHECK(runner.active_count() == 1);

    ev.set();                        // 唤醒等待（同步 resume → 协程完成）
    runner.pump_all();               // 回收已完成
    CHECK(steps == 2);
    CHECK(runner.active_count() == 0);  // 完成后回收

    // 多个 task 并发
    tg::single_consumer_event e2, e3;
    int s2 = 0, s3 = 0;
    runner.push(staged(e2, s2));
    runner.push(staged(e3, s3));
    CHECK(runner.active_count() == 2);
    runner.pump_all();
    CHECK(s2 == 1 && s3 == 1);
    e2.set();
    runner.pump_all();
    CHECK(runner.active_count() == 1);  // e2 完成回收，e3 仍挂起
    e3.set();
    runner.pump_all();
    CHECK(runner.active_count() == 0);

    // cancel_all：销毁未完成 task 帧。契约（见头注释）：此后不得再由宿主
    // set() 对应等待事件——典型关机顺序为 cancel_all() → 销毁宿主。此处只断言
    // 销毁后容器为空、pump 安全；**不**再 set(e4)（那会 resume 已销毁帧，属违约）。
    tg::single_consumer_event e4;
    int s4 = 0;
    runner.push(staged(e4, s4));
    runner.pump_all();
    CHECK(s4 == 1);
    runner.cancel_all();
    CHECK(runner.active_count() == 0);
    runner.pump_all();               // 无 task，安全
    CHECK(s4 == 1);                  // 已销毁 → 不再推进
    (void)e4;                        // e4 不再 set：宿主随后销毁（顺序契约）
    return ok;
}

// 与动画 done() 联合：非循环播完 → done 协程经 runner pump 完成（B1 回归）
bool test_with_animation_done() {
    bool ok = true;
    const SceneAsset& asset = inline_asset();
    REQUIRE(asset.animation_set_count() == 1);
    const auto& set = asset.animation_set(0);

    AnimationPlayer player;
    player.bind(set);
    CHECK(player.play("nonloop"));

    TaskRunner runner;
    bool finished = false;
    auto wait_done = [&]() -> tg::task<> {
        co_await player.done();
        finished = true;
    };
    runner.push(wait_done());
    runner.pump_all();
    CHECK(!finished);                // 播放中：挂起

    // 推进到非循环播完（advance 触发 finish → done 事件同步 resume 等待协程）
    for (int i = 0; i < 3 && player.playing(); ++i) {
        player.advance(0.5);
    }
    CHECK(!player.playing());
    runner.pump_all();               // 回收已完成的协程
    CHECK(finished);                 // B1 未修时（loop 覆盖粘连）done 事件不 set → 永不完成
    CHECK(runner.active_count() == 0);
    return ok;
}

// 与 Tween wait() 联合：完成 → runner pump resume
bool test_with_tween_wait() {
    bool ok = true;
    TweenManager mgr;
    TweenSpec spec;
    spec.duration = 1.0;
    const auto id = mgr.add_float(0, 1, spec,
                                  [](const TweenManager::Sample<float>&) {});

    TaskRunner runner;
    bool done_flag = false;
    auto waiter = [&]() -> tg::task<> {
        co_await mgr.wait(id);
        done_flag = true;
    };
    runner.push(waiter());
    runner.pump_all();
    CHECK(!done_flag);
    mgr.tick(2.0);                   // 完成 → done 事件 set（同步 resume 等待者）
    runner.pump_all();               // 回收已完成
    CHECK(done_flag);
    CHECK(runner.active_count() == 0);
    return ok;
}

}  // namespace

int main() {
    test_push_pump_recycle();
    test_with_animation_done();
    test_with_tween_wait();
    std::printf("[task_runner test] checks=%d failures=%d\n", ::tg_test::g_checks,
                ::tg_test::g_failures);
    return ::tg_test::g_failures == 0 ? 0 : 1;
}
