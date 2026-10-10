# XiaoHong App Hub v1.0 — Mac 烧录和测试指南

## 固件文件说明

| 文件 | 大小 | 说明 |
|------|------|------|
| `baseline_all.fwpkg` | 1.89MB | 官方原版固件（不含 App Hub） |
| `modified_all.fwpkg` | 1.90MB | App Hub 修改版固件（含贪吃蛇游戏） |
| `baseline_load_only.fwpkg` | 1.77MB | 官方原版 load-only 模式 |
| `modified_load_only.fwpkg` | 1.78MB | App Hub 修改版 load-only 模式 |

## Mac 烧录步骤

### 1. 安装 ws63flash 工具

```bash
# 从 GitCode 克隆
git clone https://gitcode.com/xiaohong-ai/ws63flash.git
cd ws63flash
# 按照仓库 README 安装依赖
```

### 2. 连接设备

- 用 USB 线连接 XiaoHong 设备到 Mac
- 确认设备被识别（`ls /dev/tty.usb*` 应能看到串口设备）

### 3. 烧录固件

```bash
# 烧录 App Hub 修改版固件（推荐）
ws63flash firmware/modified_all.fwpkg

# 或烧录官方原版固件（对比测试）
ws63flash firmware/baseline_all.fwpkg
```

### 4. 验证烧录成功

- 设备重启后，LCD 屏幕应显示主界面
- 修改版固件：主界面应包含 AI 对话等原有功能

## App Hub 使用方法

### 打开应用大厅

- **长按 VolDw（音量减键）** → 打开应用大厅

### 应用大厅界面

- 屏幕显示"应用大厅"标题
- 列表显示已注册的应用（贪吃蛇、Tetris、Breakout、LiquidDuck）
- 底部提示："Vol+/-选择 Wakeup确认"

### 选择和启动应用

- **Vol+ / Vol-** → 上下移动选择
- **Wakeup 短按** → 确认/启动选中的应用
- **Wakeup 长按** → 退出应用大厅

### 贪吃蛇游戏操作

- **Vol+** → 向上移动
- **Vol-** → 向下移动
- **Wakeup 短按** → 确认（开始/暂停）
- **Wakeup 长按** → 退出游戏

> 注意：Tetris、Breakout、LiquidDuck 为预留接口，选择后会显示"即将推出"提示。

## 对比测试

### 基线测试（官方原版）

1. 烧录 `baseline_all.fwpkg`
2. 验证 AI 对话、Wi-Fi、语音等原有功能正常
3. 长按 VolDw → 应无反应（没有 App Hub）

### 修改版测试（App Hub）

1. 烧录 `modified_all.fwpkg`
2. 验证原有功能仍正常工作
3. 长按 VolDw → 应打开应用大厅
4. 选择"贪吃蛇" → 游戏应正常启动
5. 玩游戏验证按键响应正常
6. 长按 Wakeup → 退出游戏返回应用大厅
7. 再次长按 Wakeup → 退出应用大厅返回主界面

## 故障排除

### 烧录失败

- 确认 USB 线是数据线（不是纯充电线）
- 尝试不同的 USB 端口
- 检查 ws63flash 是否有权限访问串口设备

### App Hub 无法打开

- 确认烧录的是 `modified_all.fwpkg`（不是 baseline）
- 长按 VolDw 至少 1 秒
- 检查 LCD 屏幕是否正常显示

### 贪吃蛇游戏卡顿

- 游戏 tick 间隔为 100ms，属正常
- 如果完全无响应，检查按键是否正常工作

## 技术信息

- 芯片：HiSilicon WS63（RISC-V rv32imfc）
- 系统：OpenHarmony LiteOS-M
- 显示：240×240 LCD (ST7789)
- 内存：348KB SRAM
- App Hub 内存占用：~15KB（对象网格渲染，非 Canvas）
- 编译时间：~52 秒（8核 32GB ECS）
