# 项目 Agent 指南

## 1. 工作目标与资料

这是一个使用 **Rust + Bevy** 开发的单机传统 Roguelike RPG。当前工作从设计进入可运行体验的制作。

- 开始任务前，阅读 `game-design-notes.md` 中与任务有关的章节。
- `reference/` 保存研究与参考资料。借鉴其机制、叙事和美术方法，创作本作自己的内容。
- 目标工程的 `Cargo.toml` 和 `Cargo.lock` 决定 Bevy 版本。按该版本查询文档与源码。
- 项目名称仍是暂定名称。设计笔记中标为候选的神系、任务与终局仍是候选。

## 2. 必须保留的设计方向

- **成长结构：个人构筑 → 队伍构筑 → 信仰与权柄构筑 → 国家与文明的成长。** 高层成长继续赋予队友培养、据点生活和装备研究以价值。
- **叙事递进：西幻／日式西幻的日常 → 黑暗奇幻 → 更宏大的世界真相。** 后续揭示重新解释玩家经历过的地方、人物与机制。
- 日常生活温暖、具体，劳动、饮食、培养和经营本身就应令人满足。
- 战斗与世界时间采用行动驱动回合制。表现动画使用的真实时间与游戏时间分开。
- 手动状态等待玩家行动。自动行动可观察、可暂停、可接管。
- 每个存档是一名固定主角和唯一一支外出队伍。
- 地图逻辑使用二维网格。场景使用 HD-2D：2D 角色与 3D 环境结合。
- 镜头采用固定方向的高角度斜俯视，允许缩放。当前不制作战术高度差和复杂可破坏地形。
- 据点与远处区域按经过的游戏时间抽象结算。当前没有真实时间驱动的离线成长。

## 3. 原生桌面运行与性能测试

### 硬性运行规则

- **直接在用户当前的 Wayland 桌面运行游戏。** 使用真实窗口与硬件 GPU。
- **禁止使用 Xvfb 或 `xvfb-run` 运行游戏。** 画面与性能验证使用当前桌面，而非虚拟显示服务器。
- 保留当前会话的 `WAYLAND_DISPLAY`、`XDG_RUNTIME_DIR`，以及存在时的 `WAYLAND_SOCKET`。每次运行读取实际值，禁止硬编码用户名、socket 编号或显示编号。
- 检查 Wayland socket 可访问，并确认窗口实际使用 Wayland 后端。`DISPLAY` 同时存在时，不能据此认定后端。
- 后端选择方法必须符合目标工程所用的 winit / Bevy 版本。运行失败时报告具体原因；性能验证不得改用 XWayland、离屏模式或软件渲染替代。
- 纯逻辑测试可以无窗口运行。它们不替代桌面画面与性能验证。

### 目标分辨率与帧率

- **目标物理分辨率与渲染分辨率均为 2560×1440。** 配置窗口时区分逻辑尺寸、物理像素和 DPI 缩放。
- **开启垂直同步。** Bevy 窗口使用 `PresentMode::Mailbox`(不支持就AutoVsync)，记录帧率不低于30

### 性能记录

- 使用 release 构建测量性能。开发构建用于迭代，结果须标明构建类型。
- 等待素材加载、shader 编译和场景稳定后采样。将冷启动卡顿与稳定运行的数据分开。
- 比较前后保持场景、随机种子、镜头、分辨率、缩放因子、画质和采样时长一致。
- 记录构建参数、CPU / GPU 与驱动、图形后端、窗口与渲染分辨率、显示刷新率、帧率档位、present mode / VSync、采样时长与帧数。优先报告帧时间中位数与 P95，必要时补充 FPS。
- 说明窗口是否失焦、最小化或受到帧率限制。诊断工具和截图读回的开销单独处理。

## 4. 截图与自动化验证

### 截图

**绝对禁止使用 `grim`。禁止通过 shell、脚本、工具封装或子代理间接调用它。**

游戏截图使用 Bevy 引擎内截图。

Bevy 0.16.1 原型已采用的接口：

