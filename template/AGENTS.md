# 游戏项目 Agent 开发指南（trogue）

> 先定体验与深度，再选择实现路径。成功标准是玩家愿意继续玩，不是功能写完。

## 1. 先回答游戏问题

开始实现前，先在项目根写设计文档（如 `DESIGN.md`），回答（按题材取舍）：

- 体验目标是什么，核心循环/核心活动一句话说清；
- 玩家每分钟做哪些重要决定，而不是只执行操作；
- 张力与释放来自哪里（挑战、变化、风险、构筑、叙事皆可）；
- 第一段体验的目标、节奏和可观察反馈。

没有这些答案不要堆功能。**`DESIGN.md` 写完并通过自查前，不得探索引擎能力**：不读 `engine/include/trogue/`、不查引擎能做什么、不按接口凑功能。设计定稿后需要什么再查什么。

## 2. 到可玩闭环

1. 写设计与验收标准，确定最小可玩闭环。
2. 在 `game/` 实现最小闭环（用 `tg::SceneSpec` / `SceneAsset::create` 内存拼装，或写 `tools/` 离线烘焙脚本输出 `tro-*` JSON；tro-* JSON 不要求手写，Godot 只是可选标注工具）。
3. 构建运行：先 IPC/日志数值验证，再读截图检查表现。
4. 试玩是必选步骤：真人或视觉子代理实际玩几轮，按设计目标评价；不满足回设计，不要用工程代码掩盖体验问题。
5. 迭代直到达标。ctest 只证明逻辑没坏，替代不了试玩。

## 3. 设计边界

- 玩法决策归 `game/`：输入、状态、实体生命周期、规则、AI、动态碰撞、相机、UI、音频、存档等。
- 引擎是通用执行层：不拥有你的世界，不根据 `type` 猜玩法，不自动管理实体。
- `engine/` **不封装图形/音频**：2D 矢量图元（圆/多边形/线段/环）与音频直接调 raylib（`DrawCircle*`/`DrawPoly*`/`DrawLine*`/`DrawRing*`、`InitAudioDevice`/`LoadSound`…）；引擎只管 tilemap 绘制、确定性推进与 IPC 传输。
- 不重复造轮子：实现前先确认代码库（含引擎）有无现成实现，有则直接复用，没有就在 `game/` 自己写。

## 4. 资产

资产来源按项目设计自选（`tg::SceneSpec` / `SceneAsset::create` 内存拼装、`tools/` 离线烘焙脚本、PixelLab MCP、用户提供的工具）。**`tro-*` JSON 不要求手写**——是编辑器/PixelLab 导出与离线工具烘焙的产物，运行时通过 `SceneAsset::load` 统一消费。模板不把 PixelLab 作为运行时依赖；生成后的权威输入仍是 `assets/` 下的 tro-* JSON + PNG。CJK 字体优先独立 `.ttf`/`.otf` 直载；`.ttc`/超大字符集才用 `tools/gen_font.py` 烘焙（`game/src/ui_font.*` 是可选参考）。

模板自带 `assets/textures/pixellab/` 下的离线 PixelLab 贴图（PNG + `assets/pixellab_manifest.json` 记录来源 ID 与 sha256），供两个范式范例使用。**不需要 PixelLab 服务或网络即可构建和运行**；要换美术，删掉对应 PNG 并替换即可。

### PixelLab tileset15

如果安装了可选的 `pixellab/` 快照，PixelLab 标准 top-down 16-tile `tileset15` 可通过：

```bash
python3 pixellab/pxlab.py import-tileset \
  --meta <tileset-metadata.json> \
  --image <tileset.png> \
  --name <name>
```

转换结果是 trogue `tro-tileset v2` 的 `dual_grid` / `dual_grid_corners` 表，不是普通 cell-terrain 的两个 terrain pool。使用 `tools/dual_grid_scene_gen` 时，输入必须是 `(w+1)×(h+1)` 的 `vertex_grid`；每个视觉 cell 直接采样 NW/NE/SW/SE 四个顶点。普通 `tools/scene_gen` 的 `.`/`#` 网格和多数投票规则仍只适用于普通 cell-terrain tileset。

## 5. 实现与验证

纯逻辑放不依赖 raylib 的模块；对象模型自选 OOP/ECS。`game/examples/` 里有两个可选范例（若目录还在）：`swarm/` 用并置组件数组 + 系统函数（ECS 风格），`platformer/` 用类层次 + 多态敌人（OOP 风格）——同一份引擎公共 API，两种模型都开箱可用；它们是起手式参考，不是模板推荐的架构，不需要就整个删掉。

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug && cmake --build build
cd build && ctest --output-on-failure
cd .. && ./build/bin/trogue
python3 tools/ipc_smoke.py
```

纯逻辑 ctest；IPC 结构化快照断言；**截图写到 `/tmp/<agent-session>/`**（例如
`/tmp/claude-1000/scratch/`），不要写到项目内 `scratch/`——会话结束 `/tmp` 自然回收，
项目目录保持干净、不被几十张 PNG 噪音污染。读图直接下结论、不做逐像素比对；观
察不到的问题由用户提醒。验收标准以设计文档为准。生成的每个资产都要确认被游戏使用。
确实需要在项目内留视觉证据（例如 PR 截图）再放进 `scratch/` 并写明用途。

## 6. 调试

1. 先读文件再改文件（edit/write 以 read 记录为准）。
2. **IPC 命令参数是不可信输入**：每个字段显式校验类型与范围，非法输入统一返回错误包络；禁止裸 `.get<T>()`（类型不符会抛异常，被引擎兜底成 `internal error`）。冒烟脚本必须为**每条命令**覆盖至少一条负向路径（类型错/缺字段/越界 → `ok:false`，且错误信息不是 `internal error`——后者意味着校验缺失）。模板自带工具/脚本在发布前过一遍冒烟。
3. IPC 用 `status`/`list_entities`/`get_entity` 和游戏自有命令定位；`screenshot` 拿完整帧；结束发 `quit`，残留进程 `pkill -x trogue`。
4. 冒烟脚本属项目自有文件，随游戏 IPC 命令增删维护。
