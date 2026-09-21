# Pico Agent ReAct 轻量化加固路线

本文档记录 Pico Agent 在阶段 A 前四周完成后、继续增加玩法积木前的运行时加固计划。目标是让现有单 Agent
ReAct 链路更可靠、可恢复和可评测，同时保持 Harness 独立、简单任务低延迟以及未来 Provider、外部 Agent 和
Code Harness 的扩展空间。

> 状态：**进行中**。R1、R2、R3、R4 已完成，交付与验收见
> [ReAct 加固 R1：Task State 与 Context Assembler](AgentReActHardeningR1_TaskStateAndContextAssembler.zh-CN.md)；
> [ReAct 加固 R2：Observation、Evidence 与行动振荡控制](AgentReActHardeningR2_ObservationEvidenceAndOscillation.zh-CN.md)；
> [ReAct 加固 R3：分块检索、BM25 与低置信 Query Rewrite](AgentReActHardeningR3_RagAndQueryRewrite.zh-CN.md)；
> [ReAct 加固 R4：Memory 视图、阈值压缩与 Revision 失效](AgentReActHardeningR4_MemoryViewsCompressionAndInvalidation.zh-CN.md)；
> 下一项为 R5 条件式 Reflection、恢复阶梯与真实场景 Eval。

## 固定决策

- 当前只维护一种 Agent 执行模式：`ReAct`。暂不实现 Direct、Plan-Execute、GoT、多 Agent、Handoff 或模式路由器。
- 现有 Build Plan/DAG 只作为复杂任务的结构化 Task State 和审批产物，不建立第二套执行 Runtime。
- MCP 继续只使用本地 Streamable HTTP；暂不实现 stdio。MCP 是 Adapter，不是 Agent Runtime 或安全边界。
- 不要求、不展示、不持久化模型原始 CoT；只保存目标、步骤、简短决策摘要、工具事实、验证证据和失败原因。
- 不迁移到 LangChain/LangGraph。只参考成熟系统的 Checkpoint、Store、Interrupt、Tracing 和 Guardrail 机制。

## 架构与依赖边界

```text
AI Chat / Codex / Claude Code / MCP Client
                  |
                  v
             Adapter Layer
                  |
                  v
 Agent Runtime: ReAct / Task State / Context
                  |
                  v
 Harness: Validate -> Permission -> Approval -> Transaction
          -> Execute -> Verify -> Journal
                  |
                  v
        Editor / World / Asset / Process
```

- Agent Runtime 决定“做什么、下一步是什么、目标是否完成”。
- Harness 决定“工具能否执行、如何审批、怎样事务化、如何验证副作用和回滚”。
- Adapter 只负责协议和 UI 映射，不能复制策略、审批或工具执行管线。
- Harness 不得依赖 Provider、Memory、RAG、MCP、UI 或 Agent Task State；Agent Runtime 只能消费 Harness 返回的
  结构化事实，不能绕过 Harness 直接修改 Editor。
- Agent 层的 Progress Check 判断结果是否推进用户目标；它不能重复实现 Harness 的 Tool Verifier。

## 最小运行时结构

### `FAgentRuntime`

保留现有循环，在内部统一为：

```text
Assemble Context -> Model -> Tool Call -> Harness
 -> Observation -> Progress Check -> 下一轮或结束
```

不为 Query Rewrite、Reflection、Memory 或 Planner 各建立一套常驻 Runtime。

### `FAgentTaskState`

第一版只保存：

```text
Goal
SuccessCriteria
Constraints
CurrentStep
RemainingSteps（简单任务可为空）
EvidenceRefs
OpenQuestions
Revision
```

依赖关系是 `RemainingSteps` 的可选字段。只有跨多个资源、包含多个写操作或存在明确前置关系时才创建；普通查询和
单工具修改不能被强制转换为完整 DAG。

### `FAgentContextAssembler`

实现为无状态 Helper 或纯函数，只负责筛选、排序、预算和渲染：

```text
Instructions
Task State
Conversation / Memory
Latest Observations
```

它不拥有数据库、不调用模型、不执行检索，也不修改 Task State。Knowledge、Session 和工具观察继续由现有所有者
管理，Assembler 只消费带 Source、Trust、Revision、TokenCost 和 Expiry 的候选上下文。

## 按需增强策略

| 能力 | 默认路径 | 触发条件 |
| --- | --- | --- |
| Task State | 轻量存在，简单任务不生成步骤列表 | 不产生额外模型调用 |
| RAG | 精确标识符、字段和 BM25 | 当前步骤缺少项目事实 |
| Query Rewrite | 默认关闭 | 指代不清、检索为空或低置信度 |
| Memory Retrieval | 按当前实体、资源和步骤查询 | 最近上下文不足 |
| Context Compression | 默认不运行 | 达到上下文高水位线 |
| Reflection | 默认不运行 | 后置条件失败、振荡或最终完成前证据不足 |
| Checkpoint | 增量事件 | 写操作、审批、中断边界和任务结束 |

简单任务的目标路径仍是“一次模型调用、一次工具调用、一次验证”。任何增强能力都必须支持 Feature Flag，并由
Eval 证明收益后才能默认启用。

