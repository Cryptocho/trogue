# 里程碑 5A 计划书：引擎运行时边界重构

- 计划书编号：`docs/plan-5.md`（M5A；原“ECS 玩家最小闭环”方案作废）
- 日期：2026-09-07
- 状态：**第九次审查中（第八次 NOT PASS 的 5 项已逐项修订）**
- 变更原因：用户明确引擎应像 LÖVE2D 一样只提供低层能力，使用者可以在 `game/` 自由选择 OOP、ECS 或二者并存；`TgWorld`、`TgEntity` 这类运行时世界/实体概念不得由 engine 定义。
- 本版修订重点（前七轮累计）：不透明资产句柄与 token 归属、checked-copy/path helper、无 world IPC、watcher 契约、显式 render、CMake/CTest smoke、bare tilemap 规则、payload 限额/ownership、ping 不可覆盖、token 分配状态机、nm 黑白名单、render/asset/watcher 测试 seam、candidate_transfer、步骤 16 回填 H1–H8；本版新增（第八轮 5 项）：watcher seam 与真 poll 的 cap 边界分工明确（seam `cap==n`→-2 区分坏事件与 buffer 小、poll 统一 -1；`n` 一律指 basename 字节长度）、seam 原型统一收口到 `test_seams.h`（`scene_asset_internal.h` 不再含 seam）、nm 白名单补齐 4 个 seam（含 `tg_watcher_test_classify`）、CMake 关闭验收改为确定性文件/退出码断言（不再依赖 `ctest -N` 文本）、IPC 边界例区分「分帧接受（不因长度关闭）」与「解析层 invalid request」，H 清单加「处理结果」列且步骤 16 填列后按 H6 删除。
- 前八次审查的阻塞意见均已逐项转化为可执行 API、实现门禁和验证命令；实现前仍须第九次审查 PASS 与用户批准。

## 目的

本阶段先纠正引擎与游戏的根本边界，再进入任何 game-side ECS 或 OOP 玩法迁移。目标不是把 ECS 从 engine 搬到另一个名字，而是让 engine 完全不拥有游戏运行时对象：

```text
engine: tro-* 资产句柄 + tile 资源 + 绘制原语 + tile 查询 + IPC 传输
             │
             │ 只读 descriptor / 显式 draw / callback
             ▼
使用者 game: 自己定义 GameObject、ECS registry、组件、系统、规则和生命周期
```

完成后：

1. `engine/` 公共 API 中不再有 `TgWorld`、`TgEntity`、engine 实体池、按 id 改实体位置、spawn/despawn 或按 `type` 分支的运行时 API。
2. `tro-scene` 被加载为 `TgSceneAsset` 资产句柄；其中的 entity 只通过复制 API 暴露为 `TgSceneEntityDesc` 快照，不是运行时实体。
3. entity descriptor 的 `solid` 只作为 game 的导入提示；engine 的碰撞查询只检查 tile 层，不隐式把场景对象加入碰撞集合。
4. engine 渲染 tile 层与显式传入的 sprite，不遍历或排序 game 对象；对象位置、生命周期、动画和排序由 game 决定。
5. IPC 只负责 JSON-lines 传输、响应包络和 game callback；`list_entities`、`spawn`、`step`、`query_ecs` 等命令不再由 engine 实现或理解。
6. 现有演示程序在 `game/` 定义自己的 `GameApp`/`GameObject` 兼容模型，继续提供旧冒烟脚本需要的调试命令，以证明旧行为是 app policy 而非 engine contract。
7. 增加 OOP/ECS 两种独立 consumer smoke，证明使用者可以在不依赖 engine 对象模型的情况下链接同一套库。
8. 为下一阶段 game-side ECS/OOP 玩法迁移留下清晰、稳定、模型无关的 API；本阶段不实现具体 ECS。

## 架构决策

### 1. engine 不定义运行时世界与实体

以下概念全部属于使用引擎的人/`game/`，禁止进入 engine 公共头或 engine 私有实现：

- `World`/`Entity` 运行时容器及实体池；
- OOP 游戏对象、ECS registry、component、system、archetype、handle；
- 输入解释、移动规则、动态碰撞、AI、回合、战斗、动画状态和生命周期；
- “玩家”“敌人”“solid component”等由 `type` 或其他 descriptor 字段触发的玩法语义。

engine 可以定义资产领域的数据结构，例如不透明的 `TgSceneAsset` 资产句柄，以及可复制的 `TgSceneEntityDesc`、`TgSceneLayerInfo`、`TgSceneSpriteDesc` 快照。层与 tileset 只作为 asset 内部资源存在，**公共头不声明 `TgSceneLayer`/`TgTileset` 类型**（第三次审查阻塞项 1），也没有任何公共 API 接收层/资源句柄；层信息一律经 `TgSceneLayerInfo` copy-out 取得。这些类型分别代表资产、只读描述和视觉值，不代表可变游戏对象。

`TgSceneEntityDesc` 的字段仍保持 schema 语义：`id/type/x/y/w/h/z/color/solid/sprite`。其中 `type` 是不透明字符串，`solid` 是通用导入提示；engine 不因二者执行任何游戏分支。scene descriptor 的 id 必须在资产内唯一；运行时对象 id、冲突处理和是否导入由 game 自己决定。

M5A 对 `props` 与 `animations` 采取“严格校验、asset 内保留、descriptor copy-out 不携带”的明确策略：它们不会被静默映射为 engine/game 对象，也不承诺通过 `TgSceneEntityDesc` 透传。若未来使用者需要读取完整 JSON payload，另立版本化 immutable JSON API；本阶段不提供该 API。

### 2. 不透明场景资产与只读快照

#### 2.1 公共类型边界

`scene.h` 只对资产句柄做前置声明，禁止在公共头暴露资源布局、tiles 数组、GPU 纹理指针或可写 layer/entity 指针。公共头必须显式 include `<stdbool.h>`, `<stdint.h>`, `<stddef.h>` 和 `config.h`，不通过已删除的 `world.h` 间接提供类型：

```c
typedef struct TgSceneAsset TgSceneAsset;
```

- **公共头不声明 `TgSceneLayer`/`TgTileset` 类型**：没有任何公共 API 创建、取得或接收 layer/tileset 句柄，因此不保留无使用者的前置声明；层信息只经 `TgSceneLayerInfo` copy-out 取得，tileset 只作为 asset 私有资源存在。私有资源布局定义于 engine 私有头（见 2.2 与「预期文件变更」）。
- asset 与 descriptor sprite 之间的归属绑定由 `asset_token` 完成：token 由引擎**进程级非零单调计数器**在每次成功 load 时分配并写入**私有 `struct TgSceneAsset`**；`0` 保留表示「无 sprite」，永不作为有效 token。**token 定位定案（阻塞项 1）**：`TgSceneSpriteDesc.asset_token` 是**公共值字段**（copy-out 填充、随快照可被调用方修改），同时 asset 自身是 opaque、其内部 token 存储不在公共头、无 `asset->...` 公共访问。因此 token 是**最佳努力的归属校验而非安全边界**：调用方篡改自己快照中的 token 只会导致后续 draw 因 token mismatch 失败，不会读取或写入其他 asset 的任何数据（draw 对错误 token 一律失败、绝不回退或越界）。任何需要防篡改的严格隔离都不在本阶段范围内，也不依赖 token 达成。显式 draw 由 engine 内部经私有 accessor 取 asset token 与 snapshot token 比较；公共头只暴露 snapshot 值字段，不暴露 asset 内部字段（第三次审查阻塞项 2 + 第五次审查阻塞项 1 定案）。

以下两个类型是**调用方拥有的值快照**，不是 asset 内部对象的指针视图。调用方可以修改自己的副本，但不会修改 engine asset：

```c
typedef struct TgSceneSpriteDesc {
    bool has;
    uint64_t asset_token;           // 公共值字段：copy-out 填充，0=无 sprite；
                                    // 调用方可改，但篡改只导致 draw 失败（best-effort，非安全边界）
    int tileset_index;             // >=0 为本 asset 的图集索引，-1 为独立贴图
    int tile;
    char texture[TROGUE_PATH_MAX]; // <= TROGUE_PATH_MAX-1 字节；不会静默截断
    float rx, ry, rw, rh;
    float ox, oy;
} TgSceneSpriteDesc;

typedef struct TgSceneEntityDesc {
    char id[TROGUE_NAME_MAX];      // <= TROGUE_NAME_MAX-1 字节
    char type[TROGUE_NAME_MAX];    // 不透明 spawn/archetype key
    float x, y, w, h;
    int z;
    unsigned char color[4];
    bool solid;                    // 仅 descriptor 导入提示
    TgSceneSpriteDesc sprite;
} TgSceneEntityDesc;

typedef struct TgSceneLayerInfo {
    char name[TROGUE_NAME_MAX];
    int width, height;
    int origin_x, origin_y;
    bool solid;
    int tileset_index;             // -1 = palette 层
    int nonempty_tiles;
} TgSceneLayerInfo;
```

不提供 `const TgSceneEntityDesc *`、层指针或 tiles 缓冲区指针访问器；统一使用 copy-out API。copy-out 只包含 M5A 定义的 entity 基础字段；`props`/`animations` 不在快照中，详见上面的 payload 策略：

```c
TgSceneAsset *tg_scene_asset_load(const char *path);
void tg_scene_asset_destroy(TgSceneAsset *asset);
const char *tg_scene_last_error(void);

const char *tg_scene_asset_name(const TgSceneAsset *asset);
const char *tg_scene_asset_path(const TgSceneAsset *asset);
bool tg_scene_asset_tile_size(const TgSceneAsset *asset, int *out_w, int *out_h);
int tg_scene_asset_layer_count(const TgSceneAsset *asset);
bool tg_scene_asset_layer_info(const TgSceneAsset *asset, int index,
                               TgSceneLayerInfo *out);
int tg_scene_asset_entity_count(const TgSceneAsset *asset);
bool tg_scene_asset_copy_entity(const TgSceneAsset *asset, int index,
                                TgSceneEntityDesc *out);
bool tg_scene_asset_background(const TgSceneAsset *asset,
                               unsigned char out_rgba[4]);
int tg_scene_asset_palette_count(const TgSceneAsset *asset);
bool tg_scene_asset_palette_color(const TgSceneAsset *asset, int index,
                                  unsigned char out_rgba[4]);
```

`tg_scene_asset_layer_info()`、`tg_scene_asset_copy_entity()`、颜色输出函数在成功前不得写入半成品；`out==NULL`、索引越界或 asset 为 NULL 返回 false。返回的字符串指针（name/path）只读且仅在 asset 存活期间有效；需要跨 asset 交换保存的 game 必须复制到自己的缓冲区。

#### 2.2 所有权和生命周期

- `tg_scene_asset_load()` 成功返回完整、独立拥有资源的新 asset；失败返回 NULL，不修改任何已有 asset。
- **asset_token 分配状态机（阻塞项 1 定案）**：`scene_asset.c` 持有一个进程级 `static uint64_t counter`，**初始为 0**。每次成功 load 按以下顺序执行，语义唯一：
  1. 若 `counter == UINT64_MAX` → 本次 load **立即失败**（返回 NULL 并记录错误），**counter 不变**；
  2. 否则 `counter += 1`，取 `counter` 的新值作为本次 asset 的 token 存入私有 `struct TgSceneAsset`。
  该序列保证 token 恒为 `[1, UINT64_MAX]` 且单调不回绕；**不采用「先递增后检查」**，失败路径不修改 counter。**token 存储只在私有 asset 结构内读写，公共句柄无该字段**；公共头唯一的 token 出现处是快照值字段 `TgSceneSpriteDesc.asset_token`（copy-out 填充、best-effort，见 2.1）。`tg_render_scene_sprite` 等引擎内部经私有 accessor 比较 snapshot token 与 asset token。`TgSceneSpriteDesc.asset_token==0` 恒表示无 sprite；任何成功 copy-out 的 sprite 恒携带非零 token。连续 load/destroy 的测试须验证每次成功 load 得到不同 token、旧 snapshot 的 token 无法匹配新 asset。
- **token 耗尽路径的测试 seam（阻塞项 1/2/18 定案）**：计数器耗尽路径必须在无窗口测试中可断言。机制与构建隔离固定如下：
  - **全部测试 seam 的声明集中放单个私有头 `engine/src/test_seams.h`**（阻塞项 2/5/6/17 一致性）：token 注入（`tg_scene_asset_test_seed_token`/`tg_scene_asset_test_reset_token`，定义于 `scene_asset.c`）、render 三段统计（`tg_render_test_stats`，定义于 `render.c`）、watcher 分类（`tg_watcher_test_classify`，定义于 `hotreload.c`）三个 seam 的**原型都以 `#ifdef TROGUE_TEST_SEAMS` 整体保护放在 `test_seams.h`**；`scene_asset_internal.h` **只**保留 `struct TgSceneAsset`/`TgSceneCandidate` 私有布局与 candidate 函数声明，**不再提供任何 seam 原型**。`test_seams.h` 不进公共 include、不安装，**只允许被定义方（`scene_asset.c`/`render.c`/`hotreload.c`）与 `tools/path_smoke.c` include**（加入 grep 门禁）。**语义定案（阻塞项 1）**：`seed_token(value)` 直接把 counter 设为 `value`，**允许 `value ∈ [0, UINT64_MAX]`（含 0 与 UINT64_MAX）**，不做钳制——测试用它构造任意初态；`reset_token()` 把 counter 置 0（等价 `seed_token(0)`）。
  - **构建隔离**：`engine/CMakeLists.txt` 始终构建生产静态库 `trogue_engine`（**不带** `TROGUE_TEST_SEAMS`，不含任何测试符号）。当 **`TROGUE_BUILD_CONSUMER_SMOKES AND BUILD_TESTING` 都为 ON** 时（与 tools 加入条件一致，见 §E），额外以同一源文件列表构建 `trogue_engine_test`（`add_library(trogue_engine_test STATIC ${ENGINE_SOURCES})` + `target_compile_definitions(... PRIVATE TROGUE_TEST_SEAMS=1)`）。**只有 `trogue_engine_test` 含 seam 符号；`game` 与生产 `trogue_engine` 消费方永不链接 `trogue_engine_test`**。Debug/Release 的 consumer-smoke 配置都按此构建两个库；关闭配置（option OFF 或 BUILD_TESTING=OFF）只产 `trogue_engine`。
  - smoke 链接分工固定：`tools/path_smoke`（需 token seam 与 path/限额断言）链接 `trogue_engine_test`；`trogue_oop_client_smoke`/`trogue_ecs_client_smoke` 为证明真实使用者可链接，链接生产 `trogue_engine`（不测 seam）。
  - **符号验证（`nm`）**：见「验证」节给出的精确过滤命令。
  - 断言序列固定（path_smoke 的 Debug/Release 构建都执行，seam 只在 `trogue_engine_test`，与 `TROGUE_DEBUG` 无关）：
    1. `reset_token()` 后 load 成功且 token = 1（验证归 0 语义）；
    2. `seed_token(UINT64_MAX-1)` → load 成功且 token = `UINT64_MAX` → 再次 load 返回 NULL 且 `tg_scene_last_error()` 非空，**随后 `seed_token(0)` 等价 reset 验证 counter 未被失败路径改写**（失败不改 counter：再 seed `UINT64_MAX-1` 后 load 仍得 `UINT64_MAX`）；
    3. `seed_token(UINT64_MAX)` → load 直接失败（第 1 步路径），失败后 counter 仍为 `UINT64_MAX`（再 load 依旧失败）→ `reset_token()` 后 load 成功恢复。
