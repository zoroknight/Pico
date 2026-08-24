# 第 7 月第 3 周：Agent Tool 安全管线与编辑器事务

本阶段让 Agent 从“只能规划”进入“能够受控操作编辑器”的状态。核心目标不是工具数量，而是确保错误参数、
未知工具、权限拒绝、用户拒绝、执行异常和验证失败都不会留下半次修改。

## 固定执行顺序

```text
ToolCall
 -> Validate
 -> Permission
 -> Approval
 -> Transaction
 -> Execute
 -> Verify
 -> Commit

任一步失败
 -> 不执行，或 Rollback
 -> ToolResult(error)
 -> Agent 进入有限 Repairing
```

顺序不可交换。尤其不能先执行再询问用户，也不能在 Schema 校验前打开事务或访问文件。

## AgentToolRegistry

每个工具注册以下静态信息：

- 稳定工具名和用途说明。
- 权限：`ReadOnly`、`ModifyWorld`、`WriteProject` 或 `LaunchProcess`。
- 输入字段类型、Required、数值范围、字符串长度与路径格式。
- Handler 与可选 Postcondition Verifier。

Registry 可以生成排序稳定的 JSON Schema Catalog，后续 DeepSeek/Kimi Provider 只读取 Catalog，不直接
理解 C++ Handler。默认禁止修改 World、写项目和启动进程；调用方必须显式打开权限。修改权限默认要求审批。

路径字段支持两种专用格式：

- `pico-asset-path`：必须是合法 `/Game/...` 虚拟资产路径。
- `pico-project-relative-path`：拒绝绝对路径、盘符和 `..`，规范化结果必须仍位于 Project Root。

## 审批和状态机

修改型工具会让 Runtime 进入 `AwaitingApproval`。审批结果与完整 `CallId + ToolName + Arguments` 绑定，
不能批准参数 A 后替换成参数 B 执行。已经成功完成的同一 ToolCall 恢复时直接复用结果，不重复审批和执行；
同一 CallId 若改名或改参数则作为冲突关闭流程。

当前审批通过 `IAgentToolApproval` 注入，自动化测试可确定地批准或拒绝。第 4 周 AI Chat Workspace 会实现
真正的 Allow/Reject UI；没有 Approval 实现时，修改型工具默认拒绝。

## 事务与验证

`FEditorAgentToolExecutor` 没有另造撤销系统，而是适配已有 `FEditorTransactionManager`：

```text
Begin
 -> CaptureWorld(before)
 -> Handler 修改真实 World
 -> Verifier 查询真实后置条件
 -> CaptureWorld(after) / Commit
 -> 进入原有 Ctrl+Z / Ctrl+Y 历史
```

Handler 失败、抛异常或 Verifier 不通过时，通过 `ReplaceWorld(before)` 恢复完整 World 和 Selection。
因此 Agent 创建的 Actor 可以直接使用编辑器现有 Undo 删除。

## 首批编辑器工具

| 工具 | 权限 | 作用 |
| --- | --- | --- |
| `editor.world.describe` | ReadOnly | 返回 World、Actor 和 Component 数量 |
| `editor.selection.describe` | ReadOnly | 返回当前主选择和完整选择路径 |
| `editor.actor.spawn` | ModifyWorld | 创建带根组件的 Empty 或 Cube Actor |
| `editor.actor.set_location` | ModifyWorld | 通过稳定对象路径修改 Actor 世界位置 |

只读工具不审批、不打开事务。修改工具会审批并进入同一条 World Snapshot Undo 历史。保存、删除资产、运行、
打包和进程工具本周没有开放，避免在审批 UI 完成前扩大危险面。

## Trace 与审计

每个阶段记录 `stage / succeeded / message`。Trace 随 ToolResult 写入 Session JSONL，而不是只保留在内存里，
因此第 4 周 UI 可以恢复并显示历史审批、验证和回滚原因。

## 自动化验收

`PicoAgentTests` 验证：

- 错误类型和 `../` 路径在 Handler 前拒绝。
- 权限拒绝和用户拒绝零副作用、零事务。
- 成功修改只 Begin/Commit 一次。
- 后置条件失败恢复修改前值。
- Schema Catalog 与顺序 Trace 可读取。
- CallId 修改参数后拒绝，不能借用旧审批或幂等结果。

`PicoEditorTests` 使用真实 World 和事务验证：

- Agent 创建 Cube 后可以使用普通 Editor Undo 删除。
- 非法 Actor 名触发完整 World 回滚且不增加 Undo 项。
- 只读工具不请求审批、不创建事务。

## 当前边界

- 本周没有可视化审批窗口，Editor 尚未自动启动 Agent Host。
- 没有保存 World、资产修改、Play、Build、Package 或进程工具。
- 没有把任意 PFunction 自动暴露给模型；工具必须显式注册并定义权限。
- 没有真实 Provider、流式响应和 Chat Workspace。

这些边界让第 4 周可以专注 UI、真实 Provider 和异步 Host 接线，而不用重新设计安全执行层。
