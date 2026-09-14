# 里程碑 22：运动/物理主线第一期（M22a）——时间步进与虚拟输入原语 + 模板确定性模拟基建

- 计划日期：2026-09-14
- 状态：已实现（四轮审查 PASS：拆分+钉语义 → 契约闭合 → 公式对齐 → 终审 PASS；按审查意见修订）
- 前置：里程碑 21 已完成；平台跳跃探针（`~/workspace/engine_test/platformer`）完结并产出 `FEEDBACK.md`（下文「建议 A/B/C」对其第二节/第三条编号）。本期兑现其中建议 A（虚拟输入）、B（pause/step_frames/resume）、C（固定步累加器）；碰撞族（物-1~5：kinematic_step/resolve_overlap/单向平台/DynBox/SolidGrid）**拆至 M22b（`docs/plan-23.md`，另走门禁）**——体量与验证基础设施按审查裁定分两期。
- 参考：`AGENTS.md`「功能准入判据」「引擎公共 API 边界」「编码规范」；`engine/include/trogue/coro.hpp`（程序错误抛 `std::logic_error` 先例）、`config.hpp`（限额常量位置）；探针实证：`game/src/app.cpp:40,373-378,424-429,465-528`；模板现状：`template/game/src/main.cpp`（变步长、事件驱动格子游戏——见 §2.3 语义重设计）

## 1. 目的与问题

### 1.1 背景与判据自检

平台跳跃探针完结结论：引擎静态碰撞半边零翻车、静态查询不再扩展；缺口在动态半边与时间步进。本期交付**确定性模拟的两块地基**——没有它们，`step_frames` 式逐帧回归、黄金回放、注入无伪影的自动化试玩都无法成立；它们也是 M22b 碰撞族 E2E 验证的基建（碰撞测试要用 `step_frames` 做 IPC 级确定性断言）。

| 条目 | 机制性 | 确定性 | 可无头测试 | 与玩法无关 |
|------|--------|--------|------------|------------|
| StepClock（固定步累加 + 暂停/授步 + 溢出护栏） | 时间推进纯算术 | 同输入同步数 | 黄金序列 | 是（插值与否归 game） |
| VirtualInput（注入队列 + 步边界消费） | 事件排序与边界提取 | (t,入队序) 稳定序 | 事件表断言 | 是（键位映射归 game） |

反向自检：无任何一项只能回答「让某款游戏更好玩」。土狼/输入缓冲/可变跳高/冲刺充能/踩杀判定/重力曲线/下落加重等**手感参数不进引擎**，格子敌人边缘折返 AI 等**玩法策略不进引擎**（探针报告已自行划界，本计划采纳）。

### 1.2 需求实证（真实调用点）

| 拟入符号 | 消费方实证（平台跳跃探针） |
|----------|---------------------------|
| `StepClock` | `app.cpp:465-528` 手写 `accum` + `guard<5` 累加器（建议 C）；本仓库三个 game 循环各写一遍 `dt=GetFrameTime()`（`game/src/main.cpp:1213`、`anim_viewer.cpp:264`、`template/game/src/main.cpp:404`）——Roadmap 既有挂账「届时定义 advance(dt) → 整步数 + 插值 alpha（插值与否由 game 决定）」本期兑现 |
| `VirtualInput` | `app.cpp:40` pending 队列、`:373-378` `input` 注入命令、`:424-429` `flush_input`/`pending_count` 调试命令（建议 A）；两位独立试玩代理把注入伪影（按键撕裂/幽灵滑行）误诊为游戏 bug，10+ 次假阳性排查 |
| 模板固定步改造 | FEEDBACK 建议 B 全文：探针自建全套（固定步/种子/事件 seq），「引擎/模板应提供」；模板现状变步长事件驱动，无法做确定性回归 |

### 1.3 明确不在范围

