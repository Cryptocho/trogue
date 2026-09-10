# 里程碑 11：动画触发策略——独立动画查看器（播放/暂停/切换）

- 计划日期：2026-09-10
- 前置：里程碑 5（引擎 AnimationSet/AnimationPlayer）、里程碑 10（帧动画消费，E2E 全过）已完成
- 用户拍板（2026-09-10）：使用 `assets/scenes/soldier_animated_sprite_2d.json` 搭建场景，士兵**放大 3 倍**便于观察；**按任意键**在播放/暂停间切换；**鼠标点击**切换动画
- 与 M10 拍板的关系：M10「walk 暂不使用」指**玩法触发**（移动→walk）不做；本查看器把资产全部 clip 作为**数据**枚举暴露（含 walk），不赋予任何玩法语义，不与之冲突

## 1. 目的与问题

- M10 闭环验证了「被动播放」（reload → play idle → advance → 采样绘制）。game 层对播放器的**主动触发/切换**（pause/resume、切 clip、指定播放）尚无任何消费者——这是 plan-10 §7 遗留清单「触发策略」的第一块，也是引擎动画原语面向真实游戏（状态机驱动切动画）前必须验证的能力。
- 本期做一个最小交互验证台：加载士兵场景 → 3x 放大观察 → 任意键播放/暂停 → 鼠标左键轮转 clip。

## 2. 载体决策：独立可执行 `anim_viewer`（不改 trogue demo）

| 方案 | 结论 |
|------|------|
| **A. 独立可执行 `anim_viewer`（game/src/anim_viewer.cpp）** | **采用**。「任意键」与回合制 demo 的键位语义（方向键移动 / F5 重载 / ESC 退出）天然冲突；独立工具对 roguelike demo 与 smoke 61 **零风险**，且长期留作动画手动测试台 |
| B. demo 内按场景切模式 | 否决：main.cpp 已约 1000 行，按场景分叉行为属隐性模式开关，复杂度不成比例 |

- viewer 只依赖 engine 公共头（`tg::SceneAsset`/`tg::AnimationPlayer`/`tg::Ipc`）+ raylib（game 层可直接调 raylib，demo 先例）；**不引入 game_core**（无回合/战斗/AI）。
- 场景数据从 `tg::SceneEntity` 值快照读取（`entity_count()`/`entity(0)`：x/y/color/sprite.offset），不改 descriptor 语义、不保存可变实体状态（符合引擎边界）。

## 3. 引擎 API 对齐（全部读自源码，非记忆）

| 事实 | 出处 |
|------|------|
| `AnimationPlayer` 已有 play/stop/pause/resume/playing/clip_name/frame_index | `engine/include/trogue/animation.hpp` L58-124 |
| **无 `paused()` 查询**（pause/resume 为 void，`playing()` 暂停中仍 true）→ 无法从播放器读暂停态 | 同上 L69-70、L81 |
| 暂停语义：`advance()` 挂起时间推进、`current_frame()` 仍返回冻结帧 | `engine/src/animation.cpp` L144-147、L191-201 |
| `play()` 清除 paused_ 并重置时间——切换 clip 即恢复播放（引擎语义，viewer 依赖它） | animation.cpp L65-75 |
| `AnimationSet::clip_count()/clip_name(i)` 支持 clip 枚举 | animation.hpp L44-47 |
| `SceneAsset::entity(i)` 返回值快照（x/y/color/sprite） | `engine/include/trogue/scene.hpp` L59-68、L103-109 |
| `render_sprite` 独立贴图形态即士兵绘制路径（M10 已端到端验证） | `engine/src/render.cpp` L267-287 |
| 缩放：`render_sprite` 无 scale 参数（`DrawTextureRec` 原尺寸绘制） | render.cpp L262-286 |

**本期引擎改动仅一处**：`AnimationPlayer::paused() const`（header inline 一行，对称 `playing()`）。viewer 凭它决定 pause/resume，避免 game 侧自持 bool 与「play() 清 pause」产生状态漂移。

**缩放决策**：本期**不加** `render_sprite` scale 参数——相机 zoom 3（game 侧 `Camera2D` 参数，demo 已在用同一机制）即达成「放大 3 倍便于观察」；`TEXTURE_FILTER_POINT` + 整数倍 = 最近邻无损。引擎渲染原语的变换扩展留待真实游戏出现非均匀变换需求时再议（需求驱动，避免为本期验证台扩 API 面）。

## 4. 方案（viewer 行为定义）

### 4.1 启动与画面

