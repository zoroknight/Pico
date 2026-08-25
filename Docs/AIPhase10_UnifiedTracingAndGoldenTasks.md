# Agent 加固：统一 Trace 与 Golden Task 基线

本阶段在现有 Session、Tool Trace、Durable Operation Journal 和 Skill Eval 上增加两层可重复验证能力。实现仍位于
`PicoAgentCore`，不依赖编辑器 UI、真实 Provider 或 Gameplay 模块。

## 统一 Trace

每次 `FAgentRuntime::Run` 生成独立 `RunId`，每轮模型交互生成 `TurnId`。Session JSONL 中持久化完成态 Span：

```text
AgentRun
  -> AgentTurn
       -> Model.Generate
       -> Tool.Approval
       -> Tool.<name>
       -> Run.Validation
```

Span 记录 `SpanId`、`ParentSpanId`、开始时间、微秒耗时、结果和错误；同一阶段产生的 Message、ToolCall、ToolResult、
Status 与 Checkpoint 自动继承当前 Run/Turn/Span 标识。旧 Session 不含新字段时仍可加载，聊天历史构建会忽略
`TraceSpan` 事件，不把可观测性数据发送回模型。

当前 Trace 解决本地关联和回归取证，尚未增加完整编辑器时间线视图、远程导出或云端 Trace 服务。

## 固定路由 Eval

`Tests/Agent/Fixtures/IntentRoutingCases.tsv` 从 18 条扩展到 37 条中英文生产表达，覆盖：

- General、Play、Package 与多 Skill 组合。
- “运行但不要打包”及反向否定。
- 启动游戏、生成安装包、英文 release/preview 等同义表达。
- 只检查日志、修改属性等不应激活现有 Skill 的输入。
- 批量清理、恢复上一轮修改，以及“不要删除”这类否定表达。

Play/Package Skill Trigger 同步覆盖 Intent Router 已识别的表达，避免 Intent 正确但 Skill Catalog 未开放的分裂状态。

## Golden Task Runner

`FAgentGoldenTaskRunner` 从版本化 JSON 加载任务，为每个任务建立隔离 Session，使用注入的 Provider、Tool Executor、
Fixture 准备器和 Verifier 执行 `FAgentRuntime`。Runner 固定检查：

- 预期最终状态、最大 Tool Call 数。
- 必须调用的工具和禁止调用的工具。
- 环境 Verifier 给出的确定性后置条件。
- RunId、计数器、耗时和 Session 证据路径。

报告通过临时文件后原子替换为 `GoldenTaskReport.json`。首批十个离线任务覆盖方块创建与属性、碰撞房间、第三人称
角色、Validate/Save/Play、Validate/Save/Package、审批拒绝零副作用、重复保存幂等、反射属性修改与保存、批量
删除和整轮 Agent 修改恢复。

这些任务使用脚本 Provider 和内存场景/进程状态，目的是固定 Harness 行为，不访问 DeepSeek/Kimi，也不启动真实
编辑器、Play 或 Packager。第 10 月继续为同一个 Runner 提供 Editor Fixture 适配器，在临时项目副本中验证真实
World、真实 Play 日志和真实 Stage。

## 批量修改与 Run ChangeSet

`editor.object.batch_set_properties` 可对最多 32 个精确对象、总计 128 个反射属性执行一次原子修改；
`editor.actor.delete_many` 会先验证最多 64 个 Actor 路径，再统一删除。任一目标或属性无效时，整个调用失败，
不会留下部分结果；成功后只产生一个审批和一个 Undo 事务。

每次 Agent Run 开始时，编辑器在 Game Thread 捕获 World 前态；结束时只有指纹发生变化才把前后 `.pworld`
快照和元数据写入项目 `Saved/Agent/ChangeSets`。`editor.agent.list_changes` 只读列出最近记录，
`editor.agent.revert_run` 恢复指定 Run 的前态。列表直接标记当前 World 是否匹配每个 Run 的 before/after：
匹配 after 才允许恢复；匹配 before 表示 Undo 或先前恢复已经完成，无需重复执行；两者都不匹配说明用户或后续
Agent 已继续编辑。Preflight 在开启 World 事务前完成该判断，失败不会触发无意义的 World 重建。恢复本身仍走
正常事务，因此可用一次编辑器 Undo 撤回。

`scene-cleanup.pskill` 约束模型：明确路径集合使用批量删除；“恢复刚才整轮工作”先列出 ChangeSet，再按精确
RunId 恢复，不能把两种破坏性操作混在一次工作流中。

## 自动化基线

- `PicoAgentTests`: 95/95。
- `PicoEditorTests`: 140/140。
- Trace 层级和 JSONL 重载均有固定测试。
- 37 条 Intent/Skill 路由和 10 个 Golden Tasks 均为离线、确定性测试。

后续 GAS、PicoGraph 和 ECS 每增加一组 Skill，必须追加路由用例、Verifier 和 Golden Task，而不能在聊天窗口增加
另一套临时判断。
