# 里程碑 12：autotile 机制（TerrainTable + pick_tile）与内存加载（load_json）

- 计划日期：2026-09-11
- 前置：导出器 peering_bits 已真实导出（2026-09-10 修复 TerrainMode 枚举错配，`assets/tilesets/test_tileset_1.json` 16 tile 四边标注与 Godot 侧逐一核对一致）；props 透传修复已落地
- 用户拍板（2026-09-11）：
  1. **引擎定位判据**：引擎 = Agent-first 的薄执行原语层（机制性、确定性、可无头测试的功能进引擎；音频总线/混音、shader 管理、粒子等美学/玩法决策载体留在 game 直调 raylib）——本条随本里程碑写入 AGENTS.md 固化
  2. **分工复刻 AnimationPlayer 模式**：地形指派/程序生成（语义、触发）归 game；「按 bits 规则选 tile id」（机制、采样）归 engine
  3. **API 形状**：const 自由函数选择器 + 独立只读值类型 `TerrainTable`（非持可变状态的类）
  4. **失败契约**：精确命中 → 评分降级 → 查无此 terrain 报错；同分取最小 tile id；不做种子随机变体
  5. **schema 克制**：tro-scene 不加地形驱动层；场景保持烤死的 tile id，动态场景走选择器
  6. **内存加载**：新增 `SceneAsset::load_json`（拒绝「可变层 API」与「逐格 render_sprite」两条歧路）

## 1. 目的与问题

- autotile 是 Roadmap 最后一个引擎机制空白。tileset v2 的 `peering_bits` 已透传但引擎零消费（`grep terrain engine/src/scene_asset.cpp` = 0 命中，文档级未知键宽容使其「路过」）：程序生成地图（挖沟、刷地形、roguelike 关卡生成）没有任何机制把「这格是草地」变成「这格画 tile 7」。
- 程序生成的地图**没有 SceneAsset**：`render_scene` 只画资产里的层，公共 API 无内存加载入口（解析在 `tg::detail`，仅测试可达）。选择器做出来也画不出来。
- 本期交付两块引擎机制 + 一个 game 层演示载体，收敛生成流程为：**game 生成地形指派 → pick_tile 填 tile → 内存加载 → render_scene**。

## 2. 边界（复刻 animation.hpp 句式，随 AGENTS.md 固化）

| | 谁 | 内容 |
|---|---|---|
| 语义/触发 | **game** | 「这格是草地」「玩家挖掉了这格」——地形指派、噪声/生物群系等程序生成（纯玩法） |
| 机制/采样 | **engine** | 「按这套 bits 规则，这格该画哪个 tile」——纯函数选择器 + bits 解析校验 |

- engine 不保存地形状态、不做扩散式重排（Godot 画笔的多格批量语义不进引擎）、不消费任何玩法概念。
- 选择器是**无状态纯函数**：同一 (table, terrain_set, terrain, pattern) 输入永远同一输出，热重载/重放无视觉抽签。

## 3. 查证事实（全部读自本地源码，非记忆）

| 事实 | 出处 |
|------|------|
| Godot 运行时画笔汇合点：`set_cells_terrain_connect` → `terrain_fill_constraints` → 逐格 `_get_best_terrain_pattern_for_constraints` | `reference/godot-4.7.2-stable/scene/2d/tile_map_layer.cpp:1851、2384、2431` |
| **评分语义**：候选 = 该 terrain set 内实际存在的 pattern 集合（pattern = center terrain + 每个合法位 terrain 值，未标注 = −1）；每候选按「约束位 mismatch 计数」打分（priority 缺省 1），取最小分；同分取 RBMap 序（pattern 值序）确定性优先 | 同上 L1851-1908 |
| 拿到 pattern 后取 tile：`get_tiles_for_terrains_pattern` 精确集合 → `get_random_tile_from_terrains_pattern` **随机**取一 | `tile_set.h:543-544` |
| 合法位由 mode 决定（sides=4 边 / corners=4 角 / corners_and_sides=8），`is_valid_terrain_peering_bit_for_mode` | `tile_set.h:483` |
| 未标注位 = −1 参与比较；对「只标四边」的 tileset，四角 mismatch 对全体候选均匀 +1，不改变排序（理论推导，见 §8 遗留） | `TerrainsPattern` 构造器 `tile_set.cpp:312-316`（`bits[i] = −1`）+ `TileData::get_terrains_pattern`（`tile_set.cpp:6569`） |
| 引擎 `TilesetMeta` 无任何 terrain 字段；`load_tileset_meta` 只挑字段解析、文档级未知键宽容 | `engine/src/scene_impl.hpp:55-76`、`engine/src/scene_asset.cpp`（grep terrain = 0） |
| `SceneAsset::load(path)` 是唯一公共加载入口；文件读取（`read_text_file`）与解析（`parse_json_text`/SceneLoader）可分离 | `engine/include/trogue/scene.hpp:88`、`engine/src/scene_asset.cpp` |
| ErrorCode 语义：kInvalidArgument=调用参数非法、kNotFound=查询未命中、kSchemaViolation=资产格式拒绝 | `engine/include/trogue/types.hpp:20-29` |
| 实测资产：`test_tileset_1.json` = corners_and_sides 模式 1 terrain（ground）16 tile，仅四边标注（4-bit 全集）；8 方向名与导出 `peering_bits` 键名一致（= Godot `CELL_NEIGHBOR_ENUM_TO_TEXT`，`tile_set.cpp:323`，square 合法 8 名） | 2026-09-10 导出核对记录 + 本地源码 |