- asset 拥有自己的 tile 数组、descriptor 快照数据和场景 tileset/GPU 资源；调用方只能通过 copy/info/query API 读取，不能释放、替换或写入内部资源。
- descriptor、name/path 返回指针以及 `TgSceneSpriteDesc.asset_token/tileset_index` 都只在所属 asset 存活期间有效。sprite draw 必须在同一 asset 存活期间同步完成；归属校验由 engine 内部完成——`tg_render_scene_sprite` 实现中经私有 accessor 取出 asset token 与 snapshot 中的 token 值比较（**公共头不出现 `asset->...` 字段访问**；snapshot 的 token 是调用方可改的值字段，改坏只会 draw 失败，见 2.1 定案），token 不匹配直接失败。不能保存旧 asset 的裸指针或把旧 index 绑定到新 asset。
- game 热重载必须 `load candidate → 复制/校验绑定 → 成功后交换自己的 asset 指针 → 销毁旧 asset`。任何导入失败都保留旧 asset 和旧 game state；engine 没有 reload、rollback 或 player 保留 API。
- asset destroy 必须释放所有层 tile 数组、场景 tileset/GPU 资源、内部 descriptor 数据与 payload deep-copy 引用；不释放 game 的对象或全局独立贴图缓存。
- `TgTileset` 不进入公共头（见 2.1）；场景使用的 tileset 由 asset 私有持有并管理，不能通过公共字段访问。M5A 不提供让 game 修改 scene-owned tileset 的 API。
- engine 私有实现共享的资产布局（`struct TgSceneAsset`、私有 tileset/layer 结构）放在 `engine/src/scene_asset_internal.h`，供 `scene_asset.c`/`render.c` 使用，不安装、不进公共 include。
- 运行时无锁、单线程；asset load/destroy、render 和 query 不可并发调用，render 调用期间 asset 必须保持存活并且拥有有效 raylib 图形上下文（纯 palette/bare 的解析可不加载 GPU 纹理）。

bare scene 的 tile 宽高输出为 `0,0`；非 bare 场景输出 `[1,256]` 的尺寸。缺少 entity `w/h` 时，非 bare 使用场景 tile 尺寸，bare 使用 schema 固定默认值 `16x16`（兼容现有 bare 产物风格；不依赖 `soldier_animated_sprite_2d.json` 是否在 M5A 校验范围内）；显式 `w/h` 必须满足校验清单，不依赖某个“世界”默认值。

### 3. 场景 schema 校验清单

`tg_scene_asset_load()` 是原子 candidate loader。解析任何字段失败都销毁 candidate 并返回 NULL；不得通过 `snprintf`、浮点到整数转换或数组分配静默截断/溢出。

所有固定缓冲区复制和路径拼接统一经过 engine 私有 checked-copy helpers。helper 实现放独立私有模块 `engine/src/path.c`（声明放 `engine/src/path.h`，均不进公共 include，CMake source list 列出）：

```c
// engine/src/path.h（engine 私有）
bool tg_copy_json_string(char *dst, size_t cap,
                         const json_t *value, bool allow_empty);
bool tg_join_assets_path(char *dst, size_t cap,
                         const char *relative_path);
```

helper 契约（阻塞项 9 的精确化）：
- `tg_copy_json_string`：`dst==NULL` 或 `cap==0` 直接失败返回 false；JSON string 必须无 embedded NUL，UTF-8 字节长度 `< cap`（不含终止 NUL）；失败不写 `dst` 任何字节；`allow_empty` 控制空串是否合法。
- `tg_join_assets_path`：输入必须是规范化 assets-relative path——拒绝绝对路径、驱动器前缀、空组件、`.`/`..`、`/` 开头与 embedded NUL；只接受 `/` 分隔符。输出固定为完整运行时 filesystem 路径 `assets/<relative>`（如 `"tilesets/floor.json"` → `"assets/tilesets/floor.json"`），长度 `< cap` 且用 checked `size_t` 加法；`dst==NULL`/`cap==0`/任何失败均返回 false 且不写 `dst`。
- **基准路径契约（统一）**：schema 内所有资源引用（scene tileset `path`、独立贴图 `texture`、animation texture）统一经 `tg_join_assets_path()` 解析为 `assets/` 下的进程 CWD 相对路径；render 的独立贴图路径解析与 asset loader 使用同一 helper，不再各写各的。`tg_scene_asset_load(path)` 的入参是**外部 scene 文件的 filesystem path**（相对进程 CWD，如 `assets/scenes/demo.json`），不属于 assets-relative schema 引用：它仍须非空、无 embedded NUL、长度 `< TROGUE_PATH_MAX`，且拒绝绝对路径/目录穿越组件，过长直接失败不截断（见 3.1）。
- 除常量默认值外，engine 的 `scene_asset.c`/`path.c`/`render.c`/`hotreload.c` 禁止直接对 schema/path/name/id/type 使用 `snprintf`；通过 grep/代码评审门禁检查（`tileset.c` 已删除，私有 tileset loader 并入 `scene_asset.c`，见「预期文件变更/最终文件布局」）。

#### 3.1 字符串和路径

- 必须字符串的字段若存在则必须是 JSON string；缺省字段才使用文档定义的默认值。
- 所有复制到固定缓冲区的字符串都拒绝 embedded NUL、空字符串（允许缺省的字段除外）和长度 `>=` 对应容量；`id/type/name/path/texture` 不得截断。
- `id` 必填且非空；`type` 缺省为 `"unknown"`，显式提供时必须为非空合法字符串；asset 内 id 严格唯一。
- scene tileset `path`、独立 sprite `texture`、animation texture 路径必须是相对 `assets/` 的规范化路径：拒绝绝对路径、驱动器前缀、空组件、`.`/`..` 穿越和嵌入 NUL；只使用 `/` 分隔，并统一由 `tg_join_assets_path()` 检查。watcher 输出的 basename 不经过 `tg_join_assets_path`（watcher 只做裸 basename 后缀/组件检查，game 在自己固定目录前缀下拼接，见 §7 分工）。
- **资源解析时机二分定案（阻塞项 19）**：asset load 阶段按两类区分——**tileset JSON**（`.json` 元数据：tile 尺寸/count/name/引用一致性）在 load 阶段**读取并解析**（图集模式 tile 值域校验需要 count，tileset tile 尺寸一致性需要其 JSON），但**禁止读取/加载任何纹理文件**（`.png` 等 GPU 资源）与独立贴图文件——贴图路径只做字符串校验，文件存在性只在 render 懒加载阶段处理（见 §5 失败哨兵）。独立贴图 `texture`/animation texture 路径同理只校验字符串。这保证无窗口 consumer smoke 加载含图集/贴图引用的 fixture 时**除 tileset JSON 外零文件依赖**（consumer bare fixture 不含 tilesets，零依赖）。
- **`tg_scene_asset_load(path)` 入参 grammar 定案（阻塞项 15）**：`path` 是**外部 scene 文件的进程 CWD 相对路径**，与 schema assets-relative 引用是两套语法，各自独立校验：
  - `path` 必须非空、无 embedded NUL、UTF-8 长度 `< TROGUE_PATH_MAX`；
  - **lexical grammar**：只允许 `/` 作分隔；每个组件非空且不得为 `.` 或 `..`；不得以 `/` 开头（拒绝绝对路径）；不得含 drive 前缀（`X:`/`//`）；**不得出现重复分隔符 `//`**；以 `/` 结尾拒绝；
  - 不解析/规范化符号链接或真实文件系统路径，只做上述词法校验 + 打开失败时返回 NULL 并记录错误；
  - 过长直接失败不截断。
  watcher 一侧的路径 = game 固定前缀 `TROGUE_SCENE_DIR`（`assets/scenes`）+ `/` + 已复核的 basename，**天然满足同一 grammar**（前缀与 basename 各自已通过词法检查），game 拼接后直接交给 `tg_scene_asset_load`。

#### 3.2 根、tilemap 和三态模式

**字段层级定案（阻塞项 5）**：`tilesets`、`palette`、`tile_width`、`tile_height`、`layers` 全部是 **`tilemap` 的子键**（与 demo/test 资产一致）；根上不存在这些键。

- 根必须 object，`format` 必须为 `"tro-scene"`，`version` 必须为整数 `2`。
- **模式判定顺序固定（阻塞项 5）**：`scene_asset.c` 按以下顺序判定，先判模式再判尺寸，错误文本对应单一阶段：
  1. 根必须是 object、`tilemap` 必须存在且为 object；否则拒绝；
  2. 读 `tilemap.tilesets`/`tilemap.palette`：二者同现 → 拒绝；`tilesets` 存在 → 图集模式；否则 `palette` 存在 → palette 模式；两者都缺省 → 看 `tilemap.layers`——缺省或空数组 → **bare 模式**，非空 → 拒绝（「无 tilesets/palette 但有非空层」）；
  3. 尺寸判定在模式确定之后：bare 模式允许 `tile_width`/`tile_height` **同时缺省**并输出 `0,0`；非 bare（图集/palette）模式 `tile_width`/`tile_height` **必须同时存在**且为整数 `[1,256]`（只出现其中一个 → 拒绝；缺省 → 拒绝，错误文本为「非 bare 场景缺少 tile 尺寸」）。
- `tilemap.layers` 若存在必须是 array；palette/图集模式需要 array（可为空）；bare 仅允许缺省或空 array。**缺省规范化定案（阻塞项 5）**：`tilemap.layers` 缺省与 `"layers":[]` 在 parser 内部规范化为**完全相同的结果**（零层），所有 accessor/query 输出一致，二者等价不可区分；bare 判定也把二者视为同一种合法输入。
- `tilesets` 存在时必须是 `1..TROGUE_MAX_SCENE_TILESETS` 个对象，name/path 合法且 name 唯一；每个 loaded tileset 的 tile 尺寸必须和场景尺寸相同（tileset JSON 的读取与解析时机见「预期文件变更/tileset 解析」）。
- `palette` 存在时必须是 `1..TROGUE_MAX_SCENE_PALETTE` 个合法颜色字符串；`tilesets` 与 `palette` 互斥，palette 空数组不作为合法模式。
- 图集模式每层必须有合法 `tileset` 引用；palette 模式每层不得有 `tileset` 字段；层数组最多 `TROGUE_MAX_SCENE_LAYERS` 项。
- 每层 name 缺省为 `"layer"`，显式 name 必须合法；width/height 必须整数 `[1,4096]`；`width*height` 使用 checked `size_t` 乘法并在分配前验证。
- origin 缺省 `[0,0]`；若存在必须是两个有限、可表示 `int` 且整数值的 number，禁止把小数静默截断。
- tiles 必须是恰好 `width*height` 的整数数组；`-1` 表示空，其余值必须落在本层 palette 或 tileset 的合法范围内。

#### 3.3 number、颜色、sprite 和可选 payload

- JSON number 必须 finite，且转换为 float 后仍 finite；坐标和尺寸的绝对值不超过 `FLT_MAX/4`，避免后续 AABB 加法溢出。
- entity `x/y` 必须 finite/float-safe，可为负数（坐标系仍是像素左上角）；显式 `w/h` 必须 `>0` 且不超过 `FLT_MAX/4`；缺省规则见 2.2。`z` 缺省 0，显式 number 必须 finite 且在 `int` 范围内，按现有 schema 的向零取整语义保存。
- `color` 缺省白色；若存在必须是合法 `#rrggbb` 或 `#rrggbbaa` 字符串，非法类型/内容拒绝，不再静默回退白色。
- `sprite` 若存在必须严格匹配图集形态 `{tileset:string,tile:integer}` 或独立贴图形态 `{texture:string,region?:[x,y,w,h],offset?:[x,y]}`；两种形态互斥；tileset 引用和 tile 范围必须合法。**对象键策略定案（阻塞项 4）**：① 图集形态只允许键 `tileset`/`tile`，出现 `region`/`offset`/`texture` 或任何未知键 → **拒绝载入**（不再沿用 AGENTS「写了被忽略」的历史措辞，该措辞在步骤 16 同步修改）；② 独立贴图形态只允许键 `texture`/`region`/`offset`，未知键 → 拒绝；③ `region`/`offset` 缺省合法。**region/offset 数值规则逐字段定案**：region 的 x/y 必须有限且 `>=0`（贴图内矩形不允许负起点），w/h 必须有限且 `>0`；offset 的 x/y 允许任意有限 float（**可为负**，用于锚点/居中偏移）；所有值先按 float 检查（JSON number finite 且 float-safe，绝对值 ≤ `FLT_MAX/4`），再**直接赋值**给 raylib `Rectangle`/`Vector2`（raylib 这些类型字段即 float，无截断、无整数转换、无 UB）。region 缺省 = 整图，在 render 懒加载取得贴图尺寸后补齐；缺省语义不改变校验。
- `props` 若存在必须为 object；asset 在生命周期内持有其 immutable jansson payload；M5A 不提供 payload 访问 API，不将其映射为 engine/game 运行时对象，asset destroy 时统一 decref。
- **payload 持有与释放（props 与 animations 统一规则）**：校验通过后对 `props`/`animations` 的 JSON 值各做一次 `json_deep_copy()`，副本挂到私有 asset 的每-descriptor payload 持有点（每类一个 `json_t*`，无则 NULL）；asset destroy 时对每个非 NULL payload 恰好一次 `json_decref`。`json_deep_copy` 保证 payload 与 scene root 不再共享子节点，descriptor copy-out 永不接触 payload 内部指针。
- **payload ownership state machine（阻塞项 13/9/3 定案）**：`scene_asset.c` 的 load 流程围绕**唯一一个 candidate 清理函数** `static void tg_scene_asset_candidate_destroy(TgSceneCandidate *cand)` 与**唯一一个转移函数** `static void tg_scene_asset_candidate_transfer(TgSceneCandidate *cand, TgSceneAsset *asset)` 组织；**任何失败出口只允许调 candidate_destroy**，禁止任何分支直接对 root 或子资源 `json_decref`。单一所有权链固定为：
  1. `json_load_file` 得到 scene root，**root 是唯一 owner** 直到 load 成功返回或失败路径结束；
  2. 每解析一个 entity 并校验其 `props`/`animations` 通过后，立即 `json_deep_copy()` 取副本；`json_deep_copy` 返回 NULL 一律视为**分配失败**（payload 为空对象/空数组时同样会返回非 NULL 的合法 jansson 对象，不依赖类型判定）→ 失败路径；
  3. 副本挂到 candidate 的该 descriptor payload 持有点（candidate 独立于 root，不含 borrowed 指针——**任何阶段都不保存指向 root 子节点的裸 `json_t*`**，只保存 deep-copy 副本或全 NULL）；
  4. **descriptor 数组、layer/tile/tileset 等所有动态资源在 candidate 内累积**，candidate 结构对每个可释放对象都持有「指针 + count/capacity + 所有权标志」三元组（见下 transfer 契约）；descriptor 数组扩容、tileset 数组、layer 资源任一分配/校验失败 → 失败路径；
  5. **`candidate_destroy` 契约**：对 candidate 内**每个仍持所有权标志**的对象按固定顺序释放——先逐个 decref 每个 descriptor 的非 NULL payload → 释放 descriptor 数组 → 释放各层 tile 数组与私有 tileset 资源 → 释放 candidate 结构自身；已转移（所有权标志为 0）的字段无条件跳过。**root 不在 candidate_destroy 内释放**。
  6. **`candidate_transfer` 契约（阻塞项 3 定案）**：成功路径调用它把所有权从 candidate 移交 asset——对**每一个**可转移对象执行「指针赋值给 asset + candidate 该字段置 NULL + count/capacity 归 0 + 所有权标志清 0」；**非指针字段（count/capacity/标志）同样必须显式清零**，不允许只置 NULL 指针。转移后 `candidate_destroy(cand)` 对 candidate 是**无条件 no-op**（所有权标志全 0）。load 函数两个出口：成功出口 = `transfer` → `candidate_destroy`（no-op）→ root 一次 decref → 返回 asset；失败出口 = `candidate_destroy` → root 一次 decref → 返回 NULL。**root 的 decref 只能出现在 load 函数本身，每路径恰好一次；payload 的 decref 只发生在 candidate_destroy（失败路径、对象仍属 candidate）或 asset destroy（成功转移后）。**
  该状态机在步骤 6 实现时以注释落地；path_smoke 在成功 load 后调用 `tg_scene_asset_destroy(asset)` 并经 seam 断言（若提供）或 valgrind/ASan 逐阶段释放检查验证无 double-free/漏释（见「验证」）。
