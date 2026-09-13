# 里程碑 21：tro-scene 描述符表达力泛化（props 暴露、sprite offset/flip、rotation、场景级 props）

- 计划日期：2026-09-13
- 状态：计划中（待审查）
- 前置：里程碑 18–20 已完成；「API 通用性评估结论（2026-09-13 拍板）」已固化；三个模板派生探针项目（平台跳跃 / 顶视角动作 / 塔防）已建好待开发。
- 参考：`AGENTS.md`「资产规范：tro-scene v2.1」「引擎公共 API 边界」「编码规范」；`engine/src/scene_asset.cpp`（`parse_sprite`:782、`parse_entity`:1010、`parse_meta`:220）、`engine/include/trogue/scene.hpp`（`SpriteDesc`:47、`SceneEntity`:59）、`engine/include/trogue/render.hpp`（`render_sprite`）

## 1. 目的与问题

### 1.1 背景：为什么在实测前做

对场景 JSON 的格式审查结论：tro-scene 约束的是「初始视觉布局的表达粒度」，不约束游戏形式；但存在四处**表达力缺口**，且三个待测探针中至少两个会立刻撞上（平台跳跃的朝向翻转、塔防的旋转炮塔）。本里程碑在探针开工前把这些缺口补齐，避免探针为绕开缺口在 game 层手写「从 JSON 读不到、只好硬编码」的补丁——那会污染需求输入（探针本应暴露的是运动/物理原语需求，不是格式缺口）。

同时兑现 P4-③（`render_sprite` 翻转语义拍板：加显式 flip 而非文档化负 scale）。

### 1.2 判据自检（逐项）

| 条目 | 机制性 | 确定性 | 可无头测试 | 与玩法无关 |
|------|--------|--------|------------|------------|
| ① 实体 props 存储 + 快照暴露 | 数据透传，不解释 | 同输入同快照 | 是（解析→快照断言） | 是（语义归 game） |
| ② 图集形态 sprite 可选 offset | 数据字段对齐 | 是 | 是 | 是 |
| ③ sprite flip_x/flip_y（descriptor + render_sprite） | 镜像采样，纯绘制原语 | 是 | 解析无窗口断言 + E2E 截图 | 是（谁翻转归 game） |
| ④ 实体 rotation 透传 + render_sprite rotation 参数 | 旋转绘制原语 | 是（固定角度约定） | 同上 | 是 |
| ⑤ meta.props 场景级透传 + 访问器 | 数据透传 | 是 | 是 | 是 |

反向自检：没有任何一项回答「让某款游戏更好玩」——全部是「让描述符携带 game 需要的初始数据 + 引擎忠实绘制」的机制能力。

### 1.3 需求实证

- **props 暴露（①⑤）**：导出插件自 v1.1 起持续写入实体 props，但引擎「校验后丢弃」——`SceneEntity` 快照无此字段，game 经引擎 API 拿不到，透传承诺落空（`scene_asset.cpp:1018` 注释自认「忽略不存」）。探针（塔防波次参数、平台跳跃关卡参数）是即刻消费方。
- **flip（③）**：任何横向卷轴/朝向敏感游戏的刚需；P4-③ 已在 Roadmap 挂账（「文档化负 scale 或加显式 flip」）。
- **rotation（④）**：塔防炮塔瞄准、双摇杆武器指向——两个探针类型的标准需求；当前 descriptor 与 render_sprite 均无旋转，只能绕开。
- **图集 offset（②）**：两形态不对称是历史遗留——图集形态白名单仅 `tileset`/`tile`，写 `offset` 当前**直接拒绝载入**（`scene_asset.cpp:792-796`，kSchemaViolation「未知键」；AGENTS.md 旧表述「写了被忽略」不准，随本里程碑更正文档），对齐成本极低。

### 1.4 明确不在范围

- **背景层/视差/渐变**：美学与相机策略，game 直调 raylib（`meta.background` 单色保持）。
- **descriptor 级 scale**：运行时表现参数，`render_sprite` 已有 scale 入参；spawn 恒等缩放无实证。
- **非矩形 collider/多边形 descriptor**：碰撞规则归 game（复刻既有边界）。
- **骨骼/形变动画**：tro-animations 帧表边界不变。
- **scene_exporter 侧改动**：本里程碑只扩引擎 schema 与运行时；导出插件按需后补（探针手写 JSON 即可消费新字段）。

## 2. 方案

### 2.0 版本策略

全部为**只增可选字段 + 快照/绘制原语增量**，wire/文件 `version` 仍为 2，旧资产零迁移（文档修订号记 v2.2，与 v2.1 同性质：只增不改）。

### 2.1 实体 props 存储 + `SceneEntity::props` 暴露（①）

