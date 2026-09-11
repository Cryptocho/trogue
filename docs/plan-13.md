# plan-13：PixelLab 资产管线（pixellab → tro-* 转换层）

- 状态：**已完成（2026-09-11；两轮审查 PASS 后实现，W0-W6 落地）**
- 日期：2026-09-11

## 1. 目的

打通 Agent 自主游戏开发的最后一环——美术资产来源。接入 PixelLab MCP 作为上游像素美术生成器，并建立配套的**确定性转换层**，把 PixelLab 产物（角色/动画/Wang 瓦片集/地图网格/物件）编译为本仓库既有的 tro-* 运行时资产。转换层与 `editor/`（Godot 导出管线）**平级**：都是「上游创作输入 → assets/ 中的 tro-*」，引擎与 game 运行时不感知它的存在。

配套动作（已完成，不入仓库）：全局 skill `~/.agents/skills/pixellab-mcp/SKILL.md` 已创建，教会 Agent 正确使用 PixelLab MCP（成本纪律、异步模型、像素网格纪律、格式转换原则）。

## 2. 定位与原则（复用 AGENTS.md 既有约束）

- **上游工具层，零运行时依赖**：engine/game 不出现任何 PixelLab 概念；产物只落 `assets/`（tro-* JSON + PNG）。
- **不改 schema**：适配现有 tro-scene v2.1 / tro-tileset v2 / tro-animations v1，不为 PixelLab 增字段；遇 schema 缺口走 schema 演化评审，不顺手改。
- **明确损失必须 warning**：不支持的一律拒绝导入并说明，不静默伪造兼容。
- **headless、确定性、可校验、幂等**：同输入二次运行产物 byte-identical；机器可读诊断。
- **不重复造轮子**：autotile 选择复用引擎 `tg::load_terrain_table`/`tg::pick_tile`（C++ 小 CLI），Python 侧不复刻评分算法。
- **双路径不变**：规则明确的资产 Agent 仍直接手写 tro-*；PixelLab 路径只在需要美术生成力时使用。
- **API 形态**：MCP 调用由 Agent 交互完成（已配好 MCP）；转换器只消费**免鉴权下载 URL + Agent 保存的 MCP 响应元数据 JSON**，不内置任何 API token、不直连 api.pixellab.ai。

## 3. 非目标

- 不做 PixelLab 产物的运行时加载/播放格式（运行时只认 tro-*）。
- 不把 UI 面板/字体纳入 tro-*（UI 面板当普通 texture 用；字体由 game 直调 raylib `LoadFont`）。
- 不做 25-tile（transition_size=1.0）4×8 Wang 集、hex/isometric 瓦型、sidescroller 瓦片集（本期只做 square top-down 16-tile 集）。
- **flow C 本期单场景只支持单个 tro-tileset（双地形 lower/upper）**；多地形链式 tileset 混排场景留后续。
- 不做"一键全流程"批量编排脚本；子命令逐个可用即达标。

## 4. 现状勘察（已验证事实）

- **引擎侧无需改动**：`terrain.hpp` 已实现 `TerrainMode::corners`（4 角位 `top_left_corner` 等 + `terrain_bit_valid`），plan-12 的 `TerrainTable`/`pick_tile` 按 Godot 语义覆盖三 mode；terrain_test 基线 7 组用例在（corners 正向覆盖待 W1 补全）。
- **独立 tro-animations 文件引擎侧无加载器**：引擎只消费场景实体**内嵌** `animations`（动画集名 = entity id）；`assets/animations/*.json` 无人加载（现存 soldier 资产是场景内嵌同构副本）。因此 flow A 的运行时验证走「内嵌测试场景」路径（§7 E2E）。
- **PixelLab 账户当前为空**（0 characters / 0 tilesets；余额 1962 generations，Tier 1 订阅）——fixture 需在 W0 新生成。
- `get_topdown_tileset`（standard）产出 **16-tile（4×4 sheet）corner-based Wang 集**，可 `base_tile_id` 链式保证多套地形风格衔接；返回 download links，**不返回逐 tile placement rules**（那是 tiles_pro 的能力）→ 排列顺序必须实测锁定（W2）。
- `get_character`/`get_object` 返回逐方向逐帧帧序列 URL；`animate_character` v3 `keep_first_frame=true` 时实际帧数 = frame_count + 1；PixelLab 不提供 fps/loop 元数据。
- `get_map` 输出 ASCII terrain 网格（机器可读真值）；`view_map` 是渲染图（人审 + W2 比对基准）。
- 下载 URL 免鉴权；Pillow 环境可用（此前像素比对已用过）。
- `tools/CMakeLists.txt` 有现成 C++ 工具目标模式（链 `trogue_engine` + nlohmann，WORKING_DIRECTORY=项目根）。

