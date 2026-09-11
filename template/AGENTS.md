# 游戏项目 Agent 开发指南（trogue）

> 本文件是本项目的 Agent 权威指南。**先写文档理清设计，再动代码。** 目标：让
> Agent 自主完成一款游戏——实现、构建、生成资产、运行验证、迭代调试，直到功能完成。
>
> **schema 权威源**：tro-scene / tro-tileset / tro-animations 的字段定义来自
> trogue 仓库的 AGENTS.md；本文件是随模板分发的**快照**。若上游 schema 演化而
> 下方字段表未更新，以 trogue 仓库为准（本项目引擎即该快照里的 engine/）。

## 项目结构

```
project/
├── AGENTS.md          # 本文件
├── CMakeLists.txt     # 聚合：engine + tools + game
├── engine/            # trogue 引擎（C++20 静态库，快照，勿手改）
├── editor/            # Godot 4.7 可选视觉标注/导出工程（快照）
├── pixellab/          # PixelLab MCP → tro-* 转换层（Python，快照）
├── tools/             # scene_gen（离线场景生成 CLI）+ ipc_smoke.py
├── game/              # 你的游戏（随意改写）
└── assets/            # 你的资产（引擎按 CWD assets/ 约定读取）
```

快照（`engine/`、`pixellab/`、`editor/`、`tools/scene_gen.cpp`）由上游同步，
**不得手改**；要改引擎请改 trogue 仓库。本项目含更新器 `scripts/sync_from_source.sh`：
在项目根重跑即从上游**临时克隆**取最新模板，只刷新 vendored 快照，**不动**你的
`game/`、`assets/` 与项目自有文件。项目自有：`game/`、`assets/`、`CMakeLists.txt`、
`tools/CMakeLists.txt`、`tools/ipc_smoke.py`、`README.md`、`.gitignore`、本文件。

## 引擎公共 API 边界（重要）

引擎是**薄执行原语层**，不是游戏框架：

- `engine/include/trogue/*.hpp` 是唯一公共面，统一 `namespace tg`、纯 C++、RAII；
  内部直接调 raylib C API。game 只经这些头使用引擎。
- **引擎不规定游戏架构**：没有 `World`/`Entity`/ECS/组件/系统。对象模型（OOP
  或 ECS）由你在 `game/` 定义。
- **场景资产是只读快照**：`tg::SceneAsset`（RAII）持有 tile 层、图集、descriptor；
  `tg::SceneEntity` 是值快照（通用 spawn descriptor）。引擎不保存运行时实体位置。
- **绘制是显式的**：`tg::render_scene` 只画 tile 层；sprite/色块由你调用
  `tg::render_sprite`/`tg::draw_rect` 绘制，自行排序。引擎不隐式遍历实体，
  descriptor 的 `type`/`solid` 不触发引擎玩法分支。
- **碰撞是低层查询**：`tg::is_solid_at`/`tg::rect_hits_solid`/`tg::tile_at` 只查
  `solid:true` 的 tile 层；实体碰撞、动态碰撞由你实现。
- **通用表现原语归引擎**：
  - `tg::AnimationPlayer` + `tg::AnimationSet`：帧动画采样（fps/loop/seek/回调/
    `co_await done()`）。你决定何时播哪条、绑到哪个对象；播放器**不自动 draw**。
  - `tg::TweenManager`：float/`Vec2`/`Color` 补间（时长/缓动/延迟/循环/回调/
    `co_await wait()`）。你决定补间谁、目标值、触发时机。
  - `tg::TerrainTable`/`tg::pick_tile`：autotile 纯函数（8 向 pattern → tile id）。
    地形指派/程序生成归你；引擎只做确定性采样。
- **功能准入判据**：引擎只收**机制性、确定性、可无头测试**的执行原语；音频总线、
  shader 管理、粒子等美学/玩法决策载体由 game 直接调 raylib 实现。
- **不重复造轮子**：引擎已有的通用能力（补间、动画、autotile、tile 查询）**直接
  复用**，不要在 game 层手写等价物。
- **数值精度纪律**：像素/网格对齐的移动用固定时长 tween（`TweenManager`）并播完
  **精确 snap 到整数像素**；不要用指数趋近 lerp（float 永不收敛，残差被绘制截断
  成错位/抖动）。绘制用浮点原语（引擎 `draw_rect` 内部即浮点；raylib 层避免
  `DrawRectangle(int)` 截断）。
- **注释自足纪律**：代码注释（含 docstring）必须自足——直接陈述契约、语义与
  「为什么」，**不得引用本仓库内部文档**（`plan-N`/`docs/plan-*.md`/`§x.y`
  节号/里程碑/门禁编号/`AGENTS.md` 指针/查证副本路径等）。这些文件不随代码
  分发，对读者是悬空噪音。合法的外部溯源（如对齐原版 `trogue-orign/*.lua`、
  Godot 上游符号名）可以保留。

