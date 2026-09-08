# 里程碑 5 分卷 4：通用表现原语（Animation 帧播放器 + Tween + 演出协程）

> 前置阅读：综述、5.1、5.2、5.3。本卷实现 AGENTS「通用表现能力引擎内置」：engine 提供**执行原语**（帧采样、补间推进、完成/帧事件、协程等待），**触发与切换决策归 game**；播放器不自动绘制、不持有/不绑定 game 对象。

## 1. 边界重申

- 引擎做：按数据（fps/loop/帧表/缓动/时长）推进与采样，产出一个**视觉/数值结果**（当前帧 `SpriteDesc`、当前补间值）、帧事件/完成通知、可 `co_await` 的完成句柄。
- game 做：决定「对象此刻播哪条 clip / 起哪个补间、target 是什么、结束如何衔接」，并把引擎输出应用到自己对象（`render_sprite`、写字段）。
- 播放器与补间**不引用 game 实体**；销毁/取消由 game 通过句柄与宿主生命周期管理。

## 2. 动画数据：结构校验（load 期，配合 5.2 §2.6b）

**范围定案（阻塞项 3）**：本里程碑消费 **scene 内嵌 `animations` 帧表**（`tro-scene` 实体级字段，含现有 `assets/scenes/soldier_animated_sprite_2d.json` 内嵌 7 动画）；**独立 tro-animations 文件（`tro-animations` v1，顶层含 `format`/`version`）的加载 API 本里程碑不实现**，其消费在 5.6 §9 遗留登记（后续里程碑补 `AnimationSet::load(path)`，剥离 `format`/`version` 或走独立 loader）。因此：

- scene 内嵌 `animations` object 只允许 `textures`/`animations` 两 key（其所属 entity 已通过 5.2 entity key 白名单到达；不存在 `format`/`version`）。
- 独立 tro-animations 文件**不在本里程碑校验范围**，也不出现在 5.2/5.4 load 路径；5.6 §9 记录为遗留。
- 若需要把独立 tro-animations 直接作为播放源（如编辑器素材预览），由 game 自行解析或延后（记录遗留）。

scene 内嵌动画集结构校验（load 期，数据入 asset 只读动画集，供播放器消费）：

- 顶层 object 只允许 `textures`/`animations` 两 key。
- `textures`：array（可为空；空时 `animations` 必为空，否则帧引用越界拒绝）；每项为过 5.2 §2.1 的 assets-relative 路径字符串。
- `animations`：array（可为空）；每个 clip object 只允许 `name/fps/loop/frames`（未知 key 拒绝）；`name` 非空且集内唯一；`fps` finite >0；`loop` bool；`frames` 为 array（可为空；空帧 clip 合法与否按「可空数组」接受但播放时长 0——定案：空 frames 接受，播放视为即时完成，便于占位）。
- 每帧 object 只允许 `texture/region/offset`；`texture` int 且在 `textures` 索引内；region x/y>=0 finite、w/h>0（缺省=整图，render 期补齐）；offset finite 可负。与 5.2 sprite 数值规则一致。
- 限额常量（config.hpp）：`kAnimTexturesPerEntityMax`/`kAnimClipsPerEntityMax`/`kAnimFramesPerClipMax`/`kAssetAnimFramesMax`（定义见 5.2 §2.6；此处引用）。

## 3. `tg::Animation` 帧播放器

### 3.1 数据源与生命周期

- **命名映射（与 AGENTS「引擎公共 API 边界」一致）**：AGENTS 目标称动画能力为 `tg::Animation`；本计划把「数据」与「播放器」拆成 `AnimationSet`（只读动画集）与 `AnimationPlayer`（播放器），组合即 AGENTS 所述 `tg::Animation` 能力。公共头 `animation.hpp` 提供两者；若后续需单名句柄再收敛，不改变本卷语义。
- `AnimationSet` **最小公共面**（只读）：`std::string_view name() const;`（唯一标识，便于调试）、`int clip_count() const;`、`std::optional<std::string_view> clip_name(int) const;`、`bool has_clip(std::string_view) const;`、以及内部 clip 帧数据访问（供 player 采样，可仅 `friend`/detail 可见的查询）。scene 内嵌 `asset.animations()` 返回 `const AnimationSet&`（存活期 = asset）。
- 播放器由 game 创建（值/句柄），构造绑定**非拥有** `const AnimationSet*`：`AnimationPlayer` 不拥有数据；**契约：绑定 asset 必须先于播放器销毁**（失效后调用为安全 no-op 而非未定义——定案：播放器记录绑定有效性，失效访问返回 `false`/`!valid()` 不崩；跨 reload 由 game 先销毁/重绑）。
- `AnimationSet` 统一抽象支持 scene 内嵌与（未来）独立 tro-animations loader 产同构数据；**本里程碑只有 scene 内嵌来源**（§2 范围定案），独立文件加载记入 5.6 §9 遗留。单测可内联构造 `AnimationSet` 数据验证播放器逻辑。

