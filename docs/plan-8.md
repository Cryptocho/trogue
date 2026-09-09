# 里程碑 8：tro-tileset 多格 tile（size_in_atlas）与纹理原点支持

- 计划日期：2026-09-09
- 前置：里程碑 5（C++ 引擎）、6（最小闭环）、7（IPC 事件通道）已完成；`editor/` Godot 4.7.2 headless 导出链路可用
- 参考：`AGENTS.md`「资产规范：tro-tileset v2」「引擎公共 API 边界」；`editor/addons/scene_exporter/tro_schema.gd`、`engine/src/scene_asset.cpp`、`engine/src/render.cpp` 现状；Godot 语义查证见 §2（`reference/godot-4.7.2-stable/`）

## 1. 目的与问题

**问题**：`editor/assets/test.tscn` 的 Decorations 图集中，tile (10,10) 是一个**多格 tile**：
`size_in_atlas = (3, 5)`（48×80 像素）、`texture_origin = (-2, 30)`、`y_sort_origin = -4`。
当前管线在两处丢失了这组数据：

1. **导出插件**（`tro_schema.gd` `build_tileset_groups`）：每 tile 只写 `col/row`，
   不读 `get_tile_size_in_atlas` / `TileData.get_texture_origin` / `TileData.get_y_sort_origin`；
2. **引擎**（`scene_asset.cpp` `parse_tileset_file` → `TilesetMeta.tile_rects`）：
   region 恒按单格 `(col*tw, row*th, tw, th)` 建表；`render.cpp` `render_scene`
   把每个 tile 画在**格子左上角**。

结果：3×5 的树被压成 16×16 的碎片，位置也不对——多格 tile 完全无法正常显示。
用户拍板：修改 editor 插件与引擎层。

**范围（本里程碑实现）**：

1. **tro-tileset schema 只增可选字段**（`version` 仍为 2，向后兼容零迁移）：
   `tiles[]` 条目新增 `size_in_atlas` / `texture_origin` / `y_sort_origin`（§3.1）。
2. **导出插件**导出三个新字段 + margins/separation 非零 warning（§3.2）。
3. **引擎**解析三字段、按多格 region 建 tile 表、`render_scene` 按 Godot 语义
   （cell 中心对齐 + texture_origin 偏移）定位绘制（§3.3）。
4. 测试与文档：schema 正/反例单测；重导出回归；运行期截图视觉验收；
   `AGENTS.md` tro-tileset 小节增补（§4/§5）。

**明确不在范围**：margins/separation 图集布局、alternative tile 变体、逐 tile y-sort
（引擎渲染模型不变）、实体图集形态 sprite 的居中语义变化、逐 tile 元数据公共查询 API。

## 2. Godot 语义查证（4.7.2-stable 本地源码，reference/godot-4.7.2-stable/）

1. **纹理 region**（`scene/resources/2d/tile_set.cpp:5296` `TileSetAtlasSource::get_tile_texture_region`）：
   ```cpp
   region_size = texture_region_size * size_in_atlas + separation * (size_in_atlas - 1);
   origin      = margins + frame_coords * (texture_region_size + separation);
   ```
   本项目资产 margins/separation 均为 0 → region = `(col*16, row*16, sw*16, sh*16)`。
2. **绘制位置**（`scene/2d/tile_map_layer.cpp:2732` `compute_transformed_tile_dest_rect`，
   alternative 0 无翻转/转置）：
   ```cpp
   dest_rect.position = -0.5 * size;      // 相对 cell 中心
   dest_rect.position -= texture_origin;
   ```
   → **dest 左上 = cell 中心 − region.size/2 − texture_origin**。
   同式亦见 `tile_set.cpp:5317/5328`（`is_position_in_tile_texture_region`），交叉印证。
   注：Godot 实际实现 dest.size 先加 `FP_ADJUST`（1e-5，消除相邻 tile 绘制缝隙，
   `tile_map_layer.h:386`），语义推导忽略之。
   **向后兼容**：1×1 且 origin=(0,0) 时 `cell中心 − (16,16)/2 = 格子左上角`，
   我方引擎新公式与旧公式**逐位一致**（整数像素 + 二进制精确半值，float 运算无残差；
   我方不引入 FP_ADJUST）。
3. **y_sort_origin**（`scene/2d/tile_map_layer.cpp:552`）：y-sort 排序键 =
   `map_to_local(cell).y + tile_y_sort_origin`——相对 **cell 中心**、向下为正，
   仅 `TileMapLayer.y_sort_enabled` 时消费。
4. **实测自洽**：3×5 树 tile 的 dest 左上 = cell 左上 + (−14, −62)，48×80 纹理底部
   = cell 左下 +2px——树根正好落在格底；`y_sort_origin=-4` 即排序线在 cell 中心上方 4px。