- CLI：`--scene`（缺省 `assets/scenes/soldier_animated_sprite_2d.json`）、`--port`（缺省 **48765**，与 demo 48764 可并存）、`--zoom`（缺省 3，须整数 ≥1，非法值回退缺省）
- 窗口 960x540（同 demo），标题 `[trogue] anim viewer`，`SetTargetFPS(60)`
- 加载场景：`animation_set_count()==0` → 日志警告，无动画照画静态 sprite（不崩）；有动画 → bind set 0，`play("idle")` fallback 第一个 clip（与 demo reload 语义一致）
- 相机：`Camera2D{offset={w/2,h/2}, target=(0,0), rotation=0, zoom=N}` 固定。已知假设：士兵 descriptor (0,0) + offset [-50,-50] + 100x100 帧 → 视觉中心恰为世界原点（本查看器为该场景验证台专用；通用居中泛化见 §8）
- 绘制：`render_scene`（bare 场景零层，no-op）→ 动画集已绑定时 `current_frame()` 采样 + 实体 sprite.offset 组合（同 demo M10 公式：实体锚点 + 帧偏移）→ `render_sprite(tint=实体 color)`；采样失败回退静态 sprite（画面永不空白）
- HUD（屏幕空间、EndMode2D 之后、≤4 行 16-20px，英语与 demo 一致，不侵入士兵区域 screen y≥120）：clip 名/序号、PLAYING/PAUSED、zoom、fps、操作提示（any key: play/pause / click: next clip / ESC: quit）

### 4.2 输入 → 动作（触发策略本体）

- **任意键**：每帧 drain `GetKeyPressed()`，出现 ≥1 个非 0 键码 → 调 `toggle_pause()` **一次**（同帧多键只切换一次，防奇偶抵消）。ESC 例外：raylib 默认 ESC 触发 `WindowShouldClose`，循环条件先行退出，天然不参与切换（HUD 已注明 ESC=quit）
- **鼠标左键**：`IsMouseButtonPressed(MOUSE_BUTTON_LEFT)` → `next_clip()`
- 动作函数为唯一逻辑入口，键鼠与 IPC 共用（§4.3）：
  - `toggle_pause()`：`anim.paused() ? anim.resume() : anim.pause()`
  - `next_clip()`：`clip_count()==0` 时 no-op（无动画场景防模零，与 §4.1 不崩目标一致）；否则 `clip_index = (clip_index + 1) % clip_count` → `play_clip(i)`
  - `play_clip(i)`：`anim.play(set.clip_name(i))`（play 清 pause 重置时间——**暂停中点击 = 切换并恢复播放**，引擎语义，HUD/文档如实说明）
- clip 枚举序 = 资产数组序（attack01→attack02→attack03→death→hurt→idle→walk→回绕），轮转一周回原 clip

### 4.3 IPC（最小 4 命令，Agent 免键鼠验证）

- 复用 `tg::Ipc`（JSON-lines、包络同 tro-ipc v1）；viewer 是**独立进程独立端点**，不进 demo 命令表，AGENTS.md 单独注明：
  - `status` → `{scene, clip, clip_index, frame, paused, playing, zoom, fps}`
  - `anim` → `op`: `toggle_pause` | `next_clip` | `play`（附 `clip`: 名字；无效名报 `ok:false`）→ 返回与 status 同构数据；顺带实现 `help`（列 4 命令，一行成本）
  - `screenshot` → `path?`（缺省 `anim_view_<时间戳>.png`；复用 demo 的 `rlDrawRenderBatchActive()` + `LoadImageFromScreen` 管线——批 flush 教训沿用）
  - `quit` → 干净退出（防端口占挂）
- IPC op 与键鼠走**同一动作函数**：E2E 覆盖逻辑本体；原始键鼠映射（GetKeyPressed/IsMouseButtonPressed → 动作）为薄 if 层，人工确认
- Release（TROGUE_DEBUG=OFF）下 `tg::Ipc` 为桩：viewer 窗口行为不受影响；E2E 需 Debug 构建（项目惯例）

### 4.4 生命周期

- 单场景运行，**无热重载/watcher/F5**（最小验证台，见 §8）
- 退出：`anim.bind(tg::AnimationSet{})` 收尾（与 demo 一致）→ `asset.reset()` → `tg::shutdown_render()` → `CloseWindow()`

### 4.5 测试与验证手段

