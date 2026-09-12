# 游戏项目 Agent 开发指南（trogue）

> 本文件教 Agent **怎么做游戏**，不是引擎 API 说明书。成功标准不是“功能都写完”，而是玩家愿意继续玩：先定体验与深度，再选择实现路径；功能完成只是及格线。
>
> **schema 权威源**：tro-scene / tro-tileset / tro-animations 的契约以随模板分发的引擎实现和上游项目说明为准。本文件只保留开发时需要的摘要。

## 1. 目标与工作方式

### 1.1 先回答游戏问题

开始实现前，先在项目根写设计文档（可叫 `DESIGN.md`，已有设计文档也可复用），至少回答：

- 核心循环是什么，用一句话说清楚；
- 玩家每分钟做哪些重要决定，而不是只执行操作；
- 玩家为什么会失败，失败后为什么愿意重开；
- 变化、风险、资源、构筑或叙事如何产生纵深；
- 第一局的目标、难度曲线和可观察反馈是什么。

没有这些答案，不要因为某个 API 很方便就开始堆功能。先定体验与深度，再打开引擎参考区寻找实现路径；引擎已有能力是工具，不是题材菜单。引擎没有的能力也不要成为玩法天花板：按目标决定补在 game 还是提出引擎改进。

### 1.2 从设计到可玩闭环

1. 写设计与验收清单，确定最小可玩循环。
2. 在 `game/` 实现最小闭环，必要时手写场景 JSON 或生成测试资产。
3. 构建并运行，先用 IPC/日志做数值验证，再读截图检查实际表现。
4. **试玩是必选步骤**：真人或视觉子代理至少玩几轮，询问“有趣吗、难度合适吗、还想再来一局吗”。不满足就回到设计，不要用更多工程代码掩盖体验问题。
5. 重复调整规则、反馈、节奏和内容，直到达到目标。

ctest 能证明逻辑没有坏，截图能证明画面没有明显坏；乐趣、节奏和难度不能被 ctest 覆盖。可用无头 bot 批量测通关率、平均深度和失败分布，但仍不能替代试玩。

### 1.3 文档原则

**文档给契约与意图，不给绕坑说明。** 如果文档需要写“必须先做 X，否则会失败”，先判断这是正式契约还是实现事故泄漏；若是事故，应修代码让这句话消失。文档应如实区分真实限制与项目自己的选择。

## 2. 项目结构与更新

```
project/
├── AGENTS.md          # 本文件：游戏设计、实现、验证指南
├── CMakeLists.txt     # 聚合 engine + tools + game
├── engine/            # trogue 引擎快照，勿手改
├── editor/            # Godot 可选视觉标注/导出快照
├── pixellab/          # PixelLab 转换工具快照
├── tools/             # scene_gen、冒烟脚本与项目工具
├── game/              # 你的游戏：玩法、对象模型、输入、UI、音频
└── assets/            # 你的运行时资产
```

`engine/`、`pixellab/`、`editor/`、`tools/scene_gen.cpp` 是上游快照。需要引擎修复时修改上游项目；派生项目更新时在根目录运行：

```bash
./scripts/sync_from_source.sh
```

更新器会临时浅克隆上游，将最新快照覆盖到当前项目，不需要用户克隆整个仓库；会保留你的 `game/`、`assets/`、项目 CMake、README、测试和本文件。空目录运行同一个脚本则安装完整模板。`--url`、`--ref`、`--source` 可覆盖来源，`--full` 才会重置项目自有文件。

## 3. 设计指南

- 玩法决策归 `game/`：输入、状态机、实体生命周期、规则、AI、动态碰撞规则、相机、UI、音频、粒子、shader 和存档。
- 引擎是通用执行层：它不拥有你的世界，不根据 `type` 猜玩法，不自动管理实体。
- 不重复造轮子：动画播放、Tween、静态地形查询、autotile 等能力直接复用引擎。
- 反向规则同样成立：引擎没有的机制不能限制设计；若该机制跨游戏、确定、可无头测试，就应考虑补引擎，否则由 game 自己实现。
- 网格/回合移动使用固定时长 tween，结束时精确 snap；连续即时运动不受这条网格纪律约束。
- 视觉反馈必须服务决策：命中、受伤、阻挡、选择、失败原因和下一步目标应清楚可见。