## 3. 方案

### 3.1 Schema：tro-tileset v2 只增可选字段（version 仍为 2）

`tiles[]` 条目新增（全部可选；缺省 = 现行为；旧资产零迁移）：

| 字段 | JSON 类型 | 缺省 | 语义 |
|------|-----------|------|------|
| `size_in_atlas` | `[w, h]` int 数组 | `[1, 1]` | tile 覆盖的图集格子数；region = `(col*tw, row*th, w*tw, h*th)` |
| `texture_origin` | `[x, y]` int 数组（可负） | `[0, 0]` | Godot 纹理原点透传；绘制偏移 = **减去** origin（§2.2） |
| `y_sort_origin` | int | `0` | y-sort 排序键偏移（相对 cell 中心，向下为正）；引擎**解析存储、暂不消费** |

- 校验（错误码 `kSchemaViolation`，与既有风格一致）：数组长度必须为 2 且元素为 int；
  `size_in_atlas` 元素 ≥1（上限 4096 防御）；`texture_origin` / `y_sort_origin`
  绝对值 ≤65536 防御。
- region 越界（`col+sw > columns` 或超出贴图尺寸）**不在 load 期校验**——与现状
  `col/row` 同（load 不读纹理文件，plan-5.2 §2.3 约定），绘制期采样行为由 raylib 兜底。
- `tiles[]` 键白名单**不新增**（现状即不拒绝 terrain_set/custom_data 等透传键，保持一致）。

### 3.2 导出插件（editor/addons/scene_exporter/tro_schema.gd）

- `build_tileset_groups` tile 循环内追加（非缺省才写字段，与 `peering_bits` 省略风格一致，
  单格 tile 导出**零 diff**）：
  ```gdscript
  var size_in_atlas: Vector2i = atlas.get_tile_size_in_atlas(coords)
  if size_in_atlas != Vector2i.ONE:
      entry["size_in_atlas"] = [size_in_atlas.x, size_in_atlas.y]
  var t_origin: Vector2i = td.get_texture_origin()
  if t_origin != Vector2i.ZERO:
      entry["texture_origin"] = [t_origin.x, t_origin.y]
  var yso: int = td.get_y_sort_origin()
  if yso != 0:
      entry["y_sort_origin"] = yso
  ```
- **margins/separation 明确损失 warning**：图集 `margins`/`separation` 非 0 时
  `warnings.append(...)`（引擎 col/row→像素映射不含偏移，导出可能有损，必须说明）。
- `_match_atlas_tile`（实体 Sprite2D → 图集形态）**不改**：其匹配条件
  `region.size == (tw, th)` 天然只命中单格 tile；多格 region 落入独立贴图形态，
  由导出器换算出正确 `offset`——语义本就正确，无需扩散改动。
- columns/rows（`_fill_texture_metrics`）含义是图集**格子**数，不受 size_in_atlas 影响，不改。

### 3.3 引擎（engine/src/）

- **scene_impl.hpp**：`TilesetMeta.tile_rects`（`std::vector<Rect>`）升级为
  ```cpp
  struct TileVisual {
      Rect region{0, 0, 0, 0};   // 贴图子矩形（含 size_in_atlas 扩展；w/h==0 = 无绘制，防御不变）
      Vec2 texture_origin{0, 0}; // Godot 纹理原点（绘制时减去）
      int y_sort_origin = 0;     // 透传存储，暂不消费（引擎无逐 tile y-sort）
  };
  std::vector<TileVisual> tile_visuals;  // 与 tile_count 对齐，数组顺序即 id
  ```
  使用点仅两处（`scene_asset.cpp` 建表、`render.cpp` 读），无测试 seam 依赖
  （seam 白名单保持 5 符号不变）。
- **scene_asset.cpp** `parse_tileset_file`：解析三字段 + §3.1 校验；
  region 按 `(col*tw, row*th, sw*tw, sh*th)` 建；`texture_origin`/`y_sort_origin` 缺省补零。
- **render.cpp** `render_scene` 图集绘制位置改为 Godot 语义（浮点原语不变）：
  ```cpp
  const Rect r = tv.region;
  const float wx = info.origin_x + tx * impl.tile_w + impl.tile_w * 0.5f
                 - r.w * 0.5f - tv.texture_origin.x;
  const float wy = info.origin_y + ty * impl.tile_h + impl.tile_h * 0.5f
                 - r.h * 0.5f - tv.texture_origin.y;
  ```
  1×1 且 origin=0 时与旧公式逐位相等（§2.2），既有资产（demo/forest/tile_map_layer）零回归。
