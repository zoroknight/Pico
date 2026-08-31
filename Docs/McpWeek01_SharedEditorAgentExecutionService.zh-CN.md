# MCP 第 1 周：共享 Editor Agent 执行服务

## 本周目标

在实现 MCP 协议和端口之前，先把 PicoEditor 已有的 Agent 工具执行语义从 `AgentChatWorkspace.cpp` 中提取出来。
内置 DeepSeek/Kimi 聊天、后续 MCP Server 和无 UI 测试入口必须复用同一个执行服务，不能各自维护 Game Thread、
Journal、取消和异步操作规则。

本周不实现 JSON-RPC、MCP Toolset 或 HTTP Server，也不改变现有 Agent 能调用的工具集合。

## 修改前的问题

`FEditorAgentToolExecutor` 已经公开并负责：

- Schema 验证与 Tool Policy；
- 用户审批和编辑器事务；
- Tool Handler、Verifier 与结构化 Tool Result；
- World、资产、Gameplay、Play 和 Package 工具。

但是它外面还有一层私有的 `FGameThreadToolExecutor`，定义在聊天窗口源文件中。该私有层负责 Game Thread 投递、
Skill 白名单、Play/Package Intent 防误调用、Operation Journal、异步完成等待和 Run ChangeSet 生命周期。结果是：

- 外部 MCP 即使找到 `FEditorAgentToolExecutor`，也无法获得完整、安全的执行语义；
- 直接再写一个 MCP Wrapper 会形成第二套取消、Journal 和审批链；
- 排队等待 Game Thread 时取消任务，旧实现仍可能在后续帧执行已经取消的回调；
- Last Trace、Intent、Skill 和 Journal 上下文没有为未来多 Session 并发定义隔离方式。

## 新架构

新增公开的 `FEditorAgentExecutionService`，位于 `PicoEditorCore`：

```text
Agent Runtime / future MCP Adapter
  -> FEditorAgentExecutionService
     -> Session / Run / ToolCall context
     -> Intent and Skill guard
     -> durable Operation Journal
     -> Game Thread Dispatcher
     -> FEditorAgentToolExecutor
        -> Validate / Permission / Approval / Transaction
        -> Execute / Verify
     -> Play or Package final completion wait
```

`AgentChatWorkspace` 仍负责聊天气泡、Provider 设置和审批按钮。审批对象是 UI Adapter，不拥有工具执行规则；它由
`FEditorAgentToolExecutor` 通过统一管线调用。原私有 `FGameThreadToolExecutor` 已删除。

## 关键实现

### Game Thread 边界

工作线程调用 `Execute` 后只向 `FGameThreadDispatcher` 投递回调。实际 Editor Tool Handler 仍只在 Game Thread
执行。多个只读调用可以同时等待；修改调用通过独立 Mutation Lane 串行进入 Journal 和执行链。

### 取消与关闭

每个排队调用使用共享状态区分 `Queued`、`Started`、`Done` 和 `Abandoned`：

- 在 Game Thread 开始前取消：标记 `Abandoned`，后续 Pump 只完成取消结果，不调用 Tool Handler；
- Handler 已开始：等待本次 Game Thread 调用完成，再把 Cancellation Token 交给异步完成等待；
- Editor 关闭：停止接收新调用，使尚未开始的回调失效，不保留裸的执行服务指针。

这修复了“用户已经取消，但排队回调在下一帧迟到修改场景”的生命周期风险。

### Session、Run 与 Journal

持久操作的内部主键改为：

```text
SessionId / RunId / ToolCallId
```

对 Provider 和 Session Event Log 返回的仍是原始 `ToolCallId`。因此同一 Run 内重试能够复用已应用结果，而不同
聊天或未来不同 MCP Session 即使产生相同 ToolCall ID，也不会错误复用另一会话的操作。

Session、Run、Intent、Skill 白名单和 Last Trace 按调用工作线程保存。它们不会因为另一个并发只读调用完成而
覆盖当前调用要读取的 Trace 或权限上下文。

### 异步 Play 与 Package

共享服务接受可注入的 Async Completion 回调。Chat Workspace 迁移后仍调用现有
`FEditorAgentToolExecutor::WaitForAsyncCompletion`，所以 Play/Package 保持“返回最终状态”，没有退化为只返回
“进程已经启动”。

## 自动化验收

`PicoEditorTests` 新增以下确定性覆盖：

1. 两个工作线程可同时提交只读调用；
2. Tool Handler 实际只在测试绑定的 Game Thread 执行；
3. 同一 Session 的持久修改只执行一次，重复调用恢复 Journal 结果；
4. 不同 Session 使用相同 ToolCall ID 时不会串账；
5. 排队调用被取消后，即使继续 Pump 也不会产生迟到副作用；
6. 服务关闭后拒绝新调用且不再向 Dispatcher 投递任务。

现有 `PicoAgentTests` 同时作为 Harness、Failure、Recovery、Skill、RAG 和 Golden Task 回归集。

## 后续边界

第 2 周只在该服务之上实现传输无关的 `PicoMcpCore`。JSON-RPC Core 不得依赖 Editor；本周的执行服务也不得
反向依赖 MCP。HTTP、Toolset 映射和真实 MCP Client 验收分别留在第 3～4 周。
