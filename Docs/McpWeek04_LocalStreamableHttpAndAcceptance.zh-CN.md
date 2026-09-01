# MCP 第 4 周：本地 Streamable HTTP 与真实验收

## 本周目标

第 4 周把前 3 周已经完成的共享执行服务、传输无关 Core 和 Toolset Adapter 接到真实外部客户端，形成下面这条链：

```text
MCP Client
  -> 127.0.0.1 Streamable HTTP
  -> PicoMcpHttp
  -> PicoMcpCore
  -> PicoMcpAgentAdapter
  -> FEditorAgentExecutionService
  -> 现有 Tool Registry / Approval / Transaction / Verify
  -> Game Thread
```

MCP 只是新的协议入口。它没有复制 Agent Harness，也不能绕过中文审批、Undo、Revision、Verifier 或游戏线程。

## 实现结果

### 隔离的 HTTP 层

- 新增独立 `PicoMcpHttp` 模块；公开头只依赖 `PicoMcpCore`，HTTP 实现细节不进入 Editor、Agent 或 Runtime。
- 使用 `cpp-httplib`，不手写 HTTP Parser；版本固定为 `v0.51.0` 对应提交
  `d66d9a95997d51a8ba9822a611d1267757741535`。
- 保留现代 `2026-07-28` 每请求 `POST` 路径，同时支持 Codex 使用的标准
  `initialize -> notifications/initialized` Streamable HTTP Session。普通响应返回 JSON，工具调用返回 SSE 流。
- 检查 `Content-Type`、双 Accept、`MCP-Protocol-Version`、`Mcp-Method` 和 `Mcp-Name`；Header 与 JSON-RPC
  Body 不一致时返回稳定错误，不执行工具。
- SSE 客户端断开时关闭对应 Core Peer，取消查询继续传入 Adapter、Agent 和共享 Editor 执行服务。
- 请求大小、并发执行和输出继续受 Core 与 Harness 的既有边界约束。

标准客户端由 Server 分配随机 `Mcp-Session-Id`，后续请求必须携带已登记 Session；Server 停止时统一关闭 Session
并取消未完成调用。缺少或格式错误的 Session 返回 HTTP 400；Editor 重启后客户端携带的旧 Session 返回 HTTP
404，客户端据此重新发送 `initialize` 建立新 Session。不能持久化并复活旧 Session，因为旧进程的审批与执行上下文
已经不存在。现代 Inspector 路径仍保持请求级 Peer，两条入口最终进入同一个传输无关 Core。

### 本地安全边界

- Server 默认关闭，只允许绑定 `127.0.0.1`，不接受 `0.0.0.0`、局域网地址或公网地址。
- 每次请求都验证 `Host` 与 `Origin`；非 localhost 来源在进入 JSON-RPC Core 前拒绝。
- 使用至少 32 字符的随机 Bearer Token，并采用常量时间比较。
- Token、端口、Endpoint 和 Toolset 开关保存在 Git 忽略的
  `Saved/Editor/McpServer.ini`，与模型 Provider API Key 分开。
- Agent Tool、Resource、响应和日志均不提供读取或修改 Token/API Key 的入口。
- 第一版静态 Bearer Token 是本地 Development Editor 的防误连措施，不是公网 OAuth 替代品；远程 Server、
  多租户、Shipping Runtime 和 LAN 监听仍明确禁止。

### Editor 接入

MCP 不再属于 AI Chat。`PicoEditorApp` 持有共享 `EditorAgentHost`，内置 AI Chat 和外部 MCP Adapter
都只消费该 Host。通过 `View -> External Agents` 打开独立工作区：

- `Enabled`：持久化启停；默认关闭。
- `Port`：配置回环端口，默认 `8765`。
- `Endpoint`：当前 URL 与运行状态。
- `Sessions / Accepted / Rejected / Active`：连接与请求诊断。
- `Copy Generic Config`：复制带临时 Bearer Header 的通用 MCP/Inspector 配置。
- `Codex -> Copy Config`：复制不含 Token 的 Codex TOML；配置只记录 Endpoint 和
  `PICO_MCP_BEARER_TOKEN` 环境变量名。
- `Launch CLI`：在当前项目目录打开对应外部客户端，并只向该子进程及其后代注入当前 Token；Codex Adapter
  通过进程级 `-c mcp_servers.pico_editor...` 参数注入本次连接配置，不替换用户的 `CODEX_HOME`。