## 4. 资产与内容生产

### 4.1 场景与字体

简单场景、测试地图和程序生成内容可以直接写 tro-* JSON，不需要 Godot。Godot 只是可选的视觉标注/预览工具，不是运行时依赖。

CJK 字体优先使用独立 `.ttf`/`.otf`，由 raylib 直接加载并用 `IsFontValid` 验证；只有拿到 `.ttc`、字符集过大或图集尺寸受限时，才使用 `tools/gen_font.py` 烘焙 PNG 与度量 JSON。模板中的 `game/src/ui_font.*` 是可选参考，不是起步目标的硬依赖。

### 4.2 PixelLab

调用 PixelLab MCP 前先查 `https://api.pixellab.ai/mcp/docs`；批量前检查余额，pro 模式先报价再确认。生成后必须在游戏实际显示尺度验收：看角色方向、动画循环、调色板、碰撞占位和同组资产一致性；不合格就重掷或修图，不要把 `get_*` 只当状态查询。

```bash
python3 pixellab/pxlab.py import-character --meta <json> --name <n>
python3 pixellab/pxlab.py import-tileset --meta <json> --image-url <url> --lower <名> --upper <名>
python3 pixellab/pxlab.py import-map --grid <file> --tileset <tro-tileset> --scene <name> --out <path>
python3 pixellab/pxlab.py verify
```

PixelLab 的 rotation 行与动画行建议使用不同名字（例如 `<name>_rot_<dir>`），避免与动画 clip 的名字混淆；这是命名约定，不是把转换事故伪装成玩法规则。

**瓦片集地形标注归用户 + Godot（2026-09-12 拍板）**：地形/Wang 瓦片集的 terrain/peering_bits 标注由用户在 Godot 建 TileSet（.tres）用地形画笔逐格完成，经 scene_exporter 导出 tro-tileset（或直接提供 Godot 格式瓦片集）；Agent 不做自动标注，也不开发顶点级输入管线。`import-tileset`/`import-map` 的 MCP 产物只作**占位资产**（视觉占位、临时测试）：自动转换的 peering_bits/归池只保证格级特征——格级地形输入与多数投票归约原理上表达不了顶点居中特征（1 格洞等），映射与选择语义本身已验证无误，缺口仅在输入端；需要顶点级特征时由用户在 Godot 手工逐格摆瓦片（地形画笔把变体烘进 cell，无粒度损失）。

## 5. 实现

优先把纯逻辑放进不依赖 raylib 的模块，游戏对象模型自选 OOP/ECS。引擎公共契约见附录；本章不复制 API 细节。实现顺序应围绕最小可玩循环，而不是围绕附录中的接口列表。

使用已有能力：`AnimationPlayer` 负责帧推进，`TweenManager` 负责数值/位置/颜色补间，`TerrainTable`/`pick_tile` 负责确定性 autotile；game 决定何时触发、如何组合。引擎的 `SolidGridView` 可接受 game 自持的碰撞掩码，适合程序生成地图。