- **render.cpp** `render_sprite` 图集形态：region 自动含多格尺寸（经 `tile_visuals`）；
  绘制锚点语义不变（`pos + offset` 为纹理左上）。Godot 的「cell 中心对齐 + texture_origin」
  是 **tile 层**语义，实体 sprite 的 Godot 等效摆放由 game 构造 `SpriteDesc.offset` 自行表达
  （AGENTS.md 写明）。
- **y_sort_origin**：解析并存储于 `TileVisual`，`render_scene` 不消费（引擎整层按数组序绘制，
  无逐 tile 排序）。同层重叠多格 tile 的覆盖次序为**行主序扫描序**（下方格后画）；
  Godot 关闭 y-sort 时按 quadrant 组织绘制、不逐 tile 保证次序，两者不做逐 tile 对齐——
  如素材重叠观感错误，需启用逐 tile y-sort（§6 遗留）。未来逐 tile y-sort
  里程碑再消费/暴露公共查询（需求驱动，不预先造 API）。

### 3.4 文档

- `AGENTS.md`「tro-tileset v2」小节：三字段定义表、绘制语义（dest = cell中心 − size/2 − origin）、
  y_sort_origin「透传存储暂不消费」边界、已知限制（margins/separation、alternative、
  多格 tile 逻辑上仍只占一个 cell——solid 查询/tile_at 语义不变、
  同层多格 tile 重叠覆盖次序 = 行主序扫描序）。

## 4. 步骤（含开发流程 3~7）

1. **插件**：§3.2 改动 → `godot --headless --import` + headless 重导 `test.tscn`；
   核对 `assets/tilesets/test.json` tile 0 出现三字段（`[3,5]` / `[-2,30]` / `-4`）、
   `test_1.json`（16 个单格 tile）零 diff、场景 JSON 不变。
2. **引擎**：§3.3 三处改动（scene_impl.hpp / scene_asset.cpp / render.cpp）。
3. **单测**：`tools/tests/scene_schema_test.cpp` 新增正例（三字段全带/部分带可加载）
   与反例（`size_in_atlas` 非数组/长度≠2/非 int/≤0/超上限 `[4097,1]`；
   `texture_origin` 类型错/超上限 `[70000,0]`；`y_sort_origin` 类型错/超上限 70000
   → `kSchemaViolation`），全部经公共 `SceneAsset::load` 临时文件路径。
4. **回归**：Debug + Release 构建零告警（`-Wall -Wextra -Wpedantic`）；ctest 全绿；
   `python3 tools/ipc_smoke.py` 44/44。
5. **视觉验收**：起服加载 `assets/scenes/test.json` → IPC `screenshot` → 读图核对
   3×5 树按 §2.4 预期位置显示（dest 左上 = 所在 cell 左上 + (−14, −62)）。
   注意：该层为 15 棵相互重叠的密林（行主序覆盖），最上行树冠 dest y = 16−62 = −46
   越出层顶属正常；核对以树冠完整度与任一格 +2px 根部落点为准，勿把重叠当 bug。
6. **subagent 检查**未提交代码（合理/优雅/风格统一/无逻辑问题；禁止自检）。
7. 更新 `CHANGELOG.md` 与 `AGENTS.md`（§3.4）。
8. 询问用户 commit message（英文预览，确认后提交**所有**变更，禁止直接提交）。

## 5. 验证清单

- [ ] 重导出：`test.json` 三字段正确、`test_1.json` 零 diff、`test.tscn` 场景 JSON 其余不变
- [ ] 旧资产零破坏：无新字段的 tileset（demo/forest/tile_map_layer）照常加载；
      1×1 位置与旧式一致由 §2.2 公式推导保证（整数量 + 精确半值逐位还原），
      demo/forest 截图目测无回归
- [ ] schema 单测：正例可载、反例全拒
- [ ] Debug/Release 零告警、ctest 全绿、smoke 44/44
- [ ] 截图：3×5 树位置符合 §2.4 手算预期，无 16×16 碎片

## 6. 遗留与边界

- margins/separation 图集布局：插件 warning 提示（明确损失），支持留待真实素材出现阻塞。
- alternative tile 变体：既有 v2 限制不变（警告忽略）。
- 逐 tile y-sort 与 `y_sort_origin` 消费：引擎渲染模型不变；未来按需评估
  （数据已存储，届时补公共查询即可）。
- 实体图集形态 sprite 对多格 tile 的居中摆放：由 game 经 `SpriteDesc.offset` 表达，引擎不隐式处理。
- 图集动画帧（`animation_columns` 等 `get_tile_texture_region` frame 维度）：本项目未用，不纳入。
