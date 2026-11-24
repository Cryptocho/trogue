# 青灯河湾：河岸参考落地实景

[全景实机](wide.png) · [近景实机](detail.png) · [关闭水面精灵倒影](no-mirrors.png) · [左右验光与对照页](../index.html)。这些 PNG 来自 Bevy 主窗口。用作生成输入的是[确认的风格图](../art-direction/riverside-study-02.webp)，不是游戏里的一层背景。

## 运行

`tools/gloam/view.sh --2d` 从河湾开始。`空格` 切换到原 HD-2D 夜林并能切回来，两边保持缩放；地形不再相同。滚轮缩放，`R` 只切换水中倒影，`←` / `→` / `0` 切换苔石左灯、右灯和正常夜景，`N` 切换精灵法线，`S` 切换地面接触影，`F` 切换雾，`B` 切换 Bloom，`F12` 保存截图，`Esc` 退出。场景静止，无战斗、弹幕或 UI。

## 使用的素材

`generate_image` 接收同一张风格图作为实际图像输入，分别生成三种树、三件主物与草土材料样本。保留生成请求和原始结果，按参考尺度加工为少色、二值透明度的独立颜色图；每个精灵另有线性法线图。制图用的输入不需要随运行资产分发。

|资产|运行数据|原图、加工与选择记录|
|---|---|---|
|圆冠树、云杉、窄柏|[树清单与根部锚点](../../../assets/gloam/riverside/trees/manifest.json)|[来源](../../../tools/gloam/riverside/trees/source.json) · [未受光与两侧验光素材表](../../../tools/gloam/riverside/trees/review.png)|
|蓝兜帽旅人、苔石、直柱黄灯|[物件清单、脚点与玻璃中心](../../../assets/gloam/riverside/props/manifest.json)|[来源](../../../tools/gloam/riverside/props/source.json) · [独立精灵图](../../../tools/gloam/riverside/props/review/sprites-review.png)|
|草岸与潮土|[单独的地表色图](../../../assets/gloam/riverside/bank-color.png)|[请求与原画记录](../../../tools/gloam/riverside/bank/source.json) · [原生笔触](../../../tools/gloam/riverside/bank/swatches-review.png)|

离线复现（仓库根目录）：

```sh
uv run --project tools/gloam/terrain tools/gloam/riverside/trees/prepare.py
uv run --project tools/gloam/terrain tools/gloam/riverside/props/process.py
uv run --project tools/gloam/terrain tools/gloam/riverside/bank/prepare.py
```

[地图布局](../../../assets/gloam/riverside/layout.json)规定草岸形状、浅水亮区和精灵脚点；常规画幅约 30.4% 是水。暖光位置取实际灯图的玻璃中心，不取灯脚。地面阴影短而实；水下青绿渐变、岸线亮边与暖灯倒影分开。

**怎么形成倒影：**同一对象的原图、法线、光照和玻璃蒙版再绘制一次，绕低于草岸的水面翻转。只保留落在水域蒙版内的片元，水纹让少数行错开一两格。不是一条假金线，也不是每帧做三维反射追踪。草木使用有限材质色；光晕与水下衰减在 512×288 画布内平滑计算，避免硬色带。最后整数 5 倍复制到真实 2560×1440 窗口，没对物件轮廓做双线性模糊。

## 验证

- 原生 Wayland 同窗测试从河湾开始。真实 `R` 移去水中虚像，滚轮放大，按空格去 HD-2D 再回来；返回前后的缩放画面差 **0 个画布像素**。测试进程正常退出。
- 关闭 Bloom 隔离光学散射后，`R` 改变 **16606 个水中像素，岸上 0 个**。在按玻璃原始蒙版翻转计算的位置，139 个像素平均红通道增加 105.06/255；对岸柏树在青水中的 1382 个剪影像素平均亮度下降 35.01/255。只关闭倒影时水底青光仍存在。
- 所有发布的窗口图都通过 `5×5` 同色方块检查。左右白灯用同一强度、仰角和距离。按原始法线横轴取近景石身中段，而不是凭截图挑最亮点：

|石身朝向|画布像素数|左灯亮度 /255|右灯亮度 /255|
|---|---:|---:|---:|
|朝左|289|139.75|46.75|
|朝右|454|28.33|112.13|

[图像测量脚本](../../../tools/gloam/riverside/check_view.py)。本机单项控制图、键盘测试和采样日志在 `captures/gloam/riverside/`。全景稳定样本：release `--locked --no-default-features`，Ryzen 5 5500 / RX 6500 XT，RADV Mesa 26.2.7 / Vulkan，Wayfire 原生 Wayland，DPI 1，100 Hz、Mailbox/VSync。固定河湾布局、静态灯、512×288 世界画布、2560×1440 最终合成，正常缩放 25 px/m。加载后预热至少 2 秒与 32 帧；5.001 秒内 3859 个应用帧，中位 1.25 ms、P95 1.57 ms。不含截图回读，窗口全程有焦点，Wayland 未报告最小化状态，应用没有附加帧率上限；应用帧数不等于屏幕显示帧数。
