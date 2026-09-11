#pragma once
// animation.hpp —— 帧动画播放器与只读动画集。
//
// 边界：engine 只做执行原语（帧采样、fps/loop、完成/帧事件、协程等待）；
// 触发/切换/应用归 game；播放器不绘制、不持有/绑定 game 对象。
//
// AnimationSet：只读动画集。scene 内嵌 animations 在 load 期已校验并解析为
// detail::AnimationData（私有 scene_impl.hpp）；
// AnimationSet 是它的 const 视图：存活期 = 所属 asset，公共面只读查询。
// AnimationPlayer：非拥有绑定 `const AnimationSet*`（失效访问 = 安全 no-op，
// 播放器记录绑定有效性，不崩溃）。
//
// 协程等待：`player.done()` 可 co_await；同一完成事件至多一个等待协程
// （single_consumer）；宿主（asset/player/manager）必须先于协程
// 销毁，或先 stop()/取消使等待即时完成——由 game 保证。推进容器用
// 引擎的 tg::TaskRunner（启动/回收；见 task_runner.hpp）。

#include <cstdint>   // std::uint64_t
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "trogue/coro.hpp"   // tg::task / single_consumer_event
#include "trogue/scene.hpp"  // SpriteDesc

namespace tg {

// ── 动画数据模型：私有（detail） ──
namespace detail {
struct AnimData;   // 定义于私有 scene_impl.hpp（含 frames/textures/clips）
struct AnimClip;   // 同上
}

// ════════════════════ AnimationSet（只读动画集视图） ════════════════════

class AnimationSet {
public:
    AnimationSet() = default;  // 空集（无效绑定源）

    // 名 = 所属 entity 的 id（entity→动画集映射）；空视图为空串
    std::string_view name() const;

    int clip_count() const;
    // 无该索引 → nullopt
    std::optional<std::string_view> clip_name(int index) const;
    bool has_clip(std::string_view clip) const;

private:
    const detail::AnimData* data_ = nullptr;  // 非拥有；存活期 = asset
    friend class AnimationPlayer;
    friend class detail::SceneLoader;         // SceneAsset 解析时构建视图
    void set_data(const detail::AnimData& d) noexcept { data_ = &d; }
};

// ════════════════════ AnimationPlayer ════════════════════

class AnimationPlayer {
public:
    AnimationPlayer() = default;

    // 绑定动画集（非拥有）。绑定后未播放任何 clip；随机访问安全。
    void bind(const AnimationSet& set);

    // 播放某 clip；无效名 → false。restart_if_same=false 时同名 clip 已播则
    // 不重置（继续）。
    bool play(std::string_view clip, bool restart_if_same = true);
    void stop();     // 停止并复位
    void pause() { paused_ = true; }
    void resume() { paused_ = false; }

    // 播放速度倍率（1 = 原始 fps）；s>0 返回 true，否则 false 不变
    bool set_speed(float s);

    // 跳到 clip 内某时刻（秒，0..duration 截断）；无 clip → false
    bool seek(double seconds);

    // 覆盖 clip 的 loop 标记（on/off）；无 clip → false。
    // 覆盖仅作用于**当前 clip**：play() 切换到其它 clip 即复位为 clip 自身
    // loop（不跨 play 保留）——避免上一段动画的 loop 语义粘到下一段。
    bool looping(bool on);

    bool valid() const { return set_ != nullptr; }  // 是否已绑定动画集
    bool playing() const;
    // 暂停查询（对称 playing()）：pause/resume 后调用方凭它决定下一次
    // toggle，避免 game 侧自持 bool 与 play() 清暂停产生状态漂移（单一事实源）
    bool paused() const { return paused_; }
    double time() const { return time_; }
    int frame_index() const;

    // 当前 clip 名；未播放 → 空
    const std::string clip_name() const;

    // 推进：game 每帧（或固定步长）调用；返回是否仍在播放（未播/无 clip → false）
    bool advance(double dt_seconds);

    // 当前帧视觉结果（未播放/无 clip → has=false）；填 asset_id 供归属校验。
    SpriteDesc current_frame() const;

    // 帧事件（可选，默认空）：进入新帧时触发（索引=帧号）。
    // 注意：advance() 触发 on_frame/on_finish 后仍继续本拍剩余逻辑，
    // 回调内改播放器状态（play/seek/stop）可能非直观 —— 推荐回调只做
    // 应用/通知，播放状态切换放在 advance() 返回后。
    void on_frame(std::function<void(int)> cb) { on_frame_ = std::move(cb); }
    // 完成事件：非 loop 播完触发一次
    void on_finish(std::function<void()> cb) { on_finish_ = std::move(cb); }

    // 完成等待（演出脚本）：非 loop 播完/已停止/无 clip → 立即完成；
    // co_await 后由 tg::TaskRunner 推进。single_consumer 纪律。
    tg::task<> done() const;

private:
    const AnimationSet* set_ = nullptr;
    int clip_index_ = -1;       // 当前 clip（<0 = 未播放）
    double time_ = 0.0;         // 播放累积时间（秒）
    float speed_ = 1.0f;
    bool playing_ = false;
    bool paused_ = false;
    bool loop_override_ = false;    // 显式 loop 覆盖
    bool has_loop_override_ = false;
    bool finish_called_ = false;    // 非 loop 播完：on_finish/done 只触发一次
    int last_frame_ = -1;           // 上一帧索引（on_frame 去重）
    mutable single_consumer_event done_evt_;  // done() 等待（mutable：done 为 const）

    std::function<void(int)> on_frame_;
    std::function<void()> on_finish_;

    const detail::AnimClip* cur_clip() const;  // 当前 clip 或 nullptr
};

}  // namespace tg