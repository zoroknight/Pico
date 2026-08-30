# 第 7 月第 4 周：AI Chat Workspace 与真实 Provider

本阶段把前三周的任务系统、可恢复 Agent Runtime 和安全工具管线接入 PicoEditor。目标不是让模型直接控制
任意 C++ 接口，而是建立一条用户可见、可审批、可撤销、可恢复的最小编辑器 Agent 闭环。

## 完整调用链

```text
AI Chat 输入
 -> PicoTask Worker
 -> Agent Runtime + Session JSONL
 -> Fake / DeepSeek / Kimi Provider
 -> Tool Call
 -> Approval UI
 -> Game Thread Dispatcher
 -> EditorAgentToolExecutor
 -> Validate / Permission / Transaction / Execute / Verify
 -> World + Editor Undo
 -> Tool Result + Trace
 -> Provider 下一轮或最终回答
```

HTTP 和模型规划在 Worker 执行。创建对象、读取或修改 World、更新 Selection 和提交编辑器事务都通过
`FGameThreadDispatcher` 回到 Game Thread。窗口关闭或编辑器退出时先拒绝尚未处理的审批并请求取消，随后
`FTaskSystem::Shutdown` 等待 Worker 退出，最后才销毁工作区和 Dispatcher。

## AI Chat Workspace

PicoEditor 默认把 `AI Chat` 停靠在 `Details` 一侧，也可以通过 `View -> AI Chat` 重新打开。窗口包括：

- Provider 与模型选择。
- 上次使用的 Provider 与各 Provider 的 Model 保存到源码检出目录下、Git 忽略的
  `Saved/Editor/Agent/ChatSettings.ini`，重启编辑器或打开其他项目时自动恢复；项目交接参数优先。
- 当前 Provider、模型、会话名称和独立 Session 文件诊断。
- 请求超时和有限重试设置。
- 可恢复且可审计的 User、Assistant、Tool Call、Tool Result 和 Trace 历史。底层 JSONL 仍逐事件保存，聊天正文
  将同一用户请求中的 Assistant 操作描述合并为连续内容，并在该轮末尾只显示一个默认折叠的工具调用摘要。
- 工具摘要折叠时显示调用、成功和失败数量；展开后显示工具表格，每个工具还能单独展开参数、结构化结果和 Trace。
- 同一 Provider 下可新建、切换和删除多个会话；删除前必须确认，当前会话删除后自动选择剩余会话或创建空会话。
- 打开窗口、切换会话或收到新消息时自动定位到最新记录。
- 每个消息气泡都有复制图标；双击气泡可进入只读文本选择视图并使用 `Ctrl+C`，无需全局 `Selectable` 开关。
- 用户消息与 Agent 输出保持左对齐：用户轮次使用低饱和蓝灰背景和蓝色强调线，Agent 输出使用更明亮的浅绿色
  背景和绿色强调线；错误消息使用低饱和深红背景和红色强调线。三者保留额外轮次留白，并维持适合长
  Markdown、表格和代码的宽布局。
- 使用 MD4C 解析 Markdown；标题、列表、引用、链接、行内代码和围栏代码具有独立样式，JSON 工具参数、结果和
  Trace 会先格式化再作为 `json` 代码块呈现。
- 当前 ImGui 字体图集不可靠支持彩色 Emoji。Provider 会被要求使用无 Emoji 的普通 Markdown；旧会话或未遵守
  约束的输出在绘制时省略补充平面 Emoji、变体符、连接符，以及闪电、雪花、星形等装饰性符号，只保留
  对号/叉号状态标记。对于 16 位 ImGui 字形范围在解码阶段产生的 `U+FFFD` 替代码点也会直接省略，因此不会显示
  问号乱码；这些过滤均不修改 Session 原文与复制内容。
- 代码块提供独立复制按钮；超过 12 行时默认折叠并可展开，代码块本身不建立滚轮区域，由外层聊天记录统一滚动。
- 修改工具的 Approve/Reject 面板。
- Send、Cancel 和运行状态。

为防止 Fake 脚本历史影响真实模型、或不同供应商互相污染上下文，会话按 Provider 隔离保存：

```text
<Project>/Saved/Agent/Sessions/editor-chat-<provider>-<timestamp>-<sequence>.jsonl
```

会话索引记录显示名称、创建时间和最近使用时间，正文仍是可审计的追加式 JSONL Event Log。切换 Provider 时
窗口只显示该 Provider 的会话；旧版固定文件和混合文件保留在磁盘用于诊断，但不再污染新会话上下文。重启编辑器
后会恢复最近使用的 Provider 与会话，而不是依赖模型供应商的隐式记忆。正常完成的新一轮对话保留同一会话历史，
但使用新的步骤、工具和修复预算；只有恢复尚未完成的 Run 才继承 Checkpoint 计数。运行期间 Provider、Model 和
Session 被锁定，Worker 同时捕获当轮会话路径，避免切换导致跨会话写入。

