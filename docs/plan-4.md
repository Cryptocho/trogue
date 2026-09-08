# 里程碑 4 计划书：插件 v4（动画/sprite/terrain set 导出）与插件统一

- 计划书编号：`docs/plan-4.md`（里程碑 M4，承接阶段 3）
- 日期：2026-09-07
- 状态：**subagent 审查 PASS**（含 6 条建议已全部采纳并入）；等待用户拍板后开工
- 送审记录：2026-09-07 交 subagent 只读审查（对照 AGENTS.md 门禁/开发流程、tro_schema.gd/headless_export.gd/scene.c 代码现状、soldier tscn 资产逐项核实），结论 PASS，无阻塞问题

## 目的

1. 主角素材 `editor/assets/soldier_animated_sprite_2d.tscn`（AnimatedSprite2D，7 组动画 idle/walk/attack01~03/death/hurt，100×100 序列帧）当前**无法被插件导出**：`_find_sprite2d` 只识别 Sprite2D，AnimatedSprite2D 识别不到；且未标注 `type` metadata 的动画节点在 `build_scene` 中被跳过 → 导出为无实体/无色块（已标注 type 时为无色块）。本轮让插件能完整导出动画素材。
2. 插件具备三项导出能力：**动画**（完整帧表）、**sprite**（首帧 / 独立贴图）、**terrain set**（tro-tileset v2 的 terrain_sets/peering_bits 透传，已有，保持并回归）。
3. 统一两个插件为一个：删除遗留 `tileset_exporter`（trogue-orign 遗产，功能已被覆盖），所有导出由 `scene_exporter` 承担。
4. 引擎支持**纯实体场景**（bare scene：无 tilesets、无 palette、无 tile 层），动画素材可脱离关卡独立成场景；`animations` 字段引擎透传不消费（动画播放属玩法阶段）。
5. schema-first：所有新结构先写入 AGENTS.md「资产规范」再实现（文档先行）。

## 范围

### 做

- **schema v2.1 增量（AGENTS.md 权威文档先行）**：
  - 实体新增可选 `animations`：完整帧表
    `{ "textures": [贴图路径索引表], "animations": [ { "name": ..., "fps": N, "loop": bool, "frames": [ { "texture": 索引, "region": [x,y,w,h]?, "offset": [ox,oy]? } ] } ] }`
    ——贴图路径去重入 `textures[]` 索引表，帧经索引引用；region 缺省整图，offset 缺省 `[0,0]`。
  - `sprite` 字段语义不变：动画素材导出时自动填**默认动画首帧**（引擎现有渲染即出静态画面）。
  - **双模式改写为三态**（AGENTS.md「资产规范·双模式」条目同步改写）：图集（有 `tilesets`）/ palette（有 `palette` 无 `tilesets`）/ **bare**（无 `tilesets` 且无 `palette` 且 `layers` 空或缺失，三条件同时成立才合法）；其余组合维持 v2 校验（如无 tilesets/palette 但有层 → 仍报「缺少 tilesets 或 palette」）。
  - 新增独立资产 **tro-animations v1**：`{ "format": "tro-animations", "version": 1, "textures": [...], "animations": [...] }`（结构同实体 `animations`）。
- **引擎 `engine/src/scene.c`**：裸场景支持——放宽 `scene_parse` 的两处条件（「缺少 tilesets 或 palette」「缺少 layers」）；渲染/碰撞对 0 层自然空转（render.c 层循环已安全、tg_world_create 默认 tile 尺寸 16 无除零）；其余语义与限额不变。demo/test 场景带完整 tilemap 字段，不触发新路径，无回归风险。
- **插件 `tro_schema.gd` v4**：
  - 通用 sprite 节点查找（Sprite2D + **AnimatedSprite2D**）；AnimatedSprite2D 导出 `sprite`=默认动画首帧 + `animations`=全帧表（AtlasTexture → `{atlas 贴图, region}`，fps/loop 透传；flip / 非 AtlasTexture 帧 → warning + 按 region 原样输出）。
  - 纯实体场景导出：场景无 TileMapLayer 但含实体时不再双拒绝（不再报「没有任何设置 tile_set 的 TileMapLayer」「需要至少一个 tile 层」）。
  - 新菜单「Export tro-animations...」+ headless `animations=` 参数 → `assets/animations/<name>.json`（tro-animations v1）。
  - 「Export tro-scene...」菜单项**移除**（headless `scene=` 保留，自动化通道不变；场景导出 UI 另行规划）。
  - **版本号统一为 v4**：`scene_exporter.gd` / `tro_schema.gd` / `headless_export.gd` 文件头注释、插件产物清单注释（补 `assets/animations/*.json`、`assets/scenes/*.json` bare）与 AGENTS.md 目录注释一致。
