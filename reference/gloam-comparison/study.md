# 历史对照：苔径留灯的两种夜林画法

本页记录提交 `c4a2fc3` 的同构图实验与当时的测量。现在 `--2d` 进入[青灯河湾](riverside/study.md)，不再加载这里的旧像素林地。旧图和原始制作数据保留供回看。

选的是画面，不只是渲染 API。`79d7400` 保存 HD-2D 基线；`696581b` 是用写实地表做的二维平面实验，不代表这里的像素候选。当前 `--2d` 重新画了林道、植物、旅人、树、界石和路灯。HD-2D 仍使用原来的美术。

[打开全景、近景和左右验光对照](index.html)。六张图均为 Bevy 主窗口原图。两条路线对齐的是路、地标、角色位置和主体尺度，不是像素级配准；像素版没有透视缩小，也不会把近树打散焦。

## 用实截图指导精修

[重绘参考原图](art-direction/hd2d-to-pixel.webp)由 `generate_image` 使用 [HD-2D 主窗口原图](hd2d.png)生成；[提示词与输入清单](art-direction/source.json)保留在同一目录。采用了参考图中「蓝紫夜色、暖黄路肩」、折断浅沟金色反光、斜裂石层、交织刻纹、铜灯和木纹的处理。游戏仍自己绘制地表、精灵和光照；这张参考不是运行资产。

## 从 2D 游戏和引擎资料学到了什么

