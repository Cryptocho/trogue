# 里程碑 20：单格 tile 写入（SceneAsset::set_tile_at）

- 计划日期：2026-09-13
- 状态：计划中（待审查）
- 前置：里程碑 17（`update_layer_tiles` 受限整层更新）、18/19 已完成。Roadmap「单格 tile 写入（P3，2026-09-13 拍板）」为直接输入。
- 参考：`AGENTS.md`「架构分层·设计约定」「引擎公共 API 边界」「编码规范」；`engine/include/trogue/scene.hpp:123-126`、`engine/src/scene_asset.cpp:126-160`（整层更新实现现状）、`tools/tests/scene_query_test.cpp:340-364`（既有测试基线）

## 1. 目的与问题

### 1.1 判据自检（本里程碑补齐/验证了引擎哪项通用能力）

本里程碑补齐的能力 = **「运行时 tile 层的逐格写入」**。它逐条满足功能准入判据：

| 判据 | 本里程碑 |
|------|----------|
| 机制性 | 是：既有一层写原语的窄化变体（单格寻址 + 值域校验 + O(1) nonempty 维护），不含任何玩法决策 |
| 确定性 | 是：同输入同结果；成功后 `tile_at`/`is_solid_at`/`render_scene` 立即可见（同缓冲区，无缓存层） |
| 可无头测试 | 是：纯公共 API 单测（与 `update_layer_tiles` 用例同文件同基线） |
| 与美学/玩法无关 | 是：何时写、写什么（挖墙、放置、地形演变）归 game |

> 反向自检：本计划没有任何一项只能回答「让某款游戏更好玩」。**引擎不保存地形状态、不做扩散式重排**的既有边界不变——本 API 只是「调用方主动请求改一格」的写入原语，autotile 重排仍由 game 经 `pick_tile` 自行决定。

### 1.2 需求实证（为什么是现在，而不是预造）

**工效税（2026-09-13 评估拍板记录在案）**：现有唯一写入入口 `update_layer_tiles` 是整层原子替换——「挖一格墙」需要 `tile_grid` 取整层数组（4096×4096 上限）→ 改一格 → 整层校验写回 → O(n) nonempty 重算。对破坏地形、逐帧生成地形（falling-sand 类）游戏，每格 O(n) 是实打实的工效税；且整层替换的「全有或全无」校验语义对单格编辑过重。

| 拟入符号 | 消费方实证 |
|----------|------------|
| `SceneAsset::set_tile_at` | ① demo IPC `set_tile` 探针命令（本里程碑接入）：Agent 运行时地形实验——挖墙/填墙 → `solid_at`/`get_tile` 立即断言，补齐「set_entity/spawn 运行时实验」所缺的**地形维度**（AGENTS.md 调试工作流第 3 步）；② AGENTS.md 2026-09-13 评估记录的下游需求（破坏地形/逐帧生成地形游戏） |

单符号里程碑；`update_layer_tiles`（plan-17）已确立「asset 可变窗口」先例，本 API 是同一窗口的窄化补充，非新职责。

### 1.3 明确不在范围

- **批量/区域写入**：区域填充 = game 侧循环调用 `set_tile_at`（每格 O(1)）；跨格原子性无消费方需要，不预造。
- **autotile 联动**：写一格后自动重排邻格 tile id 是玩法决策（terrain 指派归 game）；game 需要时用 `pick_tile` 自行计算再写入。引擎不保存地形状态、不做扩散式重排（既有边界，复述强调）。
- **`SceneEntity`/动画集的运行时修改**：descriptor 是只读快照，本里程碑不扩。
- **多格 tile 的占用语义**：`size_in_atlas` 多格 tile 逻辑上仍占一个 cell（既有 schema 语义），单格写不引入跨 cell 占用检查。
- **watcher/reload 交互**：内存修改不落盘；watcher/F5/reload 以磁盘文件覆盖内存修改（与 `genmap`「生成后会被覆盖——预期行为」同一语义，文档注明）。

## 2. 方案

`engine/include/trogue/scene.hpp` 的 `SceneAsset` 新增成员函数 + `engine/src/scene_asset.cpp` 实现。与 `update_layer_tiles` 同为「受限可变窗口」，错误模型一致（`ErrorOr<void>` + `kInvalidArgument`）。

### 2.1 API