### 3.2 接口（示意）

```cpp
class AnimationPlayer {
 public:
  // 绑定的动画集与 clip 名；无效名 → false
  bool play(std::string_view clip, bool restart_if_same = true);
  void stop(); void pause(); void resume();
  bool set_speed(float s);            // 1 = 原始 fps 倍率；>0
  bool seek(double seconds);
  bool looping(bool on);              // 覆盖 clip 的 loop
  bool valid() const;  bool playing() const;
  double time() const;  int frame_index() const;
  const std::string& clip_name() const;
  // 推进：game 每帧（或固定步长）调用；返回是否仍在播放
  bool advance(double dt_seconds);
  // 当前帧视觉结果（未播放/无 clip → has=false）
  SpriteDesc current_frame() const;   // 填 texture/region/offset/asset_id
  // 事件（可选，默认空）：进入新帧 / 播放结束
  void on_frame(std::function<void(int)>);       // 帧索引
  void on_finish(std::function<void()>);
  // 完成等待（演出脚本）
  tg::task<> done() const;            // 已在非播放/无 clip 态 → 立即完成
};
```

- 帧采样：时间 = clip 起始时间累积；`fps` 恒定（速度倍率缩放）；loop 播放到末尾回卷；非 loop 播完置 stopped 并触发 on_finish + 完成 done()。
- `current_frame()` 每帧根据当前时间定位帧索引（时间→帧 = floor(t*fps) 对帧数取模/截断），取帧的 texture/region/offset；**不做任何绘制**。
- 帧事件：帧索引改变时触发 on_frame。

### 3.3 多 clip 与切换

- 播放器单 clip 活动；game 可持多个 player 或多个对象各自 player；切换 = 再次 `play(name)`（restart 语义可选）。engine 不做状态机/混合。

## 4. `tg::Tween` 补间

### 4.1 设计

- **值补间器，不持对象指针**：描述 from→to、时长、延迟、缓动、循环；每次采样把当前值交给回调，由 game 应用到自己的对象。
- 三种内建类型：`float`、`Vec2`、`Color`（提供 lerp；Color 逐通道）。
- 缓动：一组命名 easing（linear/quad/cubic/sine/back/elastic/bounce in·out·inout 等，用函数表枚举 `Easing`）；默认 linear。

### 4.2 接口（示意）

```cpp
struct TweenSpec {
  double duration=1.0, delay=0.0;
  int repeats=0;              // 0 = 一次；<0 = 无限（配合 cancellation）
  Easing easing = Easing::linear;
};

class TweenManager {          // game 每帧 tick；持有活动补间
 public:
  using Id = std::uint64_t;
  template <typename T> struct Sample { T value; double t; };  // t∈[0,1]

  Id add_float(float from, float to, TweenSpec,
               std::function<void(const Sample<float>&)> on_update,
               std::function<void()> on_complete = {});
  Id add_vec2(Vec2 from, Vec2 to, TweenSpec, ...);
  Id add_color(Color from, Color to, TweenSpec, ...);
  bool cancel(Id); void cancel_all();
  void tick(double dt);       // 推进全部活动补间
  bool alive(Id) const;
  // 等待某个补间完成（演出脚本）
  tg::task<> wait(Id) const;  // cancel/不存在 → 立即完成
};
```

