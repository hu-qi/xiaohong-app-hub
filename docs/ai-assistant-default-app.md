# XiaoHong AI — 默认助手与应用大厅启动策略（PR #2）

目标硬件：第一代 XiaoHong WS63，1.54 英寸 TFT（240×240），三个物理按键。

## 已接入的行为

| 系统状态 | 首屏与行为 | 备注 |
|---|---|---|
| 首次开机，尚无 Wi-Fi 配置 | 原有 Wi-Fi 配网页面 | 不打开 App Hub；+/- 组合键在配网期被忽略 |
| 已保存 Wi-Fi 配置，重启或开机 | 原有 AI 页面及现有联网/握手流程 | **不**自动显示 App Hub，不复制 AI Agent |
| 配网完成后 | 沿用原实现的配网成功提示、系统重启，再进入正常 AI 流程 | 不在配网成功回调直接切换到 App Hub |
| AI 页面，+/- 同按重叠至少 1000ms，松开任意一键 | App Hub | 事件交给 LvglTask 完成画面切换 |
| App Hub 默认焦点 | `AI Assistant`（内置系统应用，1/5） | `M Open` 返回原有 AI 页面 |
| App Hub 选择 `Snake`（2/5） | `M Start` 启动贪吃蛇 | 原有 240×240 游戏实现不变 |
| 游戏中 M 长按 | 回到 App Hub | 下次选择 AI Assistant 即可回到 AI |
| 应用大厅中 M 长按 | 回到原有 AI 页面 | 保留现有系统 AI 交互任务 |
| M 长按约 5 秒 | 继续执行原有 Wi-Fi 重新配网流程 | 由 MainTask 处理，不被 App Hub 截获 |

## 架构实现

- `app_registry.c` 注册 `XH_APP_ID_ASSISTANT=0`，`available=true`、`system_app=true`、`default_app=true`。
- 默认系统应用是**现有 AI UI 的轻量入口**。它没有 `on_create` / `on_destroy`，避免重复创建 AI Agent、音频和网络任务。
- App Hub 打开时优先选择具有 `system_app && default_app` 标记的条目。
- 对 AI Assistant 按下 M 后，调用 `xh_app_hub_close()` 恢复原来的 AI 屏幕，而不是执行游戏式启动。
- `MainTask` 在 `reconfig_wifi` 或 `eWifi_St_ReConfiguring` 时拦截 App Hub 组合键请求；其他时间沿用 LvglTask 队列处理。
- `+ / -` 单独长按仍保持亮度调节，双键组合打开或切换大厅。

## 运行限制

- `system_app` 是应用元数据；当前 `app_installer.c` 仍是占位实现，**不存在真实的安装、卸载和权限执行器**。
- 现有 AI 后台任务并未暂停，因此 **游戏中语音并发、音频焦点、语音设备控制**还没有完整实现。M 短按在游戏中属于游戏，不能假设也能唤醒 AI。
- GitCode `.xhapp` 分发、CodeArts 像素宠物、受控设备指令、SOS 都不属于 PR #2 的本次变更，尚未实现。
- 本 PR 的主机测试与 LVGL 9.2.2 离屏渲染不等于 WS63 固件编译或实机烧录；必须验证后再合并/Release。

## 回归测试

1. `tests/host/test_app_registry.c`：真实注册表仅一个默认系统应用，AI 无重复任务回调。
2. `tests/host/test_app_hub.c`：默认 AI 返回、游戏启动、失败回滚、未实现应用与 60 次页面生命周期。
3. `tests/real_lvgl/apphub_render.c`：真实 LVGL9 离屏渲染（先切到 Snake 保留截图验证样例）及回到默认 AI。
4. `tests/host/test_key_driver.c` / `test_volume_chord.c`：物理组合键、亮度、M 短按和 5 秒配网事件。
5. `scripts/check_app_hub_visual.py`：配网保护和 UI 流程静态检查。

所有验证结果以 GitHub Actions 实际执行为准；实机检查仍需在 WS63 固件生成后完成。
