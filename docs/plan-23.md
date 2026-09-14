# 里程碑 23（M22b）：运动/物理主线第二期——动态实体半边碰撞原语

- 计划日期：2026-09-14
- 状态：计划审查通过（两轮：FAIL 5 Major → 修订 → PASS；4 项文字 Minor 已随复审意见清理），待用户拍板
- 前置：M22a（`docs/plan-22.md`）已完成；平台跳跃探针完结报告 `FEEDBACK.md` 物-1~5 为需求实证。本计划兑现物-1（kinematic_step）、物-2（DynBox 混合扫掠）、物-3（resolve_overlap）、物-4（单向平台）、物-5（SolidGrid 联动）。
- 参考：`AGENTS.md`「功能准入判据」「引擎公共 API 边界」；既有实现 `engine/include/trogue/collision.hpp`、`engine/src/collision.cpp`；探针实证：`~/workspace/engine_test/platformer/game/src/player.cpp:14-33`（手写探针脚手架）、`world.cpp:388-397`（落压石 while 回退 1px 脱出）。

## 0. 边界冲突修订（先行声明，按「实现前先把冲突写入文档并解决」处理）

里程碑 16 曾裁定「单向平台/斜坡不进引擎」。本期对该裁定做**反向修订**：单向平台（仅 tile 矩形形状）进引擎，理由与四条件见 §2.3。`AGENTS.md`「引擎公共 API 边界」里程碑 16 块将同步改写。其余裁定（刚体物理、斜坡、分层碰撞矩阵、寻路、动态-动态互推 solver）维持不变。

## 1. 目的与问题

### 1.1 背景与判据自检

探针收敛结论：静态半边（sweep_move/aabb_overlap/rect_hits_solid）全程零翻车、segment_hits_solid 未被真正用上——**静态几何查询不再扩展**；缺口全在动态实体半边。本期交付动态半边的执行原语，全部是「矩形与几何的关系」这一既有故事向动态输入的延伸：

| 条目 | 机制性 | 确定性 | 可无头测试 | 与玩法无关 |
|------|--------|--------|------------|------------|
| kinematic_step（sweep + 探针 + 事件派生） | 探针约定与差分定义收进引擎 | 同输入同结果 | 逐事件断言 | 是（手感参数归 game） |
| resolve_overlap（最小轴脱出） | 纯几何解算 | 同输入同结果 | 暴力对照 | 是（谁要脱出归 game） |
| DynBox 混合扫掠（+承载索引） | swept 解算扩一组 AABB | 同输入同结果 | 逐轴断言 | 是（承载次序归 game） |
| 单向平台（sweep 的穿越规则） | 机制性穿越语义 | 规则确定性 | 覆盖规则表 | 是（放哪些平台归 game） |
| SolidGrid（双真相同步视图） | 掩码物化 + 同步写 | 同输入同掩码 | 掩码断言 | 是（何时改格归 game） |

反向自检：土狼/输入缓冲/可变跳高/冲刺充能/踩杀判定/重力曲线/边缘折返 AI 仍不进引擎（探针报告自行划界，本计划采纳）。FEEDBACK 物-1 建议的 `KinematicBody`（持 pos/vel 的运行时对象）**不采纳**——引擎不持有游戏状态；改为无状态自由函数，速度积分与状态所有权归 game。

### 1.2 需求实证（真实调用点）

| 拟入符号 | 消费方实证（平台跳跃探针） |
|----------|---------------------------|
| kinematic_step | `player.cpp:14-33` 手写 `probe_solid`/`grounded_probe`/`wall_probe`（含「左右探墙需内缩 2px 防把相邻地面误判成墙」的探针约定）+ `:179-193` landed/hit_x/hit_y 事件派生——约 70 行纯脚手架，任何连续位移角色都要重推一遍（物-1） |
| resolve_overlap | `world.cpp:388-392` 落压石 slam 落地用 `while rect_hits_solid → m.y -= 1` 逐像素回退，O(深度) 且不优雅（物-3） |
| DynBox 混合扫掠 | 落压石/移动平台在探针中只能做成纯危险物绕过（物-2）；`set_tile_at` 粗粒度挪 tile 是现有限制 |
| 单向平台 | 探针未用上，但为品类第一高频碰撞需求；当前 sweep 语义做不了「仅下落碰撞 + 上一帧底边在平台之上」（物-4） |
| SolidGrid | 探针晶障粉碎需同时改 SceneAsset tile（渲染）与自持 `uint8` mask（碰撞），两份真相手工同步（物-5） |

