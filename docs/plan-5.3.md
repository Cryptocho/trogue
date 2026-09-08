# 里程碑 5 分卷 3：显式渲染与贴图资源

> 前置阅读：综述、5.1（工程基线）、5.2（资产/查询）。本卷定义引擎「显式绘制原语」与贴图资源生命周期；engine 不遍历或排序任何 game 对象，实体排序/相机/UI 全归 game。

## 1. 原则

- 引擎只绘制：**tile 层**（按资产内层顺序）与**调用方显式传入的 sprite/色块原语**。无隐式实体遍历、无自动 y-sort、无 descriptor 自动绘制。
- 全部绘制调用在**调用方的** `BeginMode2D(camera)` … `EndMode2D()` 区间内；engine 不调用 `BeginMode2D/EndMode2D`、不接收 camera（调用方已设置好变换）。头注释写明该前置契约。
- 资源生命周期：GPU 资源（贴图）必须持有在 asset/缓存内，绘制期间 asset 必须存活；asset 交换/销毁只在窗口绘制帧外。

## 2. 渲染 API（本卷定案；符号以此为准）

```cpp
// 返回值：供无窗口测试与 game 调试观测；正常 game 通常忽略。
enum class RenderResult {
  Drawn,             // 实际完成绘制
  Invalid,           // 参数/归属失败（asset 无效、sprite.asset_id 不匹配、
                     //   index/tile/region 非法、路径不合法）
  WindowUnavailable, // 窗口未就绪（不绘制、不记日志）
  TextureMissing,    // 贴图缺失/加载失败，该次跳过（每路径一次错误日志）
};

// 绘制该 asset 的全部 tile 层（层序=资产数组序）；不含任何实体。
// 前置：调用方处于 BeginMode2D()...EndMode2D() 区间；asset 存活。
RenderResult render_scene(const SceneAsset& asset);

// 绘制一个显式 sprite 快照。pos = 期望的左上角世界坐标（game 决定来源）。
// 归属校验：sprite.asset_id == asset.asset_id()，否则 Invalid + 错误日志。
// region 缺省(w/h==0)时按贴图尺寸补齐；锚点 = pos + sprite.offset。
RenderResult render_sprite(const SceneAsset& asset, const SpriteDesc& sprite,
                           Vec2 pos, Color tint = white);

// 便捷色块（palette/bare/无贴图时 game 可用）；不做任何实体语义。
RenderResult draw_rect(Rect world_rect, Color color);

// 进程级共享贴图缓存释放（应在窗口销毁前、所有绘制结束后调用）。
void shutdown_render();
```

- 是否使用 camera/投影：一律不接收；需要 HUD 等另行由 game 用 raylib 自由绘制（引擎不包 UI）。

## 3. 贴图资源与缓存（RAII）

- `tg::Texture`（detail/公共按需）：包装 raylib `Texture2D`，析构 `UnloadTexture`；**非拷贝、可移动**。
- 两类贴图来源：
  1. **tileset 图集贴图**：属 asset 私有资源，随 `SceneAsset` 析构释放（asset 级 RAII）。同一图集被多 asset 引用时各自持有（简单、生命周期清楚）；重复加载同路径属实现细节不做全局去重（可后续优化，不阻塞）。
  2. **独立贴图**（sprite texture / 动画帧 texture）：进程级**共享懒加载缓存**（path → `shared_ptr<const Texture>`），由 render 模块拥有，`shutdown_render()` 统一释放；跨 asset 可复用。
- 贴图加载只发生在**绘制调用**（render_scene/render_sprite 触达需要时）→ **懒加载**；asset load 期不读纹理文件（见 5.2 §2.3）。
- 缓存线程安全：无（单线程约定）。

## 4. 执行顺序与失败哨兵（沿用 C 版三段定案）

每次绘制原语调用固定三段：

1. **纯 CPU 参数/归属校验**：asset 存活引用（引用即保证）、sprite 非空、`sprite.asset_id` 匹配、index/tile/region 合法性、路径字符串经 5.2 grammar；失败→返回 `Invalid` 并记日志（参数错误属调用方 bug，**每次**记录），**不触碰**窗口/缓存/哨兵。
2. **一次 `IsWindowReady()`**（或 raylib 等价窗口就绪判定）：false → 整体 no-op（**不记日志**——窗口关闭是正常退出路径；不读写缓存/哨兵）。
3. **纹理缓存查找/加载 + 绘制**：缺失贴图/加载失败 → 该次绘制跳过并记**每路径一次**错误日志（失败哨兵：同一路径在缓存生命周期内只记一次；成功加载后清除哨兵并复用缓存）。

- 失败哨兵实现于缓存层（path→optional 状态），避免每帧刷屏。
- palette 层/色块与 bare（无纹理）也要求窗口就绪（第 2 段），但不需要纹理加载。

## 5. 各模式绘制语义

- **palette 层**：非空 tile 用 `palette_color(tile)` 画色块；`-1` 跳过。
- **图集层**：非空 tile 用该 tileset 图集区域（tile 矩形由 tile_w/h 与 tileset 布局算出）绘制；最近邻采样（像素风沿用）。tileset 的纹理在首次绘制该 asset 时懒加载。
- **bare**：零层，render_scene no-op。
- tile 尺寸 0（bare）不进入 tile draw；图集/palette 非 bare 尺寸已在 load 期保证 ∈[1,256]。
- 空层/整层无 tile：合法，跳过。

## 6. asset swap 与绘制生命周期契约

- game 热重载：`load 新 asset` → 对新 asset 重取实体/精灵/动画快照（含新 asset_id）→ 全部成功 → 在 **`EndDrawing()` 之后**交换持有的 asset（替换旧）→ 旧 asset 析构。
- 任何绘制调用期间，传入的 asset 引用必须存活；跨帧持引用属调用方错误（文档+评审注意）。
- 共享独立贴图缓存跨 asset 存活，`shutdown_render()` 在窗口关闭后调用（退出路径）。

## 7. 本卷决策清单

| # | 决策 |
|---|---|
| 1 | engine 不调 Begin/EndMode2D、不收 camera；调用方负责模式区间 |
| 2 | render_scene 仅 tile 层；render_sprite 显式单 sprite（asset_id 归属校验） |
| 3 | 图集纹理随 asset RAII；独立贴图进程级共享懒缓存 + shutdown_render |
| 4 | 三段顺序 + 每路径一次失败哨兵 + 窗口未就绪不记日志 |
| 5 | palette/图集/bare 语义与空 tile/尺寸 0 规则 |
| 6 | asset swap 在帧外；绘制期间 asset 存活 |

## 8. 验证

- 带窗口（demo/手动）：test.json/demo.json 渲染截图比对；贴图缺失每路径一次日志；reload 后旧 asset 析构不崩、新快照生效。
- 无窗口：只测「未建窗口时 render_* 走第 1/2 段安全 no-op 且不崩」+ 纯参数校验分支。经 **5.1 §7.2 seam**（`TROGUE_TEST_SEAMS` 下 `render_test_stats()` 三段计数）断言：① 传非法参数/归属不匹配 → `param_failures` 增、`window_checks`/`texture_attempts` 不变（段①先于窗口检查失败）；② 合法调用但未建窗口 → `window_checks` 增 1、`texture_attempts==0`（段②后因窗口未就绪返回、段③未执行）。该单测链接 `trogue_engine_test`。