## 5. 设计

### 5.1 目录与形态

```
pixellab/                  # 顶层目录，与 editor/ 平级（上游资产管线；依赖仅 Pillow + stdlib）
├── pxlab.py               # CLI 入口：import-character / import-tileset / import-map / verify
├── api.py                 # 下载封装（urllib，URL→字节；无鉴权逻辑）
├── character.py           # 角色帧序列 → tro-animations v1 + spritesheet
├── tileset.py             # Wang 4×4 sheet → tro-tileset v2
├── mapping.py             # 16 组合 ↔ peering_bits(corners) 显式映射表 + 顶点采样/归池规则（纯函数，可单测）
├── scene.py               # terrain 网格 → 顶点 pattern → 调 scene_gen CLI → tro-scene
└── manifest.py            # 来源 manifest 读写（upsert）
tools/scene_gen.cpp        # C++ CLI（tools/CMakeLists.txt 新目标，链 trogue_engine）：
                           #   tro-tileset JSON + 顶点 pattern JSON → tro-scene tiles（复用 pick_tile），
                           #   落盘前用 tg::SceneAsset::load_json 回读自检
pixellab/tests/            # Python 单测（无网络）
```

目录为仓库根的 `pixellab/`，与 `editor/` 字面平级（用户 2026-09-11 拍板）；`tools/scene_gen.cpp` 仍落 `tools/`（与既有 C++ 工具测试同址，复用 tools/CMakeLists 目标模式）。

### 5.2 三条数据流

**A. 角色/动画 → tro-animations**
MCP `create_character`/`animate_character` → Agent 把 `get_character` 响应存为元数据 JSON → `pxlab import-character --meta <json> --name <n> [--fps 8] [--loop walk,idle]` → 逐帧下载（本地像素网格检测，见 §5.4）→ 拼 spritesheet → `assets/textures/pixellab/<n>.png` + `assets/animations/<n>.json`。
- **spritesheet 布局规则（确定性）**：每 clip 一行、行高 = 该行最大帧高、帧从左到右紧密排列；单测断言所有帧 region 互不重叠且行内不越界。
- **尺寸上界**：任一 spritesheet 边 > 4096px → 拒绝导入（GPU 纹理安全余量）。
- clip 命名：多方向为 `<anim>_<direction>`（如 `walk_south`），单方向为 `<anim>`。fps/loop 为显式 CLI 参数（PixelLab 不提供，默认 fps=8、不 loop），manifest 记录。
- 运行时验证路径：见 §7 E2E（内嵌测试场景实体）。

**B. Wang 瓦片集 → tro-tileset**
MCP `create_topdown_tileset` → `pxlab import-tileset --meta <json> --name <n>` → 下载 4×4 sheet（**sheet 本身即 atlas，不重切片**——tro-tileset 用 texture + col/row 直接引用，columns=4 rows=4）→ 按 mapping 表给每 tile 写 `terrain_sets[0]={mode:"corners", terrains:[lower,upper]}` + 4 角 `peering_bits`，并按归池规则给每 tile 标 `terrain` → `assets/tilesets/pixellab/<n>.json` + `assets/textures/pixellab/<n>.png`。

