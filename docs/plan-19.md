# 里程碑 19：独立贴图缓存失效（reload_texture）

- 计划日期：2026-09-13
- 状态：计划中（待审查）
- 前置：里程碑 5（C++ 引擎）、15（渲染可观测性/RenderStats）、16（碰撞几何）、18（确定性随机）已完成。Roadmap「独立贴图缓存失效 API（P2，2026-09-13 拍板）」为直接输入。
- 参考：`AGENTS.md`「架构分层·设计约定」「引擎公共 API 边界」「编码规范」；`engine/include/trogue/render.hpp`、`engine/src/render.cpp`（缓存实现现状）、`tools/tests/render_test.cpp`（无窗口测试基线）

## 1. 目的与问题

### 1.1 判据自检（本里程碑补齐/验证了引擎哪项通用能力）

本里程碑补齐的能力 = **「运行时贴图资源的缓存失效」**。它逐条满足功能准入判据：

| 判据 | 本里程碑 |
|------|----------|
| 机制性 | 是：进程级缓存的条目失效原语（卸载 + 清哨兵），不含任何玩法/美学决策 |
| 确定性 | 是：同输入同行为；失效后下次绘制必触发磁盘重载（缓存查找语义不变） |
| 可无头测试 | 是：非法路径拒绝与无窗口 no-op 路径纯公共 API 可单测（无窗口时缓存恒空） |
| 与美学/玩法无关 | 是：何时失效、失效后做什么（热重载策略、文件监听）归 game |

> 反向自检：本计划没有任何一项只能回答「让某款游戏更好玩」。**「什么时候失效、失效后是否自动重监听文件」是策略，归 game**；引擎只提供失效原语。

### 1.2 需求实证（为什么是现在，而不是预造）

**迭代闭环断层（2026-09-13 评估拍板记录在案）**：引擎的场景 JSON 有完整热重载链路（watcher 秒级生效 + F5/IPC `reload`），而独立贴图（sprite/动画帧 PNG）的进程级缓存**只增不减、无失效口子**（`render.cpp:59-61` 缓存 + 失败哨兵，唯一出口 `shutdown_render()` 是进程退出语义）。后果：**Agent 改一张 png 必须重启进程**才能看到效果——对以「改→跑→观察→再改」为生命线的 Agent-first 工作流，这是资产维度上最疼的一处不对称。

| 拟入符号 | 消费方实证 |
|----------|------------|
| `tg::reload_texture` | ① demo IPC `reload_texture` 探针命令（本里程碑接入「IPC 是 Agent 的眼睛」，Agent 无需写 C++ 即可在运行中失效贴图后截图验证）；② 模板派生项目的改 png 迭代工作流（AGENTS.md 2026-09-13 评估记录）；③ `anim_viewer`（独立贴图的真实绘制者）为运行时人工验证载体 |

单符号里程碑，且 `shutdown_render` 已证明缓存管理是引擎职责（本 API 是它的窄化补充而非新职责）。

### 1.3 明确不在范围

- **文件监听扩展**：`tg::Watcher` 只报告 `.json` basename（既有契约），不扩展为监听 png——「改 png 自动失效」是 game 策略（可由 IPC 命令、编辑器钩子或未来 watcher 扩展组合实现），引擎不内置。
- **图集贴图失效**：图集贴图随 asset RAII（`SceneImpl::atlas_textures` 槽位），场景 swap 即旧资产析构、新资产全新槽位——失效需求不存在。
- **部分失效/引用计数暴露**：不做按 region 失效、不暴露缓存内容查询（缓存是引擎内部实现细节）。
- **`shutdown_render` 语义变更**：保持「进程退出统一释放」语义不变。
- **线程安全**：引擎单线程（既有约定），`reload_texture` 与绘制同线程（主循环）调用。

## 2. 方案

`engine/include/trogue/render.hpp` 新增一个自由函数 + `engine/src/render.cpp` 实现。风格与既有渲染原语一致（自由函数、`RenderResult` 之外的简单 bool 返回、参数失败计入 `RenderStats::param_failures`）。

### 2.1 API