- **payload 资源上限与计量算法（阻塞项 2 固定）**：`props` 是任意 object，为防恶意 JSON 不可控占用，校验时对 payload 施加统一限额：序列化字节数 ≤ `TROGUE_MAX_PAYLOAD_BYTES`、嵌套深度 ≤ `TROGUE_MAX_JSON_DEPTH`、键数 ≤ `TROGUE_MAX_PAYLOAD_KEYS`、字符串键/值 UTF-8 字节长度 < `TROGUE_PATH_MAX`；超限拒绝载入。计量算法固定如下，避免实现歧义：
  - **序列化字节数**：以 `json_dumpb(payload, NULL, 0, JSON_COMPACT | JSON_ENSURE_ASCII)` 返回值（不含终止 NUL 的紧凑 ASCII 编码长度）计量；固定 `JSON_COMPACT | JSON_ENSURE_ASCII` 组合，不使用默认带空白格式，保证字节数与深度遍历一致且确定性。
  - **嵌套深度递归规则**：根对象深度记 1；每进入一层对象或数组深度 +1（数组元素计入）；深度超过 `TROGUE_MAX_JSON_DEPTH` 即拒绝。实现用显式栈或递归都行，但深度语义固定为「根=1，容器每层 +1」。
  - **键数规则**：`TROGUE_MAX_PAYLOAD_KEYS` 统计 payload 内**所有对象的键总数**（含根与嵌套对象，数组不计键），累计超过即拒绝。
  - **共享节点**：jansson `json_deep_copy` 后 payload 内不存在跨子树共享节点（deep copy 打平）；计量遍历按树形结构各访问一次，不做环检测（JSON 无环）。若后续校验改用引用计数复用节点，须在代码注释中说明并按唯一路径重新计量，不改变公共限额语义。
  - 以上计量在 load 校验阶段对每个 descriptor 的 `props`/`animations` 各执行一次；超限即整次 load 失败返回 NULL。动画结构限额与通用限额的常量命名见「payload 限额作用域」。
- `animations` 若存在必须是 v2.1 object（结构校验见下）；M5A 校验并保留 payload（规则同上），但不播放、不复制进 `TgSceneEntityDesc` 或 game 运行时对象；payload 不通过公共 API 暴露。
- **animations 结构规则与空值边界（阻塞项 4/14 定案）**：`animations` 对象只允许键 `textures`/`animations`，未知键 → 拒绝。`textures` 必须存在且为 array（可为空；空 texture 表时 `animations` 必须也为空，否则帧引用必然越界被拒）；`animations` 必须存在且为 array（可为空）；每动画对象只允许键 `name`/`fps`/`loop`/`frames`（未知键拒绝）；动画 name 非空字符串且数组内唯一，fps 必须有限且 `>0`，loop 必须 bool，frames 必须为 array（可为空）；每帧对象只允许键 `texture`/`region`/`offset`（未知键拒绝），`texture` 必须是非负整数且在 `textures` 索引范围内；帧的 `region`/`offset` 完全复用 sprite 的逐字段规则（region x/y `>=0`、w/h `>0`、offset 可负、有限、直接赋值），region 缺省 = 整图。
- **props/animations 的缺省、null 与空对象定案（阻塞项 14）**：两者均为**可选字段**：字段缺省 = 无 payload；存在但类型不是 object → 拒绝载入（含 `null`——`props:null`/`animations:null` 一律拒绝，与严格校验方向一致，不把 null 当缺省）。`props:{}`（空 object）→ 接受，payload 为空对象；`animations:{}`（缺 `textures`/`animations` 键）→ 拒绝（结构不完整）。上述接受/拒绝样例全部列入 `tools/path_smoke` 的 schema 拒绝/接受断言。
- **payload 限额作用域与常量命名（阻塞项 8/3 定案）**：限额分三级，**每个常量承担且只承担一种语义**（阻塞项 3），全部在 `config.h` 定义，超限整次 load 失败：
  - **per-payload（每个 descriptor 的 props 或 animations 各自计量）**：序列化字节 ≤ `TROGUE_MAX_PAYLOAD_BYTES`、嵌套深度 ≤ `TROGUE_MAX_JSON_DEPTH`、对象键总数 ≤ `TROGUE_MAX_PAYLOAD_KEYS`、字符串键/值 UTF-8 长度 < `TROGUE_PATH_MAX`（计量算法见上）。
  - **per-entity animations 结构（每个 entity 的 animations payload 内部）**：`textures` 数组长度 ≤ `TROGUE_MAX_ANIMATION_TEXTURES_PER_ENTITY`；动画条目数 ≤ `TROGUE_MAX_ANIMATIONS_PER_ENTITY`；**每个动画**的 frames 长度 ≤ `TROGUE_MAX_ANIMATION_FRAMES_PER_ANIMATION`。三者都仅作用于单个 entity 自己的 animations。
  - **整份 asset 累计预算（防「大量实体各达上限致总量不可控」）**：所有 descriptor 的 payload 序列化字节总和 ≤ `TROGUE_MAX_ASSET_PAYLOAD_BYTES`；**所有 entity 所有动画的 frames 数总和** ≤ `TROGUE_MAX_ASSET_ANIMATION_FRAMES`（阻塞项 3：该 asset 级常量不再与 per-animation 常量重名）。
  - 比较关系统一为「计数 `>` 上限即拒绝」（等于上限合法）；每级超限的 smoke 用例（per-payload、per-animation、per-entity、asset 累计）分别列入 `tools/path_smoke`。
  - 计量方式统一：字节用 `json_dumpb` 紧凑编码；深度/键数按 §3.3 遍历规则；asset 累计在 load 全部完成后、交付前做最后检查，任一超限即整次失败。
- 所有实体数量不超过 `TROGUE_MAX_SCENE_DESCRIPTORS`；该限额是**资产 descriptor 限额**，不是 engine runtime entity pool。

### 4. tile-only 查询 API

将现有 `tg_world_*` 查询改为不依赖运行时实体的 scene API：

```c
typedef enum TgSceneQueryResult {
    TG_SCENE_QUERY_ERROR = -1,
    TG_SCENE_QUERY_CLEAR = 0,
    TG_SCENE_QUERY_SOLID = 1,
} TgSceneQueryResult;

typedef enum TgSceneTileResult {
    TG_SCENE_TILE_ERROR = -1,
    TG_SCENE_TILE_EMPTY = 0,
    TG_SCENE_TILE_OCCUPIED = 1,
} TgSceneTileResult;

TgSceneQueryResult tg_scene_is_solid_at(const TgSceneAsset *asset,
                                        float px, float py);
TgSceneQueryResult tg_scene_rect_hits_solid(const TgSceneAsset *asset,
                                            float x, float y,
                                            float rw, float rh);
TgSceneTileResult tg_scene_asset_tile_at(const TgSceneAsset *asset,
                                         int layer_index,
                                         float px, float py,
                                         int *out_value);
```

固定语义：

- 只检查 `solid=true` 的 tile layer 及其非空 tile；层矩形之外没有数据，不阻挡。实体 descriptor 的 `solid` 永远不参与 engine 查询。
- `TG_SCENE_QUERY_ERROR` 用于 NULL asset、非法/非 finite 坐标、非正尺寸、超出安全浮点范围、坏 layer index 等；`TG_SCENE_QUERY_CLEAR` 才表示合法查询且没有 solid tile。
- `tg_scene_asset_tile_at()` 对合法但层外/空格返回 EMPTY，并把 out_value 设为 -1；非法参数返回 ERROR 且不输出半成品值；占用格返回 OCCUPIED 和 tile id。
- **像素→tile 映射与区间规则（阻塞项 9 定案）**：统一为「世界像素坐标减层 origin 后向下取整」：`tx = floor((px - origin_x) / tile_w)`、`ty = floor((py - origin_y) / tile_h)`（负数输入同样 floor，如 `-0.5 → -1`；`px` 恰等于某 tile 右边界像素时属于右侧下一 tile——与半开区间一致）。所有减、除、floor 与 `int` 转换在计算前做范围验证，极大但 finite 的输入不得触发 UB；结果 tile 坐标越出 `[0,width)×[0,height)` 视为层外。**矩形查询采用半开区间** `[x, x+rw) × [y, y+rh)`：对覆盖范围内每个可能相交的 tile 判相交（AABB 半开相交：`max(x1,x2) < min(x1+rw1, x2+rw2)` 等价式），任一 solid 且非空的 tile 即返回 `TG_SCENE_QUERY_SOLID`（**短路：命中即停**）；整层扫描无命中才 `CLEAR`。**多 solid 层**：按层数组序逐层扫描，任一层的 solid occupied tile 命中即 `SOLID`（短路）；所有 solid 层都无命中才 `CLEAR`；没有任何 solid 层的 asset（palette 无 solid 层/bare）对任何合法输入返回 `CLEAR`。边界/负 origin/跨层重叠的具体断言列入「验证」。
- game 如需实体碰撞，必须在自己的 OOP/ECS 对象或组件系统中实现，并可把 descriptor 的 `solid` 复制为导入初值；这不改变 engine tile-only 契约。

### 5. 显式渲染 API

`render.h` 不再接受 `TgWorld`，不再隐式遍历实体池：

```c
bool tg_render_scene(const TgSceneAsset *asset, const Camera2D *cam);
bool tg_render_scene_sprite(const TgSceneAsset *asset,
                            const TgSceneSpriteDesc *sprite,
                            float x, float y, Color tint);
void tg_render_shutdown(void);
Color tg_color(const unsigned char rgba[4]);
```