- **引擎单测**（`tools/tests/anim_tween_test.cpp` 追加用例）：`paused()` 初值 false；pause→true、resume→false；`play()` 清 pause；**暂停冻结回归钉**——pause 后连续 `advance(dt)` 其 `frame_index()` 不变、`current_frame().has` 恒真
- **E2E**（一次性临时脚本不入库，沿 plan-10 惯例；Debug 构建 + 有窗口，Agent 驱动）：
  1. 起 viewer → `status`：`clip=="idle"`、`paused==false`
  2. 间隔 ~0.2s 采样 ≥7 次（跨度 >1.2s，覆盖 idle 周期）→ `frame` 推进且出现回绕（idle 6 帧 5fps）
  3. `anim op=toggle_pause` → 连续采样 `frame` 恒定（冻结证据）、`paused==true` → 再 toggle → 恢复推进
  4. `anim op=next_clip` × 7 → clip 按枚举序轮转一周回到 idle；每次切换后 `frame==0`
  5. `anim op=play clip=hurt` → `clip=="hurt"`
  6. screenshot 与 `Soldier_Hurt.png` 源帧 **3x 像素比对**：zoom3 + 960x540 下士兵区域 = screen `(330,120)..(630,420)`，断言 `screen(330+3u+i, 120+3v+j) == src(frame_x+u, v)`（u,v 为帧内坐标 ∈[0,100)，frame_x = 帧序×100 的 region 横排偏移，i,j∈{0,1,2}），逐 **4 帧**（hurt 为 4 帧）取最优 **≥95% 命中**（方法学沿 plan-10，±1 帧竞态免疫）
  7. `quit` → 进程退出、端口释放
  8. Agent 读截图视觉验收（3x 观感、位置正确、无撕裂/漂移）
- **键鼠人工验收**：用户按任意键/左键点击确认薄映射层（或 xdotool best-effort，不承诺）
- **回归门禁**：ctest 全绿、smoke 61/61（demo 未动，不扩 smoke）、Debug/Release 零告警

## 5. 文件清单

| 文件 | 动作 |
|------|------|
| `engine/include/trogue/animation.hpp` | 修改（+`paused()` inline getter，注释同步） |
| `tools/tests/anim_tween_test.cpp` | 修改（paused() 语义 + 暂停冻结回归钉） |
| `game/src/anim_viewer.cpp` | 新增（viewer 本体，约 250 行） |
| `game/CMakeLists.txt` | 修改（+`anim_viewer` 目标，输出 build/bin/） |
| `AGENTS.md` | 修改（目录结构 game/src 加 anim_viewer.cpp；开发命令加 viewer 运行行；IPC 节注明 viewer 独立端点 48765 与 4 命令；Roadmap 补条目） |
| `docs/plan-11.md` | 新增（本文件） |
| `CHANGELOG.md` | 修改（subagent 检查之后补条目） |

E2E 验证脚本为一次性临时脚本，不入库（沿 plan-10 先例，避免「脚本是否存在」歧义）。

## 6. 步骤（含开发流程 3~7）

1. 引擎：`paused()` getter + 单测
2. game：`anim_viewer.cpp` + CMake 目标
3. E2E 脚本验证 + 3x 像素比对 + 读图验收
4. 全量回归（ctest / smoke 61 / 零告警）
5. subagent 审查未提交代码（本计划书先走独立审查，实现完成后再整体审查）
6. 更新 CHANGELOG（检查之后）；核对 AGENTS.md
7. 英文 commit message 预览 → 用户确认 → 提交**所有**变更 → `git push`

## 7. 验证清单

- [ ] 单测：paused() 语义（初值/pause/resume/play 清除）+ 暂停冻结回归钉
- [ ] ctest 全绿；smoke 61/61；Debug/Release 零告警
- [ ] E2E：idle 推进且回绕；toggle_pause 冻结/恢复；next_clip 轮转一周且 frame 归零；play 指定 clip
- [ ] E2E：screenshot 与源帧 3x 像素比对 ≥95% 命中
- [ ] 视觉验收：读截图确认 3x 士兵播放正常（观感/位置/无撕裂）
- [ ] 人工：任意键暂停/恢复、左键切换（用户或 xdotool best-effort）

## 8. 遗留与边界

- 相机自动居中 / 多实体 / 多动画集选择策略：本期固定 set 0 + 世界原点相机（士兵场景专用假设已在 §4.1 注明），泛化留真实需求出现时
- `render_sprite` scale/变换参数（引擎渲染原语扩展）：本期相机 zoom 达目的不加；非均匀变换需求出现时再议
- 热重载/F5：viewer 不做（最小验证台）
- 帧事件（on_frame/on_finish）与协程等待（done()）的 game 消费：仍留演出脚本里程碑（plan-10 §7 原样）
- 玩法触发（移动→walk、受伤→hurt、死亡→death）：仍留士兵入战斗原型表时一并做（plan-10 §7 原样）
