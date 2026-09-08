# 里程碑 5 分卷 2：资产解析（SceneAsset）与 tile 查询

> 前置阅读：`docs/plan-5.md`、`docs/plan-5.1.md`。本卷把 `tro-scene v2.1`/`tro-tileset v2` 的**既有校验语义**（含原 C 计划多轮审查沉淀）落到 C++ 资产对象，并定义 tile-only 查询。**schema 语义不放宽**；C 版已定案的键白名单/限额/路径 grammar/空值边界全部保留，仅换载体（jansson→nlohmann、`snprintf`→`std::string`+上限校验）。

## 1. 资产类型形状

### 1.1 `tg::SceneAsset`（RAII 资源类，不可拷贝、可移动）

- 构造：`static tg::expected<SceneAsset, AssetError> SceneAsset::load(std::string_view path)`（或等价）；失败返回带诊断的 error，**不产生半成品、不修改任何既有 asset**。
- 只读视图：`const SceneAsset&` 提供查询；`const` 成员返回**值快照或 const 引用（仅 asset 生命周期内有效）**。
- 持有：已解析的 tile 层、图集/贴图**元数据**、palette、背景、descriptor 列表、动画帧表数据、payload 数据；以及 **GPU 贴图懒加载缓存**（见 5.3）。
- 唯一身份：每次成功加载的 asset 实例持有进程内单调递增的 `asset_id`（uint64，不回绕、不暴露可写）。用于 sprite 快照归属校验（见 §3 与 5.3）。

### 1.2 值快照（公共可复制 struct）

```cpp
struct Vec2  { float x=0, y=0; };
struct Rect  { float x=0,y=0,w=0,h=0; };          // 像素，见 5.3 语义
struct Color { std::uint8_t r=255,g=255,b=255,a=255; };

enum class TileQueryResult { error, clear, solid }; // tile 层查询（§4）
enum class TileLookupResult { error, empty, occupied };

struct SpriteDesc {          // 实体/动画帧的视觉描述
  bool has=false;
  std::uint64_t asset_id=0;  // 归属校验；0=无
  int tileset_index=-1;      // >=0 图集；-1 独立贴图
  int tile=-1;
  std::string texture;       // 独立贴图路径（assets-relative）
  Rect region{0,0,0,0};      // w/h==0 表示整图（render 期按贴图尺寸补齐）
  Vec2 offset{0,0};
};

struct SceneEntity {         // 通用 spawn descriptor 快照（非运行时实体）
  std::string id, type;      // type 不透明，不参与 engine 分支
  float x=0,y=0,w=0,h=0;     // 左上角；缺省 w/h 见 schema
  int z=0;
  Color color{255,255,255,255};
  bool solid=false;          // 仅 game 导入提示
  SpriteDesc sprite;
  // animations 不复制进此快照：经 asset 查询取得（§3）
};

struct LayerInfo {           // 每层只读元数据快照
  std::string name;
  int width=0,height=0,origin_x=0,origin_y=0;
  bool solid=false;
  int tileset_index=-1;      // >=0 图集；-1 = palette/bare
  std::string tileset_name;  // 图集模式 = 该层 tileset 的 name；palette/bare = 空
  int nonempty=0;
};
```

- **字符串**：快照用 `std::string`（容量由 schema 上限约束：长度 < `kNameMax`/`kPathMax` 等，见 §2；加载期超限即拒绝，不做静默截断）。
- 引擎**不提供**任何运行时实体容器/按 id 改位/spawn/despawn；`SceneEntity` 只是可复制的初始描述。

### 1.3 访问 API（本卷定案；最终头文件符号以此为准，不再外推「按某卷定」）

```cpp
class SceneAsset {
 public:
  static tg::expected<SceneAsset, AssetError> load(std::string_view path);

  std::string_view name() const;                     // 仅存活期内有效
  const LayerInfo&   layer(int index) const;         // 只读引用（存活期）
  int                layer_count() const;
  int                entity_count() const;
  SceneEntity        entity(int index) const;        // 值快照（复制）
  int                palette_count() const;
  Color              palette_color(int index) const;
  int                tile_width() const, tile_height() const; // bare→0
  // 动画帧表查询（供 5.4 播放器）：
  //   clip_count(anim_set)/ clip(name) 等，数据只读
  std::uint64_t asset_id() const;
};
```