- `Sample<T>`：避免把回调签名炸成模板元；三类型各一个 add。
- 循环：repeats 次数语义 = 完成后重播次数；无限循环需要 game 显式 cancel（文档写明，勿在 on_complete 里 leak）。
- 时间推进确定性：dt 可为固定步长；大 dt 不跳变语义（累加，完成在越过 duration 的当次 tick 触发，不补中间帧）。

## 5. 演出协程（`tg::task` 与完成等待）

- `trogue/coro.hpp` 提供：
  - `tg::task<T>`（协程载体）与 `tg::task<>`；`co_await` 返回 T（若需要）。
  - 完成信号 awaiter：`co_await player.done()`、`co_await tween.wait(id)`、或等待任意多个（`tg::when_all_ready` 若实现易得）。
- 推进模型：**单线程、显式推进**。game 主循环持有 `task<>` 并在需要时 `pump`（每帧 resume 未完成协程；engine 在 tick/advance 内 resume 已就绪等待者）。协程原语为**自研最小集**（`tg::task`/`tg::generator`/事件 awaiter，`trogue/coro.hpp`；决策记录见 5.1 §5.2——cppcoro 前置验证失败，GCC 12+ 不可编译）；若实施发现自研 awaiter 语义缺陷而需引入第三方，**与 5.1 §5.2 同口径：先记录到本卷（写明替代方案与理由）并纳入代码评审，不静默改依赖**。
- **生命周期安全**：等待者持有对 player/manager 的非拥有引用；契约 = 宿主（asset/player/manager）先于协程销毁，或先取消（`player.stop()`/`manager.cancel_all()`）使等待即时完成；manager/player 析构时未完成等待者不得悬垂（实现以「宿主存活期标志」使已析构后 resume 安全完成）。文档+评审重点。
- **完成事件纪律**：同一 `done()`/`wait(id)` 事件**至多一个等待协程**（single_consumer；5.1 §5.2）；`spawn_task` 是 game 侧辅助（持有列表+每帧 pump），engine 不提供。示例中 `[&]` 捕获要求 `m`/`tween` 在演出任务期间存活——由 game 负责（如演出任务与对象同生命周期或先取消）。

## 6. 演示集成示例（写入 game，不属 engine）

```cpp
// game 侧：绑定自己的对象（示意）
void Game::start_attack(Monster& m) {
  m.anim.play("attack01");
  // Tween 移动补间并等待
  auto tw = tween.add_vec2(m.pos, target, {0.3, 0, 0, Easing::quad_out},
      [&](auto& s){ m.pos = s.value; });
  // 或顺序演出
  spawn_task([&]() -> tg::task<> {
    co_await m.anim.done();
    co_await tween.wait(tw);
    start_next_state(m);
  });
}
```

## 7. 本卷决策清单

| # | 决策 |
|---|---|
| 1 | engine 只做执行原语；触发/切换/应用归 game；播放器不 draw、不绑实体 |
| 2 | 动画集结构与限额在 load 期校验；AnimationSet 只读、asset 存活期有效 |
| 3 | AnimationPlayer 非拥有绑定 + 单 clip 活动 + 帧/完成事件 + 完成等待 |
| 4 | Tween 为值补间器（float/Vec2/Color），不持对象指针；TweenManager 每帧 tick |
| 5 | 演出协程 tg::task，单线程显式 pump；宿主先于协程销毁或先取消；协程原语自研（5.1 §5.2），替代需先记录 |
| 6 | 循环无限补间由 game 显式 cancel；等待不存在句柄立即完成 |

## 8. 验证

- 无窗口（虚拟时钟）：给定 fps/loop/速度/seek 推进 → 帧索引序列正确；on_frame 在新帧触发一次；非 loop 播完 on_finish+done；Tween 缓动/延迟/循环采样到关键点（t=0/0.5/1）符合 easing 值；cancel 停止后续；wait/done 协程在 tick 后 resume 一次完成。
- 生命周期：销毁 player/manager 后不再 resume；asset 交换后旧 player 失效安全。
- 集成（带窗口/截图，demo 阶段）：`assets/scenes/soldier_animated_sprite_2d.json`（bare + 内嵌 7 动画 43 帧，**本里程碑唯一带动画的既有 tro-scene**）加载后，按 attack/idle/walk 等 clip 播放，抽帧与帧表预期一致；该资产同时列入 5.6 §7 回归「新 parser 必须可加载」显式清单。
