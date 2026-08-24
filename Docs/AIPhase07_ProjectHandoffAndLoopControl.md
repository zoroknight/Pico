# Agent 稳定性收尾：项目交接与循环治理

本阶段解决两个已经由真实 DeepSeek 使用暴露的问题：创建新项目后上下文断开，以及模型重复读取相同状态直到
耗尽总 Tool Budget。两项能力都放在 Harness 和编辑器生命周期边界内，不向模型开放任意进程或文件操作。

## 新项目受控交接

Pico 不在当前进程中热卸载项目。原生类、GeneratedClass、CDO、AssetRegistry 和项目配置都属于当前项目进程，
因此 Agent 创建项目后的流程固定为：

```text
editor.project.create_from_third_person_template
 -> 当前 Agent Run 完成并写入 ToolResult/Checkpoint
 -> 复制当前 JSONL Session 到新项目 Saved/Agent/Sessions
 -> 原子写入一次性 ProjectHandoff.json
 -> 若当前 World Dirty，继续使用 Save / Discard / Cancel 保护
 -> 启动 PicoEditor.exe <NewProject.pico>
 -> 新进程消费并删除 ProjectHandoff.json
 -> 恢复 Provider、Model、Session 和聊天历史
 -> 旧进程退出
```

交接只复制当前 Session，不复制 `ApiKeys.ini`、用户布局、旧 World 对象指针或 Object Handle。API Key 保存在
`<PicoEngineRoot>/Saved/Editor/Agent/ApiKeys.ini`，因此同一编辑器副本打开的新项目直接复用，不需要交接复制。
清单只有成功解析、
确认目标 Session 存在并删除自身后才算消费完成；Windows 下在删除前显式关闭输入流，避免文件共享锁失败。

## 语义只读缓存

Harness 使用以下键识别同一轮内等价的只读请求：

```text
StateRevision + ToolName + CanonicalJsonArguments
```

JSON 对象字段顺序不同不会产生新查询。成功的修改工具会递增 `StateRevision`，因此修改后的相同查询会重新执行，
不会错误返回修改前缓存。缓存命中使用新的 CallId 返回已有结果，写入 `reused=true` 和明确的 Harness Trace，
但不再次调用 Tool Handler，也不消耗实际只读调用预算。

## 无进展检测与分类预算

每个 Provider 请求都会收到结构化 Progress Ledger：原始目标、状态修订号、最近完成动作、缓存命中数，以及剩余
步骤、只读和修改预算。它是 Harness 生成的执行事实，不依赖模型自己总结。

编辑器当前每轮限制为：

- 12 个 Provider Step，其中保留最后一步用于最终回答。
- 16 个实际 Tool Call。
- 6 个实际只读调用。
- 10 个修改调用。
- 2 次修复。
- 连续 2 个只包含复用/缓存、没有新事实或状态变化的步骤后停止。

第一次独特只读查询算作进展；相同状态下重复查询不算。触发停止时 Harness 明确要求模型使用已有事实、执行已知
修改、询问缺失信息或结束回答，而不是简单把总预算调大。修改工具不会做语义去重，因为重复修改是否安全必须由
具体工具的 CallId 幂等、事务和验证决定。

## 自动化验收

- 语义相同但 JSON 字段顺序不同、CallId 不同的只读调用只执行一次。
- 连续两步重复查询在总预算耗尽前停止。
- 只读分类预算在审批和副作用之前拒绝超限批次。
- Provider 每一步收到结构化 Progress Ledger。
- 项目交接复制 Session、恢复 Provider/Model，并且清单只能消费一次。
- 编辑器本地 `ApiKeys.ini` 不会写入交接清单、Session 或目标项目。
- Debug 基线：`PicoAgentTests` 38/38，`PicoEditorTests` 127/127。

## 当前边界

- 新项目恢复同一会话，但不会在新进程中未经用户再次发送就自动执行下一轮修改。
- 只有成功完成的 Agent Run 才自动打开新项目；Run 失败时已创建项目保留，避免擅自删除用户可能需要的结果。
- 缓存和 Progress Ledger 只治理当前 Turn；跨 Turn 的长期可审计知识属于后续 Knowledge Store/RAG 阶段。
- 当前仍是非流式 HTTP；Streaming、Skill、RAG 和 MCP 不属于本次收尾。