- `tg_render_scene()` 只绘制 tile layers，层顺序是资产数组顺序，不绘制任何 entity descriptor；render 不调用 `BeginMode2D/EndMode2D`，调用方必须在同一个 `BeginMode2D(camera)` 区间内先后调用 scene 与 sprite draw，保证 camera/坐标上下文一致。
- **`tg_render_scene` 参数契约（阻塞项 14 定案）**：`asset` 必须非 NULL（NULL → false，每次记录 LOG_ERROR）；`cam` **允许 NULL**（engine 不读取 `cam`——它只要求调用方已进入 `BeginMode2D`，见上；`cam` 参数保留仅为表达「scene 绘制期待调用方 camera 上下文」，若实现不需要可弃用该参数并在头文件注明）。palette 层：每个非空 tile 用 `tg_color(palette[tile])` 绘制色块，`-1` 空 tile 跳过；图集层：非空 tile 用其 tileset 图集区域绘制，空跳过。bare 场景零层：合法调用返回 true（无绘制），不报错。**tile 尺寸为 `0,0` 的 asset 不进入 tile draw**（bare 的 tile 尺寸输出 0,0，其零层天然无 tile 可画；非 bare 场景 tile 尺寸已保证在 `[1,256]`）。`tg_render_scene` 同样走三段顺序（参数 → `IsWindowReady()` → 绘制）。
- `tg_render_scene_sprite()` 只根据调用方显式给出的、来自同一 asset 的 sprite 快照和位置绘制一个视觉对象；先验证 `asset_token`，再验证 index/tile/region；调用方决定调用次数、位置、排序、tint、动画帧和相机。
- `asset==NULL`、sprite 不属于该 asset、tileset index/tile 越界、坏 region、无有效图形上下文或贴图加载失败返回 false，并记录一次可诊断日志；不解引用悬空资源。
- atlas sprite 的 index 只在传入的 asset 存活期间有效；独立贴图路径一律经 `tg_join_assets_path()` 解析为进程 CWD 下 `assets/` 相对路径（与 asset loader 同一 helper，见第 3 节契约）。独立贴图缓存由 render 模块拥有，跨 asset reload 可复用，关闭窗口且所有绘制结束后调用 `tg_render_shutdown()`。
- 无 sprite 时由 game 决定是否调用 raylib 画色块；engine 不自动为 descriptor 画实体色块。
- descriptor 数组顺序不被解释为全局 y-sort 顺序；实体排序完全是 game policy。
- `tg_render_scene`/`tg_render_scene_sprite` 只能在主线程、`BeginDrawing()` 之后且同一调用方 `BeginMode2D()`/`EndMode2D()` 区间内调用；render 不管理 camera mode。asset swap/destroy 只能发生在 `EndDrawing()` 之后、下一帧 BeginDrawing 之前；asset 必须覆盖整个同步调用。
- **有效图形上下文的可操作判定与校验顺序（阻塞项 9/11/17 定案）**：engine 只以 raylib `IsWindowReady()` 判定窗口/图形上下文是否存在。**固定执行顺序为三段**（阻塞项 17）：① **纯 CPU 参数/归属校验**——NULL asset/sprite、sprite token 与 asset 不匹配、index/tile/region 越界、独立贴图路径字符串经 helper 校验（helper 是纯字符串函数，不触碰文件系统/GPU）；任一失败立即返回 false 并记录对应日志，**不调用 `IsWindowReady()`、不读写纹理缓存与失败哨兵**。② **一次 `IsWindowReady()`**——返回 false 即整体返回 false（不加载资源、无绘制副作用、不记日志、**不读写纹理缓存/哨兵**——窗口关闭属正常退出路径）。③ **纹理缓存查找/加载**——只有前两段通过后才查询缓存或懒加载贴图；贴图不存在/加载失败按哨兵规则记录并返回 false。palette/bare 场景理论上不需图集纹理，但任何绘制调用仍要求 `IsWindowReady()` 为真；无窗口 consumer smoke 因此不调用 render API。`BeginDrawing`/`BeginMode2D` 的当前模式状态 raylib 公共 API 无法完整查询，engine 不尝试检测，该前置条件由调用方契约保证并写进 render.h 头注释。
- **失败日志哨兵规则（阻塞项 9 固定）**：分三类：① 段①参数/归属/token/region 校验失败——调用方 bug，**每次调用记录一次** `LOG_ERROR`（纯 CPU，不影响缓存/哨兵）；② 段③贴图加载失败——**每路径一次**哨兵（同一路径在同一 asset 生命周期内只记一次日志；哨兵在成功加载该路径时清除、随 render 缓存清理或 asset 生命周期结束重置）；③ `IsWindowReady()` 为 false——不记日志。重复调用断言：同一坏路径连续 draw 多次只产生一条加载错误日志。render API 不解引用悬空资源。
- **render 三段顺序的测试 seam（阻塞项 6 定案）**：为让「窗口 false 不触碰缓存/哨兵」「参数失败不调 IsWindowReady」可自动断言，在 `TROGUE_TEST_SEAMS` 下 `render.c` 暴露累计统计 `void tg_render_test_stats(int *out_param_fail, int *out_window_checks, int *out_texture_attempts)`（原型见 `engine/src/test_seams.h`，见 2.2；分别计：段①失败次数、段② `IsWindowReady()` 调用次数、段③纹理缓存查找/加载尝试次数；测试进程内单调累计，不随调用清零）。`tools/path_smoke`（链接 `trogue_engine_test`）无窗口断言：① 传 NULL asset / token 不匹配 → 返回 false 且 `param_fail` 增、`window_checks` 与 `texture_attempts` **不变**（段①先于窗口检查失败）；② 用合法 fixture 加载 asset + 合法 sprite snapshot，未建窗口 → 返回 false 且 `window_checks` 增 1、`texture_attempts == 0`（段②通过后因窗口未 ready 返回，段③未执行、不触碰纹理缓存）。**段③的贴图加载失败「每路径一次」日志需窗口**：带窗口（game/Debug demo）人工验证并记录验收证据；无窗口 smoke 只断言段③未被执行。
- `tg_render_scene_sprite` 的 token 校验是唯一 asset 归属校验。**重绑 API 不存在（阻塞项 11 定案）**：M5A **不提供**任何「重绑/改 token」API；game 在候选交换后必须对**新** asset 调用 `tg_scene_asset_copy_entity()` **重新取得完整 snapshot**，禁止只改旧 snapshot 的 `asset_token` 或 `tileset_index` 后复用（旧 snapshot 的 token 是旧 asset 的，与新 asset 不匹配必然 draw 失败；只改 index 不换 token 同样失败）。验证加断言：旧 asset 的 snapshot 仅修改 token 或仅修改 index 后对新 asset draw 仍失败；新 asset 的 copy-out snapshot 才可用。

### 6. IPC 是无 world 的传输层与 callback 分发层

`ipc.h` 的启动 API 改为不接收 world/scene：

```c
typedef struct TgIpc TgIpc;

typedef enum TgIpcGameResult {
    TG_IPC_GAME_NOT_HANDLED = 0,
    TG_IPC_GAME_HANDLED = 1,
    TG_IPC_GAME_ERROR = -1,
} TgIpcGameResult;

typedef TgIpcGameResult (*TgIpcGameHandler)(
    const char *cmd,
    const json_t *request,
    json_t **data,
    char *error,
    size_t error_cap,
    void *userdata);

TgIpc *tg_ipc_start(int port);
void tg_ipc_destroy(TgIpc *ipc);
void tg_ipc_poll(TgIpc *ipc);
bool tg_ipc_set_handler(TgIpc *ipc, TgIpcGameHandler handler, void *userdata);
void tg_ipc_clear_handler(TgIpc *ipc);
```

callback 和传输契约固定为：

- engine 只保存监听 fd、client 行缓冲、handler/userdata 和协议状态；不保存 scene/world、实体、reload、quit 或 screenshot 状态。
- **连接状态机（阻塞项 3/12 定案）**：监听 fd 设为非阻塞。每次 `tg_ipc_poll()` 的执行顺序固定为：**① accept 阶段**：循环 accept 直到 `EAGAIN/EWOULDBLOCK`（本轮不再有可接受连接）；对每个新 fd 只做「slot 检查 + 分配 + hello write_all」，**不得 read 该 fd**（数据留在内核缓冲，下一 poll 才首次 recv）。accept 循环中：`EINTR` → 重试；其他错误 → 记录并**永久关闭监听**（后续 poll 不再 accept，已有 active 连接继续服务直到各自关闭）。**② 轮询阶段**：按固定 slot 序轮询已有 active client（见单连接状态）。**单连接状态**：
  1. **新 accept 的连接先检查 slot 占用**：active slot 已满（上限 8）→ 立即 close 新 fd，不进入任何 slot、不发 hello、不排队；未满 → 分配 slot，进入 hello 待发状态。
  2. **hello 发送**：`write_all` 失败/短写/断线 → 立即 close 并释放 slot，**该连接不进入 active**，继续 accept 下一个（不因单个 hello 失败中止本轮 accept）。
  3. **active 连接**：本 poll 内循环处理该连接的行缓冲直到无完整行可读：按行读取请求 → 解析 → 调 handler → 同步写响应 → 读下一行；同一次 recv 取得的多个请求行在同一 poll 内逐行按序处理完毕，不得因处理第一行而丢弃后续字节。**新连接的首行读取时序固定**（阻塞项 3）：新连接 hello 成功置 active 后，**本 poll 不再读取该连接的请求行**（包括 hello 期间已到达内核缓冲的数据）——其首行统一在**下一个 `tg_ipc_poll()`** 首次 recv。该规则保证任何新连接都不会在本 poll 内既发 hello 又发响应，使协议可观察行为唯一；测试据此断言「新连接建立后第一个 poll 只收到 hello，第二个 poll 才收到首个请求的响应」。新连接的 hello 与既有连接的轮询顺序不保证跨连接先后，但单连接内行序严格保持。
  4. 监听 fd 的 accept 错误已在 accept 阶段处理；client fd 的 recv/send `EAGAIN` → 留待下一 poll；`EINTR` → 重试；其他错误 → 关闭该 client 并释放 slot。
- **单行上限与逐字节状态机（阻塞项 4/6/7 定案）**：request 与 response 均以单行 JSON-lines 传输。**两种长度定义严格区分（阻塞项 4）**：① **线上字节计数** = 行首到 `\n` 之间收到的总字节数（**含 `\r`**），受 `≤ TROGUE_IPC_LINE_MAX`（=65536）限制，`\n` 本身不计；② **JSON payload 长度** = 行尾剥离 `\r` 后交给 parser 的字节数，恒 ≤ 线上计数。engine 发送时把 payload 序列化到 ≤65536 字节后追加 `\n`；**发送端只用 LF 行尾（不发送 `\r`）**。**接收状态机**：分帧层维护线上字节计数 `cnt`（0 起），逐字节处理：
  | `cnt` | 收到字节 | 动作 |
  |---|---|---|
  | `cnt < 65536` | 非 `\n` 且非 `\r` | `cnt += 1`，字节入缓冲 |
  | `cnt < 65535` | `\r` | `cnt += 1`，`\r` 入缓冲（占线上额度） |
  | `cnt == 65535` | `\r` | `cnt += 1`（=65536），`\r` 入缓冲 |
  | `cnt == 65536` | 非 `\n` | **超限**：关闭连接、丢弃缓冲、记日志、不发响应 |
  | 任意 | `\n` | 行结束：若缓冲末字节为 `\r` 先剥离（JSON payload 长度 = 线上计数 −1），再交 JSON 解析；`cnt` 清零 |
  - **边界语义（阻塞项 4）**：CRLF 行尾下，`\r` 占一个线上字节，因此**剥离后 JSON payload 最大可接受长度是 65535**（线上 65535 payload + `\r` + `\n`）；纯 LF 行尾下 JSON payload 最大可接受长度是 **65536**。**「可接受」= 分帧层不因长度关闭连接**，与业务解析结果无关（阻塞项 4：不把「分帧接受」表述为「合法请求」——解析层另有 JSON object/cmd 校验，见下）。**边界测试四例固定**，全部显式区分「分帧结果」与「解析结果」：
    ① 线上 65536 字节 + `\n`（剥离后 65536）：**分帧接受、不关闭**；解析结果取决于内容——测试构造「65535 个空格 + 合法 JSON object 前缀？」不可行，因此**用可构造的合法 JSON object 垫长到恰 65536**（如 `{"cmd":"ping","pad":"<65522 字符>"}` 序列化后恰 65536）→ 期望业务正常响应（`ping` 的 `{"ok":true,"data":{"pong":true,"version":1}}`）；若用任意非 JSON 字节填满则期望固定 `invalid request` 响应且**不关闭连接**；
    ② 线上 65537 字节无换行 → **分帧超限关闭**，无响应（无论内容）；
    ③ 线上 65535 payload + `\r` + `\n`（剥离 `\r` 后 JSON payload 65535）：**分帧接受**；同 ① 用合法 JSON object 垫到 65535 → 期望正常响应；用任意字节 → 期望 `invalid request` 且不关闭；
    ④ 线上 65536 payload 字节后接 `\r`（cnt 已 65536 再收到非 `\n`）→ **分帧超限关闭**。
    另设两条行尾常规用例：裸 `\n` 与 `\r\n` 结尾的 `{"cmd":"ping"}` 都正常响应。**分帧层断言只关心「是否因长度关闭」，解析层断言只关心响应包络**——测试脚本对每例分别记录两个期望，防止把「分帧接受但 invalid request」误判为失败。
- hello 文本本身是短固定字符串，不受业务超长影响；hello 发送同样遵守 `write_all`（成功=完整写出整条 hello，短写视为失败关闭，不进入 active）。
- request 必须是 JSON object 且 `cmd` 必须是非空 string；否则不调用 handler，返回统一 error 并继续处理该连接后续行。JSON parse 失败（非法 JSON、非 object）也返回同一固定错误包络（`{"ok":false,"error":"invalid request"}`），**不关闭连接**（超长行才关闭），继续读后续行。
- 响应 send 使用 `write_all`，任意短写/断线/非 EINTR 失败立即关闭该 client；不会影响其他 slot。
- **`ping` 处理流程与不可覆盖（阻塞项 5/15/20 定案）**：engine 对 `cmd` 的匹配逻辑**只做一次字符串比较 `cmd == "ping"`**（字节精确相等）；命中则 engine 直接处理——构造固定 data object `{"pong":true,"version":1}`（协议版本同 tro-ipc v1），**不调用 handler、handler 不可覆盖、无需注册 handler 即可工作**。不命中则**一律**走 handler 或「handler 未注册 → `command not handled`」路径；engine **没有任何业务命令 switch/else-if 链**（包括 help/status/list_entities/spawn/step/reload/quit/screenshot 一律不识别、不读取业务字段）。ping 响应与所有其他响应走**同一条**统一路径：包络构造 → `json_dumpb(JSON_COMPACT)` → 65536 上限检查 → `write_all` → 失败处置（见下），没有独立捷径。**handler 未注册时**：非 ping 的合法请求返回固定错误包络 `{"ok":false,"error":"command not handled"}`（与 NOT_HANDLED 同文），不关闭连接。命令归属表 `ping` 行标注「不可覆盖，无需 handler」。**负测试定案（阻塞项 20）**：handler-boundary smoke 注册一个「记录收到的每个 cmd 字符串」的 handler，逐一发送 help/status/list_entities/spawn/step/reload/quit/screenshot/任意未知 cmd，断言每个都被 handler 收到（或未注册时得到 `command not handled`），证明 engine 除 ping 外不实现任何命令语义；该断言与 grep「engine 源码无业务 cmd 字符串字面量 switch」共同构成禁令检查。Release 下 `tg_ipc_start` 返回 NULL，无连接即无 ping 可服务，符号仍可链接。
- 调 callback 前 engine 将 `*data=NULL`；`error_cap>0` 时将 `error[0]='\0'`。request 只在 callback 调用期间有效，callback 不得保存 request/data 或递归 poll。
- `HANDLED` 必须留下非 NULL JSON object；NULL、非 object 或未知返回值都由 engine 清理 data 后返回固定错误包络。callback 返回的 JSON object 在 engine 包络发送成功/失败后由 engine decref。
- `ERROR` 时 engine 清理非 NULL data，并使用 callback 同步写入且保证 NUL 终止的 error；空文本或 `error_cap==0` 使用固定 `game command failed`。
- `NOT_HANDLED` 时 engine 清理错误留下的 data，忽略 error buffer，返回固定 `command not handled`；engine 不根据 cmd 自行实现业务。
- engine 统一发送成功 `{\"ok\":true,\"data\":...}` 或失败 `{\"ok\":false,\"error\":\"...\"}`；callback 返回的 data 所有权在 engine 包络后由 engine decref。
- **序列化/包络构造失败处置——统一 serializer 状态机（阻塞项 7/10 定案）**：engine 内所有出站响应（ping、handler 成功、各类固定错误）共用**一个 serializer 状态机**：
  1. 尝试构造目标包络（成功 `{"ok":true,"data":...}` 或失败 `{"ok":false,"error":"..."}`），用 `json_dumpb(..., JSON_COMPACT)` 序列化到内存缓冲并检查长度 ≤ 65536；
  2. 若第 1 步任何环节失败（包络构造失败、序列化失败、超限）→ **不发送任何字节**，先 decref 相关 data/包络引用，再尝试构造固定兜底包络 `{"ok":false,"error":"internal error"}` 并序列化；
  3. 若兜底包络仍构造/序列化失败或同样超限 → 记录 `LOG_ERROR`，**关闭该连接且不发送任何字节**，释放 slot；同一连接未发完的后续请求行不再处理。
  任何出站响应都不允许发出半行 JSON。error 文本来自 callback 时先经长度检查，超长截断为 `<= error_cap-1` 的 NUL 终止字符串（截断本身不构成失败）；ping 的固定 data 构造失败**不允许降级为其他 data**，直接走第 2/3 步。测试覆盖：HANDLED 返回超长 data（>65536 字节）→ 连接被关闭且无部分响应；ERROR 返回超长 error → 响应仍为合法固定包络。
