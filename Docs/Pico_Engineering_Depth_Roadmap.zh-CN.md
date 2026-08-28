# Pico 工程深度阶段路线

## 文档定位

本文档记录 Pico 当前最高优先级的 8 周工程加固阶段：

```text
Profiler + Runtime Scalability
Agent Reliability + Decoupling
```

在本阶段完成前，AI 游戏搭建、ECS、深入渲染、更完整 GAS、MCP、Multi-Agent 和 Code Agent 全部顺延。
后续对话、规划和实施如果与其他未完成排期冲突，以本文档为准。总体长期目标仍由
[AI 优先后续开发路线](Pico_AI_First_Development_Roadmap.zh-CN.md) 记录。

性能优化的跨周总账见[性能优化总览与追踪记录](Pico_Performance_Optimization_Log.zh-CN.md)。

## 调整原因

Pico 已经完成对象、反射、GC、Gameplay、物理、动画、网络、打包、Mini GAS、PicoGraph 和 Agent Harness
的多条纵向链路。继续横向增加系统会降低学习收益，并放大以下已出现的工程债：

- 没有统一 CPU Profiler 和固定 Runtime Benchmark，优化缺少证据；
- Object、Tick、GC 和 Replication 存在随规模放大的线性或重复工作；
- Agent 通用 Editor Adapter 体积过大，并开始识别 PicoSandbox 项目类名；
- Agent Failure、Recovery、Tool Result、Context 和 Revision 的语义仍不够严格；
- 现有离线 Eval 能验证基础 Harness，但故障注入和对抗任务深度不足。

本阶段不以新增功能数量作为进度，而以“可测量、可复现、可解释、无回归”作为完成定义。

## 固定工程规则

每个优化任务必须经过：

```text
固定场景
 -> 建立 Release 基线
 -> Profiler 定位
 -> 写出优化假设
 -> 最小修改
 -> 重跑 Benchmark 与正确性测试
 -> 记录收益、代价和未采用方案
```

- 不允许先重写系统、后寻找理由。
- Debug 和 Release 数据分开；性能结论以 Release 为主。
- 使用 P50、P95、最大停顿、内存和吞吐，不只记录单次最好结果。
- 每个微基准控制在约 30～60 秒；当前阶段不要求 10 分钟压力测试。
- 没有显著或稳定收益的实验不进入 Runtime 主线，可以保留为独立研究记录。
- 性能优化不得绕过生命周期、GC、反射、事务、网络 Authority 或 Package 边界。
- Agent 优化不得用 Prompt 掩盖 Runtime、Tool、Policy 或 Verifier 的结构问题。

## 两条纵向切片

### Profiler + Runtime Scalability

目标是让 Pico 能回答：一帧时间花在哪里、对象数量增长时哪里先退化、优化是否真实有效。

当前优先热区：

1. Object Registry 的 `(Outer, Name)` 查找与对象树操作；
2. Tick Group 每帧依赖图构建和重复查找；
3. 每连接 Actor 遍历、Replication Schema 构建和属性全量编码；
4. Stop-the-world GC 的引用元数据扫描和临时分配；
5. Task System 是否存在真实可并行负载。

本阶段不预设 SoA、SIMD、对象池、并行 GC 或 Task Graph 一定有收益。

### Agent Reliability + Decoupling

目标是让 Agent 的正确性来自稳定契约、故障语义和 Eval，而不是项目类名、Prompt 或 Assistant 的最终声明。

当前优先债务：

1. 将集中式 Editor Agent Tools 拆为按子系统注册的 Capability Provider；
2. 移除通用 Editor 对 `PSandbox*` 和固定 GAS 属性名的识别；
3. 建立 Failure Taxonomy 和确定性 Recovery Policy；
4. 将 Tool Result 拆为机器事实、Artifact、诊断、Revision 和展示文本；
5. 从全局 StateRevision 演进为资源域 Revision；
6. 增加 Failure Injection、Adversarial Eval 和可比较 Metrics。

## 第 1 周：测量基础（已完成）

完成记录与命令见
[工程深度第 1 周：Profiler、Runtime Benchmark 与 Agent Metrics](EngineeringDepthWeek01_MeasurementFoundation.zh-CN.md)。

### Runtime

新增最小 `PicoProfiler`：

- `PICO_PROFILE_SCOPE(Name)` 和 `PICO_PROFILE_FUNCTION()`；
- Frame、Thread、Parent Scope、开始/结束时间；
- Scope 聚合统计与 Chrome Trace JSON；
- Release 可显式开启，Shipping 默认关闭；
- Profiler 关闭时不分配内存，宏退化为极低开销。