### 1.3 明确不在范围

- 刚体物理/约束求解、动态-动态互推（两 DynBox 互挤的传递解算——调用方按序调用即得，链式承载次序归 game）、圆/胶囊/旋转形状、斜坡、持续接触、broad-phase、寻路（金丝雀规则继续挂起）。
- `sweep_move`/`segment_hits_solid` 等既有签名一个不动（修订只做增量）。
- 根仓库 demo 玩法不变：只新增 `probe_kinematic` 纯函数探针命令（plan-16 `probe_collide` 先例）。

## 2. 方案

全部落在既有 `engine/include/trogue/collision.hpp` / `engine/src/collision.cpp`（同一模块故事：矩形/线段与静态几何 + 动态盒输入）。值类型 + 自由函数 + 单个 RAII 掩码类，无 OOP 层级、不暴露 raylib 类型、无随机（确定性）、非调试设施。内部实现复用 `solve_axis`/`nudge_clear` 等既有私有助手，不改其签名。

新常量（钉死在 collision.hpp 头部）：

```cpp
inline constexpr float kEpsilon = 1e-3f;        // 贴边/穿越判据的统一容差
inline constexpr float kKinematicProbe = 1.0f;  // 探地/探墙探针厚度（px）
inline constexpr float kWallProbeInset = 2.0f;  // 探墙上下内缩（防相邻地面误判成墙）
```

### 2.1 kinematic_step（动态实体的固定步位移 + 事件派生）

```cpp
struct KinematicEvents {
    bool grounded = false;      // 步末贴地
    bool landed = false;        // 本步发生落地（差分定义，见下）
    bool hit_ceiling = false;   // 本步向上被挡
    bool hit_wall = false;      // 本步横向被挡
    int wall_dir = 0;           // 步末贴墙方向：-1 左 / +1 右 / 0 无
};
struct KinematicResult {
    SweepResult sweep;          // 位移解算全量结果（box/blocked_x/blocked_y/result，契约同 sweep_move）
    KinematicEvents ev;
};
KinematicResult kinematic_step(const SolidGridView* views, int count,
                               Rect box, Vec2 delta);
```

语义（钉死）：

1. **位移解算**：内部先调 `sweep_move(views, count, box, delta)`——轴分离、先 X 后 Y、不重叠不变式全部沿用既有契约；`hit_ceiling = blocked_y && delta.y < 0`、`hit_wall = blocked_x`（`hit_wall` 与 FEEDBACK 建议的 `hit_x` 同义，命名取语义）。
2. **探地**（grounded）：对解算后 box 取 `[x, y+h, w, kKinematicProbe]` 探地矩形命中 solid → grounded。
3. **landed 单条差分定义**：`landed = grounded(解算后) ∧ 对入参原 box 同款探地未命中`。一步内不产生二义：从空中跨过贴地仅当「原位探空 + 落位探实」同时成立。
4. **探墙**：步末 box 左右各 1px、上下内缩 `kWallProbeInset`（防把相邻地面误判成墙，探针实证）；右命中 → wall_dir=+1，左命中 → -1（右优先，确定性）。
5. 参数非法（同 sweep_move 判据）→ `sweep.result = error`、sweep.box 返回入参原值、全事件 false、wall_dir=0（事件派生在非法入参下无意义；调用方经 `KinematicResult::sweep` 直接取得既有 SweepResult 契约）。
6. **不进引擎**：速度积分（delta 由 game 算）、土狼/缓冲/可变跳高、落地回充等手感与规则。探针只回答「贴地/贴墙/被挡」，不定义手感。

### 2.2 resolve_overlap（最小轴脱出）

```cpp
struct OverlapResult {
    Rect box{0, 0, 0, 0};
    bool resolved = false;   // false = 参数非法；true = 后置条件成立（见下）
};
OverlapResult resolve_overlap(const SolidGridView* views, int count, Rect box);
```

语义（钉死）：