## 4. 方案

### 4.1 terrain 数据解析与校验（engine）

`load_tileset_meta` 抽出**共享解析核心**（场景加载与 TerrainTable 加载共用，杜绝两套解析漂移），在现状基础上新增 terrain 字段的**解析 + 校验**（今日为宽容路过）：

| 字段 | 规则 | 违反 → |
|------|------|--------|
| `terrain_sets[]` | 数组 ≤4；元素 object；`mode` ∈ {"sides","corners","corners_and_sides"}；`terrains[]` 元素 `{name:string, color:#rrggbb}`，1..16 项 | kSchemaViolation |
| per-tile `terrain_set` / `terrain` | int ∈ {−1} ∪ [0, sets 数 / 该 set terrains 数)；−1 = 不属于任何 terrain（装饰 tile 现状）；两者必须同时为 −1 或同时 ≥0 | kSchemaViolation |
| per-tile `peering_bits` | object；键名 ∈ 8 个固定名（top_side/right_side/bottom_side/left_side/top_left_corner/top_right_corner/bottom_right_corner/bottom_left_corner）；**bit 种类必须匹配所属 set 的 mode**（sides 集合出现 `*_corner` 键 → 拒绝）；值 int ∈ [0, 该 set terrains 数) | kSchemaViolation |
| 键缺省语义 | `terrain_set`/`terrain` 缺键 = −1；`peering_bits` 缺键 = 空（全 −1）——与导出器行为对齐，手写 tileset 整键省略合法 | — |

- 存储进 `TilesetMeta::TerrainInfo`（mode + 每 tile 的 8 方向 bits 数组，缺省 −1）；渲染路径零改动（纯增量字段）。
- tileset 文档级「未知键宽容」策略**不变**（与 entity/layer 的严格白名单是两套既有语义，本期不统一）。

### 4.2 `TerrainTable` 公共值类型 + 加载入口（新公共头 `terrain.hpp`）

- **纯逻辑匹配表，不含纹理/区域字段**（选择器只产 id，渲染走场景 asset——职责锁死）：
  - `enum class TerrainMode { sides, corners, corners_and_sides }`
  - `struct TerrainTable`：mode、terrains 元数据、每 tile `{terrain_set, terrain, std::array<int,8> bits}`（方向序固定，头注释附 ↔ Godot 键名映射表：top_side, top_right_corner, right_side, bottom_right_corner, bottom_side, bottom_left_corner, left_side, top_left_corner；值 = terrain id 或 −1）
  - 可复制纯值类型，无句柄/无 RAII 副作用（符合「值类型 = 纯数据」风格总纲）
- **v1 限单 terrain_set**：`load_terrain_table` 要求 `terrain_sets` 恰好 1 组（缺失/0 组/≥2 组 → kSchemaViolation，错误消息注明 v1 限单 set）；场景侧解析**不**受限（多 set 合法，烤死渲染用不到 bits）。`pick_tile` 的 `terrain_set` 入参仅接受 0（前向兼容保留参数，越界 kInvalidArgument）
- 新限额常量落 `config.hpp`（`kTerrainSetsMax = 4`、单 set terrains 1..16），与既有限额同一风格
- 加载：`expected<TerrainTable, Error> load_terrain_table(std::string_view path)`（assets-relative，与场景同约定）。**不做 JSON 文本入口**——tileset 总是磁盘文件，无程序拼 tileset 的需求（scene 才有，见 4.4）。

### 4.3 `pick_tile` 选择器（engine，无状态纯函数）

```cpp
// pattern：8 方向期望 terrain（-1 = 该方向无同 terrain 邻居）；顺序见 TerrainTable 文档
expected<int, Error> pick_tile(const TerrainTable& table, int terrain_set,
                               int terrain, const std::array<int, 8>& pattern);
```