- 返回内部 `const&` 的访问器需在头注释明确「引用仅在 asset 存活期有效；跨 reload 请取值快照」。优先返回值快照以避免悬垂。

## 2. schema 校验规则全集（load 期，逐条拒绝即失败）

> 常量（`kLayerMax`、`kTilesetMax`、`kPaletteMax`、`kNameMax`、`kPathMax`、`k...Limit` 等）集中在 `config.hpp`；命名避免多义（一常量一语义）。

### 2.0 根与 key 策略

- 根必须是 JSON object；`format=="tro-scene"` 且 `version==2`（int）。
- 根层 key：接受 `format/version/meta/tilemap/entities`；**其他根未知 key 忽略并 `detail` warning**（为导出器未来扩展留口，如历史 `props` 不在根）。此「根宽容、结构对象严格」策略仅限根；entity/tilemap 子对象见下。
- `tilemap` **必须存在**且为 object（bare 也需 `"tilemap":{}` 或 `{"layers":[]}`，沿用 C 版与既有 bare 产物一致）。
- 文本内容须为合法 UTF-8（nlohmann 解析保证）；`std::string` 内嵌 `\0` 一律拒绝。

### 2.1 字符串/路径 grammar（detail 工具，C 版 helper 的 C++ 对应）

- 所有受长度约束的字符串：UTF-8 字节长度 **`<` 对应上限**，超限拒绝。
- **schema 内资源路径**（tileset `path`、sprite/animation `texture`）必须是 assets-relative：只允许 `/` 分隔、无空组件、无 `.`/`..`、不以 `/` 开头、无 drive 前缀、无 `//`、无 embedded NUL；`detail::is_safe_relative_path()` 集中实现，**所有路径统一过它**（禁止各模块自写）。
- **外部 scene 文件路径**（`SceneAsset::load(path)`）：非空、无 NUL、UTF-8 长度 `< kPathMax`、满足同上相对 grammar（相对 CWD）；不做符号链接解析，仅词法校验 + 打开失败报错。
- watcher 输出为裸 basename，另走 basename grammar（5.5）。

### 2.2 tilemap 子对象与模式判定（顺序固定）

1. `tilemap` 存在且 object；
2. 读 `tilemap.tilesets` / `tilemap.palette`：同现→拒绝；`tilesets` 存在→图集模式；否则 `palette` 存在→palette 模式；都缺省→看 `tilemap.layers`：缺省或空数组→**bare**，非空→拒绝（「无 tilesets/palette 但有非空层」）；
3. 尺寸在模式确定后判定：bare 允许 `tile_width/tile_height` 同时缺省并输出 0；非 bare 两者必须同时存在且为 int ∈ [1,256]，缺一/越界→拒绝；
4. `tilemap.layers` 缺省与 `"layers":[]` **完全等价**（解析结果零层，accessor/query 不可区分）。

### 2.3 tilesets / palette / layers

- `tilesets`：1..8 个 object；每项允许 key `name/path`（未知 key 拒绝）；`name` 场景内唯一、非空合法串；`path` 过 §2.1；每个被引 tileset 的 tile 尺寸须与场景一致；tileset 引用存在性在 load 期校验。
- tileset JSON（`tro-tileset` v2）在 **load 期读取并解析元数据**（tile 尺寸/count/name/引用一致性），**不读取纹理文件**；贴图文件存在性属 render 期（5.3）。
- `palette`：1..32 个合法 `#rrggbb[aa]`；空数组不合法。
- `layers` ≤4：每层 object 允许 key `name/width/height/origin/tileset/tiles/solid`（未知 key 拒绝）；`name` 缺省 `"layer"`；`width/height` int ∈[1,4096]（checked 乘法防溢出）；`origin` 缺省 [0,0]，两项须有限且 int 可表示整数值；`solid` 仅 bool；`tiles` 长度必须 == width*height，值 `-1` 空，否则落在 palette/tileset 值域；图集模式每层必填合法 `tileset` 引用，palette 模式层不得带 `tileset`。