- 解析：`parse_entity` 现有「校验 object 后丢弃」改为深拷贝存入 `SceneImpl`（受既有 `kPayloadBytesMax`/`kJsonDepthMax`/`kPayloadKeysMax` 约束，无需新限额）。
- 快照：`SceneEntity` 增加 `tg::Json props`（值拷贝；`tg::Json` 是公共别名，见 `ipc.hpp:35`，值拷贝无生命周期陷阱）。**include 方向钉死**：`scene.hpp` 直接 `#include <nlohmann/json.hpp>`，不引 `ipc.hpp`（nlohmann 本就是运行时依赖，随公共头传播合法；拷贝成本与 IPC 载荷同量级）。缺省空 object。
- 契约：引擎不解释任何键；空 object 与缺省字段等价。快照为值拷贝，无生命周期陷阱（与 `SceneEntity` 其余字段一致）。

### 2.2 场景级 `meta.props`（⑤）

- `meta` 新增可选 `props`（object，校验后存储）；`SceneAsset::meta_props()` 返回 `const tg::Json&`（asset 存活期有效，与既有字符串引用同策略；缺省空 object）。
- 语义与实体 props 对称：场景级初始参数（关卡编号、重力、波次表等由 game 自行定义）。

### 2.3 sprite 形态统一 offset + flip（②③）

`SpriteDesc` 增加：

```cpp
bool flip_x = false;   // 水平镜像
bool flip_y = false;   // 垂直镜像
```

- 解析：`parse_sprite` 两形态统一接受可选 `offset:[ox,oy]`（图集形态不再拒绝；语义与独立贴图形态一致 = 绘制锚点偏移）与 `flip_x`/`flip_y`（bool 缺省 false，非 bool 拒绝）。**图集形态 region 维持拒绝**（region 是图集 tile 的反义，白名单不含，语义钉死）。
- `render_sprite` 消费：flip 经 `DrawTexturePro` 的负宽/高 source-rect 实现（region 补齐后镜像采样区；raylib 语义已核实 `rtextures.c:4513-4514`）；tint/pos 语义不变。**负 scale 维持现状 = param_fail/Invalid**（`render.cpp:219-221` 现行为），不赋予翻转语义（P4-③ 兑现：翻转只走显式字段）。scale 语义钉死为「有限正数缩放因子」。
- `render.cpp` 既有 fast-path（`DrawTextureRec`，判据 scale==1）扩为「scale==1 且 rotation==0 且 !flip_x 且 !flip_y」，其余走 `DrawTexturePro` 全参路径。
- 帧动画帧（tro-animations frame offset）不动——动画帧表本就有 offset，flip 留待动画探针实证（当前无消费方）。

### 2.4 实体 rotation + `render_sprite` 旋转（④）

- descriptor：实体可选 `rotation`（number，度，缺省 0；有限值校验）。**语义钉死：仅数据透传**——它是 game 对象初始朝向的 spawn 提示，引擎不参与碰撞/查询语义（`w/h` 仍是 AABB 语义，旋转不改变碰撞真相）。
- `SceneEntity` 增加 `float rotation = 0.0f`。
- `render_sprite` 增加参数 `float rotation = 0.0f`（度）：**绕锚点（pos + offset）旋转，顺时针为正**（对齐 raylib `DrawTexturePro` 的角度约定与 y 向下坐标系）。flip/rotation/scale 可组合（raylib 原生支持；组合次序以 raylib 实现为准并在注释钉死）。

### 2.5 公共 API 边界增量（写入 AGENTS.md）

- descriptor/快照：`SceneEntity::props`（值拷贝透传）、`SceneEntity::rotation`（spawn 朝向提示，不进碰撞语义）、`SpriteDesc.flip_x/flip_y`、图集形态 offset 合法化。
- 渲染：`render_sprite` 增量参数 flip 语义（显式镜像，负 scale 不再待议）与 rotation（绕锚点顺时针）；**不进引擎**：背景层、视差、自动朝向、绘制排序。
- 资产规范：v2.2 修订段落（只增字段清单）。

## 3. 步骤（含开发流程 3~7）

1. `engine/include/trogue/scene.hpp`：`SpriteDesc`（flip_x/flip_y）、`SceneEntity`（props/rotation）、`SceneAsset::meta_props()` 声明 + 契约注释（自足，不引用内部文档）。
2. `engine/src/scene_asset.cpp`：`parse_sprite`（统一 offset + flip）、`parse_entity`（rotation + props 存储）、`parse_meta`（props）、快照构造处填充新字段。
3. `engine/include/trogue/render.hpp` + `engine/src/render.cpp`：`render_sprite` flip（负宽高 source-rect）与 rotation（`DrawTexturePro` rotation/origin）实现；参数顺序与既有调用兼容（新参数带缺省，零破坏）。
4. 单测 `tools/tests/scene_query_test.cpp` 追加：props 深拷贝/等价缺省/非法类型拒绝、图集 offset/flip 解析、rotation 有限值/缺省、meta.props 访问器、既有用例零回归。
5. E2E：`assets/scenes/demo.json` 增一个 flip+rotation 的探针实体；起服 → 截图 → Agent 读图确认镜像与旋转正确；`tools/ipc_smoke.py` 断言快照新字段（字段位置：`sprite` 对象内含 `flip_x/flip_y`，`rotation` 为实体顶层字段）。
6. 回归：Debug + Release 零告警；`ctest` 全绿；`ipc_smoke.py` 全绿。
7. 模板刷新：维护者模式 `sync_from_source.sh`；`template/API.md` 资产摘要补新字段（一句话级）。
8. 文档：`AGENTS.md` 资产规范 v2.2 段、公共 API 边界增量、Roadmap 勾选（P4-③ 部分兑现）。
9. subagent 审查未提交代码（禁止自检）。
10. CHANGELOG（审查通过后）。
11. 询问用户 commit message（英文预览，确认后提交所有变更并推送）。

