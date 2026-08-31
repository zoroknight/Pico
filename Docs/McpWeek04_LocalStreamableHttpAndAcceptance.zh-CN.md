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
- 实现现代 `2026-07-28` 每请求 `POST` 路径。普通响应返回 JSON，工具调用返回当前请求独占的 SSE 流。
- 检查 `Content-Type`、双 Accept、`MCP-Protocol-Version`、`Mcp-Method` 和 `Mcp-Name`；Header 与 JSON-RPC
  Body 不一致时返回稳定错误，不执行工具。
- SSE 客户端断开时关闭对应 Core Peer，取消查询继续传入 Adapter、Agent 和共享 Editor 执行服务。
- 请求大小、并发执行和输出继续受 Core 与 Harness 的既有边界约束。

HTTP Adapter 当前只开放现代无状态入口。第 2 周 Core 中的旧时代兼容仍可供其他 Transport 使用，但没有在本地
HTTP Server 上复制旧版有状态 Session。

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

AI Chat 中新增 `Local MCP Server` 设置区：

- `Enabled`：持久化启停；默认关闭。
- `Port`：配置回环端口，默认 `8765`。
- `Endpoint`：当前 URL 与运行状态。
- `Accepted / Rejected / Active`：请求诊断。
- `Copy Token`：只在用户主动点击时复制本地 Token。
- `Copy Client Config`：复制 MCP Inspector/客户端配置。
- `Regenerate Token`：Server 停止时轮换 Token。
- `Exposed Toolsets`：逐个启停 Toolset，不改 Core，也不扩大 Tool 权限。

关闭设置或退出 Editor 会停止 Listener 并等待请求收尾。默认关闭时不创建监听线程，也没有每帧 MCP Tick。

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

### 真实客户端验收

- 使用实际 `BuildCodex/Debug/PicoEditor.exe` 启动本地 Server。
- 独立 PowerShell HTTP 客户端成功完成 `server/discover`、`tools/list` 和
  `WorldToolProvider/editor.world.describe`，并读取真实 `StarterWorld` Actor。
- 官方 MCP Inspector CLI `2.4.0` 使用 `protocolEra: modern` 成功连接并列出且只列出三个顶层元工具：
  `list_toolsets`、`describe_toolset`、`call_tool`。
- 第 3 周测试继续覆盖批准修改、拒绝零副作用和 Undo；第 1 周测试继续覆盖 Play/Package 等异步操作等待最终结果。

Inspector 实测证明协议互操作，自动化证明副作用边界。中文交互审批仍保留为下面的人工可视化验收项，不能用
自动批准测试冒充人工确认。

## 可视化验收流程

1. 使用 `BuildCodex/Debug/PicoEditor.exe` 或 `BuildCodex/Release/PicoEditor.exe` 打开 PicoSandbox。
   目的：确认真实 Editor、World 和 Game Thread 链，而非 Fake Provider。
2. 展开 `AI Chat -> Local MCP Server`，确认初始 `Enabled` 为关闭，然后开启。
   目的：验证默认安全状态以及用户显式启用后才监听。
3. 点击 `Copy Client Config`，保存为 Inspector 的本地配置并运行 Inspector `2.4.0`。
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