- **插件统一**：删除 `editor/addons/tileset_exporter/`；`editor/project.godot` 仅启用 `scene_exporter`。
- 验证 + 评审 + 记录（见下）。

### 不做（范围外）

- 引擎动画播放 / tro-animations 消费：玩法移植阶段。
- 场景导出菜单 UI 重新规划（「Export tro-scene...」恢复时机另行计划）。
- tileset/纹理变更热重载（已知限制，保持不变）。

## 步骤（含 AGENTS.md 开发流程 3~7 步）

1. 文档先行：AGENTS.md「资产规范」写入 `animations` 字段、tro-animations v1、**三态模式（图集/palette/bare）**、bare 判定条件（本计划书 PASS + 用户批准后、实现前）。
2. 引擎：`engine/src/scene.c` 裸场景支持（三态中 bare 合法，其余组合维持 v2 校验）。
3. 插件 v4：通用 sprite 查找 + AnimatedSprite2D 首帧/全帧表导出 + 纯实体场景导出（`tro_schema.gd`）。
4. 独立 tro-animations 导出：菜单「Export tro-animations...」+ headless `animations=` 参数（`tro_schema.gd` + `headless_export.gd`）。
5. 「Export tro-scene...」菜单项移除（headless `scene=` 保留）。
6. 删除 `editor/addons/tileset_exporter/` + `project.godot` 插件列表精简。
7. 【验证】见「验证」节，全部通过才算完成。
8. 更新 `editor/README.md`（菜单表、headless 参数含 `animations=`、AnimatedSprite2D 素材标注说明），并入 CHANGELOG 变更清单。
9. **subagent 评审**未提交代码（禁止自检，自检无效）。
10. 评审后更新 CHANGELOG.md（评审通过前禁止修改）。
11. 核对/更新 AGENTS.md（schema 已在步骤 1 写入，此处仅按评审结果核对）。
12. 英文 commit message 预览，**等待用户确认**（禁止直接提交）。
13. 确认后提交**所有**变更（含用户素材清理的删除；不含临时测试副本），不推送（无 remote）。

## 验证

1. **插件解析**：`godot --headless --path editor --import` 无报错；`--check-only --script res://addons/scene_exporter/headless_export.gd` 语法检查通过。
2. **soldier 实测**（AnimatedSprite2D 资产；素材本体未标 `type`，对照组用临时副本 `editor/assets/_soldier_test.tscn`——在副本上加 `type` metadata，导出后删除、不入提交清单）：
   - headless `animations=res://assets/soldier_animated_sprite_2d.tscn` 导出 tro-animations v1 JSON：动画数=7，名称逐项一致（attack01/attack02/attack03/death/hurt/idle/walk），帧数一致（idle 6 / walk 8 / attack01 6 / attack02 6 / attack03 9 / death 4 / hurt 4），每帧 region=100×100，`textures[]` 去重正确（路径指向 `Soldier with shadows/` 下 PNG）。
   - headless `scene=res://assets/_soldier_test.tscn` 导出 bare scene：产物 `tilemap` **无 `tilesets`/`palette` 且 `layers` 为空或缺省**（不含 `tile_width/height` 期望形态），加载不报「缺少 tilesets」；`entities[0].sprite` = 默认动画（attack01）首帧，`animations` = 全帧表。
3. **引擎回归**：
   - bare scene JSON 加载：`./build/bin/trogue --scene <bare>` 正常起服、实体正常渲染、IPC `list_entities` 可见。
   - demo.json / test.json（56 树场景）加载 + 截图无回归。
   - `tools/ipc_smoke.py` 23/23 通过。
4. **插件统一**：`project.godot` enabled 仅 `scene_exporter`；`editor/addons/tileset_exporter/` 已删除；菜单含「Export tro-tileset...」「Export tro-animations...」，无「Export tro-scene...」；三个插件脚本文件头注释统一 v4、产物清单注释含 `assets/animations/`。
5. **工作区核对**：最终 git 变更清单 = 计划内变更 + 用户素材清理，**不含临时测试副本**；提交前与用户人工核对。

## 遗留

- 场景导出菜单 UI（「Export tro-scene...」恢复）：另行规划。
- 引擎动画播放 / tro-animations 消费：玩法移植阶段。
- 裸场景的手写/UI 生成工具：暂用手写 JSON + headless 导出。
- AnimatedSprite2D 复杂 case（flip_h/v、非 AtlasTexture 帧）：按 region 原样输出 + warning；引擎暂不消费，无风险。
- tileset/纹理变更热重载：已知限制，后续里程碑。