1. 前置：box 与 solid 重叠（不重叠时直接返回 `{box, true}`）。调用方典型来源是「快速动态盒撞进静态几何」（探针落压石场景）。
2. 对 X、Y 两轴分别计算**把重叠清空所需的最小单轴位移**（向两侧取更近者：沿该轴把 box 移出所有相交 solid 单元；多层时对同方向取各层所需的最大推距——逐层取最紧，句式同 solve_axis），取两轴中位移小者执行；相等取 X（确定性）。
3. 后置条件：`resolved == true` ⇒ `rect_hits_solid(结果 box)` 为 clear，且位移方向唯一、值为上述最小可行推距。
4. **不保证项**：推距无上限（必要时可能横穿整片实心区才脱离层矩形），不做迭代搜索最小化、不做旋转、不处理跨层折中；除参数非法外不设失败分支——有限 solid 层下单轴推进总能离开层矩形（= 无数据 = 不阻挡），因此 `resolved = false` 仅出现在参数非法时。
5. 参数非法（同 rect_hits_solid 判据）→ `{入参原值, false}`。
6. 探针 `world.cpp` 的 while 回退循环替换为一次调用（E2E 断言见 §4）。

### 2.3 单向平台（sweep 穿越规则；里程碑 16 边界修订）

单向平台是**game 自持的矩形数组**（非 tile 层），经独立入参进入位移解算——放哪些平台、何时撤销（下跳穿越）完全是 game 的数组成员决策：

```cpp
KinematicResult kinematic_step(const SolidGridView* views, int count,
                               const Rect* one_way, int one_way_count,
                               Rect box, Vec2 delta);          // 重载 2
SweepResult sweep_move(const SolidGridView* views, int count,
                       const Rect* one_way, int one_way_count,
                       Rect box, Vec2 delta);                  // 重载 2
```

规则表（钉死，全部围绕 Y 轴下落穿越——规则表条数以头注释为准，表外溢即按 plan-22 审查裁定降级金丝雀重议）：

1. 仅 `delta.y > 0`（下落）参与阻挡；向上与横向永远穿过。
2. **单一阻挡谓词**（合并 ε 容差与「已在平台内」两个条件，kEpsilon 容差优先防浮点残差误穿透）：入参 box 底边 `≤ plat.top + kEpsilon` **且** 解算新底边（未加本约束时）`≥ plat.top`（**抵达或越过**均算命中，含 `==`——消除「恰好走到底不触发」的隧道化）→ 阻挡。底边 `> plat.top + kEpsilon` 即视为已在平台内部，无论位移多大都不阻挡。
3. X 相交按**先 X 解算后的 x 跨度**与平台半开相交判定（与轴分离次序一致）。
4. 命中：`box.y = plat.top - box.h`，`blocked_y = true`（与 solid 同路进入事件派生；landed 差分定义照常成立）。
5. grounded/landed 探地矩形把单向平台一并纳入（站上平台即贴地）；探墙**不含**单向平台（侧面永远穿过）。
6. **不可逆性提示**（写进头注释，防下游误诊）：底边一旦落入平台内部（`> plat.top + kEpsilon`），此后永不阻挡——平台上移不会重新捕获、效果等同「下跳穿越后不可逆」。平台位移与承载归 game，此为规则表既定后果。

层间与通道交互（钉死）：单向平台约束与静态 solid 约束在同一 Y 轴解算中**取最紧者**（谁把停位提得更高谁生效），`blocked_y` 语义一致；探墙只看静态层。

四条件兑现（plan-22 审查裁定的四项要求，与上方规则表条数是不同维度）：① `kEpsilon` 等常量钉死在 collision.hpp 头部（见 §2 开头）；② 规则交互（穿越/贴地/探墙/最紧约束的取舍）全部写进头注释；③ 本节 + `AGENTS.md` 里程碑 16 块改写即边界修订记录；④ 规则表以头注释为唯一权威、表外溢即降级金丝雀重议。**取巧路径明确不收**：厚度参数化、双向平台、斜坡（均为规则表膨胀项）。

### 2.4 DynBox 混合扫掠（动态实心盒 + 承载索引）

```cpp
struct DynBox { Rect box; Vec2 delta; };   // 位置 + 本步位移（速度积分归 game）
struct MixedSweepResult {
    SweepResult sweep;        // 静态+动态合并的解算结果（box/blocked/result）
    int hit_dyn_x = -1;       // 阻挡 X 轴的 DynBox 下标（-1 = 无；同为阻挡取小下标）
    int hit_dyn_y = -1;
};
MixedSweepResult sweep_move_mixed(const SolidGridView* views, int count,
                                  const DynBox* others, int other_count,
                                  Rect box, Vec2 delta);
```

语义（钉死）：