- M22b 全部内容：kinematic_step、resolve_overlap、单向平台、DynBox 混合扫掠/承载、SolidGrid（各自判据与边界在 plan-23 论证；其中单向平台是对「里程碑 16：单向平台归 game」边界的反向修订，届时按「实现前先把冲突写入文档并解决」处理）。
- 音频、渲染插值（alpha 消费是 game 绘制策略）、刚体物理、动态-动态互推、寻路（金丝雀继续挂起）。
- 根仓库 demo（回合制）不改：`pause`/`step_frames` 的参考接线落在模板起步 game（派生游戏的直接起点）；demo 维持既有语义。

## 2. 方案

两个新公共头：`engine/include/trogue/time.hpp`、`input.hpp`。值类型 + 自由函数/小 RAII 类，无 OOP 层级、不暴露 raylib 类型（key 用 int + 头注释钉死「平台键值约定」）、单线程、非调试设施（TROGUE_DEBUG=OFF 照常编译）。

### 2.1 StepClock（time.hpp）

```cpp
class StepClock {
public:
    struct Tick { int steps; double alpha; bool overflowed; };
    // step_seconds <= 0 或 max_steps_per_tick <= 0 → 抛 std::logic_error
    //（构造期程序错误，对齐 coro.hpp 先例）
    explicit StepClock(double step_seconds, int max_steps_per_tick = 5);
    Tick tick(double frame_dt);   // 每帧一次：弹出本帧应推进的整步数
    void pause();  void resume(); bool paused() const;
    void credit(int steps);       // 显式授步（暂停下唯一推进来源；credit<=0 无操作）
    double step_seconds() const;
    std::uint64_t total_steps() const;     // 已弹出整步累计（可观测 + 帧粒度锚点；
                                           // 逐步对齐用调用方本地步序，见 input.hpp）
    std::uint64_t overflow_count() const;  // 护栏触发累计（仅时间通道）
};
```

语义（钉死）：
1. **对外单车道、内部双池**：`credit(n)` 加进授步池、`frame_dt` 累加进时间池，`tick` 先弹授步池再弹时间池（调用方无感知差异）。
2. `tick(frame_dt)`：`时间池 += max(frame_dt, 0)`（负 dt 按 0 + 记日志）；弹出 `min(floor(两池合计/step), max_steps_per_tick)` 个整步，**授步池优先扣减**；**溢出丢弃仅作用于时间通道的超额整步，且以时间池现有整步数为上限**（授步池未弹出的余量永远保留——`credit(n)` 授的步**绝不丢**，这正是 `step_frames n` 对任意 n 精确的保证；混合溢出时超额丢弃也不得触碰授步池）。时间池扣减后**每 tick 结束时 `时间池 < step` 恒成立**（不变式，黄金序列冻结）；`overflowed` 仅当时间通道发生丢弃时为 true 且 `overflow_count()+1`。`alpha = 时间池/step ∈ [0,1)`（排空授步期间亦然——alpha 只描述时间通道余量）。
3. **暂停 = 停止时间通道**：暂停下 `tick` 不再累加 frame_dt，但**照常弹出授步池**（含跨多次 tick 排空）；alpha 恒 0（暂停下时间池冻结）。这就是 `step_frames` 在 pause 下对任意 n 精确推进的机制（建议 B 的核心）。
4. `resume` 后 frame_dt 通道接续，`acc`/`total_steps`/计数器均不重置。
5. 黄金序列测试冻结：同 (初始态, dt 序列, credit 序列) → (steps/alpha/overflowed/total_steps) 逐位一致；**必含 `credit(max+2)` → `tick(0)` 两次 → 恰好 max+2 步、零溢出**的反丢步用例。

### 2.2 VirtualInput（input.hpp）

```cpp
struct InputEvent { double t; int key; bool down; };  // key = 平台键值（int；raylib KeyboardKey 数值约定，不引 raylib 头）

class VirtualInput {
public:
    struct Config {
        double step_seconds = 1.0 / 60.0;  // 步边界粒度（与 StepClock 同步长；两处配置，头注释互指对齐方式）
        int min_hold_steps = 0;            // >0：同键 down→up 最小步间隔（防注入撕裂；0 = 原样透传）
    };
    explicit VirtualInput(Config cfg = {});
    void push(int key, bool down, double t);              // 任意时刻注入
    std::vector<InputEvent> step_due(double boundary_t);  // 取 t < boundary 的全部事件并出队
    void flush();  int pending() const;  std::uint64_t dropped() const;
    bool down(int key) const;   // 按键状态视图（**在 step_due 消费时更新**，非 push 时）
};
```

