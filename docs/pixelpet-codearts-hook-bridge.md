# XiaoHong PixelPet — CodeArts Vibe Coding 状态宠物（产品定义）

> 状态：产品定义 / 后续独立实现；**不属于 PR #2 现有已实现的功能**。
> 硬件：第一代小鸿 AI / WS63 / 1.54" TFT / 240×240 / +、-、M。
> 设计原则：像素宠物是 **本地 CodeArts 开发活动的具象化显示器**，而不是另一款 LLM 聊天应用。

## 1. 准确定义

小鸿宠物（PixelPet）通过局域网接收开发者电脑上 **CodeArts IDE 或 CodeArts CLI 的 Hooks** 所产生的编码状态事件，将 Vibe Coding 的进度以像素表情、动作和简短状态文字呈现在小鸿的 1.54 英寸屏幕上。

核心价值：让桌上的小鸿成为「CodeArts 智能编程的实体状态灯／桌面宠物」。

**不做**：把 CodeArts 当作宠物对话大模型；传输 Prompt、源代码、终端输出或 API Key；要求 WS63 运行 IDE、CodeArts CLI、AgentTeam；把无关 AI 聊天宠物能力塞进本功能。

## 2. 真实 Hook 来源

华为云官方用户指南（2026-09）确认，CodeArts CLI 和 IDE/插件支持 JS/TS 生命周期 Hooks，包括：

- `chat.message`：用户发起任务（进入会话处理前）；
- `tool.execute.before` / `tool.execute.after`：工具调用开始／结束；
- `chat.error`：模型请求报错；
- `chat.finished`：本次聊天会话结束；
- `user.approval` / `permission.ask`：等待人工审批、请求权限；
- `turn.end` 和 `event`：轮次结束／通用总线事件。

实际“代码生成成功”“单测通过”“PR 合并”等应以可观测的工具／CI 事件为准，**不能仅凭一次 `tool.execute.after` 就断言测试通过**。IDE 和 CLI 版本的 Hook 载荷必须分别做适配与实测。

参考：
- CLI Hooks: https://support.huaweicloud.com/usermanual-cli/codeartsagent_cli_0018.html
- IDE Hooks: https://support.huaweicloud.com/usermanual-codeartsagent/codeartsagent_ug_0036.html

## 3. MVP 架构（局域网优先）

```text
开发电脑（Mac / Windows / Linux）
    CodeArts IDE Hooks          CodeArts CLI Hooks
            \                       /
             \                     /
              `-- CodeArts Hook Adapter --`
                          |
                    标准化状态事件
                          |
                 本机桥接服务（Bridge）
                127.0.0.1 Hook 收件箱
              局域网只读状态端点 /v1/state
                          |
             Wi-Fi LAN：HTTP GET / WebSocket
                          |
                      WS63 / LiteOS-M
                          |
                   PixelPet 状态机
                          |
                 LVGL 240×240 像素宠物
```

- Hook 插件仅把**最小化的状态事件**发送给开发电脑上的 loopback Bridge。
- Bridge 维护最新可信状态和时间戳，在 LAN 为已配对设备提供只读访问。
- WS63 MVP 优先采用**低频 HTTP 轮询**（例如每 1–2 秒）以减少新依赖；WebSocket 可在测得 WS63 栈和资源后升级。
- 不扫描整个局域网；第一版从已知 IP 或配对配置接入，后续再验证 mDNS 服务发现支持。
- AI Assistant 仍是系统默认应用，PixelPet 作为 App Hub 的**可选前台应用**；不存在 CodeArts 时无需阻塞配网或 AI 功能。

## 4. 规范化状态与宠物动画

| 状态 | 触发依据（示例） | 宠物表现 |
|---|---|---|
| `disconnected` | 桥接端点不可达、超出心跳 TTL | 睡觉、灰色、断连小标记 |
| `idle` | 桥接在线但无活跃任务 | 休息、眨眼 |
| `thinking` | 收到新任务／推理阶段信号 | 思考气泡、抬头 |
| `coding` | 文件写入／修改类工具运行 | 电脑前敲代码 |
| `running` | 终端命令、编译、测试工具执行中 | 忙碌、旋转齿轮 |
| `waiting_approval` | 用户审批请求 | 举手、问号 |
| `success` | 可确认的阶段完成／成功结果 | 开心、跳跃；短暂显示 |
| `error` | chat.error 或明确的工具失败 | 困惑／难过，提示失败 |
| `offline` | Bridge 在线、CodeArts 当前会话未启动 | 休眠、等待连接 |

区分 `disconnected` 与 `offline`；不可由单纯“没有收到事件”推断任务成功。状态有短暂动画与 TTL，避免过期状态卡死。多会话并发须定义当前聚焦会话、优先级和切换规则。

## 5. 网络协议（提案，非已实现）

Bridge `GET /v1/state` 返回的只读最小 JSON：

```json
{
  "schema": 1,
  "source": "codearts-cli",
  "session": "local-anonymized-01",
  "state": "coding",
  "activity": "editing",
  "seq": 104,
  "timestamp_ms": 1791000000000,
  "ttl_ms": 10000
}
```

- `source` 可为 `codearts-ide`、`codearts-cli`；`activity` 只采用固定枚举。
- 不包含代码路径、Prompt、文件内容、命令行参数、令牌或 stdout/stderr。
- `seq` 避免旧事件覆盖新事件；`ttl_ms` 过期则显示断连或待机。
- 事件时间戳仅示意，Bridge 以自身接收时间和会话心跳控制有效性。
- 设备端不直接执行任何 Hook 下发的命令。

## 6. 安全边界

- Hook 收件 API 仅监听 `127.0.0.1`；LAN 端点默认只读。
- 使用一次性配对口令／设备令牌（避免公开局域网任何人能冒充状态）；限制 Origin/Host，做速率限制、最大报文长度和重放过滤。
- 不要把 CodeArts 的云凭据、模型 Token 或 IDE 权限暴露给 WS63。
- 优先在可信私人局域网验证；如需跨 VLAN／公网必须另做 TLS／身份验证设计。
- Hook 转发失败不得阻断 CodeArts 工具执行，所有传输都是 best-effort、短超时、失败吞吐和本地缓存。
- Hook 不应改变 tool 参数、LLM 消息、权限判定或执行结果，只做只读采集。

## 7. 接受标准（MVP）

1. CodeArts CLI/IDE 各安装一个观察型 Hook，真实开发会话事件能被 Bridge 接收到（两个入口逐一验证）。
2. WS63 与开发电脑处于同一可达局域网，设备成功读取状态 API（先主机 mock，再真机）。
3. 独立像素宠物按 `idle → thinking → coding/running → success/error → idle` 正确切换。
4. 开发电脑断网、CLI 退出、Bridge 退出等场景显示超时态；恢复后无需刷固件。
5. 不破坏原有 AI Assistant、Wi-Fi 首次配网、三个物理键、亮度、音量。
6. 非法／畸形 JSON、无权限请求和重放事件不会导致固件崩溃。
7. 通过桌面 Mock 状态源开发 UI；后续切换真实 CodeArts Hook 仍使用相同协议。

## 8. 项目边界

PR #2 当前只关注第一代小鸿的 App Hub 基础、默认 AI Assistant、组合键和 LVGL 渲染。**不要在 PR #2 中夹带尚未完成的 CodeArts Bridge / PixelPet 功能**。后续应单开独立 PR：先桌面 Hook Adapter/Bridge 与模拟状态，再做 WS63 PixelPet 显示接入，最后系统集成。