- **前置校验**（kInvalidArgument）：terrain_set 越界；terrain ∉ [0, 该 set terrains 数)；pattern 中**该 mode 非法方向**的位 ≠ −1（误用防护，静默忽略违反「明确损失」）；pattern 中合法方向位 ∉ {−1} ∪ [0, terrains 数)。
- **候选**：tiles 中 `terrain_set`/`terrain` 均等于入参者；空 → **kNotFound**（查无此 terrain 的 tile——值合法但集合里没有）。
- **评分**（Godot `L1851` 的单格无状态简化：约束全给出、priority 恒 1、无扩散/current 保持项）：
  `score = Σ_{合法方向位} [tile_bit != pattern_bit]`（tile 未标注位 = −1 参与比较）
- **取最小分；同分取最小 tile id**（Godot 在 tile 层随机取，我们确定性化——拍板 4）。正确性论证：只标四边的 tileset 中四角 mismatch 对全体候选均匀贡献，排序只由已标注位驱动，与 Godot 实际选格结果一致。

### 4.4 `SceneAsset::load_json`（engine，内存加载一等公民）

```cpp
static expected<SceneAsset, AssetError> load_json(std::string_view text,
                                                  std::string_view name = "<memory>");
```

- `load(path)` 重构为「读文件 + load_json」，**唯一解析路径**，校验/限额/键规则/路径 grammar 全复用，零新语义。
- `name` 注入 SceneLoader 错误上下文：现状 `SceneLoader::load` 的 `source_path` 被弃用（`scene_asset.cpp:883` `(void)` 保留签名），本期接通——kParseError 与 kSchemaViolation 消息均携带 name；tileset path / texture 路径仍按 assets/ CWD 约定从磁盘解析（不变）；不参与 watcher（内存场景无文件可监听）。
- 拒绝的替代方案（留档）：可变层 API（破坏不可变资产与单一校验路径）；逐格 `render_sprite`（绕开层批绘，60×60 = 3600 调用/帧）。

### 4.5 game 演示载体：demo IPC `genmap`（程序生成逻辑 = game 层样板）

- 命令：`{"cmd":"genmap","seed":N,"w":W,"h":H}`（w/h ∈ [1, kLayerDimMax]，非法参数 `ok:false`）。
- game 层流程（全部决策逻辑留在 game）：
  1. 种子化 value-noise（game 自带 ~30 行确定性实现）逐格指派 terrain（ground / 无）
  2. `load_terrain_table("tilesets/test_tileset_1.json")` → 逐格 `pick_tile` 填 tiles
  3. 拼 scene JSON（图集模式：引用 test_tileset_1 + 单层 + 零实体）→ `SceneAsset::load_json` → 按既有 candidate 语义替换 demo 当前场景
- 语义与 `reload`/`turn` 并存：genmap 后 status/layers/solid_at/screenshot 照常工作（Agent 全链路可观测）。交互语义写进文档：① genmap 后 watcher 文件变更/F5/reload 会以 scene_path 重新加载、覆盖生成场景——**预期行为**；② genmap 的 swap 复用既有 candidate 语义（keep_player + reloads 自增）。
- Release（IPC 桩）下 genmap 不可用——与全部调试命令同一待遇，不新增桩面。

### 4.6 文档（AGENTS.md）

- 「架构分层/设计约定」补**引擎定位判据**：机制性、确定性、可无头测试的执行原语进引擎；音频总线/混音、shader 管理、粒子等美学/玩法决策载体归 game 直调 raylib
- 「引擎公共 API 边界」补 autotile 句（复刻 animation 句式）：TerrainTable + pick_tile 只做 bits→id 采样；地形指派、程序生成、触发归 game；engine 不存地形状态
- IPC 命令表 + `genmap`；tro-tileset v2 节注明 peering_bits 引擎已消费（autotile 选择器）；Roadmap 勾选；伞头 trogue.hpp 加 terrain.hpp

## 5. 文件清单

