# 第一代小鸿 WS63 — PR #2 固件构建与实机验收指南

> **重要：目前仓库 `firmware/` 下的所有 `.fwpkg` 都是此前编译的旧固件。**
> **它们不能用来验收 PR #2 的新版图标和四款完整小游戏。**
> 新固件必须以 PR #2 的**最新 SHA**在完整 OpenHarmony 6.1 工具链重新构建。

## 已确定的正确启动与按键行为

- **首次开机/无 Wi-Fi 配置**：保持原厂配网界面。
- **配网成功后**：沿用成功提示及重启流程，启动原有 AI Assistant（默认系统应用），**不会自动进入 App Hub**。
- **进入/关闭 App Hub**：同时按下 `+` 和 `-`，两键重叠保持至少 1 秒，松开任意一键。该行为在真实硬件上仍需验证。
- **App Hub 选择**：短按 `-` 上一个；短按 `+` 下一个；短按 `M` 打开选中的应用。
- **游戏内**：短按 `+`、`-` 依各游戏控制；短按 `M` 开始/操作；长按 `M` 退出到 App Hub。
- **单独长按 `+` / `-`**：继续调节亮度。长按 `M` 5 秒仍保留原厂重新配网入口。
- **五个内置入口**：AI Assistant、Snake、Tetris、Breakout、LiquidDuck；后四者已通过真实 LVGL9 主机测试，但**尚未进行 WS63 实机验收**。
- **安装/卸载**：仍未支持。它们是静态编译到固件的应用，不能误称 GitCode 动态可安装应用。

## 在已有完整 OpenHarmony 6.1 工作目录上构建

完整 OpenHarmony 工作目录例如 `/path/to/ohos-6.1`，其中应存在：

```text
.repo/
vendor/atomgit/xiaohong/xiaohong/BUILD.gn
device/soc/hisilicon/ws63v100/sdkv106/
```

从单独的 `xiaohong-app-hub` PR #2 源码检出开始：

```bash
git checkout feat/app-hub-visual-240x240
git rev-parse HEAD

# 1. 只读检查，绝不覆盖 SDK 内容
bash scripts/ws63-build-handoff.sh --check /ABS/PATH/TO/OHOS

# 2. 显式同步 PR 相关文件到 OHOS；先备份全部原版文件
bash scripts/ws63-build-handoff.sh --stage /ABS/PATH/TO/OHOS

# 3. 在已配置 hb 的 Linux 编译终端上执行，保留完整日志和 SHA256
bash scripts/ws63-build-handoff.sh --build /ABS/PATH/TO/OHOS
```

脚本不会操作 GitHub main、不会 SSH、不会创建 ECS、也不会烧录设备。构建证明在：

```text
/ABS/PATH/TO/OHOS/out/xiaohong/pr2-handoff/<source-sha>/
    staged.ok
    backup/
    hb-build.log
    firmware.sha256
    provenance.txt
```

仅当 `hb build -f` 成功、生成**新时间戳的** `ws63-liteos-app_all.fwpkg`、SHA256 完整时，才可认定固件构建通过。

正式产物路径：

```text
out/xiaohong/xiaohong/ws63-liteos-app/ws63-liteos-app_all.fwpkg
```

**不要把上述路径的固件产物自动上传、烧录、OTA 或发布。**

## Mac mini 烧录与实机测试（仍待执行）

1. 建议首先保留原固件与 NV/配网资料；对全量固件与仅升级固件的影响做独立确认，避免抹掉配网记录。
2. 使用现有 `ws63flash` 或受支持的 WS63 烧录工具，并在烧录前核对固件来源、芯片与串口（之前的 `/dev/cu.usbserial-310` 只是历史记录，不能假设当前仍存在）。
3. **先只读检查可用串口，征得明确烧录确认后再执行写入**；观察串口输出有无异常重启、内存错误或死循环。
4. 验证首次配网/复连、默认 AI、`+/-` 组合键、单独长按亮度、`M` 5 秒配网、AI 语音/音量和游戏输入。
5. 每款游戏连续运行 10 分钟、退出/重进 20 次；记录最低空闲堆、栈余量、帧刷新间隔、游戏分数与异常日志。若因 heap 不足、UI 绘制阻塞或输入冲突而失败，不应合并。

## 测试证据与合并门禁

- 主机端：GitHub Actions 的 C 源码 / 真 LVGL 9.2.2 / 状态与内存循环测试，**不等于 WS63 交叉编译**。
- WS63：要求完整 `hb-build.log`、固件 SHA256 和对应 Git Commit ID。
- 硬件：要求真机开机与游戏操作视频/图片、串口日志以及功能回归表。
- **三者齐全才合并 PR #2 并考虑 Release**；当前 PR 保持 Draft，旧固件一律不能作为验收依据。

详细游戏控制与低内存设计见 [docs/app-hub-playable-games.md](../docs/app-hub-playable-games.md)。
