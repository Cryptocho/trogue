# 里程碑 10：帧动画消费——让士兵真的动起来

- 计划日期：2026-09-10
- 前置：里程碑 5（引擎 AnimationSet/AnimationPlayer 已落地）、里程碑 9（快照注入点 `extra_entity_fields` 已就绪）已完成
- 用户拍板（2026-09-10）：**本期只做帧动画消费，walk 不接**；验证目标是「引擎层帧动画完整跑起来」（数据在动 → 画面在动 → 可观测可验证）

## 1. 目的与问题

- 现状：`reload_scene` 已把场景首个动画集绑定到 `tg::AnimationPlayer` 并 `play("idle")`，主循环每帧 `advance(dt)`——**数据层在动**；但绘制循环只画实体静态 `sprite`（attack01 第 0 帧），从不采样播放器——**画面不动**（用户实测：士兵静止于第一帧）。
- 本期补上最后一环：绘制循环采样 `current_frame()` 并绘制，使 `assets/scenes/soldier_animated_sprite_2d.json` 中的士兵以 idle 循环真实动画。
- 同时闭合 M9 遗留清单首条：「攻击/移动动画帧未消费：表现层后续接入 `tg::AnimationPlayer`」（本期为消费起点；触发扩展仍留遗留）。

## 2. 引擎 API 对齐（全部读自源码，非记忆）

| 事实 | 出处 |
|------|------|
| `AnimationPlayer::current_frame()` 返回 `SpriteDesc`（texture 路径/region/offset/asset_id 归属），与静态 sprite 绘制同类型 | `engine/include/trogue/animation.hpp` L92-93 |
| `render_sprite(asset, SpriteDesc, pos, tint)` 已支持独立贴图形态（texture+region+offset），士兵静态帧即走此路径 | `engine/src/render.cpp`（现网已验证） |
| `AnimationSet::name()` 返回**空**——`AnimData` 无名字段；注释明示「后续如需按 entity id 命名由 asset 提供」 | `engine/src/animation.cpp` L24-25 |
| 动画集按 entity 解析序构建（`parse_animations` 逐 entity push → `anim_sets` 按序建视图），但公共 API 无 entity→set 映射；`SceneEntity` 快照按 plan-5.4 §3.1 刻意不含动画数据 | `engine/src/scene_asset.cpp` L747/L852-854/L1032-1036、`scene.hpp` L56-60 |
| 士兵场景事实：1 实体（id=`AnimatedSprite2D`，type=`soldier`）+ 1 动画集；`idle` = 5fps × 6 帧、loop=true（1.2s/循环）；实体 sprite offset `[-50,-50]`（Godot 居中语义），动画帧自身 offset 全为 `[0,0]` | `assets/scenes/soldier_animated_sprite_2d.json` |

**本期引擎改动仅一处**：解析 entity 内嵌 animations 时记录**所属 entity id**，`AnimData` 增加 `name`（= entity id），`AnimationSet::name()` 返回它。这正是 `animation.hpp` 头注释预留的扩展路径（「按 entity id 命名由 asset 提供」），不新增 API 面、不改快照结构。

## 3. 方案

### 3.1 actor ↔ 动画集映射（engine 小改 + game 解析）

- `engine/src/scene_impl.hpp`：`AnimData` 增加 `std::string name;`
- `engine/src/scene_asset.cpp`：`parse_animations` 调用点（entity 解析处）传入 entity id 并写入 `data.name`（id 在 entity 解析早期已就绪；解析顺序核对后接线，若 id 晚于 animations 解析则调整解析序或先存后补——实现时确认）
- `engine/src/animation.cpp`：`name()` 返回 `data_->name`
- game 侧：`game::Actor` 增加 `int anim_set = -1`；`import_scene` 构建 actor 时遍历 `asset.animation_set_count()` 匹配 `animation_set(i).name() == actor.id` 命中则记录索引（O(count) 查找，count ≤ 实体数，场景级微小）
- 匹配不到 = 该 actor 无动画（-1），goblin/coin 等不受影响

### 3.2 绘制接入（game 层，零引擎渲染改动）

- 主循环绘制处：`actor.anim_set >= 0` 且 `d.has_anim` 且 `d.bound_anim_set == a.anim_set`（**Demo 新增 `bound_anim_set` 字段**——现状只存 `has_anim` bool，绑定集校验需要序号）时：
  1. `SpriteDesc f = d.anim.current_frame();`
  2. `f.has` 为真 → **offset 组合**：`f.offset = a.sprite.offset + f.offset`（实体 descriptor 锚点 + 帧自身偏移；士兵 = `[-50,-50]+[0,0]`，居中语义与静态帧完全一致），再 `tg::render_sprite(*d.asset, f, {wx, wy}, a.color)`
  3. 采样失败（`!f.has`，如未播放）→ 回退静态 sprite 绘制（既有路径），画面永不空白
- tileset 图集形态 actor（goblin 等）不走此路径，色块/sprite 逻辑原样

### 3.3 触发策略（本期边界：只 idle）

- 现状已满足：reload 后 `play("idle")`（fallback walk）、loop 持续推进——本期**不改触发逻辑**，仅保证采样绘制生效
- **明确不做**（用户拍板）：移动→walk 切换、DamageDealt→hurt、EntityDied→death 均留遗留（士兵不在战斗原型表，这些触发无从发生；且「只验证引擎链路完整」不依赖它们）

### 3.4 IPC 可观测（Agent 免截图验证动画状态）

