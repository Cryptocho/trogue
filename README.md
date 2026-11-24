# bevy美术验证

用 Rust 和 Bevy 0.16.1 对照两种美术路线：HD-2D 夜林「苔径留灯」和纯 2D 像素河湾「青灯河湾」。

在 Linux Wayland 桌面使用硬件 GPU，从仓库根目录启动：

```sh
./tools/gloam/view.sh
```

按 **空格**切换画法，滚轮缩放，`F12` 截图，`Esc` 退出。加 `--2d` 可直接进入河湾；按 `R` 对比水面倒影。

实机截图：[HD-2D 夜林](reference/gloam-comparison/hd2d.png) · [像素河湾](reference/gloam-comparison/riverside/wide.png)