### 内存加载程序生成场景

`tg::SceneAsset::load_json(text, name)` 与 `load(path)` 同一解析/校验路径——把程序
生成的 tro-scene JSON 直接喂进去渲染。地形指派 → `pick_tile` 填 id → 拼 JSON →
`load_json` 是推荐的程序生成闭环（`tools/scene_gen.cpp` 即此机制半的 CLI 示范）。

## 资产规范

### tro-scene v2.1（`assets/scenes/*.json`）

| 规则 | 说明 |
|------|------|
| 版本 | `format:"tro-scene"`, `version:2` |
| 坐标系 | 像素，原点=tilemap 左上角，y 向下；实体 x/y 为**左上角** |
| 三态 | ① `tilesets` 存在=图集模式；② 有 `palette` 无 `tilesets`=palette 模式；③ 两者皆无且 `layers` 空/缺省=**bare 纯实体场景**。图集与 palette 互斥 |
| `meta` | `{name, background:"#rrggbb"}` |
| `tilemap` | `tile_width`/`tile_height`；`tilesets`(1..8, `{name,path}` 相对 assets/)；`palette`(≤32)；`layers`(≤4) |
| 层 | `{name,width,height,solid?,origin?:[x,y],tileset?,tiles}`；图集模式每层必填 `tileset`；`tiles` 行主序，长 = w×h，`-1`=空 |
| `solid` 层 | 参与引擎 tile 查询；**层矩形外 = 不阻挡** |
| 实体 | `{id(唯一必填), type?"unknown", x,y,w,h, z?, color?, solid?, sprite?, animations?, props?}` |
| 实体 sprite | 图集 `{tileset,tile}` 或独立贴图 `{texture, region?, offset?:[ox,oy]}`；region 缺省整图；锚点 = x/y + offset |
| 实体 animations | `{textures:[路径], animations:[{name,fps,loop,frames:[{texture:idx,region?,offset?}]}]}`；名 = entity id（经 `asset.animation_set(i)` 取） |
| 实体 props | 任意 object，引擎忽略不存；game 导入时自行解释 |
| 限额 | layers ≤4、tilesets ≤8、palette ≤32、实体名 63 字节 |

### tro-tileset v2（`assets/tilesets/*.json`）

`{format:"tro-tileset",version:2, texture, tile_width,tile_height, columns,rows,
terrain_sets:[...], tiles:[{id,col,row, size_in_atlas?,texture_origin?,y_sort_origin?,
terrain_set?,terrain?,peering_bits?,custom_data?}]}`。`tiles[]` 数组顺序 = tile id
（tro-scene 引用该 id）。`terrain_sets`/`peering_bits` 供 `tg::pick_tile` 消费。

### tro-animations v1（`assets/animations/*.json`）

独立动画资产，结构 = 实体 `animations` 字段：
`{format:"tro-animations",version:1,textures:[...],animations:[{name,fps,loop,frames}]}`。
引擎只消费场景实体**内嵌** `animations`；独立文件是转换中间产物，用时把
`textures`/`animations` 内嵌进场景实体。

## Godot 编辑器（可选）

`editor/` 是可选视觉标注/预览工具，**不是运行时依赖**。Agent 可 headless 调用：

```bash
godot --headless --path editor --import
godot --headless --path editor --script res://addons/scene_exporter/headless_export.gd \
  -- scene=res://assets/a.tscn,scene=res://assets/b.tscn
# 也可 tileset=res://...tres / animations=res://...tscn
```

产物直写 `../assets/`。**简单场景/测试关卡可直接手写 tro-scene JSON，无需 Godot**
（`agent` 优先走这条路）。元数据速查见 `editor/README.md`。

## PixelLab 资产管线（`pixellab/`）

像素美术生成（外部 MCP 服务）→ tro-* 转换层。**调用任何 PixelLab MCP 工具前先查
文档** `https://api.pixellab.ai/mcp/docs`；批量前 `get_balance`；pro 模式先 confirm_cost。

```bash
# 角色/动画：保存 create_character/animate_character 的 get_character 响应为 meta JSON
python3 pixellab/pxlab.py import-character --meta <json> --name <n> [--fps 8] [--loop walk,idle]
# Wang 瓦片集：保存 create_topdown_tileset 的 metadata
python3 pixellab/pxlab.py import-tileset --meta <json> --image-url <png URL> --lower <名> --upper <名>
# 地图：get_map ASCII 网格 → 场景（依赖 build/tools/trogue_scene_gen，先构建）
python3 pixellab/pxlab.py import-map --grid <文件|-> --tileset <tro-tileset> --scene <名> --out <assets 相对路径>
python3 pixellab/pxlab.py verify        # 复核产物 sha256
```