## 6. 验证与验收

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug && cmake --build build
cd build && ctest --output-on-failure
cd .. && ./build/bin/trogue
python3 tools/ipc_smoke.py
```

纯逻辑用 ctest；IPC 用结构化快照断言；截图拷进项目可读路径后用 `read` 直接检查。验收清单至少包括：核心循环可重复、失败可理解、输入反馈明确、资产在实际尺度一致、碰撞边界无穿透、静止时视觉位置等于逻辑位置。生成的每个动画/资产都要确认确实被游戏使用。

## 7. 调试工作流

1. 先读文件再改文件，不用 bash 读取替代文件工具。
2. `cmake --build build` 后启动游戏，日志写到 `/tmp/game_run.log`。
3. 用 IPC 的 `status`、`list_entities`、`get_entity` 和游戏自有命令定位数值问题。
4. 用 `screenshot` 获取完整帧；截图写在项目内临时路径，读完删除。
5. 用 Python 像素比对和 `transform` 快照检查亚像素残差；不要只凭肉眼判断碰撞或移动。
6. 结束时发送 `quit`；残留进程用 `pkill -x trogue` 清理。

冒烟脚本属于项目自有文件，随着游戏 IPC 命令增删而维护，不是引擎固定命令集。

## 8. 附录：参考区

### A. 引擎边界摘要

引擎提供 `SceneAsset` 只读资产（另有受限的 `update_layer_tiles`）、显式 tile/sprite 绘制、动画播放器、Tween、autotile、tile 查询、静态 solid 几何原语、IPC 传输与 watcher。`AnimationAsset` 可独立加载 `tro-animations` v1；其 `view()` 只在资产存活期有效。

碰撞 API 既可从 `SceneAsset` 查询，也可从 game 自持的 `SolidGridView` 数组查询；视图不拥有 mask，逐 solid 层需保留自身 origin。引擎负责 AABB 谓词、线段 vs 静态 tile、sweep 滑移等确定性几何关系；动态实体碰撞规则、刚体物理、solver、单向平台、斜坡、碰撞矩阵和寻路属于 game。引擎边界不是开发优先级排序。

### B. tro-* 资产摘要

- `tro-scene` v2：根含 `format`/`version`，像素坐标、层为行主序 tile、`-1` 为空；图集与 palette 互斥；只有实体而没有地形的场景是合法 bare 场景。图集模式的 `tilesets` 为 1..8 个 `{name,path}`，每层引用一个 tileset；palette 最多 32 色；层最多 4 个，`origin` 是可负的世界像素偏移。
- 层条目为 `{name,width,height,solid?,origin?,tileset?,tiles}`，`tiles` 长度必须是 `width*height`，图集值域是所引 tileset 的 tile 数，palette 值域是 palette 数。solid 层只参与静态 tile 查询，层矩形外不阻挡。
- 实体 descriptor 为 `{id,type?,x,y,w,h,z?,color?,solid?,sprite?,animations?,props?}`；id 必须唯一。sprite 要么是 `{tileset,tile}`，要么是 `{texture,region?,offset?}`；region 缺省整图，锚点是 `x/y + offset`；`props` 是 game 自行解释的 object。
- `tro-tileset` v2：包含 `texture`、tile 尺寸、`columns/rows`、`terrain_sets` 和 `tiles`；`tiles[]` 顺序即稳定 tile id，`peering_bits` 供 `tg::pick_tile` 确定性选择。
- `tro-animations` v1：`format`、`version`、`textures`、`animations`；clip 有 `name/fps/loop/frames`，clip 名在动画集内唯一，frame 通过 texture 索引引用，可含 region/offset。
- 实体 descriptor 是 game 的 spawn 初值，不是引擎运行时实体；`solid` 只是导入提示。

### C. IPC 摘要

DEBUG 构建默认监听 `127.0.0.1:48764`，协议版本恒为 1；JSON-lines 每行一请求/响应，单行上限 64KB，最多 8 个连接。成功包络为 `{"ok":true,"data":...}`，失败为 `{"ok":false,"error":"..."}`，hello 是带 `event:"hello"` 的成功行。

engine 负责 `ping`、`subscribe`、`unsubscribe`、`connections` 和事件传输；订阅 filter 是事件 data 顶层字段等值匹配，多键 AND，断开即清零。响应不含顶层 `event`，事件推送含顶层 `event`；事件超长直接断开匹配订阅者。其余命令由 game 定义并同步 `tools/ipc_smoke.py`。监听脚本必须按行读取并有退出条件。

起步 game 命令包括 `status`、`list_entities`、`get_entity`、`move`、`screenshot`、`log`、`quit`；可按设计增删。`screenshot` 返回时文件已经写入，具体实体和玩法命令不属于引擎契约。

### D. 构建与依赖

需要 C++20 协程、raylib 6.0、nlohmann/json 3.11+、tl::expected 1.x。缺依赖时通知项目维护者，不自行安装系统包。引擎公共头位于 `engine/include/trogue/`，公共 API 为 `namespace tg` 的纯 C++/RAII 类型。

### E. 开发流程

每次较大改动都遵循：设计 → 最小实现 → 构建/逻辑测试 → 运行/截图 → 必选试玩 → 迭代。若发现引擎能力缺口，先判断它是否跨游戏、机制性、确定且可无头测试；符合则回上游引擎补能力，不符合则留在 game。