- [cure：《The Pixel Art Tutorial》](https://pixeljoint.com/forum/forum_posts.asp?TID=11299) 区分了「由像素组成」和「按像素造型」：轮廓、相邻色块和选择性的小亮点往往比一层散点更能交代质感。这里的苔边、石面、斗篷和树皮都按原生小尺寸绘制，不从终帧缩图后套滤镜。
- [Bevy 0.16.1 的 pixel_grid_snap 示例](https://github.com/bevyengine/bevy/blob/v0.16.1/examples/2d/pixel_grid_snap.rs) 展示双相机：世界先画在低分辨率纹理，最后向真实窗口整数放大。这里连雾、阴影与 HDR 辉光也先在小画布完成，窗口只显示最终色板中的颜色。
- [Unity 制作组：《Happy Harvest》的 2D 灯光与阴影](https://discussions.unity.com/t/2d-light-and-shadow-techniques-with-the-universal-render-pipeline/1641848)：角色、道具、地块都有颜色以外的法线或蒙版。正常的换灯要让同一物体内部的刻面换亮，而不只是给整张图片染色。制作组还区分了脚下接触影与拉长的投影；做俯视树影时，并不建议所有东西都投无限长的影子。
- [Godot 官方：2D Lights and Shadows](https://docs.godotengine.org/en/stable/tutorials/2d/2d_lights_and_shadows.html)：暗部环境色、点光、方向光和遮光轮廓是不同的输入；灯的虚拟高度可以参与逐像素法线受光，而不需要把画布变成 3D。在[官方示例图](https://docs.godotengine.org/en/stable/_images/2d_lights_and_shadows_enabled.webp)里，不同位置的灯在同一个脸形上留下不同高光，方块的遮挡影却是由轮廓决定的。直接贴一张加色光晕，不能同时替代这两件事。
- [Bevy 0.16.1：2D Bloom 示例](https://github.com/bevyengine/bevy/blob/v0.16.1/examples/2d/bloom_2d.rs)：`Camera2d` 可用 HDR 和 Bloom，没必要为灯罩辉光引入透视镜头。这里只让超阈值的灯芯和亮反光扩散；木桩和石头不是发光体。

## 我们怎样保持公平

|两版保持相同|两版有意不同|
|---|---|
|同一条曲径、三处浅水、旅人站位、界石与暖灯的位置|像素版的石阶、蕨草、朽木、树冠、角色和铜灯均为独立画稿；HD-2D 保留写实地表、草叶几何和原精灵|
|冷月来自左后方，暖灯来自角色右边，界石可用等距白灯换边|像素法线在离散亮面和暗面之间切换；HD-2D 用连续的材质反应、阴影贴图和三维雾|
|在同一台桌面用主窗口截图，原图都是 2560×1440|像素美术画布为 512×288，正常取景每横向米 25 个画素；HD-2D 直接渲染 2560×1440|

92 个预选墨色供整幅画使用。路肩在同一盏灯下从暗棕到暖金，平坦地面保持连续的入光，石头与布料保持可分的明暗面；最后才统一到色板。镜像物件反转法线横轴；玻璃蒙版以外不自发光。

## 运行和素材

从当前 Wayland 桌面执行 `tools/gloam/view.sh`。**空格**在同一主窗口切换；`--2d` 仅指定启动时从像素版看。预载两套资源，但只让选中的场景及其输出相机渲染。滚轮缩放、`B`（辉光）和 `F`（薄雾）两边共有；`F12` 截当前画法，`Esc` 退出。HD-2D 用 `D` 控制景深；纯 2D 用 `←` / `→` / `0` 切换验光灯，`N` 切换精灵法线，`S` 切换地面投影。没有 UI 或战斗层。

像素资产与制作入口：

- [地面与低植物](../../tools/gloam/paper/ground_art.py)：在原生网格画道路、残石、湿沟和前后遮挡的花草。林地与路面另外从[两张原创 PixelLab 像素画](../../tools/gloam/paper/woodland-inks/sources.json)抽取苔叶、石粒笔触，用本地色阶与法线重新排布；不加载 HD-2D 的三张 4096² 写实材质。
- [人物、界石和树](../../tools/gloam/paper/cast_art.py)：界石刻面、桦树、远冠与[木桩铜灯](../../tools/gloam/paper/lantern.py)逐层画成。旅人、近景橡树和云杉以[本次 PixelLab 原生小画稿及配套法线](../../tools/gloam/paper/canopy/sources.json)制作，保留了初稿的淘汰理由。为保持单灯构图，旅人右手下误生成的第二盏灯已从色图和法线同时移除。
- [朽木的原始生成请求与出处](../../tools/gloam/paper/deadfall/source.json)：保留 generate_image 原始透明图，分别制作 61×29 色图、裂纹与法线，作为可受光物件放在左前湿沟边。
- [输出色板](../../tools/gloam/paper/palette.py)：用 Oklab 建离线查找表；运行时无随机抖动、无整屏插值滤镜。脚本复用 `tools/gloam/terrain` 的 uv 环境，例如 `uv run --project tools/gloam/terrain tools/gloam/paper/palette.py`。

## 主窗口验收

原生 Wayland 使用实际空格、滚轮、`F`、`F12` 与左右方向键事件。只创建了一个窗口：从 HD-2D 切到纯像素，用滚轮放大后往返；关雾后再往返。两次回到像素版时，画面和离开前分别逐像素一致。完整本机记录见 `captures/gloam/live-compare/wayland.log`。

保存了新[全景](2d.png)、[近景](2d-detail.png)、[左灯](2d-key-left.png)和[右灯](2d-key-right.png)。[像素网格检查](../../tools/gloam/paper/check_grid.py)检查的是 **2560×1440 窗口 PNG**：每张图的 147456 个 `5×5` 方块均无混色，所有像素均在指定色板内；全景用了 82 色，近景用了 83 色。

**石头是否真的换了受光面？** 从 39×47 的原始界石法线取石身中段（UV `0.08<u<0.92`、`0.29<v<0.69`，不透明），按法线横轴 `Nx` 分组，再读取对应近景画布像素的平均 8 位亮度（`0.2126R + 0.7152G + 0.0722B`）。照两次的白灯只换左右，不换距离或强度。

|石面|画布像素数|左灯|右灯|
|---|---:|---:|---:|
|朝左，`Nx < −0.28`|718|158.83|111.33|
|朝右，`Nx > 0.28`|180|45.24|120.11|

2D 阴影落在地表、雾按远近分层，不假装是三维物体间的自遮挡。复取验光图：`tools/gloam/view.sh --2d --detail --stone-key left --capture /tmp/stone-left.png`，将 `left` 换成 `right` 拍另一张。

**运行条件。** Ryzen 5 5500 / Radeon RX 6500 XT，RADV Mesa 26.2.7，Vulkan，原生 Wayland（Wayfire）。Release `--locked --no-default-features`，固定地图，DPI 1，100 Hz，Mailbox/VSync。预热至少 2 秒、32 帧后采样约 5 秒，不含截图回读和写盘。两次采样均全程有焦点，Wayland 未报告最小化状态，应用无额外 FPS 上限。两套资源在同一程序内常驻，但每次只渲染所选场景。

|全景，最终窗口均为 2560×1440|场景渲染尺寸|帧数 / 秒|应用 FPS|帧时中位数 / P95（ms）|
|---|---|---:|---:|---:|
|当前纯像素（纵向 11.52 m）|512×288，窗口整数放大|3642 / 5.000|728.4|1.31 / 1.62|
|HD-2D（32°，当前构建）|2560×1440|318 / 5.002|63.6|15.51 / 16.65|

这比较两种**美术交付方案**的成本，不是同分辨率的渲染器基准。应用帧数不是 100 Hz 显示器实际显示的不同画面数。原始日志和切换取景在 `captures/gloam/live-compare/`。