**C. 地图 → tro-scene**
MCP `get_map` ASCII 网格 → `pxlab import-map --grid <ascii> --terrain B01=0,B11=1 ... --tileset <已导入 tro-tileset 名> --scene <n>` → 按**顶点采样规则**把 terrain 场转为每格 4 顶点 pattern（中间 JSON，候选池 = `pool_of_vertices(顶点)`，不用格自身 terrain）→ 调 `tools/scene_gen`（引擎 `load_terrain_table` + `pick_tile` 按该池逐格烤 tile id）→ `assets/scenes/<n>.json`（tiles 烤死，对齐 plan-12 决策）。

**manifest**：`assets/pixellab_manifest.json` 按 `(源类型, 源 id)` **upsert**（重跑不产生重复条目）。字段：源类型/源 id/下载 URL/产物相对路径/sha256/导入时间。URL 语义 = 来源记录，**可能过期不可重放**；重导入需 Agent 重新提供 MCP 元数据；**已落盘产物 + sha256 是权威**。导入时间只进 manifest 不进产物，保产物确定性；`verify` 子命令按 sha256 复核。

### 5.3 Wang 映射三要素（本期最大不确定点，全部实测锁定）

要复现 PixelLab 的摆格结果，必须锁定三项，缺一不可：

1. **16 tiles ↔ 4 角组合映射**：4×4 共 16 tiles ↔ 4 角各 ∈{lower,upper} 的 16 组合，每 tile 的 4 角 `peering_bits`。
2. **顶点采样规则**：PixelLab 按格的 4 个**顶点**取 terrain 选 tile；flow C 输入是 terrain 网格（每格一个 terrain），scene.py 必须实现「terrain 场 → 每格 4 顶点值」的判定——顶点处四个相邻格 terrain 分歧时谁赢（裁决规则），必须与 `view_map` 渲染实测对齐。
3. **归池规则**：`pick_tile` 契约是「按 terrain 入参过滤候选池 → 对合法位（corners = 4 角）评分降级」。双地形下 16 tiles 分进两个池（每 tile 标 `terrain` 0 或 1），归池 = 多数角（平分归 lower）；**查询时的 terrain 入参必须是顶点 pattern 的归池**（`pool_of_vertices`，与 tile 归池同规则），不能用格自身 terrain——少数角格会落错池被强制降级（实现期评审 B1 修正）。

锁定方法：画一张已知 terrain 网格的小地图，逐格与 PixelLab `view_map` 渲染做像素比对，反推每格用 tile 与顶点值 → 固化三项为 `mapping.py` 显式表（纯数据 + 纯函数）。
单测：**全 pattern 表断言**——2 terrain × 16 顶点组合 → 期望 tile id（含显式标注哪些 pattern 允许降级、降级到哪个 tile），16×2 条逐一断言。
若实测发现 4×4 非 corner-based（与文档不符），以实测为准调整设计并在本文件备案。

### 5.4 明确损失（拒绝或 warning）

- 25-tile 4×8（transition_size=1.0，含 pattern_4x4 孪生 wall tile）→ **拒绝导入**，error 说明本期范围。
- tile_size 非 16/32、sheet 尺寸与 4×4 不符 → 拒绝。
- spritesheet 任一边 > 4096px → 拒绝。
- **像素网格检测在转换器本地做**（Pillow 整数倍网格检测 + 缩小，确定性、零 MCP 往返；不用 MCP `unzoom_image`）：检测到 ≥2× 整数倍放大 → 还原真实网格；未检测到 → 按原生图接受（非整数倍放大无法用块一致性证明，不做拒绝；尺寸 <8px 拒绝）。
- fps/loop/方向翻转语义 PixelLab 不提供 → CLI 显式参数，不猜。

## 6. 步骤