```cpp
// 写入一层的一个 tile 格（层局部 tile 坐标，不含层 origin 像素偏移）。
// value == -1 表空格；其余值须在该层值域内（图集模式 [0, tileset.count)、
// palette 模式 [0, palette.size)）。成功后该格立即对 tile 查询与渲染可见，
// 层 nonempty 计数 O(1) 维护。
// 错误：layer_index 越界 / (tx,ty) 不在 [0,width)×[0,height) / bare 场景 /
// 值超值域 → kInvalidArgument（资产不被修改）。
// 单线程：与 asset swap 同纪律——不在绘制调用进行中调用（主循环两次绘制间）。
ErrorOr<void> set_tile_at(int layer_index, int tx, int ty, int value);
```

### 2.2 语义钉死（供审查）

1. **坐标：层局部 tile 坐标**：`(tx, ty)` ∈ `[0, width) × [0, height)`，与 `tile_grid`/`solid_mask` 的 tile 坐标约定同族；**不含层 origin**（origin 是世界像素偏移，属于查询侧的坐标换算，写入侧按层内寻址）。越界 → `kInvalidArgument`（**不是**「层外无数据」——写入与查询的层外语义不对称是刻意的：查询层外=不阻挡，写入层外=无数据可写=参数错误）。
2. **值域校验与整层更新同一规则**：图集层按所引 tileset 的 `tile_count`、palette 层按 `palette.size()`、bare 场景无可写层；`-1` 恒合法。实现上与 `update_layer_tiles` 共享值域上限的推导逻辑（提取私有 helper，避免两处漂移）。
3. **nonempty O(1) 维护（原地更新）**：旧值 == -1 且新值 != -1 → +1；旧值 != -1 且新值 == -1 → −1；其余不变。不再整层重算。实现须**原地修改 `impl_->layers[li].nonempty`**（不得重建 layers 向量）——既有测试以 `layer()` 引用捕获语义依赖原地更新，`set_tile_at` 须保持同一行为。
4. **可见性即时**：写入直接改 `impl_->layer_tiles[li][ty*width+tx]`；`tile_at`/`is_solid_at`/`render_scene`/`tile_grid`/`solid_mask` 全部读同一缓冲区，无失效/刷新步骤。
5. **失败零修改**：任何校验失败 → `tl::unexpected`，资产保持原值（与 `update_layer_tiles`「失败时不修改资产」契约一致）。
6. **单线程时序**：与「asset swap 只能在绘制帧外」同一纪律——不在 `render_scene`/`render_sprite` 执行期间调用（单线程下不会发生，契约声明）。

### 2.3 demo 探针命令（game 层）

`game/src/main.cpp` 新增 IPC 命令 `set_tile`：参数 `layer`（必填 int，层索引）、`tx`/`ty`（必填 int，**层局部 tile 坐标**）、`value`（必填 int，-1 表空）→ 调 `set_tile_at` → 成功 `{set:true, layer, tx, ty, value}`；引擎错误 → 错误包络（透传 `error().message`，与 `genmap` 的错误透传同型）；参数缺失/非整数 → 错误包络。**坐标口径换算须写进命令表**：既有 `get_tile`/`solid_at` 收世界像素坐标，`set_tile` 收层局部 tile 坐标——`tx = floor((px − 层origin_x) / tile_w)`（origin 默认 [0,0]）。加入 `help` 列表。

### 2.4 公共 API 边界（本里程碑后的增量）

`AGENTS.md`「引擎公共 API 边界」追加（里程碑 20 块）：
- **单格 tile 写入**：`SceneAsset::set_tile_at(layer, tx, ty, value)`——受限可变窗口的窄化补充（层局部 tile 坐标、值域同整层更新、失败零修改、nonempty O(1) 维护）；autotile 重排与地形指派仍归 game。

## 3. 步骤（含开发流程 3~7）