语义（钉死）：
1. **消费在固定步边界**：game 每执行一个固定步，在**步执行前**调 `step_due(i * step_seconds)`——`i` 为调用方**本地步序**（已完成步数，从 0 起，跨帧累计，每执行完一步 +1；不取 `total_steps()`——多步帧内它一次跳变，不能作逐步锚点，仅作累计观测与帧粒度锚点）。一次取走全部到期事件按 `(t, 入队序)` 稳定序应用。注入在两次边界之间的任意时刻都安全——最迟下一边界生效，绝不在步中间撕开。
2. `down(key)` 在**消费时**更新（`step_due` 应用事件后置位/清位）——状态视图与「注入侧真值」一致，未消费的注入不影响 `down()`。
3. `min_hold_steps > 0`：up 事件若距同键上次 down（消费时计步）不足 N 步 → **推迟**到满足间隔的边界出队（down 永不推迟）；**推迟期间同键新的 down 到来 → 未生效的 up 直接丢弃**（最新意图优先）。`flush()` 清空队列（含推迟中未生效的 up）后 `down()` **保持现值**直至下一个真实 up（与「消费时更新」一致——flush 撤销的是未消费事件，不伪造按键状态）。
4. 队列上限 `kPendingMax = 1024`（config.hpp）：超限丢**最旧** + `dropped()` 计数（可观测，不静默）。
5. `t` 由调用方提供（任意单调秒表；引擎只按数值排序与 `< boundary` 提取，不解释时钟来源）；`t == boundary` 归下一批（严格 `<`）。

### 2.3 模板起步 game 的确定性模拟改造（模板自有文件，语义重设计）

模板现状：变步长、事件驱动格子游戏——`move` IPC 命令在 poll 时**同步立即执行**（`template/game/src/main.cpp:325-346`），视觉 tween 每帧吃真实 dt（`:425`）。固定步制下三个语义问题钉死如下：

1. **`move` 排队化**：`move` 命令不再立即执行，转成一条注入动作（等价 `push`）在**下一个固定步边界**消费——与「注入在步边界生效」同一语义，`pause` 下自然冻结。响应包络改为 `{queued:true}`（布尔语义变化是模板自有命令，非引擎契约；模板 `ipc_smoke.py` 同步更新）。
2. **主循环形态**：`clock.tick(dt)` → 对弹出的每一步（本地步序 i）：`input.step_due(i*step)` → 消费按键/注入动作 → 逻辑更新 → `tween.tick(kFixedDt)` → `i += 1`。**暂停冻结 frame_dt 通道的推进（真实时间不再产生步）；授步照常完整执行步（逻辑 + tween 全走）**——这正是 `step_frames` 的机制；渲染与 IPC 照常跑（窗口不黑、可观测仍在）；`status.uptime_s` 保持墙钟（观测口径，不随暂停冻结——写进模板 help/文档）。
3. **E2E 断言口径（模板 ipc_smoke.py，可执行化）**：
   - `pause` → `move`（queued）→ `status` 实体逻辑位置**不变**；
   - `step_frames 1` → 恰好消费一条排队动作，实体逻辑格 **精确 +1 格**（格子游戏的「N 步期望值」= 按动作队列消费计数，不按像素）；
   - `input {key:"KEY_X",down:true}` 注入 → `step_frames 2`（min_hold=0 下不产生位移的键）→ `input_stats.pending==0`、`down(KEY_X)==true`（经 `input_stats` 暴露）；
   - `resume` → 实时推进接续（sleep 后 `status` 的 uptime_s/移动响应口径断言）。
   - 视觉断言：`step_frames` 推进至 tween 完成（`kMoveDuration≈0.12s` @ 1/60 ≈ 8 步，**推进 12 步或轮询至 `transform.moving==false`**）后，`transform.visual` **精确等于**逻辑格像素（固定步 tween 必在整步边界越过 duration 并 snap，复刻「数值精度纪律」）。