- handler 注册/清除必须在主循环外的确定时序完成；NULL ipc/NULL handler 返回 false（clear NULL 安全 no-op）。Release 下保留全部 typedef、opaque 类型和符号：`start` 返回 NULL，poll/set/clear/destroy 安全 no-op/false，不保存 userdata、不启动 TCP。
- `TgIpc` 不提供 `tg_ipc_take_screenshot()` 或 `tg_ipc_quit_requested()`；截图路径和退出标志由 game userdata 管理，game 主循环在 poll 后消费。handler 清除/销毁只能发生在 poll 返回后；不得在 callback 内清除自身或销毁 `TgIpc`。
- screenshot path 是可选相对/绝对 filesystem path 的 demo policy：game 必须 checked-copy、拒绝空字符串、长度超限和 NUL；本阶段 demo 只接受绝对路径或当前 CWD 下安全相对路径，拒绝目录穿越。每个 screenshot 请求只保留一个 pending slot：同一帧后到请求覆盖前一个并返回其最终排队路径；响应只表示 queued，下一次完整绘制后 game 消费，ExportImage 失败由 game 日志记录且不改 engine 状态。
- quit handler 只设置 game flag；engine 在当前 poll 中完成当前行响应发送并继续处理已读的前序行，poll 返回后 game 主循环退出。不会在 callback 内关闭 IPC 或递归 poll。

### 7. watcher 只报告变化

`TgWatcher` 继续是文件变化通知器，但不再与 scene reload 或任何运行时对象绑定：

- `tg_watcher_poll(w, name, cap)` 参数优先级固定：`w==NULL` 返回 0；若 `name==NULL` 或 `cap<=0` 返回 -1 且**不消费/不清除 pending**（参数错误路径与读取错误路径分开：参数错误不动 pending，事件读取错误消费该事件并丢弃）。正常返回只报告监听目录内的 `.json` basename。返回 `1` 表示一条去抖后的事件，`0` 表示无事件，`-1` 表示参数错误、队列溢出或读取错误。basename 长度 `>= TROGUE_NAME_MAX`、embedded NUL、含 `/`、`.`/`..` 组件均视为错误并丢弃该事件，不截断。**容量契约定案（阻塞项 13）**：`cap` **包含终止 NUL**；成功写出要求 `n + 1 <= cap` 且输出 buffer 必定 NUL 终止；`cap == n` → 返回 -1、不写出、**不消费**事件；`cap == n + 1` → 成功、消费并写出；`demo.json`（n=9）与长度恰 `TROGUE_NAME_MAX-1` 的 basename 分别做 `cap = n` / `cap = n+1` 边界断言。Release/no-op 实现对 NULL/name/cap 同样返回确定 0/-1，不写入 buffer。
- **`.json` 后缀判定与 watcher/path helper 分工（阻塞项 7/16 定案）**：watcher **只在事件 basename 上**做后缀判定，不拼接、不调用 `tg_join_assets_path`。后缀匹配算法固定：目录事件（`IN_ISDIR`）一律忽略不报告；文件事件名有效长度 `n >= 6` 且末尾 5 字节精确等于 ASCII 小写 `.json`（大小写不折叠），否则**不报告**。接受样例：`demo.json`、`a.json`；拒绝样例：`demo.JSON`、`demo.json.tmp`、`x.json.backup`、`.json`（长度 5 < 6）、无扩展名文件、目录。watcher 输出的是裸 basename（无 `assets/scenes/` 前缀）；game 只在**自己固定的监听目录前缀**（`TROGUE_SCENE_DIR`）下拼接该 basename，拼接前对 basename 用 checked copy 复核「无 `/`、无 `.`/`..`、长度 < `TROGUE_NAME_MAX`」。`tg_join_assets_path` **仅用于 schema 内 assets-relative 引用**（scene tileset path / sprite texture / animation texture），不用于 watcher 路径。
- **embedded NUL 的分支表（阻塞项 5/8 定案）**：inotify 事件名称区原始长度取自 `event->len`。**前置：`event->len == 0` → 直接视为坏事件丢弃（不扫描）**（阻塞项 8）。仅当 `event->len > 0` 时按以下分支表执行（以 `event->len` 为上界做 `strnlen(name, event_len)` 得 `n`）：

  | 条件 | 判定 | 动作 |
  |---|---|---|
  | `n < event_len` 且 `name[n] == '\0'` | 合法终止 | 终止 NUL 位于 `name[n]`；`n+1 .. event_len-1` 为内核 padding，**忽略** |
  | `n == event_len`（名称区无 `'\0'`） | 无终止 NUL | 坏事件 |
  | 有效长度 `n >= TROGUE_NAME_MAX` | 超长 | 坏事件 |
  | 有效名称含 `/` 或等于 `.`/`..` | 路径穿越 | 坏事件 |
  | 其余 | 合法 basename 候选 | 过 `.json` 后缀判定后经 checked-copy 输出 |

  坏事件一律**读取该事件、丢弃、不报告**。**UTF-8 不做校验**（阻塞项 5 定案）：Linux 文件名允许任意非 NUL 字节，watcher 把事件名当作**不透明字节序列**，不做 UTF-8 合法性判断；basename 只要满足「无中段 NUL、无 `/`、无 `.`/`..`、长度 < `TROGUE_NAME_MAX`」即可输出。四类测试样例固定：`event_len==0`、名称区含中段 NUL + padding、无终止 NUL（`n==event_len`）、合法终止 + padding。
- **watcher 测试 seam（阻塞项 5 定案）**：为使上述分支表与后缀/cap 判定可自动测试而无需真实 inotify 事件，`hotreload.c` 把「原始事件名称区 → basename 判定」实现为**无 fd 依赖的私有纯函数** `static int tg_watcher_classify_name(const char *name_zone, size_t zone_len, char *out, size_t cap)`（不受 `TROGUE_HOTRELOAD_ENABLED` 门控，Debug/Release 都编译；无系统调用）。返回约定：`1` = 合法 `.json` basename 且已写出（NUL 终止）；`0` = 事件合法但非 `.json`（不报告）；`-1` = 坏事件（`event_len==0`、无终止 NUL、超长、路径穿越、中段 NUL）；`-2` = 判定合法且为 `.json`，但 `cap` 不足以容纳 `n+1`（不写、不消费——seam 层专有返回值）。参数：`name_zone` 为 inotify 名称区起始、`zone_len` = `event->len`（含尾部终止 NUL/padding）；`n` = 判定得到的 basename 字节长度（不含终止 NUL）。
- **seam 与真 poll 的 cap 边界分工（阻塞项 1 定案）**：两类 API 对「cap 不足」用**不同但各自确定**的返回值，path_smoke 分别断言、不混用：
  - **seam**：`tg_watcher_test_classify(zone, zone_len, out, cap)`（`#ifdef TROGUE_TEST_SEAMS` 暴露，原型见 `engine/src/test_seams.h`）——合法 `.json` 且 `cap == n` 时返回 **`-2`** 且不写 `out`；`cap == n+1` 时返回 **`1`** 且写出 NUL 终止 basename；`cap` 更大的合法情况同 `1`；坏事件/非 `.json` 分别返回 `-1`/`0`（与 cap 无关）。
  - **真 poll 输出阶段**：`tg_watcher_poll(w, name, cap)` 在有 pending 且 `name/cap` 有效时——`cap == n` 返回 **`-1`**（不写出、不消费 pending）；`cap == n+1` 返回 **`1`**（写出 NUL 终止 basename、消费 pending）。此处 `n` 一律指 pending basename 的字节长度；`-1` 在 poll 上下文中同时承担「参数/容量错误」语义（§7 参数优先级：`name==NULL`/`cap<=0` 也返回 -1 且不消费）。**两种 API 的 `cap==n` 行为不同（seam=-2、poll=-1）是有意的分层**：seam 区分「坏事件(-1)」与「好名字但 buffer 小(-2)」以精确测分支表；poll 把容量不足与参数错误统一为 -1（对外契约只有 0/1/-1 三态）。path_smoke 对两者分别断言（见「验证」），测试样例统一以 basename 字节长度 `n` 构造。
- 真实现 `tg_watcher_poll`（仅 Debug、`TROGUE_HOTRELOAD_ENABLED` 下）从 fd 读 `struct inotify_event` 后，以**内部固定判定缓冲（容量 `TROGUE_NAME_MAX`，对合法名恒足够）**调用 `classify_name`：返回 `-1` → 读该事件丢弃并继续；`0` → 丢弃（不报告）；`1` → 该 basename 进入 150ms 防抖 pending；`-2` 在 poll 内部判定路径不可达（内部缓冲恒够），仅由 seam 直测。**输出阶段**按上一条 poll 契约执行。path_smoke 用**手工构造的字节数组**喂 `tg_watcher_test_classify` 断言全部分支表与 seam cap 边界（`event_len==0`、中段 NUL+padding、无终止 NUL、合法终止+padding、超长、`.`/`..`/`/`、大小写后缀、`cap==n` 返回 -2 不写、`cap==n+1` 返回 1 写出）——**Debug 与 Release 都可运行**（纯函数无门控）；`tg_watcher_poll` 的参数契约与输出阶段（`NULL`/`cap<=0` → 0/-1 不消费；有 pending 时 `cap==n` → -1 不消费、`cap==n+1` → 1 消费并写出）由 path_smoke 在 Debug 真实现与 Release 桩下分别断言（Release 桩无 fd，同样确定性；pending 构造经 Debug 内部路径注入或对 poll 契约只测参数优先级部分，见「验证」）。
- inotify 事件名不得包含 `/`、`..` 或 embedded NUL；game 仅拼接已知监听目录和该 basename，不接受 watcher 输出之外的路径。
- 150ms 防抖窗口内的多个事件合并为一个 pending basename；窗口尾沿补触发，不丢最后一次保存。队列溢出由 game 记录 warning，并通过显式 F5/IPC reload 重新建立 candidate。Release/no-op watcher 对 NULL/name/cap 参数同样返回确定的 0/-1 契约，不写入任何 buffer。
- game 只对当前 asset 的 basename 执行 reload，其余场景文件事件忽略或记录；F5、watcher、IPC `reload` 的合并、单帧节流、candidate 导入和状态交换全部属于 game coordinator。
- 失败的 asset load 不影响旧 asset；game candidate reconcile 失败也不得提前破坏旧 game state。

## game 兼容适配器（本阶段仅用于证明边界）

现有 `game/src/main.c` 改为定义自己的最小 `GameApp`/`GameObject` 数据，不再把 engine 类型当作游戏对象：

- `GameObject` 自己拥有运行时 id、type、position、footprint、color、sprite binding、active、scene binding 和 game collision policy；容量常量放在 `game/`，不放入 engine。
- 初始对象由 `TgSceneEntityDesc` **复制**导入。GameObject 不保存指向 descriptor、layer、asset 或 tileset 的裸指针；sprite 快照只在绑定对应 asset 存活时使用。
- game 可以选择把 descriptor `solid` 导入 `GameObject` 的碰撞标志，以保持演示效果，但这是 demo 的选择，不是 engine 行为；engine tile query 与 game object AABB query 分开。
- demo 继续实现 WASD/方向键、分轴移动、player 兼容位置保留、F5/watcher/IPC reload、相机和 HUD；这些逻辑全部在 `game/`。
- asset reload 按 candidate 流程执行：load 新 asset → 对新 asset 调用 `tg_scene_asset_copy_entity()` **重新复制**全部所需 descriptor 快照并构建临时 GameObject/binding 数组 → 全部成功后一次性交换 asset 与 game state → 再销毁旧 asset。失败时旧 asset、旧对象和旧位置全部保持。每个绑定的 sprite snapshot 必须携带**新 candidate asset 的 token**（无重绑 API，见 §5：旧 snapshot 不能改 token 复用）；交换时先完成新快照复制，旧 token 的 snapshot 全部失效后才 destroy 旧 asset。
- demo 的 player 位置保留策略只按 game 自己的 `id/type` policy 实现；engine 不知道 player，也不写回 asset descriptor。
- runtime spawn 的 id 冲突、实体查询、实体 AABB 碰撞、未知 type warning、reload 后是否保留位置都由 `GameApp` 明确实现，不写回 engine asset。
- `GameApp` 注册 IPC handler；截图请求只写入 `pending_screenshot_path`，下一帧绘制完成后由 game 用 `LoadImageFromScreen`/`ExportImage` 消费；quit 请求只设置 `quit_requested`，主循环在 poll 后检查。
- 这个 adapter 刻意采用普通 game-owned struct，不把它包装成 engine API。下一里程碑可以用同一个 asset/render/query/IPC API 替换为 ECS registry，而无需修改 engine。

### 7.1 旧 wire 命令归属表

以下命令只表示 demo 为了兼容旧 `tools/ipc_smoke.py` 保留的 wire protocol；它们不是 engine capability。handler 需要的状态全部在 `GameApp`，engine 只负责传输：

| 命令 | owner | GameApp 状态/engine 调用 | 失败与时序 |
|------|-------|--------------------------|------------|
| `ping` | engine transport（**不可覆盖，无需 handler**） | 无业务状态；data 固定 `{"pong":true,"version":1}` | engine 走统一包络/序列化/65536/关闭路径；不调用 handler |
| `help` | game | 固定 demo 命令数组 | handler 同步返回；engine 不追加命令 |
| `status` | game | 当前 asset name、game reloads、对象数、fps、uptime、port | asset 缺失时由 game 返回 error |
| `list_entities`/`get_entity` | game | GameObject 数组与复制的 sprite binding | 不读 engine 实体；不存在 id 返回 error |
| `query_entities` | game | GameObject AABB/中心距离与 type 过滤 | 参数非法或无匹配按 demo 协议返回 error/空数组 |
| `set_entity` | game | 修改 GameObject position/color；不写 asset | 是 demo projection/debug policy，不改变 engine asset |
| `spawn`/`despawn` | game | GameObject 容量、id 冲突和 active 状态 | runtime id 冲突策略由 game 定义；不调用 engine spawn/despawn |
| `layers` | game | `tg_scene_asset_layer_info` 循环 | 只返回 asset tile layer metadata |
| `solid_at` | game | `tg_scene_is_solid_at` + demo GameObject solid policy | wire 命令可含 game 实体碰撞；engine API 仍严格 tile-only |
| `get_tile` | game | `tg_scene_asset_tile_at` 循环与 game 的 solid policy | 合法空格/层外可返回空数组；非法坐标返回 error |
| `reload` | game | candidate load/import/swap、game reload_count | 同步完成后响应；失败不改旧状态 |
| `screenshot` | game | `pending_screenshot_path` | 请求成功只表示已排队，文件在后续绘制帧落盘 |
| `log` | game | `TraceLog` | 不改变 asset/game state |
| `quit` | game | `quit_requested=true` | 返回 bye 后主循环干净退出 |