- `Launch App`：请求打开客户端桌面端并向新进程注入 Token 和本次 MCP 参数。桌面端是单实例时，
  已运行实例可能不会继承新环境，因此连接失败时需要先完整退出桌面端再从 Pico 启动。
- `Regenerate Token`：Server 停止时轮换 Token。
- `Exposed Toolsets`：逐个启停 Toolset，不改 Core，也不扩大 Tool 权限。

外部客户端 Adapter 仅负责生成客户端配置和可选启动，不拥有 Tool、模型目录、推理强度或会话。
模型、推理强度和聊天 UI 始终由 Codex、Claude Code 等外部客户端自身管理。退出 Editor 时先停止 Listener，
再停止内置聊天，最后关闭共享 Host；默认关闭时不创建监听线程，也没有每帧 MCP Tick。

### Codex 本地接入

首次使用时：

1. 打开 `View -> External Agents`，开启 Server，在 Codex 行点击 `Copy Config`。
2. 推荐将片段保存到 Pico 编辑器源码根目录的 `.codex/config.toml`；其下所有项目共用同一份无密钥配置。
   当前 Codex CLI 不会仅因工作目录位于 Pico 下就自动发现这份编辑器级配置；Pico 的启动按钮会直接传入
   进程级 MCP 参数，因此不依赖该扫描行为。手动从普通终端启动时需要设置
   `CODEX_HOME=<Pico>/.codex`，或改用用户级 `~/.codex/config.toml`。
3. 回到 Editor 点击 Codex 的 `Launch CLI`。新控制台中的 Codex 以当前项目为工作目录，并从仅该进程继承的
   `PICO_MCP_BEARER_TOKEN` 读取认证信息。

外部 Codex 与内置 AI Chat 共用 Pico 的工具安全链，但审批入口不再依赖 AI Chat 窗口是否打开。任何需要审批的
调用都会显示编辑器级 `MCP / Agent Approval` 窗口：可以批准一次、拒绝，或仅对 `ModifyWorld` 权限的同名工具
在当前来源会话内放行。授权按 MCP Peer/内置 Chat Session 隔离；写项目、启动进程和其他高风险权限始终逐次
审批，重启 Editor 会清空所有会话授权。

TOML 中不会出现 Token、`Authorization` Header 或 `Saved/Editor/McpServer.ini` 路径。Token 仍只持久化在
Pico 本地开发目录的 Git 忽略文件中；PicoEditor 自身和系统环境不会被修改。Codex 进程及其子进程必须持有
Token 才能认证，因此“进程级注入”用于避免静态配置泄密，并不声称能阻止该进程主动读取自身环境。

轮换 Token 时先停止 Server，点击 `Regenerate Token`，再重新开启 Server 并重启由 `Launch CLI` 打开的
Codex；已经运行的进程不会被静默改写环境。`Copy Generic Config` 仍保留给 Inspector 等需要静态 Header 的
临时本地客户端，长期 Codex 接入优先使用无密钥配置。

Editor 重启会使旧进程内的 MCP Session 失效，这是有意的安全边界；Server 会用标准 HTTP 404 通知客户端重新
初始化。如果客户端版本没有自动恢复，可以关闭旧客户端，再从新 Editor 点击 `Launch CLI` 或
`Launch App`。不要通过持久化 Session ID 绕过重新握手。

仓库级 `.codex/config.toml` 可以随编辑器源码提交和拉取，因为其中只有回环 Endpoint 与环境变量名。每台机器
首次启动 PicoEditor 时仍会在 `Saved/Editor/McpServer.ini` 生成各自的本地 Token，仓库配置不会共享凭据。

## 自动化验收

### Debug

```text
PicoMcpTests          Passed
PicoMcpAdapterTests   Passed
PicoMcpHttpTests      Passed
PicoEditorTests       Passed
全量 CTest            27/27 Passed
```

### Release

```text
PicoMcpTests          Passed
PicoMcpAdapterTests   Passed
PicoMcpHttpTests      Passed
PicoEditorTests       Passed
```

覆盖内容包括：合法请求、认证失败、恶意 Origin、错误 Host、Accept 缺失、Header/Body 冲突、未知方法、真实回环
HTTP、SSE 工具结果、传输断开取消，以及 `HTTP -> Core -> Adapter -> Editor Game Thread` 完整链路。
此外，HTTP 测试通过真实 TCP 覆盖标准 initialize、Session Header、initialized 通知和 tools/list；Core 测试验证
环境覆盖只进入子进程；Editor 测试验证 Codex 配置包含三个元工具和环境变量引用，且不包含 Token 或静态
`Authorization`。