### 2.4 数值与颜色

- 所有 number：有限（finite）且转 float 后仍有限；坐标/尺寸绝对值 ≤ `FLT_MAX/4`。
- `x/y` 可负（像素左上角）；显式 `w/h` >0 且 ≤ `FLT_MAX/4`；缺省：非 bare 用 tile 尺寸，bare 用 `16`（沿用既定默认）。
- `z`：缺省 0；number 有限且在 int 范围内（向零取整保存）。
- `color`：缺省白；仅 `#rrggbb`/`#rrggbbaa`，非法拒绝（不静默回退）。

### 2.5 entity 级 key 与 sprite 键白名单

- entity object 允许 key：`id/type/x/y/w/h/z/color/solid/sprite/animations`；**未知 entity key 拒绝**（与 C 版严格化一致）。
- `id` 必填非空、asset 内唯一；`type` 缺省 `"unknown"`（显式须非空合法串）。
- `solid`（可选，缺省 false）：**取值宽容**——仅 `true` 字面量生效视为 true，其余任意值（含 `false`/非 bool）一律按缺省 false 处理（沿用 AGENTS 权威表与现 C 行为，不因 C++ 化收紧）。它只作为 descriptor 导入提示写入 `SceneEntity.solid`，engine 查询不读它。
- `sprite`（可选 object）两种形态互斥，**键白名单**：
  - 图集形态只允许 `tileset`/`tile`（出现 `texture`/`region`/`offset` 或未知 key → 拒绝）；
  - 独立贴图形态只允许 `texture`/`region`/`offset`（未知 key → 拒绝）；`texture` 过 §2.1；
  - region：x/y 有限且 `>=0`、w/h >0（缺省=整图，render 期补齐）；offset：有限 float 可负；不把 float 转 int 截断（raylib 即 float）。

### 2.6 props / animations：缺省、null、空、限额

- 二者均可选：缺省=无 payload；`props:null`/`animations:null` 或非 object → **拒绝**（null 不当缺省）。
- `props:{}` 空对象接受（payload=空）；`animations` object 只允许 key `textures`/`animations`（缺任一个 → 结构不完整拒绝，未知 key 拒绝）。
- `animations` 结构校验在 **load 期**完成（数据入 asset 只读动画集，供 5.4 播放器）：`textures` 非空或空数组规则 + 每帧 `texture` 索引越界拒绝等（完整结构规则与限额随 5.4 播放器一并校验，见 5.2 §2.6b 提示）。
- **payload 限额（沿用多级、一常量一语义）**：
  - per-payload：序列化字节/嵌套深度/键数上限（`kPayloadBytesMax`/`kJsonDepthMax`/`kPayloadKeysMax`）；序列化字节以紧凑编码长度计量（nlohmann `dump()` 长度，用最小分隔符模式）；
  - per-entity animations：`kAnimTexturesPerEntityMax`/`kAnimClipsPerEntityMax`/`kAnimFramesPerClipMax`；
  - asset 累计：`kAssetPayloadBytesMax`/`kAssetAnimFramesMax`（所有 descriptor 合计）；
  - 比较规则统一「计数 > 上限即拒绝」（等于合法）。

> **2.6b**：动画 clip/frame 的结构键白名单、fps/loop/帧 region/offset 数值规则与上述 limit 常量定义，在 **5.4** 与播放器契约一起给出；资产 load 期调用同一校验函数，保证「资产侧校验」与「播放器侧消费」一致。

### 2.7 加载失败与资源释放

- 单一加载路径：解析失败即返回 `expected` 的 error；中间持有的 nlohmann/json 与已分配内存由 RAII 自动释放（无 C 版手写 decref 状态机需求，但需保证「失败不修改任何既有 asset」与「不产生半成品对外可见对象」）。
- 不变量测试：重复坏输入 load 均失败且旧 asset 不受影响。

## 3. sprite 归属与动画集访问