旧 smoke 的 hello/响应读取同步修复为持久 `LineReader`：每个 socket 保留未消费的 `rest`，不能丢弃一次 `recv()` 中的多行数据。实现契约是 `LineReader.recv_line()`：先消费内部 bytearray 中的换行，未找到才 recv；`rpc()` 只发送后调用同一 reader。测试主动覆盖：一次 sendall 连续发送两条请求、hello 与响应连包、多行 response、多连接顺序；独立 handler-boundary smoke 覆盖 HANDLED object、HANDLED NULL/array、ERROR（空/截断）、NOT_HANDLED 残留 data、未知返回值和 send/断线关闭。旧 wire 仅在 Debug 的 demo adapter 启动后保证；Release/consumer smoke 不依赖这些命令，启动路径统一为 `./build/bin/trogue`。

## 范围

### 做

#### A. Engine 资产模型

- 删除 engine 公共/私有代码中的 `TgWorld`、`TgEntity`、`TgTileLayer`、`TgSprite`、实体池和 `tg_world_*` API；删除 `world.h/world.c` 的运行时世界实现；旧头不列入任何 public include/install path，最终 `engine/include/trogue/` 只能保留目标头。
- 将 scene parser 改造成独立 asset candidate loader；保留 tro-scene v2.1 的格式、三态、tileset、sprite、animation optional payload 校验、资源所有权和失败安全。
- 新增不透明 `TgSceneAsset` 资产句柄与 copy-out 的 `TgSceneEntityDesc`/`TgSceneLayerInfo`/`TgSceneSpriteDesc`；公共头不声明 `TgSceneLayer`/`TgTileset` 类型（见 2.1 与迁移矩阵）。
- descriptor id 严格唯一；不再使用 `_N` 改名作为 scene parse 行为。
- 更新 `config.h`：scene layer/tileset/palette/descriptor/animation 限额与 runtime game object 容量分离；engine 不声明 runtime entity pool。
- `TgTileset` 的旧公开 `texture/texture_path/tile_w/tile_h/count/rects` 字段全部迁为 **`scene_asset_internal.h` 内的私有 tileset 结构布局**（不再有独立 `tileset.c`/`tileset.h`）；`tg_tileset_load/destroy` 改为 `scene_asset.c` 内的 static/asset-owned 私有 loader（不出现在公共头）。`tg_parse_hex_color` 迁为 scene parser 私有 helper；所有旧 world/tileset/render/IPC 符号通过矩阵逐项删除或替换。

#### B. Engine tile 查询与渲染

- 将 collision API 改为 `tg_scene_*`，仅查询 tile layers，并使用明确的 ERROR/CLEAR/SOLID 结果。
- 将 render API 改为 `tg_render_scene` + 显式 sprite draw；移除 world/entity 遍历、引擎 y-sort 和实体色块自动绘制。
- 保留 tileset 图集渲染、独立贴图懒加载、palette/bare scene、sprite region/offset 等资产能力；所有公共资源布局改为 opaque/快照边界。
- 加入 asset 生命周期、极大有限坐标、空/bare 场景、路径安全和资源释放检查。

#### C. Engine IPC / watcher

- IPC start 不再接收 scene/world；实现 callback 注册、完整 JSON ownership、同步响应顺序、统一响应包络和 Release 桩。
- 删除 engine 内置 `list_entities/spawn/despawn/set_entity/reload/screenshot/quit` 等 command implementation；仅保留 `ping` 和 transport protocol machinery。
- watcher 保持文件通知功能，但不触发 scene reload；补齐容量、basename 安全、队列溢出和去抖语义。

#### D. Game demo adapter

- 在 `game/` 新增 `GameApp`/`GameObject`（文件名固定为 `game_app.[ch]`/`game_object.[ch]`/`scene_import.[ch]`，见「预期文件变更」）及 scene importer/reload coordinator；GameApp 是唯一运行时对象所有者。
- 迁移现有演示输入、相机、HUD、实体渲染和 collision policy 到 game；实体色块由 game 自己画，sprite 通过 engine 显式 draw。
- 通过 callback 重建旧 wire 命令响应，确保旧工具仍可用于验证；命令含义明确标注为 demo/game API。
- game 侧可按 demo policy 对 scene descriptor 的 `solid` 做实体碰撞导入，但 engine tile-only API 不改变。

#### E. 使用方式证明与 CMake

- 新增 `tools/CMakeLists.txt`，由顶层 `CMakeLists.txt` 在 engine 之后加入（条件见下）。固定 target 名称为 `trogue_oop_client_smoke`、`trogue_ecs_client_smoke` 与 `trogue_path_smoke`，均继承 engine PUBLIC include/link 依赖，C11 编译。**链接分工**：`trogue_oop_client_smoke`/`trogue_ecs_client_smoke` 链接**生产 `trogue_engine`**（证明真实使用者可链接、无 seam 符号）；`trogue_path_smoke` 链接 **`trogue_engine_test`**（需 token seam，见 2.2）。
- **fixture 独立化（阻塞项 6/8 定案）**：新增独立合法 v2.1 bare fixture `tools/fixtures/consumer_bare.json`，结构固定为：根含 `"format":"tro-scene"`、`"version":2`、**`"tilemap":{"layers":[]}`**（bare 必须存在 tilemap，见 3.2）、无 `tilesets`/`palette`，`entities` 至少两项覆盖 id/type/坐标/颜色，其中一个带 sprite，满足 3.x 全部校验，作为三个 smoke 与单元测试的唯一资产输入。**sprite 形态与零资源依赖固定**（阻塞项 8）：带 sprite 的实体使用**独立贴图形态** `{"texture":"textures/consumer_probe.png"}`；asset load 阶段对该路径**只做字符串校验，不验证文件存在、不加载贴图**（贴图在 render 懒加载阶段才处理，见 §5），因此 fixture **不提供、也不需要任何 PNG 文件**——smoke 只做 `tg_scene_asset_copy_entity()` 的 copy 与 token/路径字符串断言，**不调用任何 render API**。**不动用/不修改 `assets/scenes/soldier_animated_sprite_2d.json`**——该文件现状不在本计划范围，既不以它为 smoke fixture，也不在本计划内修改任何 `assets/` 文件（见「预期文件变更」）。若 parser 实现暴露它不满足新校验，属独立资产修复任务，记录到遗留，不进 M5A 实现。
- `tools/oop_client_smoke.c` 定义自有 `GameObject`，只 include `trogue/trogue.h`，加载 `tools/fixtures/consumer_bare.json` 做 asset load/copy/query 和 Release-safe IPC 符号，不 include `world.h`。
- `tools/ecs_client_smoke.c` 定义自有 `GameEntity`、Position/Velocity 纯 struct，执行同样的 asset/query/IPC link smoke；不实现 ECS 玩法。
- **Debug/Release 断言分离（阻塞项 6）**：两个 smoke 无窗口运行，不调用需要 GL context 的 tileset render。IPC 部分明确分档断言：Debug 下 `tg_ipc_start(port)` 可正常返回（若本机端口占用则按返回值处理，smoke 不因此失败而只是记录），Release 下断言 `tg_ipc_start()==NULL` 且 `poll/set/clear/destroy` 安全 no-op/false；两个 smoke 都不依赖任何业务命令，只验证「IPC 符号可链接、Release 桩行为确定」。
- target 的 working directory 固定为 `${CMAKE_SOURCE_DIR}`，用 `add_test(NAME trogue_oop_client_smoke COMMAND trogue_oop_client_smoke WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})`、ECS 与 `trogue_path_smoke` 对应测试加入 CTest。
- **CTest 与构建条件（阻塞项 11/18 定案）**：顶层 `CMakeLists.txt` 的**语句顺序固定**（阻塞项 18）：`include(CTest)`（先于一切，提供 `BUILD_TESTING` 并注册顶层 CTest 文件）→ `option(TROGUE_BUILD_CONSUMER_SMOKES "..." ON)` → `add_subdirectory(engine)` → `if(TROGUE_BUILD_CONSUMER_SMOKES AND BUILD_TESTING) add_subdirectory(tools) endif()`。engine 的 `CMakeLists.txt` 在 `add_subdirectory(engine)` 时读取**同名的同一变量**（已在顶层定义），并只在 `if(TROGUE_BUILD_CONSUMER_SMOKES AND BUILD_TESTING)` 条件下额外构建 `trogue_engine_test`（与 tools 的加入条件完全一致，见 2.2）；不存在 engine 先于顶层 option 读到未定义变量的顺序问题。`tools/` 仅在 option 与 `BUILD_TESTING` 都为 ON 时加入；tools 内三个 smoke target 与 `add_test` 都创建在该条件下，保证「无 target 即无 test」一致。两套完整命令（阻塞项 11）：
  - Debug：`cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug -DTROGUE_BUILD_CONSUMER_SMOKES=ON -DBUILD_TESTING=ON && cmake --build build --target trogue_oop_client_smoke trogue_ecs_client_smoke trogue_path_smoke && ctest --test-dir build -R 'trogue_.*(client_smoke|path_smoke)' --output-on-failure`
  - Release：同命令但 `-DCMAKE_BUILD_TYPE=Release`、构建目录 `build-release`；Release 只验证 asset/query/IPC stub 与 path/限额逻辑，不连接 TCP。
  - **关闭验证（阻塞项 2/3 定案）**：用 `-DTROGUE_BUILD_CONSUMER_SMOKES=OFF -DBUILD_TESTING=OFF` 重新 configure 到独立目录（如 `build-notest`）并 `cmake --build build-notest`，**configure 输出重定向到文件**（如 `cmake -B build-notest -S . -DCMAKE_BUILD_TYPE=Debug -DTROGUE_BUILD_CONSUMER_SMOKES=OFF -DBUILD_TESTING=OFF > /tmp/trogue_cfg_notest.log 2>&1`）。验收断言（全部是确定性的文件/退出码检查，**不依赖 `ctest -N` 的文本输出**——阻塞项 3：`include(CTest)` 在 `BUILD_TESTING=OFF` 时不保证产生可预测的 `ctest -N` 文本）：① `test ! -f build-notest/CTestTestfile.cmake`（`BUILD_TESTING=OFF` 时不注册顶层 CTest 测试文件，`tools/` 未加入故无子目录 CTest 文件）；② **不存在** `trogue_engine_test` 归档——`test ! -f build-notest/lib/libtrogue_engine_test.a` 且 `find build-notest -name '*engine_test*' | grep -q . && exit 1 || true`；③ **不存在** 三个 smoke 可执行文件——`find build-notest \( -name 'trogue_oop_client_smoke' -o -name 'trogue_ecs_client_smoke' -o -name 'trogue_path_smoke' \) | grep -q . && exit 1 || true`；④ `grep -q 'add_subdirectory(tools)' /tmp/trogue_cfg_notest.log && exit 1 || true`（configure 日志不出现 tools 子目录处理行——若实现输出不含该行，则改为断言 `tools` 目录下无任何构建产物 `find build-notest -path '*tools*' | grep -q . && exit 1 || true`，二选一写明实际采用哪种）。作为辅助信号（非验收依据），可再执行 `ctest --test-dir build-notest -N` 并预期无测试列出。以上四件套构成对 `TROGUE_BUILD_CONSUMER_SMOKES=OFF` 的确定性归档/目标级验收。
- **helper 与校验的单元测试归属（阻塞项 12/6 固定）**：新增独立无窗口 CTest target `trogue_path_smoke`（源文件 `tools/path_smoke.c`），集中覆盖：`tg_copy_json_string`/`tg_join_assets_path` 契约（NUL、长度、路径穿越、绝对路径、`cap==0`、`dst==NULL`、失败不写）；parser 的 schema 拒绝/接受样例——三态与 tilemap 规则（含 **`tilemap.layers` 缺省与 `"layers":[]` 完全等价**断言、尺寸同时缺省/单缺省拒绝、非 bare 缺尺寸拒绝、未知键拒绝、`null` 拒绝、`props:{}` 接受、`animations:{}` 拒绝、region/offset 边界）；payload 限额每级超限（per-payload/per-animation/per-entity/asset 累计）与空值边界；token 单调与耗尽 seam 断言；**watcher 契约**（阻塞项 5/13：经 `tg_watcher_test_classify` seam 断言 NUL 分支表/后缀/cap 边界的全部分支——纯函数 Debug 与 Release 都编译可测；另在 Debug 真实现与 Release 桩下分别断言 `tg_watcher_poll(NULL,...)` 返回 0、`name==NULL`/`cap<=0` 返回 -1 且不消费——无真实目录也可测，见 §7）。schema 负例所需临时 JSON 写入 `tools/.smoke_tmp/`（该目录 gitignore，步骤 13 收尾删除，不提交）。它 `target_link_libraries(... PRIVATE trogue_engine_test)`（见 2.2），仅在 `TROGUE_BUILD_CONSUMER_SMOKES=ON` 时构建，`add_test(NAME trogue_path_smoke ... WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})`；Debug/Release 均可运行（无窗口、无 raylib GPU 依赖，纯 asset/path/logic 逻辑）。**不**采用「并入 ipc_smoke.py 或两个 client smoke 内嵌断言块」的备选方案，保证 helper 契约可被单一测试目标严格复核。

### 不做

- 不在 engine 实现 ECS、OOP 基类、组件 registry、对象池、handle、spawn/despawn、动态碰撞或游戏系统。
- 不在本阶段实现 game-side 完整 ECS 玩家回合闭环；原计划中的 `ecs_version`、event ring、`step/query_ecs/events` 延后到新的 game 玩法计划。
- 不扩展 Godot 插件、tro-scene/tro-animations schema、autotile、动画播放、敌人、AI、战斗和 RuleEngine。
- 不保留 engine 按 `type=="player"` 的 reload 分支；player 兼容策略若暂时需要，只能存在于 game demo adapter。
- 不修改 `trogue-orign/`、Godot 资产和导出插件。
- 不在本阶段为完整 `props/animations` 增加公共 JSON payload API；只校验并由 asset 私有持有，copy-out descriptor 明确丢弃该两类字段。

## 旧 API 到新归属的迁移矩阵

