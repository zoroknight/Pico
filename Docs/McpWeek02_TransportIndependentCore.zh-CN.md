# MCP 第 2 周：传输无关协议核心

## 本周目标

在不启动端口、不依赖 HTTP、不链接 Editor 的前提下，实现可以被 Fake Transport、未来 Streamable HTTP 和测试
入口共同驱动的 `PicoMcpCore`。协议层只负责解析、版本、请求边界、取消和 MCP Tool 形状；审批、事务、Game
Thread 与具体编辑器能力继续属于第 1 周提取出的 `FEditorAgentExecutionService` 及现有 Harness。

本周实现以当前 MCP `2026-07-28` 规范为主。该版本已经从“连接先 initialize”改为无状态请求：每个请求在
`params._meta` 中携带协议版本和客户端能力。为兼容 MCP Inspector 或仍使用 `2025-11-25` 的客户端，Pico
同时保留受限的旧版 `initialize -> notifications/initialized` 路径，因此属于 dual-era server core。

## 模块边界

```text
Fake Transport / future HTTP Adapter
  -> FMcpServerCore
     -> JSON-RPC + MCP version/error/limit/cancellation
     -> IMcpToolProvider
        -> 第 3 周 Toolset Adapter
           -> FEditorAgentExecutionService
```

`PicoMcpCore` 只链接 `PicoCore` 与线程库，不链接 `PicoAgentCore`、`PicoEditorCore`、ImGui 或网络库。公共头也不
暴露 `nlohmann::json`，以便以后替换 JSON 实现而不污染调用方 ABI。

`FMcpRequestContext::PeerId` 只用于：

- 区分并发请求 ID；
- 将 `notifications/cancelled` 定位到正在执行的调用；
- 在旧协议中保存有上限的初始化状态；
- Transport 关闭时取消该来源尚未完成的工作。

现代协议不会从 `PeerId` 推导会话、权限、项目或对话状态。需要跨请求保存的业务状态，后续必须使用显式 Handle。

## 已实现协议

- JSON-RPC 2.0 request、response、notification 和标准错误码；
- 明确拒绝 batch，避免半成功和副作用顺序不确定；
- 现代 `server/discover`、`ping`、`tools/list`、`tools/call`；
- 每请求校验 `io.modelcontextprotocol/protocolVersion` 与 `clientCapabilities`；
- 不支持版本返回 `-32022` 和可用版本列表；
- 旧版 `initialize`、`notifications/initialized`、初始化前 `ping` 与生命周期门；
- Tool 描述使用 JSON Schema 2020-12 默认语义，并保持确定顺序；
- Tool 业务失败返回 `result.isError=true`；未知 Tool、非法结构和协议问题返回 JSON-RPC Error；
- `notifications/cancelled`、Transport 关闭取消、迟到响应抑制、重复 in-flight ID 拒绝；
- 请求字节数、JSON 深度、字符串长度、旧会话数、每来源并发数、Tool 时限和结果大小上限。

第一版取消是协作式的：Provider 必须检查 `FMcpCancellationToken`。Core 在取消或来源关闭后即使收到迟到结果也不
再回包，防止上层误认为操作仍有效。第 3 周 Editor Adapter 还必须把同一个 Token 继续传入共享执行服务。

## 自动化验收

`PicoMcpTests` 使用 Fake Provider，不打开 Socket。覆盖：

- 现代发现、稳定 Tool 排序、结构化结果和服务身份；
- 旧版初始化门、重复初始化、会话上限和关闭清理；
- 非法 JSON、batch、null ID、未知方法、未知 Tool；
- 缺失元数据、版本不匹配、请求/字符串/深度/结果上限；
- 并发重复 ID、通知取消、Transport 关闭和真实 Deadline；
- Tool 业务失败不被误报为协议或 Transport 故障。

最终专项结果为 Debug/Release `29/29` 断言通过；Debug 全套 CTest 为 `25/25` 目标通过。

验收命令：

```powershell
cmake --build BuildCodex --config Debug --target PicoMcpTests -j 4
BuildCodex\Debug\PicoMcpTests.exe
ctest --test-dir BuildCodex -C Debug --output-on-failure
```

## 第 3 周结果

Toolset Adapter 与安全闭环已经完成，见
[MCP 第 3 周：Toolset Adapter 与安全闭环](McpWeek03_ToolsetAdapterAndSafety.zh-CN.md)。实现把现有 Capability
Provider 映射为默认三个元工具，并将调用交给 `FEditorAgentExecutionService`，没有把 Editor 类型塞回 Core，
也没有让 MCP 自建审批、事务或权限系统。

HTTP Header、Host/Origin、Bearer Token、监听线程和 Editor Settings 仍属于第 4 周；当前没有开放任何端口。