### 真实客户端验收

- 使用实际 `BuildCodex/Debug/PicoEditor.exe` 启动本地 Server。
- 独立 PowerShell HTTP 客户端成功完成 `server/discover`、`tools/list` 和
  `WorldToolProvider/editor.world.describe`，并读取真实 `StarterWorld` Actor。
- 官方 MCP Inspector CLI `2.4.0` 使用 `protocolEra: modern` 成功连接并列出且只列出三个顶层元工具：
  `list_toolsets`、`describe_toolset`、`call_tool`。
- Codex CLI 使用进程级 MCP 配置覆盖和 Token 完成标准 MCP 握手，并成功开始
  `pico_editor/list_toolsets`；非交互测试在工具审批处按预期停止，没有执行编辑器副作用。
- 第 3 周测试继续覆盖批准修改、拒绝零副作用和 Undo；第 1 周测试继续覆盖 Play/Package 等异步操作等待最终结果。

Inspector 实测证明协议互操作，自动化证明副作用边界。中文交互审批仍保留为下面的人工可视化验收项，不能用
自动批准测试冒充人工确认。

## 可视化验收流程

1. 使用 `BuildCodex/Debug/PicoEditor.exe` 或 `BuildCodex/Release/PicoEditor.exe` 打开 PicoSandbox。
   目的：确认真实 Editor、World 和 Game Thread 链，而非 Fake Provider。
2. 打开 `View -> External Agents`，确认初始 `Enabled` 为关闭，然后开启。
   目的：验证默认安全状态以及用户显式启用后才监听。
3. 点击 `Copy Generic Config`，保存为 Inspector 的本地配置并运行 Inspector `2.4.0`。
   目的：验证 Endpoint、现代协议时代和 Bearer Header 可直接由外部客户端使用。
4. 调用 `tools/list`，应只看到三个元工具；再用 `list_toolsets` 和 `describe_toolset` 查看能力。
   目的：确认外部客户端按 Toolset 渐进发现，不把全部内部工具平铺暴露。
5. 通过 `call_tool` 调用 `WorldToolProvider/editor.world.describe`。
   目的：读取当前真实 World，验证 HTTP 到 Editor 游戏线程的只读链路。
6. 通过 `call_tool` 调用 `WorldToolProvider/editor.actor.spawn` 创建测试 Cube，并在 Editor 内批准中文弹窗。
   目的：验证外部写操作不能绕过审批、事务和后置验证。
7. 在 Editor 中执行正常 Undo，确认 Cube 被完整删除；再拒绝一次创建请求。
   目的：验证批准操作可撤销，拒绝操作零副作用且不增加 Revision。
8. 发起工具调用后关闭 Inspector 连接，观察 `Active` 回到 0，且被取消的未执行修改不会稍后出现。
   目的：验收 SSE 断开到共享执行服务的取消传播。
9. 分别调用 `editor.play.start/stop`；Package 仅在确实需要生成测试包时单独验收。
   目的：确认异步工具返回最终完成/停止结果，而不是只报告进程已启动。
10. 关闭 `Enabled`，再次连接应失败，退出 Editor 后也不应留下监听端口或后台进程。
    目的：确认生命周期和默认零持续开销。

## 仍然不做

- MCP Client、Sampling、Prompts、完整 Resources、Multi-Agent 和 Code Harness。
- 公网/LAN Server、OAuth、多租户、Shipping Runtime 和云端部署。
- 自动暴露全部反射函数，或允许外部 Client 绕过 Capability Provider 与安全管线。

后续若有真实需求，优先把可审计 Project Knowledge Store 作为只读 Resource；协议层继续保持可替换，不侵入
Gameplay、Object、Network 或 Render Runtime。

## 规范依据

- [MCP 2026-07-28 Transports](https://modelcontextprotocol.io/specification/2026-07-28/basic/transports)
- [MCP 2026-07-28 Streamable HTTP](https://modelcontextprotocol.io/specification/2026-07-28/basic/transports/streamable-http)
- [MCP 2026-07-28 Authorization](https://modelcontextprotocol.io/specification/2026-07-28/basic/authorization)
- [MCP Inspector](https://github.com/modelcontextprotocol/inspector)
- [OpenAI Codex MCP](https://learn.chatgpt.com/docs/extend/mcp?surface=cli)
- [OpenAI Codex Config Reference](https://learn.chatgpt.com/docs/config-file/config-reference)