## Memory 与 RAG 取舍

- 四种记忆是逻辑分类，不是四套物理数据库。Session Event Log 保存线程内状态；Project Knowledge Store 通过
  `Kind=Semantic/Episode/Procedure/Entity` 保存跨会话知识。
- Entity Memory 复用 AssetDescriptor、对象路径、稳定 ID 和多域 Revision，不复制完整资产数据库。
- 项目事实按来源 Revision、存在性和验证状态失效，不使用通用的纯时间衰减删除有效资源事实。
- 第一版 RAG 只增加切块、精确字段、BM25、实体/依赖召回和确定性融合；没有 Benchmark 证据前不增加独立向量
  数据库。Embedding 后端只能通过现有检索接口可选接入。
- Query Rewrite 只生成检索查询，不能替换用户原始指令或改变授权范围；默认先使用确定性实体解析，低召回时才
  允许额外模型调用。

## Harness 兼容要求

本阶段不改变 Tool Provider、权限、审批、事务、Undo、Journal、MCP Toolset 或 Game Thread 语义。若结构化工具
结果仍缺少通用执行事实，只允许扩展共享结果字段：

```text
OperationId
ReadRevisions
WrittenRevisions
Artifacts
VerifiedFacts
```

这些字段必须对内置聊天、MCP、Codex 和未来外部 Agent 一致可用，不能包含 ReAct 专用 Prompt 或模型状态。

## 五周实施计划

本计划作为阶段 A 第四周后的可靠性门。通过后再继续阶段 A 第五至八周的玩法积木、PicoGraph、端到端组装和
真实 Scenario Runner。

| 周次 | 工作 | 验收标准 |
| --- | --- | --- |
| R1（已完成） | 最小 `FAgentTaskState`、轻量 `FAgentContextAssembler`、上下文/Token/延迟基线和 Feature Flags | 简单任务的模型轮次、工具次数不增加；上下文来源与裁剪可解释 |
| R2（已完成） | Harness 结果到 Observation 的统一映射、成功条件/Evidence 绑定、最近行动指纹和 Revision 进展判断 | Agent 不能仅凭 `succeeded` 宣称完成；A-A、A-B-A 可受控停止且不误杀正常连续读取 |
| R3（已完成） | 文档切块、精确字段/BM25、Entity/Revision 索引和低召回 Query Rewrite | RAG Benchmark 的 Recall@3/MRR 从 0.8571 提升到 1.0；Rewrite 为本地确定性扩展；原始用户指令保持不变 |
| R4（已完成） | Knowledge `Kind`、Episode/Entity 轻量视图、阈值式摘要压缩和 Revision 失效 | Session Episode 可从 Event Log 重建；旧 Revision 默认退出上下文；四类视图共用一个 Store；压缩按阈值触发且可关闭 |
| R5 | 条件式 Reflection、恢复阶梯、真实 Editor/Play/联机/Package Eval 和性能回归门 | 最多一次反思修正；仍失败时询问或停止；形成成功率、Token、延迟和重复调用对照报告 |

## 防膨胀质量门

每项新增能力至少改善以下一项，否则保持关闭或删除：

- Verified Task Success Rate；
- 重复工具调用率与路径振荡率；
- 无证据完成声明率；
- 中断恢复成功率；
- 平均 Token、完成时间或人工介入次数。

固定回归门槛：

1. 简单任务不得增加模型轮次；本地上下文装配 P95 目标小于 `5 ms`；
2. 循环指纹、最近观察和 Trace 均有固定上限，Checkpoint/Artifact 有保留与清理策略；
3. RAG、Rewrite、Reflection 和 Compression 均可独立关闭，关闭后退回现有 ReAct 基线；
4. 新增依赖不得反向进入 Harness；MCP 关闭时 Agent Runtime 与内置聊天仍可完整工作；
5. 没有确定性 Eval 或真实场景数据证明收益的向量检索、额外模型调用和新抽象不得默认启用；
6. 修改 Provider、Context、Memory、RAG、Prompt 或 Runtime 后必须运行离线 Golden Tasks；达到阶段 R5 后还必须
   运行真实 Editor Fixture 和双客户端 Scenario。

## 明确延期

- Direct、Plan-Execute、GoT、多 Agent、Handoff 和并行子 Agent；
- MCP stdio、远程公网 MCP、多租户和云端 Agent 服务；
- 四套独立 Memory Service、独立分布式向量数据库和每轮固定 Reflection；
- 为展示架构而增加的通用插件内核、第二套 Tool Pipeline 或第二套审批系统。

## 参考原则

- OpenAI Agents SDK：保持少量原语，Loop、Tool、Guardrail、Session 和 Trace 可以组合但不要求堆叠。
- Anthropic Building Effective Agents：从简单、可组合模式开始，只在评测证明结果改善时增加复杂度。
- LangGraph Persistence：区分线程 Checkpoint 与跨线程 Store；Pico 只吸收语义，不引入其完整运行时。
- Codex Core/Tool Orchestrator：UI 与核心 Loop 分离，审批、沙箱和重试集中在工具执行边界。