```cpp
// 使独立贴图缓存中的指定条目失效：卸载已缓存贴图并清除失败哨兵，
// 下次绘制该路径时从磁盘重新加载。
// 仅作用于进程级独立贴图缓存（sprite/动画帧）；图集贴图随 asset RAII
//（换资产即自然重载），不在作用范围。
// path 为 assets 相对路径（与 SpriteDesc::texture 同一约定）。
// 返回：path 合法 → true（无论此前是否在缓存中，失效请求均生效——
// 「确保下次绘制重新读盘」这一契约对未缓存路径同样成立）；
// path 非法（空/不安全相对路径）→ false + param_failures 计数。
// 需 GL 上下文的调用时序与绘制原语一致：缓存条目仅在窗口存活期的绘制中
// 产生，本函数按单线程主循环约定在窗口存活期调用（GL 上下文存在）；
// 从未开窗的进程（无头测试场景）缓存恒空 → erase 为空操作、恒 no-op 返回 true。
// 单线程：与绘制同线程（主循环）调用。
// 计数语义：本函数不触碰 window_checks/texture_attempts；仅非法路径
// +1 param_failures（供「失效后绘制 → texture_attempts 增长 = 重读盘」佐证）。
bool reload_texture(std::string_view texture_path);
```

### 2.2 语义钉死（供审查）

1. **失效 = 卸载 + 清哨兵**：`g_texture_cache.erase(path)`（shared_ptr 析构 → `UnloadTexture`，GPU 释放）+ `g_texture_failed.erase(path)`（失败哨兵清除——此前加载失败的路径，失效后下次绘制会**重试**加载并恢复「每路径首失败记一次日志」语义）。只清这两处，缓存查找逻辑零改动。
2. **未缓存路径返回 true**：契约是「确保下次绘制重新读盘」而非「报告缓存状态」——对不在缓存的路径（从未加载过/已卸载），该契约天然成立。调用方无需区分。
3. **GL 上下文与调用时序**：erase 触发 `UnloadTexture` 需 GL 上下文。精确契约：缓存条目**仅在窗口存活期的绘制中产生**；本函数按单线程主循环约定**在窗口存活期调用**（GL 上下文存在）。「从未开窗」的进程（无头测试场景）缓存恒空，erase 为空操作——与「绘制原语在无窗口时安全 no-op」的既有测试基线一致。不声明「窗口关闭后」的行为（该形态不在单线程主循环约定内）。
4. **在途引用安全**：缓存条目是 `shared_ptr<const SharedTexture>`，erase 仅移除缓存持有份；单线程约定下不存在「绘制调用进行中失效」的时序（失效发生在主循环两次绘制之间）。若未来出现多线程，契约已声明单线程前提。
5. **非法路径**：空路径或不安全相对路径（复用 `detail::is_safe_relative_path`，与 `render_sprite` 的 texture 校验同一判据）→ false + `param_failures` 计数 + 错误日志。**不触碰缓存**。
6. **确定性**：`reload_texture(p)` 后紧跟 `render_sprite(p)` 的行为 = 缓存未命中路径的既有行为（磁盘加载成功 → 绘制；失败 → TextureMissing + 首失败日志）。无新分支语义。

### 2.3 demo 探针命令（game 层）

`game/src/main.cpp` 新增 IPC 命令 `reload_texture`：参数 `path`（必填字符串）→ 调 `tg::reload_texture` → 成功 `{reloaded: true, path}`；引擎返回 false（路径不安全）或 `path` 缺失/非字符串 → 统一错误包络（与 `move` 的 `blocked` 结果语义分离——命令要么生效要么报错，不返回「软失败」）。加入 `help` 命令列表。

### 2.4 公共 API 边界（本里程碑后的增量）

`AGENTS.md`「引擎公共 API 边界」追加（里程碑 19 块）：
- **贴图缓存失效**：`tg::reload_texture(path)`——独立贴图进程级缓存的失效原语（卸载 + 清失败哨兵，下次绘制重读盘）；图集贴图不在此范围；失效时机与文件监听策略归 game。

## 3. 步骤（含开发流程 3~7）

