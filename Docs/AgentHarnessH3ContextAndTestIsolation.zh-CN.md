# H3 上下文减重与测试目标隔离

H3 只处理已证实的发送视图重复和构建边界，不改变 ReAct 决策、Tool Policy、审批、事务、Revision、Checkpoint 或 JSONL 审计。

## 上下文

`pair6c/on` 的既有 Request Trace 显示三次请求累计 `observation_bytes=28,416`、`conversation_bytes=27,308`。Progress Ledger 的最近观察可含与同轮 Tool Result 重复的 `facts`、`state_changes` 和 `revision_changes`。Context Assembler 现在只在以下条件全部成立时从**发送给模型的 Ledger** 去掉相应字段：同一 `call_id` 的 Tool Result 仍在最终有界消息窗口里，并且结构化字段值相等。若消息被裁剪、字段不相等、结果无法解析或 Ledger 已因预算被丢弃，保留原内容。节省字节回写 `observation_bytes` 和 `total_bytes`；底层 JSONL、Checkpoint、工具结果与 Artifact 不改。

没有删除 Episode、历史投影和 Task State：Episode 来自别的会话，不是当前 Tool Result 的副本；历史投影在当前消息窗口之外仍可能是唯一线索；Task State 与 Ledger 虽有小字段重复，但两者可能分别因预算而被舍弃，当前无证据支持直接删除。此阶段也不改变完整 Tool Catalog 或强行缩短回答。

## 构建边界

- Golden Runner 与 Fake Provider 的头文件及实现移至 `Tests/Agent/Support`，由 `PicoAgentTestSupport` 构建；`PicoAgentCore` 不再编译或公开这两份测试 API。`PicoAgentTests`、`PicoEditorTests` 和专用于 Fake 文件协议场景的 `PicoAgentHost` 链测试支撑目标。
- `PicoEditor` 仅在 Debug 配置编译并显示 Fake Scene Agent。Release 只提供真实 Provider；保存过 `fake` 的旧设置不会在 Release 选中不存在的 Provider，而回到默认真实 Provider。正式 Editor 目标不链接 `PicoAgentTestSupport`。
- Runtime 保留狭窄的故障注入回调入口；注入点列表和逐次消费逻辑只在测试装配中创建。正式 Editor 不提供回调，不携带测试故障脚本状态。

## 验证与后续

Debug/Release 的 `PicoAgentTests`、`PicoEditorTests`、`PicoEditor`、`PicoAgentHost` 均构建成功；两种配置的 CTest 均通过。Debug 单项断言为 Agent 212/212、Editor 174/174。新增回归验证：当 Tool Result 留在模型消息时 Ledger 删除相同事实副本；当它被裁剪时 Ledger 保留事实。现有长会话投影、Golden、审批、撤销、故障恢复与 Checkpoint 回归仍通过。生成的项目文件验证 `PicoAgentCore` 不编译 Fake/Golden，`PicoEditor` 不链接测试支撑库，Release 无 Fake 编译宏。

这证明了发送视图的局部去重和构建隔离，**不证明真实 Provider 的 Token/缓存收益或回答质量改善**。按 [H1 真实任务集](AgentHarnessH1RealTaskEval.zh-CN.md) 用长会话 H1-07、Selection H1-03～05、审批/Undo H1-08～10 对比任务质量、证据与未命中 Token；只有同质量、同条件的配对才能谈缓存归因。可视化检查时，Debug 的 AI Chat Provider 列表应含 Fake Scene Agent，Release 列表不应含它。