- `SpriteDesc.asset_id` 由 `SceneAsset::entity(i)` 复制时填充（= 该 asset 的 `asset_id`）。**不提供**「重绑/改 id」API；跨 asset 重绑必须对新 asset 重新取快照。
- 归属校验语义：best-effort 防「A 的 sprite 拿去 B 渲染」，非安全边界；渲染（5.3）校验 `sprite.asset_id == asset.asset_id()`，不匹配即失败。
- 动画集：`asset.animations()` 返回只读句柄/视图，供 5.4 `AnimationPlayer` 绑定 clip；其内部纹理/region 引用同样只在该 asset 存活期有效。**归属**：动画集与它产出的帧 `SpriteDesc` 携带所属 `asset_id`，使 5.4 `current_frame()` 结果能通过 5.3 `render_sprite` 的归属校验（播放器帧「能播也能画」）。

## 4. tile-only 查询

**签名定案（阻塞项 4）**：自由函数 + `const SceneAsset&`（引用即保证 asset 有效，**无 nullptr 错误类别**）；`SceneAsset` 无成员的查询，全走下列自由函数：

```cpp
// error 条件：坐标/尺寸非有限、矩形 w/h 非正、layer_index 越界。
TileQueryResult   is_solid_at(const SceneAsset& asset, Vec2 world);
TileQueryResult   rect_hits_solid(const SceneAsset& asset, Rect world_rect);
TileLookupResult  tile_at(const SceneAsset& asset, int layer_index, Vec2 world,
                          int* out_value);   // out_value==nullptr → error
```

固定语义（沿用 C 版定案）：

- 只查 `solid==true` 的层与非空 tile；层矩形外=无数据=不阻挡；descriptor `solid` 永不参与。
- 像素→tile：世界坐标减该层 origin 后 **floor**（负坐标同样 floor，如 -0.5→-1）；结果越出 `[0,width)×[0,height)` 视为层外；减/除/floor/int 转换前做范围校验，极大但有限输入不得 UB。
- 矩形用半开区间 `[x,x+w)×[y,y+h)`；任一 solid 非空 tile 命中即 `solid`（短路）；全部扫描无命中才 `clear`。
- 多 solid 层按层序逐层、命中短路；**无任何 solid 层**的 asset（palette 无 solid 层/bare）对合法输入返回 `clear`。
- `tile_at`：合法层外/空格→`empty` 且 `*out=-1`；占用→`occupied`+值；非法参数或 `out_value==nullptr`→`error` 不写 out。
- 全部查询为 `const`、无锁、单线程调用方推进。

## 5. 本卷决策清单（审查核对）

| # | 决策 |
|---|---|
| 1 | SceneAsset RAII 不可拷贝可移动；SceneEntity/LayerInfo/SpriteDesc 值快照 |
| 2 | asset_id 单调归属校验；无重绑 API |
| 3 | schema 校验语义全集（§2）不放宽，键白名单/限额/路径/空值边界保留 |
| 4 | 根 key 宽容 warning；tilemap/entity/sprite/animations 结构对象严格 |
| 5 | tileset JSON load 期读元数据、纹理 render 期加载 |
| 6 | tile 查询 floor/半开/短路/无 solid 层 clear |
| 7 | 失败安全与 RAII 释放、不产生半成品 |

## 6. 测试要点（无窗口）

- 每个 §2 拒绝规则各一个反例断言；layers 缺省≡空数组；bare tile 尺寸 0；重复 id；三态组合错误；未知键拒绝样例；payload 多级限额边界（=上限合法、>上限拒绝）；路径 grammar 反例。
- asset_id 单调、跨 asset 快照归属失败、坏输入不影响既有 asset。
- **asset_id 耗尽 seam**：`TROGUE_TEST_SEAMS` 下暴露 `asset_test_seed_id(std::uint64_t)`/`asset_test_reset_id()`（同 5.1 §7.2 seam 契约，符号列入测试库白名单）；断言 seed `UINT64_MAX` → 下次 load 返回错误且不产生新 asset、不回绕；reset 后恢复。仅测单调/不回绕时亦可只 seed 到接近上限验证。
- tile 查询：负坐标/边界/半开/多 solid 层短路/无 solid 层。