1. **实现**：`render.hpp` 声明 + 契约注释（自足）；`render.cpp` 实现（§2.2 第 1/3/5 条）。
2. **单测** `tools/tests/render_test.cpp` 追加用例（无窗口基线，见 §4.1）。
3. **demo 探针**：`main.cpp` 命令 `reload_texture` + `help` 登记（§2.3）。
4. **冒烟**：`tools/ipc_smoke.py` 追加断言（合法路径 honored、非法路径拒绝、help 登记），既有断言零回归。
5. **人工 E2E（真实缓存路径）**：给 `assets/scenes/demo.json` 添加一个独立贴图实体（texture 用**现存** `textures/goblin.png`——demo 此前无独立贴图实体，链路 `draw_entity_sprite` → `render_sprite` 独立贴图形态 → `get_or_load_texture` 已核对存在）。验证序列：① 起服 → `screenshot` 记录基线；② 进程内 `reload_texture` 失效该路径 → 修改该 png（或换内容）→ `screenshot` 目视确认新内容可见（重读盘证据：`texture_attempts` 增长 + 内容变化）；③ 还原 png。
6. **回归**：Debug + Release 构建零告警；ctest 全绿；ipc_smoke 全绿。
7. **模板刷新**：维护者模式 `template/scripts/sync_from_source.sh` → 独立副本构建验证。**范围澄清**：同步脚本只刷新 vendored 快照（engine/pixellab/editor + tools/scene_gen.cpp），**不触碰模板自有文件**（template/game/、template/tools/ipc_smoke.py）——模板起步 game 不实现 `reload_texture`，模板冒烟**不跟随**本次增补（模板自有文件的手工跟进属其自身里程碑）。
8. **文档**：AGENTS.md——① API 边界增量（§2.4）；② Roadmap P2 勾选；③ **IPC 命令表**追加 `reload_texture` 行（对齐 `probe_collide`/`genmap` 先例）；④ 顺带订正陈旧断言计数（「AI Agent 调试工作流」「开发命令」节的 61/44 项 → 实际基线）。
9. **subagent 审查**未提交代码（禁止自检）。
10. 更新 `CHANGELOG.md`（审查通过后）。
11. 询问用户 commit message（英文预览，确认后提交全部变更并推送）。

### 文件清单

| 文件 | 动作 | 对应 |
|------|------|------|
| `engine/include/trogue/render.hpp` | 改（新增声明） | §2.1 |
| `engine/src/render.cpp` | 改（实现） | §2.2 |
| `tools/tests/render_test.cpp` | 改（追加用例） | §4.1 |
| `game/src/main.cpp` | 改（探针命令 + help） | §2.3 |
| `assets/scenes/demo.json` | 改（独立贴图实体，走真实缓存路径） | 步骤 5 |
| `tools/ipc_smoke.py` | 改（追加断言） | 步骤 4 |
| `template/engine/**` | 刷新（vendored 快照；模板自有文件不动） | 步骤 7 |
| `AGENTS.md`、`CHANGELOG.md`、`docs/plan-19.md` | 改/新增 | 步骤 8/10 |

## 4. 验证清单

### 4.1 `render_test` 追加（无窗口、纯公共 API）

- `reload_texture("textures/anything.png")` 无窗口 → 返回 true（no-op 契约）、不崩溃、stats 完全不变（合法路径不触碰任何计数）。
- `reload_texture("")` / `reload_texture("../x.png")` / `reload_texture("textures/../x.png")` → false 且仅 `param_failures` 各 +1（window_checks/texture_attempts 不变）。
- 与既有用例同基线：不 InitWindow，全程安全 no-op。

### 4.2 E2E / 回归

- [ ] ipc_smoke：`reload_texture` 合法路径 `{reloaded:true}`、非法路径错误包络、help 登记三项全绿；既有 70 项零回归。
- [ ] 步骤 5 人工验证：进程内失效后，下一帧绘制重新读盘（改图内容可见），无崩溃、无僵尸纹理（Drawn 但全透明的既有教训回归点）。
- [ ] Debug + Release 零告警；ctest 全绿；模板独立副本构建通过。
- [ ] 本次 diff 无内部文档指针（注释自足纪律）。

## 5. 遗留与边界

- **「改 png 自动失效」**：需 watcher 扩展（监听非 .json）或 game 侧文件监听，属策略组合，本里程碑不做；当前由 Agent 经 IPC 命令显式失效即满足迭代闭环。
- **anim_viewer 端点命令**：48765 命令表独立维护，本里程碑不加 `reload_texture`（其验证由步骤 5 人工路径覆盖）；若未来 anim_viewer 需要热迭代体验，另起微小增量。
- **缓存观测**：不提供缓存内容枚举 API（`RenderStats` 三项计数已够 Agent 断言「重载发生」：失效后绘制 → `texture_attempts` 增长即重读盘证据）。