1. **实现**：`scene.hpp` 声明 + 契约注释；`scene_asset.cpp` 实现（值域 helper 与整层更新共享）。
2. **单测** `tools/tests/scene_query_test.cpp` 追加用例（与 `test_update_layer_tiles` 同文件，见 §4.1）。
3. **demo 探针**：`main.cpp` 命令 `set_tile` + `help` 登记（§2.3）。
4. **冒烟**：`tools/ipc_smoke.py` 追加断言（见 §4.2），既有断言零回归。
5. **人工 E2E**：起服 → `set_tile` 在 walls 层挖开一格墙 → `solid_at` 翻转 + screenshot 目视确认该格视觉变化 → `set_tile` 填回 → 还原确认（写-读-视觉三断言闭环）。
6. **回归**：Debug + Release 构建零告警；ctest 全绿；ipc_smoke 全绿。
7. **模板刷新**：维护者模式同步 → 独立副本构建验证（模板自有文件不动，同 plan-19 裁决）。
8. **文档**：AGENTS.md——① API 边界增量（§2.4）；② **改写** Roadmap P3 条目：`set_tile_at` 已落地；「小区域填充」= game 侧循环（零引擎语义，每格 O(1)），注明收窄理由（对齐 plan-18 对固定步长的移交先例）；③ IPC 命令表追加 `set_tile` 行（含坐标换算式）；④ 调试工作流第 3 步补「地形实验走 set_tile」一句；⑤ 冒烟计数订正（74 → 新基线）。
9. **subagent 审查**未提交代码（禁止自检）。
10. 更新 `CHANGELOG.md`（审查通过后）。
11. 询问用户 commit message（英文预览，确认后提交全部变更并推送）。

### 文件清单

| 文件 | 动作 | 对应 |
|------|------|------|
| `engine/include/trogue/scene.hpp` | 改（声明） | §2.1 |
| `engine/src/scene_asset.cpp` | 改（实现 + 值域 helper） | §2.2 |
| `tools/tests/scene_query_test.cpp` | 改（追加用例） | §4.1 |
| `game/src/main.cpp` | 改（探针命令 + help） | §2.3 |
| `tools/ipc_smoke.py` | 改（追加断言） | 步骤 4 |
| `template/engine/**` | 刷新（vendored 快照；模板自有文件不动） | 步骤 7 |
| `AGENTS.md`、`CHANGELOG.md`、`docs/plan-20.md` | 改/新增 | 步骤 8/10 |

## 4. 验证清单

### 4.1 `scene_query_test` 追加（无窗口、纯公共 API）

- **fixture 来源（零新增资产）**：palette 场景沿用本文件既有临时写法；图集模式引用 `assets/tilesets/tile_set.json`（16 tiles，`scene_schema_test` 同款先例）；多格 tile（`size_in_atlas`）用 `assets/tilesets/test_tileset.json`。
- 合法写入：图集场景某格 -1 → 0 → `tile_at` 读回一致；palette 场景色块索引写入（无窗口不触 GL，写后经 `tile_at` 断言）。
- **nonempty O(1) 正确性（交叉对账，程序钉死）**：一串混合 `set_tile_at`（+1/−1/不变三态各含）→ `tile_grid` 物化整层 → 经 `update_layer_tiles` 写回（触发整层重算）→ 断言写回 ok 且 `nonempty` 与逐次 O(1) 维护值一致。
- solid 联动：写 walls 层某格（solid 层）→ `is_solid_at` 翻转；写回 → 翻转还原。
- 错误路径（资产不被修改，断言 `kInvalidArgument` 即可——bare 场景在公共路径上由 layer_index 越界先触发，勿断言独立错误消息文本）：层索引越界 / tx 负 / ty ≥ height / bare 场景 / **palette 层值 ≥ palette_count** / 图集层值 ≥ tile_count / 值 < -1 → `kInvalidArgument`；错误后 `tile_at` 读回原值。
- 多格 tile（`size_in_atlas`）层：写其 cell 不影响其他 cell（一格一 cell 语义回归）。

### 4.2 E2E / 回归

- [ ] ipc_smoke 新增断言全绿：`set_tile` 成功响应 → `get_tile`/`solid_at` 立即可见 → 挖墙/填墙往返；错误包络（缺参、越界、bare 不适用场景复用既有场景断言值域错误）；help 登记。既有 74 项零回归。
- [ ] 步骤 5 人工 E2E：写-读-视觉三断言闭环。
- [ ] Debug + Release 零告警；ctest 全绿；模板独立副本构建通过。
- [ ] 本次 diff 无内部文档指针（注释自足纪律）。

## 5. 遗留与边界

- **批量写入/事务性多格更新**：无消费方，不预造；game 循环调用即可，跨格原子性待真实需求。
- **写盘**：内存修改不回写场景 JSON 文件（资产文件是上游创作输入，运行时修改是会话态）；「持久化修改」由 game 导出 tro-scene 另行实现。
- **watcher 覆盖语义**：内存修改后被 watcher/F5/reload 以磁盘内容覆盖——与 `genmap` 同一预期行为，文档已注明。
- **`SolidGridView` 路径**：game 自持碰撞掩码（plan-17）不经本 API；本 API 只作用于 `SceneAsset` 自有层。