1. 其余 DynBox 在本步内视为**瞬时 AABB 障碍**（不参与彼此的本步位移——多盒链式承载由调用方按序多轮调用编排，引擎不做 solver）。
2. 解算与 `sweep_move` 完全同构（轴分离、先 X 后 Y、贴边、不重叠不变式），只是每轴的阻挡集 = 静态层 ∪ 各 DynBox AABB。
3. **承载语义收窄为索引报告**：`hit_dyn_x/hit_dyn_y` 只报告「谁挡的」；骑乘/携带的位移增量（站在移动平台上跟着走）由 game 用该下标自算——引擎不保存上一步关系、不推平台（FEEDBACK 物-2 的 ride/carry 增量属关系记忆，归 game 状态）。静态层与某 DynBox 同时阻挡同轴时，索引仍报告该 DynBox 的下标（静态层不占下标，`hit_dyn_*` 只描述动态阻挡者）。
4. `sweep_move` 本体签名与语义一个不动；混合能力只走本重载。
5. 起始即与某 DynBox 重叠：该方向解算为 0 位移（同 sweep_move 对静态重叠的约定，不保证脱出；resolve_overlap 不扩 DynBox 版本，动态重叠解算归 game）。
6. 参数非法（任一 DynBox box/delta 非有限或尺寸非正）→ result=error。

### 2.5 SolidGrid（双真相同步视图，物-5）

```cpp
class SolidGrid {   // RAII 自持掩码；与 SceneAsset solid 层保持同步的碰撞真值载体
public:
    SolidGrid() = default;
    // 从 asset 第 layer 层物化掩码；层不存在或非 solid 层 → error（不创建）。
    static ErrorOr<SolidGrid> load(const SceneAsset& asset, int layer);
    // 整层重物化：O(w·h)。层不存在或非 solid 层 → error，旧掩码保留。
    tg::expected<void, Error> refresh(const SceneAsset& asset, int layer);
    // 单格同步：委托 asset.set_tile_at 写资产内存态，成功后同步本掩码该格。
    // 双调用合一，消灭「改了渲染忘改碰撞」的两真相失步（物-5 实证）。
    // asset 写失败（值域/坐标错）→ error，掩码零修改。
    // 注意：asset 为非 const 引用（set_tile_at 是受限可变窗口）。
    tg::expected<void, Error> set_tile(SceneAsset& asset, int layer,
                                       int tx, int ty, int value);
    const SolidGridView& view() const;   // layer_id = load/refresh 的 layer
    // 拷贝禁用（拷贝会使 view_.mask 悬垂）；移动自动修正指针（移动赋值后
    // 源对象的 view() 失效）——代码审查裁定修订原「重新取 view()」补救句，
    // 该补救对拷贝无效。
};
```

语义（钉死）：

1. 掩码语义与既有物化一致：`tiles != -1 → 1`（掩码只记阻挡与否，不记 tile 值）。
2. `refresh` 遇非 solid 层 → error（不静默清空）；O(w·h) 成本写进头注释（大地图高频改格用 set_tile 而非 refresh）。
3. `set_tile` 的**同步原子性**：先写 asset（失败即整体返回 error、掩码不动），成功后才改掩码——两份真值在任何失败路径下都不分叉。
4. 不进引擎：何时改格、watcher 触发 refresh 的策略归 game；`SceneAsset::set_tile_at` 签名不动。

## 3. 步骤（含开发流程 3~7）

1. 契约钉死复核：§2.1–2.5 全部语义直接落 collision.hpp 头注释（注释自足，零内部文档指针）。
2. `collision.hpp`：新常量 + KinematicEvents/KinematicResult/OverlapResult/DynBox/MixedSweepResult + kinematic_step×2 + resolve_overlap + sweep_move 重载 2 + SolidGrid。
3. `collision.cpp`：实现（复用 solve_axis/nudge_clear/materialize_views；单向平台在 solve_axis 的 Y 通道加穿越判定；混合扫掠在每轴 allowed 上并入 DynBox 约束）。
4. 单测 `tools/tests/kinematic_test.cpp`（事件派生：landed 差分/贴墙内缩/hit_ceiling；单向平台规则表逐条 + 下跳穿越 + 平台边缘横穿；混合扫掠逐轴 blocked + hit_dyn 索引 + tie 取小下标；resolve_overlap 暴力对照 ≥40 组 + 参数非法返回 `{原值, false}`；SolidGrid load/refresh/set_tile 失败零修改与同步断言）→ `tools/CMakeLists.txt` 注册。
5. demo `probe_kinematic` IPC 命令（plan-16 `probe_collide` 先例）：入参 `kinematic`/`resolve`/`mixed`/`one_way` 各分支（纯函数，天然确定性），响应返回解算盒与事件；`tools/ipc_smoke.py` 增对应断言。
6. 回归：Debug + Release 零告警；ctest 全绿；根冒烟（87 + 新增）全过。
7. 模板维护者刷新：`template/engine/**` 携带新原语（sync 脚本维护者模式）。
8. 文档：`AGENTS.md`——架构分层 collision 行描述扩充、里程碑 16 API 边界块修订（单向平台进引擎，§0 冲突解决记录）、新增里程碑 23 块、Roadmap 运动主线勾选 M22b；CHANGELOG（审查通过后）。
9. subagent 审查未提交代码（禁止自检）。
10. commit message（英文预览，确认后提交推送）。

