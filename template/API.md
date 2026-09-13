# 引擎 API 与资产参考（trogue）

> **本文件是参考手册，不是设计输入。** 按 `AGENTS.md` 的工作方式：先写好设计文档、确定核心循环与验收标准，**设计定稿后**再来这里查实现路径；不要反过来从本文件的功能清单出发拼游戏。引擎边界不是开发优先级排序。

## A. 引擎边界摘要

引擎提供 `SceneAsset` 只读资产（另有受限的 `update_layer_tiles`）、显式 tile/sprite 绘制、动画播放器、Tween、autotile、tile 查询、静态 solid 几何原语、IPC 传输与 watcher。`AnimationAsset` 可独立加载 `tro-animations` v1；其 `view()` 只在资产存活期有效。

碰撞 API 既可从 `SceneAsset` 查询，也可从 game 自持的 `SolidGridView` 数组查询；视图不拥有 mask，逐 solid 层需保留自身 origin。引擎负责 AABB 谓词、线段 vs 静态 tile、sweep 滑移等确定性几何关系；动态实体碰撞规则、刚体物理、solver、单向平台、斜坡、碰撞矩阵和寻路属于 game。

使用已有能力：`AnimationPlayer` 负责帧推进，`TweenManager` 负责数值/位置/颜色补间，`TerrainTable`/`pick_tile` 负责确定性 autotile；game 决定何时触发、如何组合。

网格对齐的移动惯例：网格/回合制移动用固定时长 tween，结束时精确 snap 到目标整数像素；连续即时运动不受这条约束。实现细节见 `engine/include/trogue/tween.hpp`。

公共 API 为 `namespace tg` 的纯 C++（无继承/无虚函数，自由函数 + RAII 资源类 + 值类型快照），完整契约以 `engine/include/trogue/*.hpp` 的头文件注释为准——本文件只是导航，头文件是权威。

## B. tro-* 资产摘要

- `tro-scene` v2：根含 `format`/`version`，像素坐标、层为行主序 tile、`-1` 为空；图集与 palette 互斥；只有实体而没有地形的场景是合法 bare 场景。图集模式的 `tilesets` 为 1..8 个 `{name,path}`，每层引用一个 tileset；palette 最多 32 色；层最多 4 个，`origin` 是可负的世界像素偏移。
- 层条目为 `{name,width,height,solid?,origin?,tileset?,tiles}`，`tiles` 长度必须是 `width*height`，图集值域是所引 tileset 的 tile 数，palette 值域是 palette 数。solid 层只参与静态 tile 查询，层矩形外不阻挡。
- 实体 descriptor 为 `{id,type?,x,y,w,h,z?,color?,solid?,rotation?,props?,sprite?,animations?}`；id 必须唯一。sprite 要么是 `{tileset,tile,offset?,flip_x?,flip_y?}`，要么是 `{texture,region?,offset?,flip_x?,flip_y?}`；region 缺省整图（仅独立贴图形态），offset 缺省 `[0,0]`（两形态统一，锚点 = `x/y + offset`）；flip 为镜像采样显式字段。`rotation`（度）是 spawn 朝向提示，不改变 AABB 碰撞语义；`props` 与场景级 `meta.props` 是引擎不解释的自由透传 object，语义归 game。
- `tro-tileset` v2：包含 `texture`、tile 尺寸、`columns/rows`、`terrain_sets` 和 `tiles`；`tiles[]` 顺序即稳定 tile id，`peering_bits` 供 `tg::pick_tile` 确定性选择。
- `tro-animations` v1：`format`、`version`、`textures`、`animations`；clip 有 `name/fps/loop/frames`，clip 名在动画集内唯一，frame 通过 texture 索引引用，可含 region/offset。
- 实体 descriptor 是 game 的 spawn 初值，不是引擎运行时实体；`solid` 只是导入提示。

## C. IPC 摘要

DEBUG 构建默认监听 `127.0.0.1:48764`（`--port` 可改），协议版本恒为 1；JSON-lines 每行一请求/响应，单行上限 64KB，最多 8 个连接。成功包络为 `{"ok":true,"data":...}`，失败为 `{"ok":false,"error":"..."}`，hello 是带 `event:"hello"` 的成功行。

engine 负责 `ping`、`subscribe`、`unsubscribe`、`connections` 和事件传输；订阅 filter 是事件 data 顶层字段等值匹配，多键 AND，断开即清零。响应不含顶层 `event`，事件推送含顶层 `event`；事件超长直接断开匹配订阅者。其余命令由 game 定义并同步 `tools/ipc_smoke.py`。监听脚本必须按行读取并有退出条件。

起步 game 命令包括 `status`、`list_entities`、`get_entity`、`move`、`screenshot`、`log`、`quit`；可按设计增删。`screenshot` 返回时文件已经写入，具体实体和玩法命令不属于引擎契约。

多个游戏实例并行运行时端口冲突：同一端口只有一个进程能 bind，给每个实例传不同 `--port`。

## D. 构建与依赖

需要 C++20 协程、raylib 6.0、nlohmann/json 3.11+、tl::expected 1.x。缺依赖时通知项目维护者，不自行安装系统包。引擎公共头位于 `engine/include/trogue/`。