```rust
use bevy::render::view::screenshot::{Screenshot, save_to_disk};

commands
    .spawn(Screenshot::primary_window())
    .observe(save_to_disk(path));
```

- 其他 Bevy 版本按其实际 API 实现。
- 截图等待素材就绪和目标状态成立后触发。`ScreenshotCaptured` 表示 GPU 回读的图像就绪；文件保存成功并且可读取后，再返回路径。依据：[Bevy 0.16.1 截图实现](https://github.com/bevyengine/bevy/blob/v0.16.1/crates/bevy_render/src/view/window/screenshot.rs)。
- Agent 必须打开截图，检查构图、遮挡、角色可读性、光影和 UI。发出了截图请求，不等于获得了画面证据。
- 主窗口截图包含实际绘制到该目标的 Bevy UI。需要单独检查世界渲染、UI 或特定渲染目标时，明确截图目标和包含的图层。
- 成品证据来自实际运行结果。图像编辑用于制作素材，不用于伪造验证画面。

### 测试接口

- 优先复用工程已有的测试入口。需要自动化时，可接入 Bevy Remote Protocol（BRP）或小型本地 IPC。
- 先检查当前版本的 feature、插件与支持的方法。远程接口须实际接入，才可作为可用能力使用。
- 现有原型尚未启用 `bevy_remote`。Bevy 0.16.1 接入 HTTP BRP 需要该 feature，并显式添加 `RemotePlugin` 与 `RemoteHttpPlugin`；默认地址为 `127.0.0.1:15702`。
- 该版本内置 BRP 提供反射数据访问等能力。游戏动作、UI 操作和截图需要自定义方法或 IPC；通过 `rpc.discover` 确认运行进程实际暴露的方法。依据：[BRP 官方实现](https://github.com/bevyengine/bevy/blob/v0.16.1/crates/bevy_remote/src/lib.rs)。
- 按当前测试需要提供语义命令，例如加载固定种子场景、暂停、单步、执行角色行动、读取状态、获取诊断或请求截图。
- 命令进入正常的游戏行动与状态更新流程。测试场景初始化与玩家实际操作在记录中分开标识。
- 验证按钮命中、焦点或拖拽时，走实际输入处理链。语义命令适合验证游戏规则，两种测试分别说明。
- 后台传输线程通过通道向 Bevy 系统提交命令，由系统在明确的调度阶段执行。异步截图返回请求标识，完成后查询结果，避免持有 `World` 等待未来帧。
- IPC 记录请求、完成状态与错误。涉及截图时，在保存成功后返回路径。
- 开发接口只监听本地回环地址或本地 Unix socket，按需启用。测试结束后关闭本次启动的进程和连接。
- Wayland 下优先使用应用内部接口与现有快捷键，减少对全局键鼠注入的依赖。

## 5. 2D 素材来源与选择

三条素材路径均可使用，按效果和适配成本选择：

| 路径 | 适用内容 | Agent 的责任 |
| --- | --- | --- |
| PixelLab MCP | 像素角色、多方向动画、物件、地形、UI、像素修整与法线 | 熟悉工具目录和实际效果，使用匹配的生成、编辑与导出流程 |
| `generate_image` | 概念、角色与场景素材原图、纹理、需要参考图编辑的画面 | 自行处理输出分辨率、去背景、拆分、像素化与图集 |
| 浏览器寻找现成素材 | itch.io 美术包、GitHub 开源字体及其他有明确授权的资源 | 检查实物、风格、规格与许可证，保留出处 |

### PixelLab：先熟悉工具，再选择管线

官方工具总览：<https://api.pixellab.ai/mcp/docs>。

- 第一次使用 PixelLab 前，通读当前官方工具目录，理解各类工具产生什么结果。
- 每次选择工具时，阅读当前挂载工具的说明与 schema。需要完整选项时，使用 `search_help(tool="工具名")`。
- 掌握下表各类工具的差别。新工具和参数以当前文档为准。

| 工具类别与主要名称 | 实际用途与选型依据 |
| --- | --- |
| `create_character`、`create_character_pro_flash` | 建立可复用角色及多方向视图。比较 standard / v3 / pro 与 Pro Flash 的输入、画布、体型模板和方向限制 |
| `create_character_state`、`create_object_state`、`manage_state` | 同一个体的服装、姿态、受伤等变化，或状态复制与拆分；制作另一名 NPC 应创建新资产 |
| `animate_character`、`animate_object` | 角色与物件动画。按实际支持的模板、文字动作或骨架模式选择；检查每个方向和每一帧 |
| `animate_image`、`animate_image_pixminimax`、`animate_with_skeleton_v3` | 从图片生成动作，或用骨架约束姿态；适合已有首帧和特定动作需求 |
| `create_portrait_character`、`set_character_portrait`、`create_vocal_animation`、`create_talking_gif`、`get_lip_sync` | 肖像、全身像转换与视觉口型；按任务需要选用，口型图像不等于语音生成 |
| `estimate_skeleton`、`estimate_custom_skeleton` | 为人形或特殊形状估计骨架；先检查关节，再做受控动画 |
| `create_object_pro_flash`、`create_1_direction_object`、`create_8_direction_object`、`expand_object_to_8_directions` | 单方向或多方向道具、设施与物件。区分持久化物件与普通生成图片 |
| `create_image_pixen`、`create_image_pixflux`、`create_image_pro`、`create_image_pro_flash`、`create_image_pro_from_style` | 自由图像与风格参考生成。模型、输出数量和尺寸约束不同 |
| `create_topdown_tileset`、`create_path_tiles`、`create_sidescroller_tileset`、`create_isometric_tile`、`create_tiles_pro` | 分别解决俯视地形过渡、连通道路、横版地形、等距瓦片与独立瓦片。匹配真实镜头和拼接规则 |
| `create_building_kit`、`create_room`、`cut_out_room_furniture` | 建筑组件、整体房间与家具切出。检查哪些内容已画进平面图，哪些能单独摆放 |
| `create_map`、`edit_map`、`inpaint_map`、`place_map_tiles`、`create_map_object`、`place_map_object`、`move_map_object`、`remove_map_object`、`edit_map_areas`、`view_map` | PixelLab 地图制作与预览；导入 Bevy 时仍需落实本作的网格、碰撞与渲染数据 |
| `create_ui_asset`、`create_ui_states`、`update_ui_asset`、`create_font` | UI 模板、拆分元素、交互状态与字体。检查 nine-slice、裁切和缩放；当前字体生成覆盖拉丁字符，中文正文另选字体 |
| `edit_image`、`edit_image_pixen`、`edit_image_pro_flash`、`inpaint_image`、`inpaint_image_pro_flash`、`edit_animation`、`transfer_outfit` | 整图、局部、整段动画修改与参考换装。普通 edit 会重绘，精确保留 mask 外像素用 inpaint |
| `remove_background`、`correct_pixelart`、`reduce_colors`、`unzoom_image`、`image_to_pixelart`、`image_to_pixelart_pro`、`image_to_pixelart_pro_flash` | 去背景、像素清理、调色板统一、恢复放大的像素网格，以及将普通图片转为像素画 |
| `create_normal_map` | 从图片或动画生成配套法线。检查法线方向约定、帧序与实际受光效果 |
| `pixelart_workbench`、`vfx_workbench` | 精确像素编辑、拼接、逐帧处理与特效制作。先运行 `describe start` 理解命令；按文档区分确定性操作与生成操作 |
| `import_object`、`import_tiles`、`get_upload_url`、`save_to_asset`、`export_asset`、`select_object_frames` | 上传、导入、保存、候选帧选择与导出。检查资产 ID、图集布局、元数据与实际文件内容 |
| `get_*`、`list_*`、`wait_for_jobs`、`cancel_job`、`get_balance`、`get_pro_flash_capabilities`、`search_help`、标签与重命名工具 | 检查资产、等待任务、查询能力与报价、管理结果；`set_canvas_size` 调整透明画布边距，删除类操作影响远端持久资产 |

关键选型差别：

- `create_character` 的 v3 文本创建主要面向人形。v3 的旋转参考图被视为 south 正面，三分之四侧面会使方向标签偏转。standard、v3、pro 的尺寸含义和参考能力不同。
- Pro Flash 是独立工具族，先生成首图，再补方向。`template_id="custom"` 没有骨架，选择文字驱动动画；需要模板或骨架动画的身体应选匹配模板。
- 自由图像的 Pro 通常返回多个候选，Pro Flash 返回单图。选中图片后，按需要导入或保存为可管理的角色、物件。
- `create_topdown_tileset` 的 standard 固定高俯视，pro 才响应 `view`。共享 base-tile ID 串联两种地形的过渡，并检查实际接缝。
- 风格图、角色旋转参考图、概念图和地图上下文用途不同。复用已经验收的风格锚点，按工具要求裁切或补透明边距。
- `unzoom_image` 恢复已经放大的像素网格；转像素工具负责重绘普通图片。转换后重新检查尺寸与透明通道，部分管线会生成白底。
- `reduce_colors` 对相关方向与动画帧整组处理。inpaint 使用同尺寸 mask，并核对黑白保留约定。像素工作台产生新结果 ID，写回持久资产使用 `save_to_asset`。
- `create_normal_map` 支持图片与动画条带。核对 OpenGL / DirectX 法线方向、帧数与顺序，再测试实际 Bevy 材质；其他引擎的接法不作为 Bevy API 使用。

工具的模式、候选数量、尺寸与计费规则分别查询，遵守工具自身的报价确认流程。PixelLab 的聊天、云端游戏代理与 sandbox 部署工具服务其他工作流，本地 Bevy 开发按本项目工程运行。

操作要求：

1. 先确定镜头方向、目标像素密度、画布、角色比例、色板和参考图。
2. 用少量代表素材确定风格锚点，再扩展同一套资源。
3. 生成请求返回 job ID 与 asset ID 后，保存 ID 和参数。使用 `wait_for_jobs` 等待，再用匹配的 `get_*` 检查目标资产；账户中其他任务完成不等于本次资产完成。请求超时后先查状态，再决定是否重提。
4. 打开原图、方向图与动画帧检查实际结果。多方向角色重点检查脸、装备、非对称细节、脚底位置和轮廓。
5. 对可定位的问题使用局部编辑、状态、像素工作台或调色工具；整体方向错误时重新生成。
6. 处理 review 候选选择后导出。读取实际文件与 layout 元数据，核对透明通道、画布和单元尺寸、方向顺序、帧数、帧时长与 pivot，再接入 Bevy。动画可能保留输入首帧，也可能扩大画布，不能按请求值猜布局。

PixelLab 的房间图、地图和瓦片首先是图像资产。HD-2D 场景里的实体几何、受光、遮挡、碰撞和导航由游戏工程实现。

### `generate_image`：输出必须经过素材加工

- 调用前阅读工具 schema。明确用途、视角、比例、背景、布局与参考图职责。
- 保存原始输出。检查真实图片尺寸与实际单元边界，建立可重现的裁剪、去背景、拆分和图集加工流程。
- 根据目标像素密度制作游戏分辨率版本。原生像素图保持像素网格；高分辨率画作需要专门的缩小、量化或像素网格恢复。
- 清理残余底色、白边、半透明噪点和被画进图片的棋盘格。逐帧对齐脚底与 pivot，补齐图集间距，检查采样串色。
- 方向与动画必须有实际对应的画面。检查镜像对武器、服饰和动作含义的影响。
- 在目标镜头、实际缩放和游戏光照下检查加工结果。

### AI 生成重试授权

**项目作者授权 PixelLab MCP 与 `generate_image` 不限次数重试，直到满意，或经过有依据的迭代确认无法进一步改善。**

- 每轮查看结果，记录具体缺陷，调整提示、参考图、参数、模型或后处理方案。
- 不设任意的固定重试上限。比较并保留最佳候选，说明最终选择。
- 平台额度不足、权限不足或工具故障时，报告具体错误与可继续的替代路径。重试授权不包含购买素材、充值或订阅。

### 浏览器素材与字体

- **itch.io 素材必须使用 browser 工具查看实际图片，尤其是作者提供的游戏内使用示例。** 检查完整场景中的搭配效果，再判断素材是否适合本项目。
- 浏览器调查同时查看演示、规格与作者说明。字体可从 GitHub 的开源项目中寻找。
- 检查是否允许商业游戏使用、修改和随游戏分发。保留许可证、作者、来源链接、版本及必要署名。
- 检查不同来源素材的视角、像素密度、人物比例与色板是否协调。
- 遇到合适的付费资源，记录链接、价格、授权范围和适配理由，供项目作者选择。购买需另行授权。

## 6. 3D、材质与 HD-2D 光影

### 建模管线：blender-4.5 + bpy

- **3D 建模统一使用 `blender-4.5` 和其 Python API `bpy`。** 体素和普通模型都遵循这条管线。
- 使用 Blender 自带的 Python 执行 `bpy` 脚本。离线批处理使用 `blender-4.5` 的 `--background`、`--python` 选项。
- 建模、UV、材质分配与导出尽量脚本化。保留制作脚本、必要的 `.blend` 源文件与纹理来源。
- 按工程需要导出 glTF / GLB，检查坐标、单位、应用后的变换、网格、材质和动画。
- Blender 程序材质与节点效果需要烘焙为导出支持的纹理，或在 Bevy 中实现对应 shader。最终材质效果以 Bevy 实景为准。
- Blender 后台模式用于离线资产加工；游戏画面与性能验收仍按第 3 节在当前 Wayland 桌面进行。

### 模型与材质

- 3D 资产可以采用体素或普通建模。以实际游戏镜头下的轮廓、尺度和材质一致性判断效果。
- 检查法线、切线、UV、模型朝向、单位、表面细节与导出后的实际外观。
- 区分 albedo / 发光颜色的色彩空间与 normal / roughness / metallic 等数据纹理的线性采样。
- 为木材、石材、金属、布料和植被配置合适的粗糙度、金属度与法线。检查透明或裁切材质的深度、排序、双面与投影行为。
- 2D 角色与植物需要明确的 3D 受光与投影方案，并在移动光源下验证。需要时结合裁切面片、法线贴图或自定义材质。
- 生成法线只能补充表面受光。遮挡、轮廓和接触关系仍要结合几何与渲染方案处理。

### 光影与 shader

**光影是 HD-2D 的核心表现手段。主动尝试有表现力的 shader，并在真实场景中比较效果与开销。**

- 先建立清楚的主光、环境光、接触阴影与冷暖层次，再调后处理。
- 让光源、发光体和实际照亮的区域互相对应。像素人物与 3D 环境共享可信的空间关系。
- 可探索水面与岸边交互、体积雾与光束、植被风动、庇护边界、权柄纹样、受击与异变等效果。
- 为效果提供可调参数与开关。使用同一镜头检查开关前后，记录性能变化。
- 检查透明叠加、阴影、shader 分支、采样次数与分辨率的成本。跨镜头缩放检查像素闪烁与图集接缝。
- 景深、bloom、雾与调色服务构图。敌我轮廓、交互物、状态和行动范围保持可辨。

## 7. 实现与交付

- 游戏逻辑、存档事实与渲染表现分开。行动调度遵循游戏时间，动画与镜头遵循表现时间。
- 辅助 Python 工具需要第三方依赖时，可以使用 `uv` 创建虚拟环境并管理依赖。保留依赖声明与运行方式，保持环境隔离；`bpy` 脚本仍使用 `blender-4.5` 自带 Python 执行。
- 复用既有代码与资产管线。高频系统避免重复分配、重建资产和无关的全量计算。
- 视觉或交互变更完成后，在当前 Wayland 桌面启动实际场景，执行相关操作，检查引擎内截图及错误日志。
- 验证使用覆盖改动的最小场景。逻辑测试、桌面操作、视觉检查与性能测量各自报告实际结果。
- 交付说明包含启动方式、操作方式、可见变化和证据路径。有性能结论时，附采样条件与数据。
- 素材加工步骤与来源授权随资产保留，保证后续可以重新生成或替换。
- **音效暂不处理。** 当前不制作、接入或调优音效；已有原型音频维持原状。
