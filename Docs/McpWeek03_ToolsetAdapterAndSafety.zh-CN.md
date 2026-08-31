# MCP 第 3 周：Toolset Adapter 与安全闭环

## 本周目标

把第 2 周的传输无关协议核心接到 Pico 已有 Agent Harness，但不开放真实端口，也不让 MCP 获得绕过编辑器安全
管线的捷径。外部客户端只发现三个稳定元工具：

- `list_toolsets`：列出 Capability Provider 形成的版本化 Toolset；
- `describe_toolset`：读取一个 Toolset 内的工具、权限和输入 Schema；
- `call_tool`：调用已启用 Toolset 中确实归属于它的工具。

真实的 `editor.world.describe`、`editor.actor.set_reflected_properties`、`editor.play.start` 等工具不会平铺为 MCP
顶层工具。新增玩法或系统能力仍先注册 `IAgentCapabilityProvider`，随后自动进入 Toolset Catalog，不需要修改 MCP
Core。

## 最终链路

```text
MCP request
  -> FMcpServerCore
  -> FMcpAgentToolsetAdapter（三个元工具、Toolset 隔离）
  -> FEditorAgentExecutionService
  -> FAgentToolRegistry
  -> Validate / Permission / Approval / Transaction / Execute / Verify
  -> Game Thread
```

新增 `PicoMcpAgentAdapter` 是独立静态库，只依赖 `PicoMcpCore` 与 `PicoAgentCore`。`PicoMcpCore` 仍不依赖
Agent 或 Editor；`PicoEditorCore` 可以使用 Adapter，但协议模块不会反向包含编辑器类型。

## Schema 与结果映射

Adapter 从 `FAgentToolRegistry::BuildToolCatalogJson()` 读取 Capability Provider、权限、说明和 `input_schema`，按
Provider 名分组并稳定排序。`describe_toolset` 返回的 Schema 仍由原 Agent Definition 产生，因此 MCP 不维护第二
份参数定义。

`call_tool` 的结构化结果包含：

- `facts`、`artifacts` 和 `diagnostics`；
- `state_changes`、`revision_changes` 和 `recovery_hint`；
- `status`、`failure_class`、`reused`；
- 完整 Validate 至 Verify Trace。

Artifact 对外使用模型安全视图，不返回本地相对路径。Agent 工具业务失败仍是 MCP `isError=true` Tool Result；
未知 MCP 元工具或协议结构错误才是 JSON-RPC Error。

## 审批、事务与幂等

Adapter 不判断“某项修改是否安全”，只按顺序调用现有 Executor：

1. `BeginRun`；
2. 如需要则 `PrepareApproval`；
3. `Execute`；
4. 成功且非只读时 `CommitDurableResult`；
5. `EndRun`。

因此审批拒绝发生在 Handler 和事务之前，Revision 不变、零副作用；批准后仍进入普通编辑器事务，可由已注册的
Undo/Redo 工具恢复。

JSON-RPC ID 只要求“正在执行期间不重复”，响应后可以再次使用，因此不能直接作为 Operation Journal 的幂等键。
Adapter 默认给每次调用生成新的安全 ToolCall ID。需要对失败重试保持幂等时，客户端可在 `call_tool` 中提供全局
唯一的 `operation_id`；相同 `operation_id + toolset + tool` 会得到相同 ToolCall ID，由共享执行服务恢复已记录结果。

## 取消与隔离

`FCancellationToken` 新增外部取消查询入口，把第 2 周的 `FMcpCancellationToken` 继续传入 Agent Handler。取消、
超时或 Transport 关闭后，排队中的 Game Thread 工作可以放弃，已经协作执行的工具可观察取消，迟到结果不会回包。

固定隔离规则：

- Tool 必须属于调用指定的 Toolset，不能借另一个 Provider 越权；
- Toolset 可独立启停，无需修改 MCP Core 或 Agent Registry；
- `call_tool` 拒绝未声明字段，提示注入文本不能伪装成控制参数；
- 不自动暴露 `PFunction`，也不提供 Shell、任意文件或 API Key Tool；
- MCP 不替代 Permission、Approval、Transaction、Verifier 或 Journal。

## 自动化验收

`PicoMcpAdapterTests` 当前 `15/15` 通过，覆盖三个元工具、版本和 Schema、结构化结果、审批拒绝、事务提交、
Undo、Provider 归属、Toolset 禁用、提示注入、JSON-RPC ID 复用、显式幂等 ID 和跨层取消。

`PicoEditorTests` 额外建立真实组合链：

```text
MCP Core -> Toolset Adapter -> FEditorAgentExecutionService -> Game Thread
```

组合测试证明 MCP 来源的编辑器操作仍只在 Game Thread 执行。最终结果：Adapter 专项 Debug/Release `15/15`、
第 2 周协议专项 `29/29`、Editor 专项 `155/155`、Debug 全套 CTest `26/26` 通过。

## 第 4 周入口

当前仍没有监听端口和可视化 MCP Client。第 4 周才加入本地 Streamable HTTP、Editor Settings、随机 Bearer
Token、Host/Origin 校验，并使用 MCP Inspector 与至少一个真实客户端完成可视化端到端验收。