新 IPC 命令（模板自有命令集）：`pause`、`resume`、`step_frames {n}`、`input {key,down,t?}`（t 缺省 = **上一边界** `(i−1)*step`——即最近一次 `step_due` 所用的边界值，i 为本地已完成步数；因此注入后**立即下一步生效**，与「最迟下一边界生效」吻合。模板 handler 用本地步序计算，引擎命令层不感知）、`input_flush`、`input_stats {pending,dropped,down:[...]}`；全部登记 `help`。

### 2.4 明确不进引擎

- `sweep_move`/既有碰撞签名**一个不动**（M22b 才评估扩展）。
- 引擎不感知 IPC 命令（pause/step_frames 是模板 handler 调 StepClock/VirtualInput 的薄封装）；引擎不持有游戏状态、不注册 update 回调（库不翻转为框架）。
- StepClock 与 VirtualInput 不合并构造（正交原语各自无头可测；头注释互指对齐惯例：**调用方本地步序 `i`，步执行前调 `step_due(i*step)`**，`i` 每执行完一步 +1；`total_steps()` 仅作累计观测）。

## 3. 步骤（含开发流程 3~7）

1. 契约钉死复核：§2.1/2.2/2.3 的全部语义直接落头注释与模板 help 文案（注释自足）。
2. `engine/include/trogue/time.hpp` + `engine/src/time.cpp`：StepClock。
3. `engine/include/trogue/input.hpp` + `engine/src/input.cpp`：VirtualInput；`config.hpp` 加 `kPendingMax`。
4. 伞头 `trogue.hpp` 追加；`engine/CMakeLists.txt` 源清单追加。
5. 单测 `tools/tests/step_clock_test.cpp`（黄金序列：正常推进/暂停冻结/credit 授步含 `tick(0)` 取满/单帧 clamp 与溢出后态/负 dt/构造非法抛异常）、`tools/tests/virtual_input_test.cpp`（乱序稳定序/边界严格 `<`/min_hold 推迟与再 down 丢弃/down 消费时更新/flush/pending/dropped/上限丢最旧）→ `tools/CMakeLists.txt` 注册。
6. 模板改造：`template/game/src/main.cpp` 固定步主循环 + 六条 IPC 命令 + help；`template/tools/ipc_smoke.py` 按 §2.3 断言口径重写（模板自有文件，手工维护——上游 sync 永不覆盖，记入遗留）。
7. 回归：Debug + Release 零告警；根仓库 ctest 全绿（demo 未动，冒烟 87 项不受影响）；模板独立副本构建 + 起服 + 模板冒烟全过。
8. 模板维护者刷新：`template/engine/**` 携带新模块（sync 脚本维护者模式）。
9. 文档：`AGENTS.md`——架构分层模块清单（+time/input）、引擎公共 API 边界里程碑 22 块、Roadmap（建议 A/B/C 兑现勾选 + M22b 拆分说明）；模板 `AGENTS.md` **不动**（零倾向纪律；参考接线即示例）。
10. subagent 审查未提交代码（禁止自检）。
11. CHANGELOG（审查通过后）。
12. commit message（英文预览，确认后提交推送）。

### 文件清单

| 文件 | 动作 |
|------|------|
| `engine/include/trogue/time.hpp`、`engine/src/time.cpp` | 新增 |
| `engine/include/trogue/input.hpp`、`engine/src/input.cpp` | 新增 |
| `engine/include/trogue/config.hpp`、`trogue.hpp`、`engine/CMakeLists.txt` | 改 |
| `tools/tests/step_clock_test.cpp`、`virtual_input_test.cpp` | 新增 |
| `tools/CMakeLists.txt` | 改（注册测试） |
| `template/game/src/main.cpp` | 改（固定步主循环 + 6 命令） |
| `template/tools/ipc_smoke.py` | 改（模板自有，断言口径重写） |
| `template/engine/**` | 刷新 |
| `AGENTS.md`、`CHANGELOG.md`、`docs/plan-22.md` | 改/新增 |