首批 Scope：

```text
EngineLoop.Tick
World.Tick
Tick.Schedule
Tick.Execute
Physics.Step
Net.Receive
Net.Replication
GC.Mark
GC.Sweep
Render.Frame
```

建立 `PicoRuntimeBenchmarks` 独立 Target：

- Object：1K、10K、100K 创建、查找、重命名和销毁；
- Tick：1K、10K TickFunction，静态和依赖变化；
- GC：不同存活率、引用密度和 Outer 深度；
- Replication：100、1K Actor，1%/10%/100% Dirty Ratio。

### Agent

建立统一 Metrics Schema：Run/Turn/Tool 数、Provider/Approval/Tool/Validation 延迟、上下文字节、Repair、
Cache Hit、Failure Class、Forbidden Tool 和 Completion Rate。

### 周末验收

- Chrome Trace 可以还原一帧主要阶段；
- Benchmark 输出版本化 JSON/CSV；
- Agent Run 输出机器可读 Metrics；
- 本周不提交任何未经基线证明的性能优化。

验收状态：Profiler 主干 Scope、Chrome Trace/聚合导出、独立 Quick/Full Benchmark 契约、版本化 JSON/CSV、
Agent Run Metrics 和 Golden Task Metrics 引用均已落地。Debug Core/Agent 测试分别为 102/102、101/101，
Release Agent 为 101/101；Debug/Release Quick Benchmark 均通过。完整硬件基线、P50/P95 和 Failure Taxonomy
按边界留给第 2 周。

## 第 2 周：基线与失败语义

### Runtime

运行并保存 Debug/Release 基线，记录 P50、P95、最大耗时、对象/Tick/连接/字段规模、GC 阶段时间、
Replication CPU/BytesPerFrame，以及核心对象 `sizeof`。输出 `RuntimeBaseline.json` 和基线分析文档。

### Agent

新增与 Run Phase 正交的 `EAgentFailureClass`：

```text
None
ModelProtocol
InvalidArguments
PermissionDenied
ApprovalRejected
PreconditionFailed
ExecutionFailed
VerificationFailed
Infrastructure
BudgetExceeded
Conflict
Cancelled
```

建立 `Retry/RefreshState/Replan/WaitForApproval/AskUser/Rollback/Abort` Recovery Policy 表。现有
`EAgentStatus` 只表示 Run 阶段，不继续扩展成错误原因集合。

### 周末验收

- 能根据数据列出 Runtime 最昂贵的三个 Scope；
- 每类 Failure 有默认 Recovery Action 和是否可重试；
- PermissionDenied、ApprovalRejected 和非法参数不会进入无意义自动重试。

验收状态：已完成。Debug Quick 7 样本与 Release Full 5 样本基线已保存，输出包含 P50/P95/max、
Replication BytesPerFrame、GC/Tick Scope 和核心类型尺寸。数据确认最昂贵的端到端 Case 为 100K Object
Rename/Create/Find，Profiler 累计前三为 `Tick.Schedule`、`Tick.Execute`、`GC.Sweep`。12 类 Failure 均有固定
Recovery Policy，失败语义已贯穿 ToolRegistry、Session、Runtime、Metrics 和 Golden Task；非法参数、权限拒绝、
审批拒绝均不会自动重试。详见 `EngineeringDepthWeek02_BaselineAndFailureSemantics.zh-CN.md`。

## 第 3 周：Object Index 与 Tool 解耦

### Runtime

在稳定 Handle Slot Registry 之外增加：

```text
(OuterHandle, FName) -> ObjectHandle
```

Add、Rename、Destroy、GC 和 Registry Reset 必须同步维护索引；Debug 提供一致性检查。暂时不增加 Class、Tag、
Path 等次级索引。

### Agent

引入 `IAgentCapabilityProvider`，按职责拆为：

```text
WorldToolProvider
ObjectToolProvider
AssetToolProvider
BlueprintGraphToolProvider
GameplayToolProvider
ProjectProcessToolProvider
```

每个 Provider 独立提供 Tool Descriptor、Handler、Knowledge Record、Verifier 和 Revision Read/Write Set。
保持现有 Tool Name 和 Session 协议兼容。

### 周末验收

- 100K Object 查找不再扫描完整 Slot 表；
- Object/GC/序列化测试全部通过；
- 现有 Tool Catalog 和 Golden Tasks 不减少；
- 通用 Editor Provider 不再引用 `PSandbox*` 类名。

