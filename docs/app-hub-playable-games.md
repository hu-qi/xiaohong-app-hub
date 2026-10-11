# PR #2 — 240×240 App Hub 图标及小游戏交付说明

硬件目标：小鸿 AI 第一代 WS63，1.54 英寸 TFT，240×240，物理按键 `+`、`-`、`M`。

## 图标为什么改用 LVGL 原生图形

上一版只有 Snake 使用一张内嵌 32×32 RGB565 图，其他应用依赖单个字母占位。WS63 端的实际图像解码、颜色格式和像素结果尚未得到实机确认，不能依赖“图片描述符存在”作为显示成功的证据。

现在 **AI Assistant、Snake、Tetris、Breakout、LiquidDuck** 五个图标均用 LVGL 内置矩形、圆角和颜色绘制：14 个固定 `lv_obj_t` 图元循环复用，切换卡片时只更新坐标、尺寸、颜色和可见性。无图片解码器、外置字体、动态文件系统资源依赖。原 RGB565 图标源文件已停止链接并删除。

真实 LVGL 9.2.2 离屏测试通过后，才可认定**桌面 LVGL 层**可见；WS63 实机 240×240 显示仍需验证。

## 内置游戏（不需要下载、不代表动态安装）

| 应用 | 操作 | 完整玩法 |
|---|---|---|
| Snake | `+` 左转，`-` 右转，`M` 开始/暂停 | 12×12 网格、食物、增长、碰撞、得分及重开（已有功能） |
| Tetris | `+` 左移，`-` 右移，`M` 开始/旋转/重开 | 10×16 棋盘、7 种方块、下落/碰撞/锁定、消行、分数、游戏结束 |
| Breakout | `+` 球拍左移，`-` 右移，`M` 开始/暂停/重开 | 球、球拍、28 块砖、反弹、3 条生命、清关和失败 |
| LiquidDuck | `+` 鸭子左游，`-` 右游，`M` 开始/暂停/重开 | 接住蓝色水滴得分、避开红色污染物、3 条生命、得分提速 |

*所有游戏：长按 `M` 返回应用大厅；应用大厅中长按 `M` 返回原有 AI Assistant。* 应用大厅和系统配网、AI、按键组合方式继续沿用 PR #2 的原方案。

LiquidDuck 当前是专门适配三按键的 **接雨滴／避污染** 迷你游戏，并非二维流体物理引擎；后续如果需要流体模拟，应该另做资源预算及设计。

## 低内存设计

- Tetris 最初使用 160 个 LVGL cell 对象，真实 LVGL 自动运行时在创建游戏阶段段错误。已通过 CI 运行日志定位。
- 原 Snake 也使用了 144 个独立格子对象；现同样改为单个 LVGL 自绘对象（游戏状态只需约 288 字节的蛇身数组），减少堆压力。
- 改为 **一个自绘 LVGL 棋盘对象 + 160 字节静态棋盘数据**；`LV_EVENT_DRAW_MAIN` 绘制 160 格，游戏状态变化后对棋盘对象做 invalidate，无全屏帧缓冲。
- Breakout 只分配 28 个砖块、球和球拍对象；LiquidDuck 复用 6 个水滴对象及少量鸭子几何部件。
- 游戏逻辑全部使用 `xh_app_entry_t` 的 `create/destroy/on_key/on_tick`；不直接从 GPIO 中断操作 LVGL，也不自行启动额外线程。
- 动态资源（音频、外部图集、大文件）未引入。

## 验收与风险

1. 源码级测试：注册表全部 4 个游戏 `available=true`，且有非空游戏回调。
2. 真实 LVGL 9.2.2：逐个渲染 5 个图标并检查像素确有颜色变化；依次启动 4 款**真实源码**游戏，运行 360 次 tick 并执行左右操作；长按 M 返回应用大厅，最终回 AI。CI 输出 `icon-0.png` 至 `icon-4.png`、`game-1.png` 至 `game-4.png` 和兼容旧命名的 `apphub-240x240.png`，打包在 Actions 的 `apphub-lvgl9-240x240` artifact。截图是 Linux 上真实 LVGL 离屏渲染结果，不是 WS63 实机拍摄。
3. 本阶段不提供新的 `.fwpkg`，也没有宣称已经在 WS63 编译/烧录。
4. 下一步需要在原 OpenHarmony WS63 SDK 生成可追溯提交号的固件，检查 heap 水位、堆栈占用、长时间运行、界面刷新、按键手感，再烧录真机。

**不要用仓库早先的 `firmware/modified_all.fwpkg` 验收本次 PR：旧固件不包含这些新游戏及图标实现。**

## App Hub 按键导航校正

首页底部显示 `- Prev`、`+ Next`，实际按键处理已同步为 **短按 - 上一个、短按 + 下一个**；进入各游戏后按照各自的游戏按键规则操作。组合长按 `+/-` 约 1 秒后松开切换 App Hub。

## 2026-10-11: Build handoff and heap stress validation

- Added `scripts/ws63-build-handoff.sh` with **read-only `--check` as default**. `--stage` explicitly backs up the current SDK vendor files before overlay, and `--build` requires a staged commit and produces fresh firmware + SHA256 / build logs. Paths are listed in `scripts/ws63-overlay-manifest.txt`.
- First-generation WS63 LVGL target allocates a **240×20×2 = 9,600-byte partial RGB565 buffer** under OpenHarmony 6.1. The native LVGL 9.2.2 renderer now tests this same `LV_DISPLAY_RENDER_MODE_PARTIAL` path instead of using a full 115,200-byte buffer.
- After initial icon and game validation, CI runs **20 more full cycles**, visiting all four actual game implementations. In native LVGL, reported free heap was **53,232 bytes before and after** the cycles, with no sustained decrease. This is evidence for the host LVGL allocator only; it does not measure the WS63's total free SRAM/heap.
- The verified workflow is [PR #2 LVGL partial-buffer and stress checks](https://github.com/hu-qi/xiaohong-app-hub/actions/runs/38112769682).
- **Cross-compile and device flash are still NOT complete.** Read the corrected [firmware build and flashing guide](../firmware/FLASH_AND_TEST_GUIDE.md), and do not flash any old `firmware/*.fwpkg` as a build of this PR.
