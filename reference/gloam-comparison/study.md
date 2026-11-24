# 苔径留灯：像素游戏与 HD-2D

选的是画面，不只是渲染 API。`79d7400` 保存 HD-2D 基线；`696581b` 是用写实地表做的二维平面实验，不代表这里的像素候选。当前 `--2d` 重新画了林道、植物、旅人、树、界石和路灯。HD-2D 仍使用原来的美术。

[打开全景、近景和左右验光对照](index.html)。六张图均为 Bevy 主窗口原图。两条路线对齐的是路、地标、角色位置和主体尺度，不是像素级配准；像素版没有透视缩小，也不会把近树打散焦。

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

像素版的暗部有青、蓝紫与树皮棕，灯下有赭红和浅金。82 个预选颜色是整幅画的输出色板，不是要求每屏用完；脚下投影、石头的小面和水边反光也遵循同一套颜色。每个材料可有独立法线。镜像物件反转法线横轴；玻璃蒙版以外不自发光。

## 运行和素材

从当前 Wayland 桌面执行 `tools/gloam/view.sh --2d`；去掉 `--2d` 是 HD-2D。两版都支持滚轮缩放、`B` 切换 Bloom、`F` 切换雾、`F12` 保存主窗口、`Esc` 退出。纯 2D 的 `←` / `→` 从左 / 右照亮界石，数字 `0` 移走验光灯，`N` 切换精灵法线，`S` 切换地面投影。正常进入夜林时没有白色验光灯。HD-2D 的 `D` 控制光学景深；纯 2D 刻意保留全景清晰的线条。

像素资产与制作入口：

- [地面与低植物](../../tools/gloam/paper/ground_art.py)：在原生网格画道路、残石、湿沟和前后遮挡的花草。林地与路面另外从[两张原创 PixelLab 像素画](../../tools/gloam/paper/woodland-inks/sources.json)抽取苔叶、石粒笔触，用本地色阶与法线重新排布；不加载 HD-2D 的三张 4096² 写实材质。
- [人物、界石和树](../../tools/gloam/paper/cast_art.py)：界石刻面、桦树、远冠与[木桩铜灯](../../tools/gloam/paper/lantern.py)逐层画成。旅人、近景橡树和云杉以[本次 PixelLab 原生小画稿及配套法线](../../tools/gloam/paper/canopy/sources.json)制作，保留了初稿的淘汰理由。为保持单灯构图，旅人右手下误生成的第二盏灯已从色图和法线同时移除。
- [朽木的原始生成请求与出处](../../tools/gloam/paper/deadfall/source.json)：保留 generate_image 原始透明图，分别制作 61×29 色图、裂纹与法线，作为可受光物件放在左前湿沟边。
- [输出色板](../../tools/gloam/paper/palette.py)：用 Oklab 建离线查找表；运行时无随机抖动、无整屏插值滤镜。脚本复用 `tools/gloam/terrain` 的 uv 环境，例如 `uv run --project tools/gloam/terrain tools/gloam/paper/palette.py`。

## 主窗口验收

在当前 Wayland 桌面保存[全景](2d.png)、[近景](2d-detail.png)、[左灯](2d-key-left.png)和[右灯](2d-key-right.png)。[像素网格检查](../../tools/gloam/paper/check_grid.py)检查的是 **2560×1440 窗口 PNG**，不是中间纹理：每张图的 147456 个 `5×5` 方块均无混色，所有像素均在指定色板内。全景实际用了 63 色，近景 65 色。窗内没有 UI 或战斗层。

**石头是否真的换了受光面？** 从 39×47 的原始界石法线取石身中段（UV `0.08<u<0.92`、`0.29<v<0.69`，不透明），按法线横轴 `Nx` 分组，再读取对应近景画布像素的平均 8 位亮度（`0.2126R + 0.7152G + 0.0722B`）。照两次的白灯只换左右，不换距离或强度。

|石面|画布像素数|左灯|右灯|
|---|---:|---:|---:|
|朝左，`Nx < −0.28`|431|121.59|65.71|
|朝右，`Nx > 0.28`|286|90.32|184.61|

只关闭精灵法线：石身中段 RGB 平均绝对差为 11.32/255，旁边裸露泥路控制区的差为零。关闭地面投影、薄雾或 Bloom 时，分别有 15440、28388、170 个画布像素改变。2D 阴影落在地表、雾按远近分层；两者不是三维物体间的自遮挡。复取验光图：`tools/gloam/view.sh --2d --detail --stone-key left --capture /tmp/stone-left.png`，将 `left` 换成 `right` 拍另一张。

**运行条件。** Ryzen 5 5500 / Radeon RX 6500 XT，RADV Mesa 26.2.7，Vulkan，原生 Wayland（Wayfire）。Release `--locked --no-default-features`，种子 841701，DPI 1，100 Hz，Mailbox/VSync。素材与管线就绪、预热至少 2 秒和 32 帧后，采样约 5 秒；截图回读和写盘不计入。下表仅列全程有焦点的全景；Wayland 未报告最小化状态，应用没有额外 FPS 上限。近景截图一次采样时失焦，未列作性能对照。

|全景，最终窗口均为 2560×1440|场景渲染尺寸|帧数 / 秒|应用 FPS|帧时中位数 / P95（ms）|
|---|---|---:|---:|---:|
|当前纯像素（纵向 11.52 m）|512×288，窗口整数放大|3976 / 5.000|795.1|1.22 / 1.50|
|原 HD-2D（32°，沿用原实测）|2560×1440|319 / 5.003|63.8|15.46 / 16.61|

像素工作量不同，这是两种**美术交付方案**的成本，不是同分辨率的渲染器基准。应用帧数不是 100 Hz 显示器实际显示的不同画面数。本机验证日志与单项开关截图在 `captures/gloam/pixel-study/`，HD-2D 基线日志在 `captures/gloam/canvas-study/`。