验收状态：已完成。Object Registry 新增 `(OuterHandle, FName) -> ObjectHandle` 索引，Add/Rename/Destroy/
GC/Reset 同步维护并提供双向一致性检查。相同 5 样本 Release Full 合约下，100K Create/Find/Rename P95
分别提升约 195.2x、181.6x、153.6x；Destroy 的 Outer 子对象扫描仍是已记录热点。现有 33 个 Editor Tool
已归属六类 `IAgentCapabilityProvider`，Catalog 暴露 Provider 与 Revision Read/Write Set，Provider 同时生成 Knowledge
Manifest 且原子安装。Tool Name、Golden Tasks 和 Session 协议保持兼容，通用 Editor Tool 不再包含 `PSandbox*`
类名。详见 `EngineeringDepthWeek03_ObjectIndexAndAgentCapabilities.zh-CN.md`。

## 第 4 周：Tick Cache 与结构化 Tool Result

### Runtime

Tick Scheduler 使用 Generation 驱动缓存：

```text
Register / Unregister / Prerequisite Change
 -> ScheduleGeneration++
 -> Tick Group Dirty
 -> 下一帧重建一次
 -> 普通帧执行 Cached Schedule
```

增加 RegistrationId 快速索引、依赖环诊断和 Build/Execute 分项计时。保持 Game Thread 串行，不在本周接入
Task System 并行执行。

### Agent

Tool Result 增加结构化字段：

```text
Status
FailureClass
Facts
Artifacts
Diagnostics
StateChanges
RevisionChanges
RecoveryHint
```

LLM 展示文本从结构化结果生成，不作为 Runtime 事实。大结果写入 Artifact，消息历史只携带 Handle 和摘要。

### 周末验收

- 静态 Tick 图的普通帧不再重建拓扑；
- 动态注册/注销/依赖变化只触发必要 Group 重建；
- Verifier、Trace、Replay 和 UI 共用结构化 Tool Result；
- 不通过解析自然语言判断 Tool 是否成功。

## 第 4 周后置门：Object Hierarchy Index

第 4 周验收完成后、进入第 5 周 Replication Scaling 前，处理第 3 周基准暴露出的 Destroy 热点。该任务是
已排期的性能债务，不再只作为风险备注。实现范围严格限定为对象层级关系，不顺带增加 Class、Tag 或 Path Index。

### Runtime

增加：

```text
OuterHandle -> ChildHandle Set
```

用它替代 `HasChildObjects` 和对象树销毁中的全 Slot 扫描。Add、Destroy、GC Sweep、Outer 变更和 Registry Reset
必须同步维护索引；Debug 提供父子双向一致性检查。先记录索引的额外内存与维护耗时，再决定 Child Set 的具体容器，
不得只为了降低单项耗时而破坏稳定 Handle 和 Outer 生命周期规则。

### 周末验收

- `HasChildObjects` 不再扫描完整 Live Slot；
- 单对象销毁、递归对象树销毁、GC Sweep、Slot 复用和 Outer 变更测试全部通过；
- 100K Destroy 使用与第 3 周相同的 Release Full 合约重新采样，并与 `7.573 s` P95 基线对比；
- 报告 Destroy P50/P95/max、索引内存成本和 Add/Destroy 维护成本；
- 若数据证明索引收益不足以覆盖复杂度，保留测量结果并明确否决原因，不强行合入。

## 第 5 周：Replication Scaling 与故障注入

### Runtime

按风险从低到高完成：

1. 按 `PClass` 缓存 `FReplicationSchema`；
2. `(ConnectionId, NetObjectId)` Channel Index；
3. NetObject Handle Index；
4. 消除 Field Delta 的重复线性查找；
5. 分别统计 Gather/Compare/Serialize/Queue；
6. 增加 Actor ReplicationGeneration、Property Dirty Mask 和 Connection LastObservedGeneration。

初期保留可选完整轮询校验，用来发现漏标 Dirty。暂不实现复杂 Replication Graph。

### Agent

实现可控 Failure Injection：Provider Timeout/Invalid JSON、Crash Before Execute、Crash After Side Effect、
Crash Before Persist、Duplicate ToolCall、Approval Rejection、Verification Failure 和 Session Append Failure。

### 周末验收

- 无变化 Actor 不再重复编码全部属性；
- 双客户端现有同步、预测与修正结果不变；
- 副作用完成但结果未记录时先 Reconcile，不重复创建对象；
- 不可恢复错误停止并保留 Journal、Trace 和现场。