| 文件 | 动作 |
|------|------|
| `engine/include/trogue/terrain.hpp` | 新增（TerrainMode/TerrainTable/load_terrain_table/pick_tile） |
| `engine/include/trogue/trogue.hpp` | 修改（+terrain.hpp） |
| `engine/include/trogue/scene.hpp` | 修改（+load_json 声明） |
| `engine/include/trogue/config.hpp` | 修改（+kTerrainSetsMax 等限额常量） |
| `engine/src/scene_asset.cpp` | 修改（共享 tileset 解析核心抽出 + terrain 解析校验 + load_json 内存路径） |
| `engine/src/scene_impl.hpp` | 修改（TilesetMeta 增 TerrainInfo） |
| `engine/src/terrain.cpp` | 新增（TerrainTable 加载 + pick_tile） |
| `engine/CMakeLists.txt` | 修改（+terrain.cpp） |
| `tools/tests/scene_schema_test.cpp` | 修改（terrain 解析校验反例集 + load_json 等价性） |
| `tools/tests/terrain_test.cpp` | 新增（pick_tile 全用例） |
| `tools/tests/CMakeLists.txt` | 修改（+terrain_test） |
| `game/src/main.cpp` | 修改（genmap 命令 + 指派/编排 + load_json swap） |
| `AGENTS.md` | 修改（定位判据、API 边界、IPC 表、tro-tileset 节、Roadmap） |
| `docs/plan-12.md` | 新增（本文件） |
| `CHANGELOG.md` | 修改（subagent 检查之后补条目） |

## 6. 步骤（含开发流程 3~7）

1. engine：共享 tileset 解析核心抽出 + terrain 解析校验 + 反例单测
2. engine：terrain.hpp/TerrainTable/load_terrain_table + 单测（合法加载、路径非法、schema 拒绝）
3. engine：pick_tile + 全用例单测（见 §7）
4. engine：load_json + 等价性单测
5. game：genmap（噪声指派 + 选择器 + load_json swap）
6. E2E（Debug 构建，Agent 驱动）：起服 → `genmap seed=7` → screenshot A → `genmap seed=7` → screenshot B → **像素逐位一致**；`genmap seed=8` 不同；`layers`/`solid_at` IPC 断言与手算表一致；`reload` 回 forest.json 不回归
7. 全量回归：ctest 全绿、smoke 61/61、Debug/Release 零告警
8. subagent 审查未提交代码（本计划书先走独立审查，实现完成后再整体审查）
9. 更新 CHANGELOG（检查之后）；核对 AGENTS.md
10. 英文 commit message 预览 → 用户确认 → 提交**所有**变更 → `git push`

## 7. 验证清单

- [ ] 解析校验反例：mode 非法串 / bit 键名非法 / bit 种类与 mode 不符 / terrain 序号越界（含负非 −1、非 int）/ terrain_set 越界 / peering_bits 非 object / terrain 与 terrain_set 只写一个 / terrains 元素缺 name 或 color / color 非 #rrggbb / peering_bits 值负、非 int、超 terrains 数；TerrainTable 路径：terrain_sets 缺失、空、≥2 组 → kSchemaViolation
- [ ] pick_tile：16-blob 全集 16 组合逐一精确命中（对照手写期望表）；残缺集（删 4 tile）降级命中手算 id；同 bits 双 tile tie → 小 id；terrain 无 tile → kNotFound；terrain_set/terrain/pattern 越界、非法方向位非 −1 → kInvalidArgument；**均匀 +1 用例**——pattern 四角位给 0（候选全不标四角）与四角全 −1 结果一致（side 位驱动排序论证的核心）
- [ ] load_json：与 load(path) 同内容等价（atlas 与 palette 两模式）；kParseError 与 kSchemaViolation 消息均携带注入 name
- [ ] E2E：同 seed 两次 genmap 截图像素逐位一致；异 seed 不同；IPC 观测（status/layers/solid_at）与手算一致
- [ ] 回归：ctest 全绿；smoke 61/61；Debug/Release 零告警
- [ ] 读截图视觉验收：生成地图地形连续、无错格

## 8. 遗留与边界

- **未标注位语义**的 Godot 等价性目前是理论推导 + 手工用例（§3 表末行）；「与 Godot 同图逐格对比」待真实多 terrain 套图出现时做
- 种子随机同 pattern 变体（Godot `get_random_tile_from_terrains_pattern`）：不做，最小 id 已保确定性；真实美术需求出现再加
- 多 terrain 评分权重（Godot constraint priority 扩散语义）：单格无状态版不引入；多 terrain 真实套图验证时再评估
- 4MB 资产级 payload 限额无测试（plan-12 审查发现的既有缺口）：独立小修，不入本期
- tro-scene 地形驱动层 / 场景存 terrain 指派：schema v3 级演化，明确不做（拍板 5）
- 实现-计划偏差备案（2026-09-11 评审后有意收窄）：① TerrainTable 只存 mode + terrain_count，不含 terrains name/color（校验后即弃——匹配只关心序号）；② terrain color 校验用 parse_hex_color（同时接受 #rrggbbaa，宽容方向有意）；③ genmap w/h 上限 64 为演示级约束（具名常量 kGenMapDimMax，远小于引擎 kLayerDimMax=4096）