- **W0 勘察（一次性，≤10 generations）**：真实生成最小 fixture——16px `grass→dirt` topdown tileset ×1、48px 士兵风角色 ×1（walk 动画**只取 `directions=["south"]`** 压成本，多方向留 W3 按需）；下载并记录真实格式（sheet 尺寸/帧命名/排列/方向语义），结论写回 §4。
- **W1 corners mode 验证**：引擎已支持；用 W0 fixture tro-tileset 走 `load_terrain_table`+`pick_tile` 实测；`terrain_test` 补 corners 全组合用例（防解析层意外）。
- **W2 锁定映射三要素**（§5.3）：像素比对法锁定 ①组合映射 ②顶点采样 ③归池 → `mapping.py` 显式表 + 全 pattern 表单测。
- **W3 数据流 A/B**：`pixellab/api.py`/`character.py`/`tileset.py`/`manifest.py` + CLI 子命令（import-character / import-tileset）。
- **W4 数据流 C**：`tools/scene_gen.cpp`（tools/CMakeLists 新目标，落盘前 `load_json` 回读自检）+ `pixellab/scene.py` + `import-map`。
- **W5 端到端验证**（见 §7）。
- **W6 文档**：AGENTS.md 目录结构补 `pixellab/`、新增「PixelLab 资产管线」章节（权威性/双路径/损失清单/「独立 tro-animations 无运行时加载器，消费走场景内嵌」的说明）、AI Agent 调试工作流补一行；本文件状态改「已完成」。
- **W7 收尾流程（门禁步骤 3~7）**：subagent 审查未提交代码 → CHANGELOG → 英文 commit message 预览等用户确认 → 提交所有变更 → push（`trogue-raylib`）。

## 7. 验证

- **Python 单测（无网络，入 ctest）**：mapping **全 pattern 表断言**（32 条）；spritesheet 拼装确定性 + region 互不重叠断言；tro-animations/tro-tileset 产出过 schema 规则断言。ctest 包一层 `python3 -m unittest`（add_test）。
- **C++ 侧（入 ctest）**：`tools/scene_gen` fixture 指派 → tiles，**断言两次运行输出逐字节一致**；`terrain_test` 补 corners 全组合用例；基线 ctest 10/10 不回退（新目标加入后递增）。
- **E2E（网络，手动一次）**：
  - flow A：生成的 animations 结构**内嵌进测试场景实体**（与 `assets/scenes/soldier_animated_sprite_2d.json` 同构，零额外成本）→ `tg::SceneAsset::load` → 起服后 IPC `anim` 快照帧序推进 + screenshot 视觉验收（Agent 自读图）。
  - flow B/C：同网格小地图走全管线 → 引擎加载渲染 → screenshot 与 PixelLab `view_map` 渲染逐格比对。
  - 回归：`python3 tools/ipc_smoke.py` 61/61 不回退（本期不碰 runtime，零成本基线纪律）。
- **确定性**：每条数据流同输入跑两次，产物 diff 为空（manifest 除外）。

## 8. 风险与降级

| 风险 | 应对 |
|---|---|
| 顶点采样/归池规则复杂或与 pick_tile 评分模型冲突 | W2 实测锁定；若复现不了 PixelLab 边界效果，降级为「可渲染但边界与 PixelLab 有差异」并 warning 备案，不阻塞管线 |
| Wang 4×4 排列与文档/推测不符 | W2 像素比对新测锁定，映射表显式可改 |
| corners mode 解析层有隐藏缺口 | W1 提前实测；小缺口修解析（机制性，合规）；大缺口则本期只导 tiles 不带 terrain（warning），引擎扩展移下期 |
| 上游格式漂移（PixelLab 更新） | manifest 记 sha256，`verify` 可重校验；转换器输入以实测为准 |
| 下载 URL 过期不可重放 | 产物 + sha256 为权威；重导入需 Agent 重新提供 MCP 元数据（manifest 语义已写明） |
| 帧序列 URL 结构复杂（每方向每动画一条） | W0 勘察确认 get_character 响应结构后才写 character.py |