## 第 6 周：GC 深化与对抗评测

### Runtime

默认只实现低风险 GC 优化：缓存每个 `PClass` 的强引用属性布局、复用 Mark Buffer/Work Stack，并分开记录
RootScan/Mark/UnreachableSort/Destroy。只有基线证明 P95 Pause 已成为真实问题，才另行规划增量 GC；并行 GC
不进入本阶段。

### Agent

Golden Tasks 增加：Knowledge Prompt Injection、未知 Tool、Skill Forbidden Tool、审批后参数篡改、旧 CallId
重放、重复查询耗尽预算、修改失败后虚假完成、Play/Package 混淆和项目 Skill 越权写 Engine。

建立确定性 RAG Benchmark：Recall@1/3/8、MRR、ContextBytes、RetrievalLatency 和 ForbiddenSourceRate。
本周不引入 Embedding。

### 周末验收

- GC 改动能说明对哪个阶段、哪个对象规模有效；
- 对抗任务不会产生未审批副作用；
- 根据检索数据决定未来是否值得做 Embedding，而不是预设答案。

## 第 7 周：Revision、上下文与可视化

### Runtime

在 PicoInspector 或独立开发面板显示 Frame P50/P95、Top CPU Scopes、Object/Tick 数、Schedule Rebuild、最近
GC、Replicated Actor/DirtyField/BytesPerFrame 和 Task Queue。正式游戏 HUD 不承载这些开发统计。

### Agent

将单一 `StateRevision` 演进为 World/Asset/Graph/Config/Knowledge Revision。Tool Descriptor 声明 Reads/Writes，
只读缓存只依赖相关 Revision。增加有界 Message History、Current Goal、Progress Ledger、最近失败、待审批操作
和大结果 Artifact Handle。

### 周末验收

- Graph 修改不会让无关 Asset 查询缓存失效；
- 长会话上下文有固定上限且不丢失审批、目标和最近错误；
- Runtime 与 Agent Metrics 均可被开发者直观看到。

## 第 8 周：综合验收与工程报告

### Runtime 固定验收

1. 100K Object 创建、查找、销毁和 GC；
2. 10K TickFunction 静态图和动态修改；
3. 1K Replicated Actor、两个客户端、1%/10%/100% Dirty；
4. PicoSandbox 真实编辑器、Play 和 Package 场景。

输出优化前后 CPU、P50/P95、内存、BytesPerFrame、ScheduleRebuild、GCPause 和正确性测试结果。不强制追求
预设百分比，必须解释数据与代价。

### Agent 固定验收

Fake Provider 与真实 Provider 分开运行普通 Golden、Adversarial、Failure Injection、Crash Recovery、
Forbidden Tool、长上下文和 Provider 对比。输出 CompletionRate、Steps、ToolCalls、RepairRate、FailureDistribution、
Token/Latency、ForbiddenSideEffect 和 RecoverableFailureRecoveryRate。

### 阶段完成门槛

- Runtime 优化均有可复现 Benchmark 和前后数据；
- Agent 通用层不存在 PicoSandbox 类名/属性名硬编码；
- Editor、Play、网络、Graph、GAS 和 Package 无行为回归；
- 形成“采用了什么、拒绝了什么、为什么”的工程报告；
- 只有门槛全部通过后，才恢复 Agent 游戏搭建闭环。

## 冻结范围

8 周内暂缓：

- ECS 和全面数据导向迁移；
- 新渲染效果、RHI 扩展和光线追踪；
- 更完整 GAS、GameplayTask 和新项目玩法组件；
- SIMD Intrinsics、全面 SoA、自定义通用 Allocator；
- Tick 全面并行、通用 Task Graph、并行/增量 GC；
- Embedding、Vector DB、MCP、Multi-Agent 和 Code Agent；
- 仅服务于单一 Demo 的 Agent Tool。

唯一允许新增的功能是完成测量、解耦、验证、恢复和可视化所必需的开发能力。

## 后续恢复顺序

```text
工程深度阶段通过
 -> Agent 游戏制作链路六周阶段
 -> 以第二个不同玩法验证复用性
 -> 再评估 ECS 是否有高密度真实用例
 -> Render Architecture
 -> 其他可选增强
```

性能数据若证明 ECS、Task Graph、增量 GC 或 SIMD 没有真实收益，应继续延期，而不是因为旧计划存在就执行。