## Provider

`FOpenAICompatibleProvider` 实现 OpenAI-compatible Chat Completions 协议，目前配置两个官方入口：

| Provider | Endpoint | 默认模型 | 密钥环境变量 |
| --- | --- | --- | --- |
| DeepSeek | `https://api.deepseek.com/chat/completions` | `deepseek-v4-flash` | `DEEPSEEK_API_KEY` |
| Kimi | `https://api.moonshot.cn/v1/chat/completions` | `kimi-k2.6` | `MOONSHOT_API_KEY` |

接口依据 [DeepSeek Chat Completion](https://api-docs.deepseek.com/api/create-chat-completion/)、
[DeepSeek Tool Calls](https://api-docs.deepseek.com/guides/tool_calls) 和
[Kimi API Overview](https://platform.kimi.com/docs/api/overview)。模型名仍是窗口中的可配置值，避免把供应商迭代
写死在引擎架构中。Pico 当前对 DeepSeek V4 显式使用非思考模式；思考模式的 Tool Call 需要额外持久化并回传
`reasoning_content`，在该协议进入 Session Event 之前不开放，避免第二轮请求被服务端拒绝。

Windows 使用 WinHTTP HTTPS Transport。HTTP 408、429、5xx 和临时传输错误会按 `Retry-After` 或有限指数退避
重试；等待退避时可取消。本阶段最初使用同步完整响应，后续已经增加 SSE Streaming，见
[`AIPhase08_StreamingKnowledgeRagAndSkills.md`](AIPhase08_StreamingKnowledgeRagAndSkills.md)。

编辑器可以把开发期密钥保存到当前 Pico 源码/安装副本专属的编辑器本地文件：

```text
<PicoEngineRoot>/Saved/Editor/Agent/ApiKeys.ini
```

该文件是明文开发配置，不写入 Windows 系统凭据，也不进入 `.pico`、`Pico.ini`、场景或资产。引擎根目录的
`Saved/` 与所有 `ApiKeys.ini` 都被 Git 忽略，Packager 也不收集引擎 `Saved`，因此正常提交和打包不会携带密钥。
它位于项目根目录之外，Agent 的文件参数又被固定限制在当前 `ProjectRoot`，同时工具注册表没有读取凭据的工具；
密钥也不会进入 Prompt、请求 JSON、Session、Trace 或日志。这里的安全边界是 Pico Agent 工具层，不是操作系统
加密：同一 Windows 用户下拥有文件系统权限的其他原生程序仍可读取这个明文文件。

在 `AI Chat -> API Key (Editor Local)` 中输入后点击 `Save / Replace Key` 即可保存；界面不会回显已经保存的值，
`Remove Saved Key` 会删除当前 Provider 的值，最后一个值被删除时同时删除文件。同一 Pico 编辑器副本打开的所有
项目共享该文件，创建项目或项目进程交接无需复制密钥。首次升级时，如果新位置还没有对应 Provider 的密钥，
Pico 会读取当前项目旧的 `<Project>/Saved/Agent/ApiKeys.ini`，迁移到编辑器本地位置并删除旧副本。

环境变量优先级高于项目本地文件，可以在启动编辑器前临时设置：

```powershell
$env:DEEPSEEK_API_KEY = "your-key"
$env:MOONSHOT_API_KEY = "your-key"
```

密钥只进入 `Authorization: Bearer` Header，不进入请求 JSON、Session、Trace 或日志。不要把密钥写进 `.pico`、
`Pico.ini`、Actor Blueprint、Content 或源代码；正式发布和 CI 应优先使用环境变量或外部 Secret 服务。

## 工具协议

Pico 工具使用带点的稳定名称，例如 `editor.actor.spawn`。Provider 会在请求侧转换为 API 允许的
`editor_actor_spawn`，收到 Tool Call 后再映射回 Pico 名称。Assistant 的 Tool Call 和对应 Tool Result 会以
`tool_call_id` 重建到后续模型请求，因此真实模型可以完成多轮“调用工具 -> 查看结果 -> 再调用工具”。

模型生成的参数永远视为不可信输入，仍必须通过第三周固定的 Schema、权限、审批、事务和后置条件验证。

## 可视化验收

无需 API Key 的确定性流程：

1. 启动 `BuildCodex/Debug/PicoEditor.exe` 并打开 PicoSandbox。
2. 打开 `View -> AI Chat`，Provider 选择 `Fake Scene Agent`。
3. 输入任意场景请求并点击 Send。Fake Provider 固定先调用 `editor.world.describe`，验证只读工具无需审批。
4. 出现 `editor.actor.spawn` 审批时点击 Approve，验证修改前必须获得用户批准。
5. 出现 `editor.actor.set_location` 审批时点击 Approve，验证第二次修改拥有独立参数和事务。
6. 在 Scene Outliner 和 Viewport 中确认新增 `AI_Cube_*` 位于 `(150, 0, 100)`。
7. 展开该轮末尾的工具调用摘要，再展开对应 Tool，确认 Trace 包含 Validate、Permission、Approval、Transaction、
   Execute、Verify 和 Commit；主时间线中不应再夹杂独立 Tool Call/Result 气泡。
8. 连续执行两次 `Ctrl+Z`：第一次撤销移动，第二次删除 Cube，验证 Agent 复用普通编辑器 Undo。
9. 再运行一次并在任一修改审批点击 Reject，确认场景无该次副作用。
10. 重启编辑器，确认对话、Tool Call、结果和 Trace 从 Session 文件恢复。

聊天工作区补充验收：

1. 点击 `New Chat` 创建两个会话，分别发送消息，来回切换并确认历史互不混合。
2. 删除一个非当前会话并确认需要二次确认；删除当前会话后确认窗口选择了仍存在的会话。
3. 重启编辑器，确认恢复最近会话并自动滚动到最后一条消息。
4. 点击任一气泡右上角复制图标，再双击气泡框选部分文字，分别验证整条复制与局部复制。
5. 让 Provider 返回列表和 JSON，确认 JSON 以格式化代码块显示；展开长代码块并滚动，确认滚轮仍控制聊天记录。
6. 让 Provider 返回 GFM 表格，确认它显示为带表头、边框和交替行底色的真实行列布局，并在窄窗口内自动换行。
7. 让 Provider 返回 `✓ ✗ ☑ ☐ → ← ★`，确认编辑器合并的 Segoe UI Symbol 字形能够直接显示，不出现问号或空框。
8. 让 Provider 返回包含 Emoji、闪电、雪花和星形的旧式内容，确认界面省略这些装饰符号且中文连续显示，同时
   `✓/✗` 等状态符号仍可见；复制气泡时 Clipboard 仍包含原始字符。
9. 执行一次包含多个查询和修改工具的任务，确认 Assistant 操作描述连续显示，最后只有一个工具摘要；展开摘要
   查看总表，再逐项展开参数、结果和 Trace。
10. 连续发送两轮消息，确认用户消息仍然左对齐且拥有蓝灰背景和蓝色强调线；Assistant 使用浅绿色底纹和
    绿色强调线，表格和代码块宽度没有受到不必要限制。触发一次 Provider 或工具错误，确认错误消息具有深红
    底纹和红色强调线。

真实 Provider 流程相同，在编辑器本地保存密钥或设置相应环境变量后选择 DeepSeek 或 Kimi。真实 API 会产生外部
请求和可能的计费，因此自动化测试不会默认调用。

## 自动化验收

- `PicoAgentTests` 验证 429 重试、API 工具名映射、多轮 Tool Call/Result 协议，以及密钥不进入 JSON。
- `PicoEditorTests` 使用隔离凭据路径验证保存、读回、删除，以及旧项目密钥向编辑器本地位置的一次性迁移；测试
  不会读取或改写开发者真实的 `ApiKeys.ini`。
- `PicoAgentTests` 验证完成后的下一轮保留历史但重置每轮预算。
- `PicoEditorTests` 运行完整 Fake Scene Agent，真实创建并移动 Cube，再通过两次 Editor Undo 完整撤销。
- `PicoTaskTests` 继续覆盖取消、异常、Dispatcher 帧预算和安全关闭。
- 当前 Debug 基线为 `PicoAgentTests` 38/38、`PicoEditorTests` 129/129。
- Progress Ledger、语义查询缓存和无进展保护见
  [`AIPhase07_ProjectHandoffAndLoopControl.md`](AIPhase07_ProjectHandoffAndLoopControl.md)。

## 当前边界

- 当前除 World/Selection、创建和位置工具外，还开放对象描述、单属性读取、批量反射属性修改、资产搜索、碰撞
  房间、第三人称角色装配、Blueprint Actor 生成/删除、World 校验/保存、项目创建、Play/Stop 和 Package；详见
  [`AIPhase05_ReflectedPropertyTools.md`](AIPhase05_ReflectedPropertyTools.md) 与
  [`AIPhase06_GameAssemblyVerticalSlice.md`](AIPhase06_GameAssemblyVerticalSlice.md)。
- 尚未开放任意材质实例创建、任意玩法代码生成、Shell、Build 系统控制或不受目录约束的文件操作。
- 当前没有语音、Embedding RAG、MCP、多 Agent 或 LangGraph 依赖；Streaming、RAG Lite 与 Skill v0 已在后续阶段完成。
- 第 7 月完整复杂场景指令仍需后续扩充确定性工具目录；模型不能绕过目录直接调用任意 PFunction。

这些限制是安全边界，不是 UI 缺陷。第 8 月 Mini GAS 和后续 PicoGraph 会在同一审批、事务和验证机制上增加
更高层能力。
