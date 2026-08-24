# 第 7 月第 2 周：Pico Agent Core 与可恢复 Session

本阶段实现 Pico 自研最小 Agent Harness 的纯数据核心。它不依赖 Editor、World、ImGui 或具体模型 SDK，
因此可以用 Fake Provider 在无网络、无 API 费用的情况下确定性复现成功、失败、修复、预算终止和取消。

## 模块边界

```text
PicoAgentHost（独立进程入口、JSON 文件协议）
  -> PicoAgentCore
       -> IAgentProvider
       -> FAgentRuntime
       -> IAgentToolExecutor（第 3 周由 Tool Registry 实现）
       -> FAgentSession（JSONL Event Log / Checkpoint）
  -> PicoTasks（协作取消）
```

`IAgentProvider` 只接收消息历史、步骤号、修复次数和 Harness 生成的 Progress Ledger，并返回文本、结构化 ToolCall 或错误。模型 Provider
不认识 World 和编辑器工具。`IAgentToolExecutor` 是刻意保留的窄接口，本周测试使用无副作用的 Fake
Executor；第 3 周再接入 Schema、权限、审批、事务、执行与验证管线。

## 状态机

```text
Idle / 终态 -> Planning
Planning -> AwaitingApproval / ExecutingTool / Repairing / Completed
AwaitingApproval -> ExecutingTool
ExecutingTool -> Validating / Repairing
Validating -> Planning / Repairing / Completed
Repairing -> Planning
任意活动状态 -> Failed / Cancelled
```

状态迁移由白名单校验，不能从 `ExecutingTool` 跳过验证直接进入 `Completed`。Provider 失败、Tool
失败或没有产生有效下一步时进入 `Repairing`，默认最多修复两次。步骤、实际 ToolCall、只读调用、修改调用、
修复次数和总耗时分别有预算，并保留最后一个 Step 用于最终回答。相同 StateRevision 下语义相同的只读查询复用
缓存；连续无进展会提前终止，详见 [`AIPhase07_ProjectHandoffAndLoopControl.md`](AIPhase07_ProjectHandoffAndLoopControl.md)。

## Event Log 与恢复

每个 Session 使用追加式 JSONL，每行是一个不可变事件：

```text
SessionCreated / StatusChanged / Message / ToolCall / ToolResult / Checkpoint / Error
```

每次追加都会立即 flush。载入时校验版本、Session ID 和连续 Sequence；中间损坏会拒绝恢复。若进程恰好
在写最后一行时退出，只修剪该不完整尾部，之前完整事件仍可恢复并继续追加。实际编辑器接入后默认位置为：

```text
<Project>/Saved/Agent/Sessions/<SessionId>.jsonl
```

Checkpoint 保存状态和预算计数。重启后的 Runtime 从最后一个持久 Checkpoint 继续，并从 Message 与
ToolResult 事件重建 Provider 上下文，不依赖供应商的隐式记忆。

## 幂等规则

每个 ToolCall 必须携带稳定 `CallId`。第一次记录 ToolCall、执行并记录 ToolResult；相同 CallId 再次到达时
读取已有结果、记录 `reused=true`，但不再次执行副作用。这解决模型重试或进程恢复导致同一 Actor 创建两次
的基础问题。第 3 周仍会为真实工具加入 Undo、验证和失败回滚；CallId 幂等不能替代事务。

## PicoAgentHost 协议

当前 Host 是独立控制台程序，使用 UTF-8 JSON 文件完成一次请求：

```powershell
PicoAgentHost.exe `
  --request=Tests/Agent/Fixtures/HostRequest.json `
  --response=BuildCodex/AgentHostTest/response.json
```

请求包含 `session_id`、`event_log`、`prompt` 和 `fake_scenario`，响应包含终态、最终文本和预算计数。
该协议用于验证进程隔离和持久化，不是最终聊天 IPC；第 4 周会在不改变 `PicoAgentCore` 的前提下增加
编辑器异步启动、取消和结果回投。

## 自动化验收

`PicoAgentTests` 覆盖 Fake Provider 确定流程、Session/Checkpoint 重启恢复、ToolCall 幂等、有限修复、
分类预算、语义只读缓存、无进展检测、Progress Ledger、项目交接、PicoTask 协作取消、不完整 JSONL 尾部恢复和
状态迁移规则。另有 `PicoAgentHost` 文件协议冒烟测试。

## 当前边界

- 没有真实 DeepSeek/Kimi 请求、流式输出、限流和超时重试。
- Editor Tool、Schema、权限、审批、事务和 Undo 已在第 3 周完成纯数据管线和首批场景工具；正式审批 UI
  仍属于第 4 周。
- 没有聊天窗口和 Session 列表 UI。
- 没有 MCP、LangChain、LangGraph、Embedding RAG 或多 Agent。
- Tool 参数暂以经过 JSON 解析验证的字符串跨边界；第 3 周 Tool Schema 再完成类型约束。

这些内容分别属于第 3、4 周或更后的对照实验，不应提前成为 Agent Core 的强依赖。