| 旧符号/旧状态 | M5A 新替代 | 新归属与处理 |
|---|---|---|
| `world.h`、`TgWorld`、`TgEntity`、`TgTileLayer` | 删除；`scene.h` 的 opaque asset + copy/info/query API | engine 不再声明运行时世界、实体或可写层结构 |
| `tg_world_create/destroy` | `tg_scene_asset_load/destroy` | engine 只管理一份独立资产 candidate；game 管理自己的对象/world |
| `tg_scene_load` | `tg_scene_asset_load` | 一次 load 返回新 asset；不修改已有 asset |
| `tg_scene_reload` | `GameApp` 的 `game_reload_candidate()` | game 负责 candidate import、状态保留和交换，engine 无 player 分支 |
| `tg_world_spawn/find/despawn` | `game_object_spawn/find/despawn` | game 自己定义 id 冲突、容量和生命周期 |
| `tg_world_is_solid_at` / `tg_world_rect_hits_solid` | `tg_scene_is_solid_at` / `tg_scene_rect_hits_solid` | engine 只查 solid tile；game 另做对象碰撞 |
| `tg_world_tile_at` | `tg_scene_asset_tile_at(asset, layer_index, ...)` | engine 返回明确 tile query result，不暴露 tiles 指针 |
| `tg_parse_hex_color` | scene parser 内部 `static parse_hex_color`；demo 的 `game_color_parse` | 不保留 engine 的 entity-oriented color parser 公共符号 |
| `tg_render_world` | `tg_render_scene` + `tg_render_scene_sprite` | engine 只画层/显式 sprite；game 自己画实体色块并决定排序 |
| `entity_to_json`、active entity count | `game_object_to_json`、GameApp count | 只在 game IPC handler 中实现 |
| `tg_ipc_start(port, world)` | `tg_ipc_start(port)` | IPC 不持有 asset/world |
| `tg_ipc_take_screenshot` | `GameApp.pending_screenshot_path` + game frame consumer | engine 不保存 screenshot 状态 |
| `tg_ipc_quit_requested` | `GameApp.quit_requested` | engine 不保存 quit 状态 |
| `world->scene_name/path/tile/layers/entities/bg` | asset accessors + GameApp binding/state | engine asset 只读；game 保存自己的状态 |
| `world->fps/uptime/reload_count` | `GetFPS`/`GetTime`/GameApp counters | 不再把应用观测字段塞进 engine asset |
| `TgSprite`/`TgTileLayer`/公开 `TgTileset` 字段 | `TgSceneSpriteDesc`/`TgSceneLayerInfo` copy-out；asset-owned private tileset | engine 公共头不暴露可写资源/GPU/tiles 指针 |
| `tg_tileset_load/destroy` | `scene_asset.c` 调用的 static/private tileset loader/destroy | 不导出公共符号；asset destroy 统一释放 tileset |
| `engine/CMakeLists.txt` 的 `world.c` 源项 | 删除 `world.c`，加入 `scene_asset.c`；公共 include 清单不含 `world.h` | 最终 grep/编译检查禁止旧头/符号 |
| `ipc.c` 内置业务 command switch | handler callback + game command table | engine 只解析、分帧、包络和调用回调 |
| `TgSceneLayer`（AGENTS 权威边界与公共头曾列为 opaque） | 公共头**不声明**；层信息仅 `TgSceneLayerInfo` copy-out | AGENTS 边界章节同步改为「公共头不暴露层/资源类型」 |
| AGENTS「架构分层/API 边界」中 `TgTileset` opaque 措辞 | 公共头不声明 `TgTileset`；asset 私有持有 | AGENTS 同步收敛，避免「opaque 但无使用者」 |
| AGENTS 历史章节残留的 engine 实体池/实体 solid 碰撞/engine y-sort/`type=="player"` reload/engine-owned IPC 措辞 | 全部标记为「历史实现（非当前 API）」 | 逐章节标题已改；M5A 完成后按「历史措辞清理清单」把仍像当前能力的措辞改写为明确的已迁移说明（见「预期文件变更/AGENTS.md」），不依赖免责声明 |
| 旧 wire 命令表（help/status/.../quit） | demo game handler 的兼容协议 | AGENTS 命令表注明「历史 demo wire，M5A 后 owner=game」，engine 不实现 |

### 历史措辞清理清单（阻塞项 7/5 的逐项落地）

实现完成后（代码评审 PASS 后的步骤 16，随 AGENTS 一次回填）逐项核对，**在下方「处理结果」列填写实际内容**，不依赖免责声明。**处理结果列现在就有（阻塞项 5）**：每项必须填写「实际文件位置 + 采用改写/删除的说明 + 完成日期」；若某项判定不再需要（如计划 H6 决定删除本清单），则在结果列写「无需处理（原因）」或「清单已按 H6 删除」并说明去向；步骤 16 结束时该列不允许留空。

| # | 位置 | 现状措辞 | 清理动作 | 处理结果（步骤 16 填写） |
|---|---|---|---|---|
| H1 | `AGENTS.md`「架构分层/引擎公共 API 边界」 | `TgSceneLayer`/`TgTileset` 为不透明类型 | 改为「公共头不声明层/资源类型；层信息只经 `TgSceneLayerInfo`」 | |
| H2 | `AGENTS.md`「架构分层/API 边界」 | engine 有 reload 分支、实体池、按 id 改位置等旧语义残留 | 收敛为 opaque asset + copy-out + tile-only query + 显式 draw | |
| H3 | `AGENTS.md` 旧命令表（IPC 协议章节） | 命令表未标 owner | 每命令标 `owner=game demo adapter`（M5A 后），顶部注明「历史 demo wire，engine 不实现」 | |
| H4 | `AGENTS.md` 阶段 2/3/M4 历史章节 | 记录了当时 engine 的 `TgWorld`/y-sort/实体 solid 碰撞等实现 | 章节标题保留「历史实现（非当前 API）」；正文明确这些实现 M5A 已删除/迁移，仅作沿革参考 | |
| H5 | `AGENTS.md` 阶段记录中 engine-owned IPC 描述 | engine 实现 list_entities/spawn/reload/screenshot/quit | 标注 M5A 后这些命令由 game handler 提供，engine 只传输 | |
| H6 | `docs/plan-5.md` 本文 | 迁移矩阵/历史措辞清单 | 步骤 16 完成后把本清单各「处理结果」列填齐，然后**删除本节清单**（它只服务实现期核对，不留在已归档计划外）；删除动作本身写入 H6 结果列 | |
| H7 | `engine/` 源码与公共头 | 旧符号（见矩阵） | 全部删除/替换，验证小节执行 `grep`/`nm` 禁止项 | |
| H8 | `AGENTS.md`「资产规范/字段与语义规则」权威表 | sprite 图集形态「不接受 region/offset，写了被忽略」 | 改为「图集形态出现 region/offset/texture 或未知键 → 拒绝载入」；props/animations/未知键策略同步（见 §3.3） | |

## 预期文件变更

### 删除/替换

- 删除 `engine/include/trogue/world.h`、`engine/src/world.c`、`engine/include/trogue/tileset.h`、`engine/src/tileset.c`（standalone tileset 模块与公开 `TgTileset` 布局整体删除，不保留 public/private 两套）；旧头不安装、不被任何目标 include。
- `engine/include/trogue/scene.h` + `engine/src/scene_asset.c`：scene 公共头改为 opaque asset handle + 只读 copy/info + candidate load + tile-only query；**`scene_asset.c` 由旧 `engine/src/scene.c` 内容改造并改名而来**（不是另起炉灶的新文件，也不是两者并存），旧 `scene.c` 不留在 source list。
- `engine/include/trogue/render.h`、`engine/src/render.c`：去掉 world/entity API，改为 scene layer + explicit sprite draw。
- `engine/include/trogue/ipc.h`、`engine/src/ipc.c`：去掉 world 指针和 engine entity command，实现 callback transport。
- `engine/include/trogue/hotreload.h`、`engine/src/hotreload.c`：保留 watcher，但补齐 basename/capacity/error 契约，不再关联 reload。
- `engine/include/trogue/trogue.h`、`engine/CMakeLists.txt`、`engine/include/trogue/config.h`：移除 world/tileset public 依赖、更新源文件、限额和公共 API；最终 public umbrella 不 include 已删除头。

### 最终文件布局（阻塞项 10 定案，实施以此为准）

- `engine/include/trogue/`：仅保留 `trogue.h`（伞）、`config.h`、`scene.h`、`render.h`、`hotreload.h`、`ipc.h`。
- `engine/src/`：仅保留 `scene_asset.c`（含私有 tileset loader、payload 管理、token 计数器与查询实现）、`path.c`、`path.h`、`scene_asset_internal.h`、`test_seams.h`、`render.c`、`hotreload.c`、`ipc.c`。`world.c`/`scene.c`/`tileset.c` 全部不在 source list。
- `engine/CMakeLists.txt`：`trogue_engine` source list 固定为 `scene_asset.c path.c render.c hotreload.c ipc.c`；`TROGUE_BUILD_CONSUMER_SMOKES=ON` 时另以同一列表构建 `trogue_engine_test`（带 `TROGUE_TEST_SEAMS=1`，见 2.2）。`test_seams.h` 是私有头（不进公共 include、不进 source list），只被 `scene_asset.c`/`render.c`/`hotreload.c`（seam 定义方）与 `tools/path_smoke.c`（调用方）include。

### 新增/修改

- `engine/src/path.c` + `engine/src/path.h`：checked-copy/path helper 私有模块（不进公共 include；`engine/CMakeLists.txt` source list 加入 `path.c`）。**include 方固定（阻塞项 17）**：`path.h` 只允许被 `scene_asset.c` 与 `render.c` include（schema 字符串复制与 assets join）；`hotreload.c`/`ipc.c` 禁止 include `path.h`（watcher 用自己的 static basename 校验，见 §7）。
- `engine/src/scene_asset_internal.h`（私有头）：`struct TgSceneAsset` 私有布局（token、payload 引用、层/descriptor、私有 tileset 句柄）+ `TgSceneCandidate` 与 `tg_scene_asset_candidate_destroy`/`tg_scene_asset_candidate_transfer` 声明（见 §3.3）。**include 方固定（阻塞项 17）**：只允许 `scene_asset.c` 与 `render.c` include；`hotreload.c`/`ipc.c`/任何公共头禁止 include。
- `engine/src/test_seams.h`（私有头）：全部 `TROGUE_TEST_SEAMS` seam 原型的**唯一**声明处（`tg_scene_asset_test_seed_token`/`tg_scene_asset_test_reset_token`/`tg_render_test_stats`/`tg_watcher_test_classify`），整体 `#ifdef TROGUE_TEST_SEAMS` 保护；`scene_asset_internal.h` 不含任何 seam 原型。**include 方固定**：只允许 `scene_asset.c`/`render.c`/`hotreload.c`（定义方）与 `tools/path_smoke.c`（调用方）include。三个私有头（`path.h`/`scene_asset_internal.h`/`test_seams.h`）的 include 关系由 grep 门禁检查（「验证」）。
- `game/src/main.c`：只调用新的 asset/render/query/IPC API，定义并维护 game-owned objects。
- **game 新增文件名固定（阻塞项 17）**：`game/src/game_app.h` + `game/src/game_app.c`（GameApp 状态、IPC handler、命令表、截图/quit 标志、计数）、`game/src/game_object.h` + `game/src/game_object.c`（GameObject 数据与 spawn/find/despawn/AABB 碰撞/JSON 序列化）、`game/src/scene_import.h` + `game/src/scene_import.c`（descriptor copy import、candidate reload coordinator）。**不使用「或等价文件」**；若实现发现需调整拆分，必须先改本计划并经代码评审记录，不得静默另起名。
- `game/CMakeLists.txt`：列出新增 game 源文件并保持 Debug/Release target 可构建。
- 顶层 `CMakeLists.txt`：加入 `tools/` consumer smoke 子目录，并提供 `TROGUE_BUILD_CONSUMER_SMOKES` option。
- `tools/CMakeLists.txt`、`tools/oop_client_smoke.c`、`tools/ecs_client_smoke.c`、`tools/path_smoke.c`、`tools/fixtures/consumer_bare.json`（新 bare fixture）：无窗口 consumer compile/link/runtime smoke、path/校验 helper 单测与固定资产输入（path_smoke 与 OOP/ECS smoke 均使用同一 fixture）。
- `tools/ipc_smoke.py`：加入持久 line reader、连包/多行与 Debug-only 启动说明；仍验证 demo callback 提供的兼容 wire protocol。
- `AGENTS.md`：本阶段实现和代码审查完成后更新实际 API、Roadmap、目录结构和阶段记录；按上述迁移矩阵新增行与「历史措辞清理清单」把历史章节改写为「历史实现（非当前 API）」，删除/迁移旧实体池、实体碰撞、engine y-sort、engine-owned IPC 的当前式措辞，不能只依赖免责声明。
- `CHANGELOG.md`：仅代码评审 PASS 后按开发流程更新。

不修改 `trogue-orign/`、`editor/`、`assets/`（本计划不修改任何现有资产；consumer smoke 使用新增的 `tools/fixtures/consumer_bare.json`，验证产生的临时文件在收尾前删除）。

## 步骤

### 开工前门禁

1. 将本修订版计划书写入仓库；**本阶段（计划审查与批准）唯一可写的文件是 `docs/plan-5.md` 自身**。
2. 交 subagent 只读审查计划书；**审查期间不修改计划书、AGENTS.md 或任何代码**（AGENTS.md 是唯一权威文档；其冲突措辞改写属用户批准后的步骤 16，审查期不进行）。
3. NOT PASS 则按意见**仅修改计划书**并重新送审，循环直到 PASS。
4. PASS 后将最终计划交用户明确批准；**用户批准前不得实现代码、不得修改 AGENTS.md/CHANGELOG.md**。

### 实现与验证（用户批准后）