- 走既有 `extra_entity_fields` 注入点（**需把 main.cpp 现有的非捕获 lambda 改为捕获 `&d`**）：actor 有动画且其集 = 当前绑定集时，快照追加 `anim: {clip: <当前剪辑名>, frame: <帧索引>}`（数据源 `d.anim.clip_name()` / `frame_index()`）
- AGENTS.md IPC 命令表 `list_entities` 行与实体快照字段说明补 `anim?`（game 层扩展字段，与 hp/ai 同类）

### 3.5 生命周期

- reload：既有顺序已正确（先 `bind(空集)` 解绑 → swap → 重新 bind + play），actor 侧 `anim_set` 随 `import_scene` 重建，无新状态
- 退出/场景切换：`bind(空集)` 已有，无新增清理

### 3.6 测试与验证手段

- **引擎单测**（`tools/tests/scene_schema_test.cpp`）：士兵场景 load 后 `animation_set(0).name() == "AnimatedSprite2D"`；无动画实体场景 `animation_set_count()==0` 回归
- **无窗口回归**：`skeleton_regression` 增加同断言（可选覆盖项，engine 单测已有硬性断言，此处仅启动自检顺带）
- **E2E 脚本验证**（有窗口，Agent 驱动；一次性临时脚本不入库）：
  1. `--scene soldier` 起服 → 以 ~0.2s（≈帧长）间隔连续采样 `get_entity` 的 `anim` 字段 ≥4 次 → 帧号按采样步长推进且出现**帧号下降（回绕，如 4→0 / 5→1）**即为 loop 证据、`clip` 恒为 `idle`
  2. 连拍 ≥3 张截图（相邻间隔 ~0.2s）→ 断言**至少一对相邻截图**士兵区域像素不同（防 IPC/调度抖动致两拍落同一动画帧的假失败）
  3. 任一截图与 `Soldier_Idle.png` **6 个 idle 源帧 region 逐一比对、任一 ≥95% 命中即通过**（方法学出处：CHANGELOG M8「树位 1523/1523 命中」；逐一遍历天然免疫截图与 `anim.frame` 快照间 ±1 帧竞态，仍证明画面显示的是真实 idle 帧）
  4. Agent 读截图做视觉验收（2026-09-10 新工作流）
- **回归门禁**：ctest 全绿、smoke 61/61（不扩 smoke——冒烟基于 demo_arena 无动画实体，士兵链路由上述脚本覆盖；记录于 §6）
- Debug/Release 零告警

## 4. 文件清单

| 文件 | 动作 |
|------|------|
| `engine/src/scene_impl.hpp` | 修改（`AnimData` + name） |
| `engine/src/scene_asset.cpp` | 修改（解析时记录 entity id） |
| `engine/src/animation.cpp` | 修改（`name()` 实现） |
| `engine/include/trogue/animation.hpp` | 修改（`name()` 注释同步：返回所属 entity id） |
| `tools/tests/scene_schema_test.cpp` | 修改（name 映射用例） |
| `game/src/game_core.hpp` / `game_core.cpp` | 修改（`Actor::anim_set` + import 解析；include `trogue/animation.hpp` 纯声明，头注释自述同步） |
| `game/src/main.cpp` | 修改（绘制采样 + offset 组合 + `bound_anim_set` + `anim` 快照注入 + skeleton_regression 可选断言） |
| `AGENTS.md` | 修改（IPC 表 `anim?` 字段；**捎带未提交的远端拓扑同步**） |
| `docs/plan-10.md` | 新增（本文件） |
| `CHANGELOG.md` | 修改（检查后补本里程碑条目） |

E2E 验证脚本为一次性临时脚本，不入库（避免「脚本是否存在」歧义）。

## 5. 步骤（含开发流程 3~7）

1. 引擎：name 记录 + `name()` 实现 + 单测（本文件 §3.1）
2. game：`Actor::anim_set` 解析 + 绘制采样 + 快照注入（§3.2/§3.4）
3. E2E 脚本验证 + 像素比对 + 读图验收（§3.6）
4. 全量回归（ctest / smoke 61 / 零告警）
5. subagent 审查未提交代码（本计划书先走独立审查，实现完成后再整体审查）
6. 更新 CHANGELOG（检查之后）；核对 AGENTS.md
7. 给出英文 commit message 预览 → 用户确认 → 提交**所有**变更（含远端拓扑同步）→ `git push`

## 6. 验证清单

- [ ] 引擎单测：`name() == entity id`（士兵场景）；无动画场景 count==0 回归
- [ ] ctest 全绿；Debug/Release 零告警；smoke 61/61
- [ ] E2E：`anim.frame` 随时间推进且出现帧号下降（回绕）、`clip=="idle"` 恒定
- [ ] E2E：≥3 张连拍中至少一对相邻截图士兵区域像素不同（画面确实在动）
- [ ] E2E：任一截图与 6 个 idle 源帧 region 逐一比对 ≥95% 命中（免疫 ±1 帧竞态）
- [ ] 视觉验收：读截图确认士兵 idle 循环观感正常（无跳帧/撕裂/位置漂移）

## 7. 遗留与边界

- walk/hurt/death 触发策略（移动切 walk、受伤切 hurt、死亡切 death）：留后续，士兵入战斗原型表时一并做；
- 每 actor 独立 `AnimationPlayer`（多动画实体并存）：本期单播放器 + 绑定集校验已正确处理单动画实体场景，泛化留遗留；
- 士兵进 demo_arena 参与战斗：用户拍板本期不做；
- 帧事件（on_frame/on_finish）与协程等待（`done()`）：引擎已支持，game 侧暂无消费者，留演出脚本里程碑。