产物落 `assets/`；`assets/pixellab_manifest.json` 记录来源。规则明确的资产仍可直接
手写 tro-*——PixelLab 路径只在需要美术生成力时使用。

## IPC 协议（tro-ipc v1.2，仅 DEBUG 构建）

- TCP `127.0.0.1:48764`（`--port` 可改），JSON-lines，每行一个请求/响应。
- 响应包络：`{"ok":true,"data":{...}}` / `{"ok":false,"error":"..."}`。
- 接入问候：`{"ok":true,"event":"hello","data":{...}}`。
- 最多 8 并发连接；单行 ≤64KB。命令字段平铺在请求对象里。
- **engine 传输层保留**命令：`ping`、`subscribe`/`unsubscribe`/`connections`。
- **其余命令语义归 game**（在 `game/src/main.cpp` 的 IPC handler 实现）。起步骨架
  已实现：`status`/`list_entities`/`get_entity`/`move`/`screenshot`/`log`/`quit`。
  随游戏设计增改命令，并同步 `tools/ipc_smoke.py`。
- **事件通道**：`tg::Ipc::publish(event, data)` 向订阅连接推送
  `{"ok":true,"event":E,"data":D}`。判别式：响应**永不**含顶层 `event` 键。订阅
  filter 为 data 顶层字段等值匹配（多键 AND）；断开即订阅清零。

## Agent 调试工作流

0. **文件工具纪律**：改任何文件前先用 read 工具读它；不要用 bash 读取代替。
   （edit/write 以 read 记录为准；未经 read 或读后变更会被拒绝。）
1. 构建：`cmake --build build`
2. 起服：`(./build/bin/trogue > /tmp/game_run.log 2>&1 &)`
3. 冒烟：`python3 tools/ipc_smoke.py`
4. 调试循环：`status`/`list_entities` 观测 → 改场景（起步游戏默认是 `main.cpp` 里
   的**内置内存场景**；若你加了 `assets/scenes/*.json` 并用 `--scene` 指定，则改
   文件后 ~0.5s `status.reloads` 自增即热重载生效）→ `screenshot` 拿画面 →
   实现 `spawn`/`set_entity` 等命令后可做运行时实验。
5. 截图视觉验收：**read 工具可直接读图**并自行下结论；项目外路径（如 `/tmp`）先
   拷进项目内可读路径再读（用完即删）。辅以**数值自证**（Python 像素比对、IPC
   快照的 transform 视图）——亚像素残差/截断肉眼易漏，机器判定优先。
6. 收尾：`{"cmd":"quit"}` 干净退出；残留进程占端口用 `pkill -x trogue` 清理。

**已知坑**：① raylib `TakeScreenshot` 破坏绝对路径，本项目用
`LoadImageFromScreen`+`ExportImage`（截图前 `rlDrawRenderBatchActive()` 强制 flush
渲染批，否则拍到残缺帧）；② 测试脚本必须按行解析 TCP 流（hello 与响应可能连包）。

## 测试

- 把游戏的纯逻辑（输入/规则/AI/状态机）写成**无 raylib 依赖**的模块，在
  `game/CMakeLists.txt` 里编进一个无窗口测试目标（文件内有示例注释），用
  `ctest` 覆盖；表现层靠截图 + 数值（IPC 快照）验收。
- 表现层「静止时位置 == 逻辑坐标」这类不变量，可由 IPC 快照的 `transform` 视图
  数值自证——亚像素残差/截断肉眼易漏，机器判定优先。

## 引擎构建依赖

- 编译器：支持 **C++20 协程**（GCC 12+ / Clang 15+ 或同能力编译器）。
- 库：raylib 6.0、nlohmann/json 3.11+、tl::expected 1.x。
- 来源：优先系统包管理器（`find_package` 自动探测）；系统缺失时可用 CMake
  `FetchContent` 按需拉取。**不要在 game 项目里 vendored 这些第三方库。**
- 缺依赖（`find_package` 失败 / 链接缺符号 / 版本不符）时**通知用户**安装，
  不要自行改动系统环境。

## 开发流程

1. 给出计划（含步骤/验证），等待批准。
2. 实现计划。
3. subagent 检查未提交代码（合理性/优雅/风格/逻辑）——禁止自检。
4. 检查后更新 `CHANGELOG.md`（若无则新建；每模块 `### 功能描述` + `- 影响的文件:`）。
5. 更新本文件（若架构/规范变化）。
6. 询问用户是否写 commit message（英文预览待确认，禁止直接提交）。
7. 确认后提交并推送。
