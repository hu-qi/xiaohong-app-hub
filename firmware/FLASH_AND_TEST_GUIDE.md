# 第一代小鸿 WS63：固件与硬件按键安全说明

> **重要：Vol+ 与 Vol- 同时按住是小鸿硬件复位操作。禁止把这组按键定义为应用大厅入口。**
> 本文替换了旧版 v2.2.0 指南中已过时的按键与烧录说明。

## 当前软件状态

- 首次无 Wi-Fi 配置：沿用原有配网流程。
- 已配网：系统仍以 AI Assistant 为默认界面。
- App Hub：保留五个内置应用入口：AI Assistant、Snake、Tetris、Breakout、LiquidDuck；游戏源码已有主机端 LVGL 验证，但实际 WS63 整机测试仍要逐项验收。
- **应用大厅安全入口：快速双击 `M`**（两次短按释放的间隔不超过 350ms）。AI 页面双击打开大厅；大厅列表双击返回 AI；游戏中双击返回大厅列表。此前 `Vol+ / Vol-` 双键长按设计已永久撤销。
- 单独 Vol+ / Vol- 短按：原有音量功能（进入游戏后作为左右游戏键）。
- 单独 Vol+ / Vol- 长按：原有屏幕亮度调节。
- 单击 `M`：保持原 AI 唤醒或游戏确认，最多延迟约 390ms；长按 `M`：保持原语音/游戏行为；约 5 秒保留 Wi-Fi 重配。
- **千万不要通过实机长按 Vol+ 和 Vol- 来测试软件切屏**，该动作会触发硬件复位，不经过应用层处理。

## 固件构建与验证

仓库 `firmware/*.fwpkg` 中的既有二进制文件，不应视为本次安全修复版本。**合并源码不会修改设备已烧录的固件。**

在独立且配置完整的 OpenHarmony 6.1 / WS63 构建环境中，从对应 Git 提交重新编译，并保留构建日志、源码 SHA、固件 SHA256。仓库脚本支持只读核对、备份后同步、编译留证：

```bash
git rev-parse HEAD
bash scripts/ws63-build-handoff.sh --check /ABS/PATH/TO/OHOS
# 验证实际源文件来自 xiaohong/xiaohong/xiaohong/
bash scripts/ws63-build-handoff.sh --stage /ABS/PATH/TO/OHOS
bash scripts/ws63-build-handoff.sh --build /ABS/PATH/TO/OHOS
```

严格核对实际构建目标为 `vendor/atomgit/xiaohong/xiaohong/BUILD.gn`，且其中未链接 `volume_chord.c`。安全修复版本应不含 `eKey_AppHub_Toggle` 或 `eDisp_AppHub_Toggle`。生成的新固件仍需在真机验证亮度、音量、单击/双击 M、M 长按、M 5 秒配网、配网/AI/音频及四款游戏。

**禁止直接拿旧固件作验收，也不要因为 GitHub CI 通过就推定已经完成 WS63 烧录。**