5. 用户批准后第一步：仅在 `AGENTS.md` 追加一段「M5A 已拍板设计决策（用户批准日期）」记录已拍板的**决策边界**（engine 不定义运行时模型、公共头不声明 layer/tileset 类型、IPC 命令归属 game、watcher 只通知、schema 键策略收紧）；**不新增/改写任何现状 API 事实措辞、不改历史章节标题**（阻塞项 16：标题与所有冲突措辞统一在步骤 16 按实际结果一次修订）；**本步不得声称任何新 API 已实现或已完成**（AGENTS 权威章节只描述当前实现）。审查期与实现中途均不改写 AGENTS 的运行时/API 状态描述。
6. 对照“旧 API 到新归属迁移矩阵”，先实现 `engine/src/path.[ch]` checked-copy helper 与 `scene_asset_internal.h` 私有布局，再实现 opaque scene asset/copy-out API 和 candidate loader，迁移所有 schema 解析、payload deep-copy/释放、tileset 所有权、bare scene 和错误安全；此时 engine 不再暴露 world/entity。先完成 public header/source list 迁移与 `tg_copy_json_string`/`tg_join_assets_path` helper，再继续 parser；不得暂时保留旧头作为兼容桥。
7. 实现 tile-only `tg_scene_*` 查询，覆盖有限输入、层外语义、bare scene、descriptor solid 不参与查询和明确 ERROR/CLEAR/SOLID 结果。
8. 实现 `tg_render_scene` 与显式 sprite draw，迁移 palette/tileset/独立贴图缓存；确认没有隐式实体遍历、实体色块或 y-sort；落实 `IsWindowReady()` 判定与每路径一次失败日志；验证 asset/context 生命周期。
9. 重构 IPC 为无 scene/world 的 callback transport，实现连接状态机、行上限/超限关闭、hello 失败不占 slot、固定错误包络与 Release 桩；watcher 仅保留通知/路径安全并落实 embedded NUL 处理。
10. 在 game 定义 `GameApp`/`GameObject`，实现 descriptor copy import、demo collision policy、输入、实体渲染调用和 candidate reload coordinator；注册 game IPC handler，恢复兼容命令。
11. 更新 `tools/ipc_smoke.py` 持久接收缓冲；执行命令归属表中的全量 demo wire 回归，确认响应来自 game callback 而不是 engine entity store。
12. 添加 `tools/fixtures/consumer_bare.json`、OOP/ECS client smoke、`tools/path_smoke.c`、`tools/CMakeLists.txt` 和顶层 option；分别在 Debug/Release 编译、链接和无窗口运行（含 Debug/Release IPC 断言分离与 path_smoke 全量断言）。
13. 执行 Debug/Release 构建、旧 `tools/ipc_smoke.py` 全量回归、以下**逐个列出**的资产加载回归（阻塞项 19）：新 fixture `tools/fixtures/consumer_bare.json`（bare v2.1，零文件依赖）必须成功；现有资产 `assets/scenes/demo.json`（palette 模式 v2，无 tileset JSON 依赖）与 `assets/scenes/test.json`（图集模式 v2，依赖其 `tilesets` JSON 解析但不读纹理）、`assets/scenes/tile_map_layer.json`（图集模式 v2，同 test.json）必须成功加载——若新 parser 暴露它们违反本计划规则（如 tilemap 缺失/未知键），则作为**独立资产修复任务**记入遗留并汇报用户，**不**在本计划内修改任何 `assets/` 文件；`soldier_animated_sprite_2d.json` 不纳入本计划验证。另执行重复 id/路径/极值/坏 sprite/坏 animation（含未知键拒绝）/payload 限额与空值边界、**asset load 层只测非法路径字符串/格式拒绝**（阻塞项 12：load 不验证贴图文件存在，因此不含「纹理文件缺失」失败用例）、连续 asset load/destroy 与 token 单调（含 seam 耗尽断言）和 `git diff --check`；同时执行 `grep`/`nm` 禁止项（含私有头 include 方门禁）、target 清单/CTest、checked-copy/path helper、sprite token、IPC 状态机/行上限边界（65536/65537）和 watcher 参数测试。**贴图缺失/每路径一次失败哨兵属于带窗口验证**（在 game 运行或 Debug demo 下手动执行，见「验证/render」），不在无窗口 smoke 断言范围内。
14. **交 subagent 评审所有未提交代码**，重点检查 API 是否仍泄漏 runtime world/entity、opaque/copy ownership、asset token、checked-copy/path helper、callback 生命周期、request 校验、响应分帧/send failure、screenshot/quit 状态机、render camera/context、watcher 安全、game adapter 与旧协议兼容；评审前禁止修改 CHANGELOG。
15. 按评审意见修复并重新送审，直到代码 PASS。
16. 代码评审 PASS 后更新 `CHANGELOG.md`，并按**实际实现结果**一次回填 `AGENTS.md` 的架构/Roadmap/目录结构/阶段记录（新增 API 名称与行为以实际头文件为准）；运行步骤 5 启动的「历史措辞清理清单」**H1–H8 全部**清单，逐项核对并把实际结果写入各行的「处理结果」列（该列不允许留空；H6 结果列写明「清单已完成使命，本节按 H6 删除」后删除本节清单）——**H8（schema 键策略：图集 sprite 出现 region/offset/texture/未知键改为拒绝载入，取代「写了被忽略」）必须一并纳入本步骤的实际回填**，不得遗漏。
17. 给出英文 commit message 预览，等待用户确认；禁止直接提交。
18. 用户确认后提交所有变更，不推送。

## 验证

### Engine API 边界

- **符号检查命令（阻塞项 2/18 定案）**：所有 `nm` 检查都**只提取 `^tg_` 前缀符号**并做排序去重后比较（忽略 raylib/jansson/编译器辅助符号），在**最终归档库**级别执行（非成员级别）。可执行命令：
  ```bash
  # 生产库黑名单：以下任一命中即失败
  nm -g --defined-only build/lib/libtrogue_engine.a | awk '{print $3}' | grep '^tg_' | sort -u \
    | grep -E '^tg_(world_|tileset_load|tileset_destroy|scene_asset_test_|render_test_|watcher_test_)' && exit 1 || true
  # 测试库额外导出白名单：相对生产库的差集必须恰为四个 seam 符号
  comm -13 <(nm -g --defined-only build/lib/libtrogue_engine.a | awk '{print $3}' | grep '^tg_' | sort -u) \
           <(nm -g --defined-only build/lib/libtrogue_engine_test.a | awk '{print $3}' | grep '^tg_' | sort -u) \
    | diff - <(printf 'tg_render_test_stats\ntg_scene_asset_test_reset_token\ntg_scene_asset_test_seed_token\ntg_watcher_test_classify\n')
  ```
  生产库允许的公共 API 符号集合 = 公共头声明的全部 `tg_` 符号；测试库 = 该集合 ∪ 上述**四个 seam 符号**（`tg_scene_asset_test_seed_token`/`tg_scene_asset_test_reset_token`/`tg_render_test_stats`/`tg_watcher_test_classify`，均 `#ifdef TROGUE_TEST_SEAMS`，原型见 `test_seams.h`）。同时 `grep`/编译检查确认公共头不存在 `TgWorld`、`TgEntity`、`TgTileLayer`、`TgSprite`、`TgSceneLayer`、`TgTileset`、`tg_world_`、engine entity pool、player/type reload branch；公共 API 没有可写内部指针、层/资源句柄或 tiles 缓冲区，层信息只经 `TgSceneLayerInfo` 暴露。`engine/include/trogue/` 最终文件清单与 CMake source list 一致，旧头不安装/不被 include。
- 独立 consumer 只 include `trogue/trogue.h` 或各公共头即可编译；不存在必须由 game 提供的隐藏符号；copy/info API 不返回 asset 内部可写指针。
- `tg_scene_asset_load()` 每次成功返回独立 asset；失败不修改旧 asset；旧 asset 在 candidate 失败后仍可渲染、查询并销毁。asset token 由进程级非零单调计数器生成、每次成功 load 不同且不接受调用方设置；copy-out/draw token mismatch 必须可测试失败；计数器耗尽路径经测试 seam（仅 `trogue_engine_test` 带 `TROGUE_TEST_SEAMS`，生产 `trogue_engine` 不含）断言：seed `UINT64_MAX-1` → 首次 load 成功 token=`UINT64_MAX` → 第二次 load 返回 NULL 不回绕 → reset 后恢复。token 是快照里的公共值字段（best-effort 归属校验，见 2.1），asset 私有存储只在私有结构内读写。
- descriptor/layer/sprite token/index 的有效期明确；`tg_scene_asset_copy_entity()` 写入 token，draw 验证 token/index；asset destroy 后没有 game/engine 使用悬空指针；tileset 与 scene-owned资源在多次 load/destroy 后无泄漏。
- 超长 id/type/name/path/texture、embedded NUL、绝对/穿越路径、重复 id、非法 schema、非法 sprite/animation、极大有限坐标都在 load 阶段拒绝，不产生半成品 asset；**资源文件缺失不在 load 阶段报错**（load 只做路径字符串校验，阻塞项 12）；所有固定复制经 `tg_copy_json_string`，所有 assets join 经 `tg_join_assets_path`，不得出现 schema 字符串直接 `snprintf`。

### Schema / tile/query/render

- palette、tileset、多层、origin、bare 场景均可加载；tile layer 数组顺序和层外不阻挡语义保持；bare 的 tile size `0,0` 和 entity 默认 `16x16` 有断言。
- `tg_scene_is_solid_at` / `tg_scene_rect_hits_solid` 只受 solid tile layer 影响；descriptor `solid:true` 不会改变结果；game 自己的实体碰撞可单独验证。
- 非有限坐标、非正矩形、极大 finite 坐标、乘法/加法溢出不会触发未定义行为；tile index 转换有边界保护，ERROR/CLEAR/SOLID 结果可区分。
- `tg_render_scene` 只绘制 layers（参数契约见 §5：asset 非 NULL、cam 可 NULL、palette 色块/空跳过、bare 零层返回 true）；实体不会因 asset 中存在就自动绘制；显式 sprite draw 的位置、offset、region、tileset 解析正确；排序由 game 调用顺序决定。
- render 三段顺序经 `tg_render_test_stats` seam 断言（无窗口）：NULL/token 不匹配 → `param_fail` 增而 `window_checks`/`texture_attempts` 不变；合法 snapshot + 无窗口 → 返回 false 且 `window_checks` 增 1、`texture_attempts == 0`。
- 无 GL context、NULL asset/sprite、token 不匹配、过期 asset sprite index、坏 texture 和 asset destroy/reload 时序均安全失败；独立贴图路径统一经 `tg_join_assets_path` 从 `assets/` 解析。tile scene 与 sprite draw 必须共享调用方 camera mode，asset swap 不发生在 Begin/EndDrawing 内。
- 连续 asset load/destroy、独立贴图缓存和 tileset GPU 资源释放通过 Debug/Release 资源检查验证；**贴图缺失/每路径一次失败哨兵在带窗口的 game/Debug demo 下验证**（无窗口 smoke 不覆盖，见步骤 13）。

### IPC/watcher

- IPC 不持有 scene/world 指针；request 非 object/缺 cmd 有固定错误响应（`{"ok":false,"error":"invalid request"}`，不关连接）；行长度**分帧边界**断言（线上 65536 纯 LF 分帧接受且业务正常响应、线上 65537 无换行分帧关闭、线上 65535+CRLF 分帧接受且剥离后 65535）——分帧层与解析层期望分开记录（垫长用合法 JSON object 或任意字节分别断言「正常响应」与「invalid request 且不关闭」），`\r` 计入线上上限且行尾剥离在分帧层；响应 serializer 状态机（目标包络失败 → 固定 `internal error` 兜底 → 仍失败则关闭不发字节）无部分响应；每连接行缓冲不丢多行（含 hello 期间到达内核缓冲的数据），hello/send failure、短写、断线均有确定关闭行为；accept 循环到 EAGAIN、监听错误永久关闭、新连接本 poll 只发 hello 不 recv 且首行下一 poll 读取——均有断言；多个请求按 client slot/行顺序处理；callback 的 request/data ownership、HANDLED/ERROR/NOT_HANDLED/未知返回值组合安全；HANDLED 超长 data（>65536 字节）导致连接关闭且无部分响应，ERROR 超长 error 仍产生合法固定包络；ping 无 handler 可用且不可覆盖；handler 记录命令名负测试证明 engine 不识别 help/status/list_entities/spawn/step/reload/quit/screenshot。
- engine 只实现 `ping`/hello/JSON-lines/包络；engine 源码无业务命令 switch/else-if（grep 门禁）；engine 不识别或存储 `list_entities`/`spawn`/`step`/`reload`/`screenshot`/`quit` 业务状态；game callback 可以返回统一包络。
- Release 下 start/poll/set/clear/destroy 符号可编译链接且不保存 userdata、不启动 TCP。
- watcher 只报告合法 basename；经 `tg_watcher_test_classify` seam 断言全部分支（`event_len==0` 丢弃、中段 NUL/padding/无终止 NUL 四类样例、超长、`.`/`..`/`/`、大小写后缀、**seam 层** `cap==n` → -2 不写、`cap==n+1` → 1 写出）——Debug/Release 均可运行；`tg_watcher_poll` **真 poll 层**参数契约与输出阶段（`w==NULL` → 0；`name==NULL`/`cap<=0` → -1 不消费 pending；输出阶段 `cap==n` → -1 不写不消费、`cap==n+1` → 1 消费并写出 NUL 终止）在 Debug 真实现与 Release 桩下分别断言；队列溢出、尾沿 debounce 和非当前 scene 文件事件有确定行为；watcher 不做 UTF-8 校验、按不透明字节处理；坏 JSON 或 game candidate 导入失败时旧 asset/旧对象保持不变。

### Game adapter/兼容

- demo 的 `GameObject` 与 scene descriptor 完全分离；reload 后没有保存指向旧 asset 的 descriptor/layer/sprite 裸指针。
- player 位置保留、运行时 spawn 冲突、实体 solid 碰撞、未知 type warning（若 demo 需要）均能在 game 代码中找到明确 policy，engine 中不存在对应分支。
- `solid_at` 的 demo wire 语义（tile + game object）与 engine tile-only API 分开验证；`layers/get_tile` 由 asset copy/query API 提供。
- 截图由 game 排队并在下一绘制帧消费，quit 由 game state 驱动；不依赖已删除 engine accessor。
- `tools/ipc_smoke.py` 现有断言全量通过；响应来自 game callback；持久 reader 不丢 hello/后续连包数据，并主动测试连包/多行。脚本只针对 Debug demo adapter，默认启动/文档路径统一为 `./build/bin/trogue`；Release 不启动 TCP、不运行该 wire smoke。
- OOP client smoke 和 ECS client smoke 都不定义/引用 `TgWorld`、`TgEntity`，且 Debug/Release 可链接、无窗口运行。

### 总体验收

- Debug/Release 双构建通过，consumer smoke（OOP/ECS/path）target 可按 option 关闭但默认通过。
- 旧 IPC 冒烟全量通过或按命令归属表完成等价迁移；不允许通过在 engine 恢复 runtime entity store 来“保兼容”。
- Godot headless/import、scene_exporter、demo/test/bare 资产回归不受破坏。
- 评审前不更新 CHANGELOG；最终 `git diff --check` 通过，临时测试资产、截图和进程均清理。

## 遗留

- 下一个独立计划：在不改 engine 的前提下，于 `game/` 选择并实现 ECS 玩法迁移（player/静态碰撞/回合/事件/Agent IPC），或实现 OOP 版本；具体模型由 game 设计决定。
- game-side 场景 reconcile、运行时对象生命周期、动态碰撞、ECS registry、OOP 对象层次、`step/query_ecs/events` 均不属于本阶段 engine API。
- 后续可提供更多 engine 低层绘制原语、资源句柄、音频/输入等模块，但每个模块必须保持不规定 game 的对象模型。
- autotile、动画播放、goblin/AI/RuleEngine、replay、seed、状态 hash、存档和网络同步继续后置。