### 文件清单

| 文件 | 动作 |
|------|------|
| `engine/include/trogue/collision.hpp`、`engine/src/collision.cpp` | 改（扩充） |
| `tools/tests/kinematic_test.cpp` | 新增 |
| `tools/CMakeLists.txt` | 改（注册测试） |
| `game/src/main.cpp`（demo probe_kinematic） | 改 |
| `tools/ipc_smoke.py` | 改（+探针断言） |
| `template/engine/**` | 刷新 |
| `AGENTS.md`、`CHANGELOG.md`、`docs/plan-23.md` | 改/新增 |

## 4. 验证清单

- [ ] kinematic_step：landed 差分定义逐条（原地不动不报 landed/下落一步着陆报/持续站立不重复报）；探墙内缩 2px 用例（相邻地面不误报 wall_dir）；hit_ceiling/hit_wall 与 blocked 映射；参数非法 sweep.result=error 且全事件 false。
- [ ] 单向平台：阻挡谓词逐边界断言（底边恰 ≤ plat.top+kEpsilon 抵达即挡、== 边界不隧道化、底边 > plat.top+kEpsilon 已在内不挡）；`delta.y ≤ 0` 全穿越；先 X 后 Y 跨度；停位 blocked_y 且 box.y = plat.top - box.h；grounded 纳入/探墙不纳入；下跳穿越（数组摘除）即穿过；**单向平台与静态 solid 同步阻挡取最紧**；**one_way 数组为空时重载与既有语义逐位一致**（回归）。
- [ ] resolve_overlap：≥40 组暴力对照（随机生成重叠盒，断言 resolved ⇒ 不重叠 且推距 ≤ 逐像素回退法）；轴 tie 取 X；参数非法返回 `{原值, false}`；多层视图逐层取最紧。
- [ ] sweep_move_mixed：静态+动态最紧约束；hit_dyn 索引正确（多盒阻挡取小下标；静态+动态同轴阻挡仍报 DynBox 下标）；sweep_move 本体回归不动（既有 collision_test 全绿）。
- [ ] SolidGrid：load/refresh 非 solid 层 error 且零修改；set_tile 失败（值域/坐标）掩码零修改、成功后 view() 立即可见；refresh 全层一致。
- [ ] demo probe_kinematic 各分支冒烟断言（确定性纯函数，无需 step_frames）。
- [ ] 根仓库零回归：ctest 全绿、冒烟 87 + 新增全过；Debug + Release 零告警。
- [ ] 新增注释零内部文档指针；模板 game 未被改动。

## 5. 遗留与边界

- **E2E 口径偏差说明**：plan-22 §5 曾预告「M22b 落地后用 step_frames 做 IPC 级确定性 E2E」；本期各原语均为无状态纯函数（probe 命令天然确定性、与模拟时钟正交），故 demo 侧以纯函数探针断言替代 step_frames——偏差已评估成立，step_frames 的用武之地在模板/派生游戏的固定步玩法回归，不在碰撞原语探针。
- **动态-动态链式承载**（多盒互推、平台叠平台）：单轮混合扫掠 + game 编序已覆盖探针需求；出现第二个消费方仍要 solver 时重议。
- **SolidGrid 高频单格刷新的资产侧批量口**（set_tile 循环 vs 批量 API）：plan-22 已裁定「小区域填充 = game 侧循环」，维持。
- **kinematic_step 混合版**（对 DynBox 同时解算+事件派生）：首个消费方需要「站上移动平台也算 grounded」的完整事件时再加；当前索引报告足够组合。
- 探针项目回流（platformer 是否替换手写脚手架）为下游动作，非本仓库交付。
