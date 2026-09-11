// animation.cpp —— AnimationSet 只读视图 + AnimationPlayer 帧采样实现。
//
// 帧采样语义：time_ 为 clip 起始时间累积（秒）；帧索引 =
// floor(time_ * fps)（速度倍率缩放后）；loop 回卷、非 loop 播完置 stopped 并
// 触发 on_finish + done()。current_frame() 只产出视觉描述（texture/region/
// offset/asset_id），不做任何绘制。帧事件：帧索引改变时触发 on_frame。
//
// 生命周期：播放器非拥有绑定 const AnimationSet*；契约 = 绑定 asset 必须先于
// 播放器销毁（否则访问悬垂为调用方错误）。无效/未播放访问为安全 no-op。
#include "trogue/animation.hpp"

#include <algorithm>  // std::clamp
#include <cmath>      // std::floor / std::fmod
#include <cstdint>
#include <string>
#include <utility>

#include "scene_impl.hpp"  // detail::AnimData / AnimClip / frame（解析产物）

namespace tg {

// ════════════════════ AnimationSet（只读视图） ════════════════════

std::string_view AnimationSet::name() const {
    // 动画集名 = 解析时记录的所属 entity id（entity→动画集映射键）。
    return data_ ? std::string_view{data_->name} : std::string_view{};
}

int AnimationSet::clip_count() const {
    return data_ ? static_cast<int>(data_->clips.size()) : 0;
}

std::optional<std::string_view> AnimationSet::clip_name(int index) const {
    if (!data_ || index < 0 || index >= static_cast<int>(data_->clips.size()))
        return std::nullopt;
    return std::string_view{
        data_->clips[static_cast<std::size_t>(index)].name};
}

bool AnimationSet::has_clip(std::string_view clip) const {
    if (!data_) return false;
    for (const auto& c : data_->clips) {
        if (c.name == clip) return true;
    }
    return false;
}

// ════════════════════ AnimationPlayer ════════════════════

void AnimationPlayer::bind(const AnimationSet& set) {
    set_ = &set;
    clip_index_ = -1;
    time_ = 0.0;
    playing_ = false;
    paused_ = false;
    finish_called_ = false;
    last_frame_ = -1;
    // 新绑定 = 全新生命周期：done 事件复位（不可把旧播放周期误判为已完成）
    done_evt_ = single_consumer_event{};
}

bool AnimationPlayer::play(std::string_view clip, bool restart_if_same) {
    if (!set_ || !set_->data_) return false;
    const detail::AnimData& d = *set_->data_;
    for (std::size_t i = 0; i < d.clips.size(); ++i) {
        if (d.clips[i].name != clip) continue;
        // 同名且仍在播：restart=false 时不重置（继续）；否则重播
        if (clip_index_ == static_cast<int>(i) && playing_ &&
            !restart_if_same) {
            return true;
        }
        clip_index_ = static_cast<int>(i);
        time_ = 0.0;
        playing_ = true;
        paused_ = false;
        finish_called_ = false;
        last_frame_ = -1;
        return true;
    }
    return false;  // 无效名/空数据
}

void AnimationPlayer::stop() {
    playing_ = false;
    finish_called_ = true;  // stop 视为「已终止」：后续 done() 立即完成
    done_evt_.set();        // 等待者即时 resume
}

bool AnimationPlayer::set_speed(float s) {
    if (s <= 0) return false;
    speed_ = s;
    return true;
}

bool AnimationPlayer::seek(double seconds) {
    if (clip_index_ < 0 || !set_ || !set_->data_) return false;
    const auto& d = *set_->data_;
    const detail::AnimClip& c =
        d.clips[static_cast<std::size_t>(clip_index_)];
    const double fps = c.fps > 0 ? c.fps : 1.0;
    const double cyc = c.frames.size() / fps;
    time_ = (c.loop && cyc > 0) ? std::fmod(seconds, cyc)
                                : std::clamp(seconds, 0.0, cyc);
    last_frame_ = -1;  // 强制下一帧触发 on_frame
    return true;
}

bool AnimationPlayer::looping(bool on) {
    if (clip_index_ < 0) return false;
    loop_override_ = on;
    has_loop_override_ = true;
    return true;
}

bool AnimationPlayer::playing() const { return playing_; }

int AnimationPlayer::frame_index() const {
    const detail::AnimClip* c = cur_clip();
    if (!c || c->frames.empty()) return -1;
    const double fps = c->fps > 0 ? c->fps : 1.0;
    const int n = static_cast<int>(c->frames.size());
    // 非 loop 播完：停在末帧而非回卷首帧（评审小问题①；time_ 已越过时长，
    // %n 会把 floor(time_*fps) 卷回 0——播完画面保持末帧视觉语义）。
    const bool loop = has_loop_override_ ? loop_override_ : c->loop;
    if (!loop) {
        const double cyc = n / fps;
        if (cyc > 0 && time_ >= cyc) return n - 1;
    }
    return static_cast<int>(std::floor(time_ * fps)) % n;
}

const std::string AnimationPlayer::clip_name() const {
    const detail::AnimClip* c = cur_clip();
    return c ? c->name : "";
}

const detail::AnimClip* AnimationPlayer::cur_clip() const {
    if (clip_index_ < 0 || !set_ || !set_->data_) return nullptr;
    const detail::AnimData& d = *set_->data_;
    if (clip_index_ >= static_cast<int>(d.clips.size())) return nullptr;
    return &d.clips[static_cast<std::size_t>(clip_index_)];
}

bool AnimationPlayer::advance(double dt_seconds) {
    const detail::AnimClip* c = cur_clip();
    if (!c || !playing_) return false;
    if (paused_) return true;  // 挂起：仍在「播放状态」但时间不推进
    if (dt_seconds < 0) dt_seconds = 0;

    // 空帧 clip（schema 允许载入）：无帧可播，播放时长 0 → 首拍即即时完成
    // （空 frames 接受，播放视为即时完成；否则非 loop 的
    // cyc==0 永不满足完成条件 → 播放永不停、done() 永久挂起）。
    if (c->frames.empty()) {
        playing_ = false;
        if (!finish_called_) {
            finish_called_ = true;
            if (on_finish_) on_finish_();
            done_evt_.set();  // 等待者即时 resume
        }
        return false;
    }

    time_ += dt_seconds * speed_;

    const bool loop = has_loop_override_ ? loop_override_ : c->loop;
    const double fps = c->fps > 0 ? c->fps : 1.0;
    const double cyc = c->frames.size() / fps;

    if (loop) {
        if (cyc > 0) time_ = std::fmod(time_, cyc);
    } else if (cyc > 0 && time_ >= cyc) {
        // 非 loop 播完：置停，finish 只一次
        playing_ = false;
        if (!finish_called_) {
            finish_called_ = true;
            if (on_finish_) on_finish_();
            done_evt_.set();
        }
        return false;
    }

    // on_frame：帧索引变化触发（last_frame 去重，seek/切换强制触发）
    const int idx = frame_index();
    if (idx != last_frame_) {
        last_frame_ = idx;
        if (on_frame_ && idx >= 0) on_frame_(idx);
    }
    return true;
}

SpriteDesc AnimationPlayer::current_frame() const {
    SpriteDesc out;
    const detail::AnimClip* c = cur_clip();
    if (!c || c->frames.empty() || !set_ || !set_->data_) return out;
    const int idx = frame_index();
    if (idx < 0 || idx >= static_cast<int>(c->frames.size())) return out;
    const detail::AnimClipFrame& f =
        c->frames[static_cast<std::size_t>(idx)];
    out.has = true;
    out.asset_id = set_->data_->asset_id;  // 归属校验（供 render_sprite）
    out.texture = f.texture >= 0 &&
                          f.texture <
                              static_cast<int>(set_->data_->textures.size())
                      ? set_->data_->textures[static_cast<std::size_t>(f.texture)]
                      : "";
    out.region = f.region;
    out.offset = f.offset;
    return out;
}

tg::task<> AnimationPlayer::done() const {
    // 已停止/从未播放 → 立即完成；否则等待 done_evt_（播完/stop/bind 时 set）
    if (!playing_ || finish_called_) co_return;
    co_await done_evt_;
}

}  // namespace tg