## 4. 验证清单

- [ ] StepClock：黄金序列逐位一致（正常/暂停/授步混合脚本 ≥3 组，**必含 `credit(max+2)` → `tick(0)` 两次 → 恰好 max+2 步、零溢出**的反丢步用例，及授步+时间同帧超 clamp 的混合溢出用例——授步不丢、超额丢弃不越时间池界）；暂停下 `tick(0)` 排空授步池且 alpha==0；单帧超 clamp 时仅时间通道丢弃、每 tick 后 `时间池 < step` 不变式；`overflow_count` 仅时间通道触发；负 dt 安全；`step_seconds<=0` 或 `max_steps_per_tick<=0` 抛 `std::logic_error`。
- [ ] VirtualInput：乱序注入按 `(t,入队序)` 稳定出队；`t==boundary` 归下一批；本地步序 `i*step` 逐步对齐（多步帧内各步 boundary 递增）；min_hold 推迟 up、推迟期新 down 丢弃未生效 up；flush 清队列（含未生效 up）后 `down()` 保持现值；`down()` 消费时更新（push 后未消费不影响）；flush/pending/dropped；1024 上限丢最旧。
- [ ] 模板 E2E（§2.3 断言口径逐条）：pause 冻结 move/step_frames 精确 +1 格/注入步边界生效/visual 整步边界精确等于逻辑格/resume 接续。
- [ ] 根仓库零回归：ctest 全绿；`ipc_smoke.py` 87 项全过（demo 未改）。
- [ ] Debug + Release 零告警；模板独立副本构建零告警 + 模板冒烟全过。
- [ ] 新增注释零内部文档指针；模板 `AGENTS.md` 未被改动。

## 5. 遗留与边界

- **M22b（碰撞族，`docs/plan-23.md` 另走门禁）**：kinematic_step（含 landed 纯几何定义：`landed = 步末探地命中 ∧ 对入参原位同款探地未命中`，单条差分定义）、resolve_overlap（含后置条件与不保证项）、单向平台（第一轮审查裁定**进**，四条件：kEpsilon=1e-3 常量钉死 / 规则交互写进头注释 / 里程碑 16 边界修订点名进文档 / 规则表外溢即降级金丝雀）、DynBox（字段 `{box, delta}`，carrier 报告下标；`sweep_move` 本体不动）、SolidGrid（refresh 遇非 solid 层 → error；O(w·h) 成本写明）；M22b 落地后用本期 `step_frames` 做 IPC 级确定性 E2E，并按 plan-16 先例评估 demo `probe_kinematic` 探针命令。
- **模板 ipc_smoke.py**：模板自有文件手工跟进，上游不自动覆盖（既有约定）。
- **探针项目回流**：platformer 是否用 StepClock/VirtualInput 替换手写累加器与注入队列，由该项目自行评估（下游动作，非本仓库交付）。
- **SolidGrid 大地图高频改格场景的单格刷新**（与 M22b 相关）：需求出现再评估。

## 6. 补记（代码审查后追加）

本变更集随车携带一项**范围外的未提交遗留**（用户 2026-09-13 直接拍板的模板文档精简，发生于本计划起草之前、上一会话）：`template/AGENTS.md` 重写（52 行极简版）+ `template/API.md` 删除（Agent 直读头文件）+ 根 `AGENTS.md`「项目模板」节相应改写。M22 实现本身未触碰这两个文件（计划 §3 步骤 9「模板 AGENTS.md 不动」、§4「模板 AGENTS.md 未被改动」对 M22 实现成立）；随本变更集一并提交，特此记录归属。代码审查（PASS）对此裁定的意见已按「补记随车变更」选项处理。