### 文件清单

| 文件 | 动作 |
|------|------|
| `engine/include/trogue/scene.hpp` | 改（字段/访问器声明；`render_sprite` friend 签名同步；直接引入 nlohmann） |
| `engine/src/scene_asset.cpp` | 改（解析/存储/快照） |
| `engine/include/trogue/render.hpp` | 改（render_sprite 契约） |
| `engine/src/render.cpp` | 改（flip/rotation 绘制 + fast-path 判据扩展） |
| `tools/tests/scene_schema_test.cpp` | 改（props/offset/flip/rotation 的 schema 拒绝与接受用例——既有 schema 用例归属地） |
| `tools/tests/scene_query_test.cpp` | 改（快照 props 深拷贝/meta_props 访问器用例） |
| `tools/tests/render_test.cpp` | 改（flip/rotation 参数校验 + RenderStats 观测用例） |
| `game/src/main.cpp` | 改（实体快照透出：`sprite.flip_x/flip_y/offset` 在 sprite 对象内，`rotation` 在实体顶层） |
| `game/src/anim_util.cpp/.hpp` | 改（`draw_entity_sprite` 共享包装增量透传 flip/rotation） |
| `assets/scenes/demo.json` | 改（探针实体） |
| `tools/ipc_smoke.py` | 改（快照断言） |
| `template/engine/**`、`template/API.md` | 刷新/改 |
| `AGENTS.md`、`CHANGELOG.md` | 改 |
| `docs/plan-21.md` | 新增（本文件） |

## 4. 验证清单

- [ ] 解析：props 非法类型拒绝；offset/flip/rotation 缺省等价；图集 offset 接受、图集 region 仍拒绝；rotation 非有限值拒绝。
- [ ] 绘制：flip 镜像、rotation 绕锚点顺时针（截图 Agent 读图确认）；flip+rotation+scale 组合不崩溃、视觉合理；**负 scale 仍为 param_fail/Invalid（现状不变）**。
- [ ] 快照：`SceneEntity::props` 深拷贝可改不污染 asset；`meta_props()` 引用随 asset 存活。
- [ ] 兼容：既有全部场景资产（demo/test/tile_map_layer 等）加载零回归；v2 旧资产（无新字段）行为不变。
- [ ] Debug + Release 零告警；ctest/冒烟全绿；模板独立副本构建通过。
- [ ] 改动文件注释无内部文档指针。

## 5. 遗留与边界

- **动画帧 flip**：tro-animations 帧表不加 flip（零消费方）；探针撞上再评估。
- **导出插件**：scene_exporter 不随本里程碑改；Godot 侧 rotation（Node2D rotation_degrees）→ descriptor 的映射留给实际需要时（探针手写 JSON 已可验证引擎侧）。
- **rotation 进碰撞**：明确不进——AABB 语义不变，旋转实体的碰撞策略归 game。
- **背景/视差**：归 game 直调 raylib，不做。

## 6. 补记（审查后追加）

本变更集除里程碑 21 本体外，还包含同日用户直接拍板的三项模板结构重构（不在上方文件清单内，未经独立里程碑）：

1. **模板 `AGENTS.md` 拆出 `API.md`**：引擎 API/资产/IPC 参考移入独立文件，`AGENTS.md` 要求「设计文档定稿后再读」——防止 API 清单作为上下文先行注入而收窄游戏设计；
2. **`sync_from_source.sh` 可选快照**：`pixellab/`、`editor/` 改为 `--with-pixellab`/`--with-editor` 显式安装/刷新，派生项目缺省不携带美术生成管线与 Godot 工具链（三种场景已实测：缺省安装/带标志安装/更新保留）；
3. **模板文档去倾向**：`AGENTS.md` 删除 PixelLab 绑定、题材预设（失败-重开循环、难度曲线）、逐像素比对等强引导表述。

理由：三者与里程碑 21 同属「降低模板示范引力」的同一动机，且互相耦合（`API.md` 同时承载 v2.2 字段摘要）；拆分提交会留下互相依赖的中间态。已由代码审查一并核查（PASS）。
