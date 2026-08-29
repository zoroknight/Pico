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

验收状态：已完成。Tick Scheduler 使用按 Group Generation 失效的 Cached Schedule 和 RegistrationId 哈希索引，
静态图普通帧不再重建拓扑；10K 首次静态调度与依赖变化 P95 相对第 2 周分别提升约 5.1x 和 8.5x，连续 60 个
静态帧 P95 为 20.647 ms 且 rebuild 为 0。Agent Tool Result 已统一 Status、Facts、Artifacts、Diagnostics、
StateChanges、RevisionChanges 与 RecoveryHint，并贯穿 Verifier、Session、Operation Journal、Replay、模型历史和
AI Chat；超过 64 KiB 的 Facts 外置为 Artifact。详见
`EngineeringDepthWeek04_TickCacheAndStructuredToolResult.zh-CN.md`。

## 第 4 周收尾门：Frame Pacing Correctness

在继续解释 Runtime 热点前，先修正 Editor/Game 当前由软件 `MaxFPS` 与 GLFW VSync 同时等待造成的帧调度
错误。实测 Release Game 在相同 StarterWorld 中由约 `28 FPS` 提升到 `59.2 FPS / 16.89 ms`；角色输入与移动
也明显更平滑。该对照证明现阶段首先要消除重复等待，而不是把 Idle/Present 时间误判为 Tick 或渲染成本。

### Runtime

建立互斥的 Frame Pacing Policy：

```text
VSync On                 -> Present/VSync 负责等待，忽略软件 MaxFPS
VSync Off + MaxFPS > 0   -> 完整 Render/Present 后由软件 Frame Pacer 等待
VSync Off + MaxFPS = 0   -> Unlimited，不主动等待
Dedicated Server         -> 使用独立 ServerTickRate，不依赖显示器
```

EngineLoop 不再在 Render/Present 之前睡眠。Editor、Game 与 Server 共享计时基础设施，但由最外层 Host 根据自身
是否拥有窗口和 VSync 决定 Pacing。保留命令行覆盖，并增加 `VSync / 30 / 60 / 90 / 120 / 144 / Unlimited`
可配置档位；默认 Release Game 使用 VSync，当前质量门槛仍为稳定 60 FPS，120 FPS 只作为性能余量测试，不在
本阶段引入多线程或渲染后端重构。

### 周末验收

- StarterWorld Release Game 默认 VSync 在 60 Hz 显示器约为 60 FPS，不再回落到约 28～30 FPS；
- `-vsync=0 -maxfps=30/60/120/0` 分别表现为对应软件上限或 Unlimited；
- Runtime 状态窗口显示 FPS、平均帧时间和当前有效 Pacing Mode；
- 30/60/120 FPS 下角色移动速度、跳跃高度、动画时长和网络状态同步语义不随渲染帧率改变；
- 保存修复前后的 Release 数据，明确区分 Game/Tick、Render、Present/VSync 与 Idle/Pacing；
- 编辑器的前台 VSync、后台 10～20 FPS 和不可见预览降频保留到第 7 周深化，不阻塞本收尾门。

验收状态：已完成。EngineLoop 不再在 Render/Present 前等待；Game、Editor 和无窗口 Launch 在完整帧末尾通过
同一 `FFramePacingSettings` 解析 VSync、Software 或 Unlimited。Windows Software Pacer 使用高精度 Waitable
Timer 与短尾段校准，30帧纯节拍 30/60/120 FPS 中位耗时分别为 `979.10/495.05/253.59 ms`，Unlimited 为
`9.67 ms`。真实 Release Game 固定60帧总耗时为 DefaultVSync `1607.46 ms`、Software30 `2561.16 ms`、
Software60 `1514.87 ms`、Software120 `1004.96 ms`、Unlimited `555.44 ms`；其中约0.55秒为启动和场景加载
固定成本。Runtime 状态窗口显示最终 Pacing Mode。详见
`EngineeringDepthWeek04_FramePacingCorrectness.zh-CN.md`。

## 第 4 周后置门：Object Hierarchy Index（已完成）

第 4 周 Tick Cache 与 Frame Pacing Correctness 收尾门验收后、进入第 5 周 Replication Scaling 前，处理第 3 周
基准暴露出的 Destroy 热点。该任务是已排期的性能债务，不再只作为风险备注。实现范围严格限定为对象层级关系，
不顺带增加 Class、Tag 或 Path Index。

### Runtime

增加：

```text
OuterHandle -> ChildHandle Set
```

用它替代 `HasChildObjects` 和对象树销毁中的全 Slot 扫描。Add、Destroy、GC Sweep 和 Registry Reset 同步维护
索引；`DestroyAllObjects` 使用叶节点队列，在子节点移除后推进父节点。Debug 提供父子双向一致性检查。Pico 当前
没有运行时 Reparent/SetOuter API，Outer 只在构造时确定；未来新增该 API 时必须原子维护 Name 与 Hierarchy
两个索引，不在本门为了清单额外创造未使用接口。

### 周末验收

- `HasChildObjects` 不再扫描完整 Live Slot；
- 单对象销毁、递归对象树销毁、GC Sweep、Slot 复用和 Registry Reset 测试全部通过；
- 100K Destroy 使用与第 3 周相同的 Release Full 合约重新采样，并与 `7.573 s` P95 基线对比；
- 报告 Destroy P50/P95/max、索引内存成本和 Add/Destroy 维护成本；
- 若数据证明索引收益不足以覆盖复杂度，保留测量结果并明确否决原因，不强行合入。

验收状态：已完成。相同 Release Full、5 样本、100K Object 合约下，平铺 Destroy P50/P95 从第 3 周的
`7100.452/7573.463 ms` 降至 `33.865/107.531 ms`，P95 约提升 `70.4x`。新增层级 Case 中，100K
直接子对象的 Create P50/P95 为 `128.412/180.959 ms`，整树 Destroy 为 `48.375/172.380 ms`；索引结构
估算存储为 `1,848,648 bytes`，约 `18.5 bytes/relation`，该估算不包含标准库节点分配器额外开销。Debug/Release
完整测试矩阵均通过。详见 `EngineeringDepthWeek04_ObjectHierarchyIndex.zh-CN.md`。

## 第 5 周：Replication Scaling 与故障注入

### Runtime

先用约 1～2 天完成最小内存观测门，再按风险从低到高推进 Replication：

1. 增加轻量 `PicoMemoryTracker`，第一版由子系统主动报告，不拦截全局 `new/delete`；
2. 固定 `CurrentBytes/ReservedBytes/PeakBytes/ElementCount/GrowthCount` 指标，并接入 Benchmark JSON/CSV；
3. 首批分类覆盖 `Object.Slots`、`Object.NameIndex`、`Object.HierarchyIndex`、`GC.Scratch`、
   `Profiler.Events`、`Replication.Schema` 和 `Replication.Channels`；
4. 按 `PClass` 缓存 `FReplicationSchema`；
5. `(ConnectionId, NetObjectId)` Channel Index；
6. NetObject Handle Index；
7. 消除 Field Delta 的重复线性查找；
8. 分别统计 Gather/Compare/Serialize/Queue；
9. 增加 Actor ReplicationGeneration、Property Dirty Mask 和 Connection LastObservedGeneration；
10. 同时记录 Schema/Channel 占用、Dirty Actor/Property 数、缓存跳过次数和每帧编码字节。

初期保留可选完整轮询校验，用来发现漏标 Dirty。暂不实现复杂 Replication Graph。

内存观测前置门验收状态：已完成。`PicoMemoryTracker` 已以固定 Tag 接入 Object、GC、Profiler 和 Replication，
Benchmark v3 输出 `memory_categories` 与 `PicoRuntimeMemory.csv`，Debug/Release 相关回归和 Release Full 七分类
自校验通过。100K Full 合约首次测得 `Profiler.Events` Peak/Reserved 约为 `120.47/138.58 MB`，确认第 7 周
Bounded Trace 的必要性。实现、口径和原始数据见
[第 5 周 Memory Observability Gate](EngineeringDepthWeek05_MemoryObservabilityGate.zh-CN.md)。

### Agent

实现可控 Failure Injection：Provider Timeout/Invalid JSON、Crash Before Execute、Crash After Side Effect、
Crash Before Persist、Duplicate ToolCall、Approval Rejection、Verification Failure 和 Session Append Failure。

### 周末验收

- 无变化 Actor 不再重复编码全部属性；
- 双客户端现有同步、预测与修正结果不变；
- Runtime Benchmark 能输出上述内存分类的当前值、保留容量和峰值，且关闭跟踪时不改变生命周期语义；
- 1K Actor、1%/10%/100% Dirty Case 能同时解释 CPU、缓存跳过率、内存和 `BytesPerFrame`；
- 副作用完成但结果未记录时先 Reconcile，不重复创建对象；
- 不可恢复错误停止并保留 Journal、Trace 和现场。

## 第 6 周：GC 深化与对抗评测

### Runtime

默认只实现低风险 GC 优化：

1. 缓存每个 `PClass` 的强引用属性布局，避免每次 GC 重复筛选完整反射属性链；
2. 复用 Mark Buffer、Work Stack、Unreachable List 和引用收集 Scratch，按历史峰值保留合理容量；
3. 分开记录 RootScan/Mark/UnreachableSort/Destroy 的 CPU、Current/Reserved/Peak Bytes 和 GrowthCount；
4. 对比不同对象规模与引用密度，确认收益来自减少重复扫描还是减少临时分配。

只有基线证明 P95 Pause 已成为真实问题，才另行规划增量 GC；并行 GC、GC Cluster 和复杂写屏障不进入本阶段。

### Agent

Golden Tasks 增加：Knowledge Prompt Injection、未知 Tool、Skill Forbidden Tool、审批后参数篡改、旧 CallId
重放、重复查询耗尽预算、修改失败后虚假完成、Play/Package 混淆和项目 Skill 越权写 Engine。

建立确定性 RAG Benchmark：Recall@1/3/8、MRR、ContextBytes、RetrievalLatency 和 ForbiddenSourceRate。
本周不引入 Embedding。

### 周末验收

- GC 改动能说明对哪个阶段、哪个对象规模和引用密度有效，并给出临时内存峰值与增长次数；
- 连续多轮 GC 不重复制造与历史峰值等量的临时分配，Object/GC/Serialization 生命周期回归保持通过；
- 对抗任务不会产生未审批副作用；
- 根据检索数据决定未来是否值得做 Embedding，而不是预设答案。

## 第 7 周：Revision、上下文与可视化

### Runtime

在 PicoInspector 或独立开发面板显示 Frame P50/P95、Top CPU Scopes、Object/Tick 数、Schedule Rebuild、最近
GC、Replicated Actor/DirtyField/BytesPerFrame、Task Queue，以及 Object/GC/Profiler/Replication 的
Current/Reserved/Peak Bytes。正式游戏 HUD 不承载这些开发统计。

将 Profiler 事件存储拆为 `Disabled`、`AggregateOnly` 和 `BoundedTrace`：长期开发默认使用聚合统计，
`BoundedTrace` 通过固定容量 Ring Buffer 只保留最近一段帧，完整 Trace 必须由用户显式开启。项目关闭、地图切换、
打包前或显式 Compact 命令可以在生命周期安全点 Trim 长期保留的峰值容量；禁止每帧自动 `shrink_to_fit()`。

深化第 4 周收尾门建立的 Frame Pacing：增加 Frame P50/P95/P99、超出 16.67 ms 的长帧计数，以及 Game/Tick、
Physics、Network、GC、Render、Runtime UI、Present/VSync、Idle/Pacing 分项。Editor 前台跟随 VSync，后台降到
10～20 FPS；只更新可见且启用 Realtime 的 Viewport/资产预览。此处依据数据决定是否增加基础质量档位，不能以
追求 120 FPS 为理由预先接入多线程、复杂 LOD 或替换渲染后端。

### Agent

将单一 `StateRevision` 演进为 World/Asset/Graph/Config/Knowledge Revision。Tool Descriptor 声明 Reads/Writes，
只读缓存只依赖相关 Revision。增加有界 Message History、Current Goal、Progress Ledger、最近失败、待审批操作
和大结果 Artifact Handle。

### 周末验收

- Graph 修改不会让无关 Asset 查询缓存失效；
- 长会话上下文有固定上限且不丢失审批、目标和最近错误；
- Runtime 与 Agent Metrics 均可被开发者直观看到；
- 连续运行时 Profiler Trace 内存保持有界，AggregateOnly 不保存逐事件历史；
- 安全点 Trim 不破坏 Object Handle、GC 引用、网络 Channel、资产缓存或编辑器事务。

## 第 8 周：综合验收与工程报告

### Runtime 固定验收

1. 100K Object 创建、查找、销毁和 GC；
2. 10K TickFunction 静态图和动态修改；
3. 1K Replicated Actor、两个客户端、1%/10%/100% Dirty；
4. PicoSandbox 真实编辑器、Play 和 Package 场景。

输出优化前后 CPU、P50/P95、内存、BytesPerFrame、ScheduleRebuild、GCPause 和正确性测试结果。不强制追求
预设百分比，必须解释数据与代价。

基于 Parent Child 数分布、Hierarchy 真实 Reserved/Peak Bytes、Create/Destroy 吞吐和 Child 遍历 P95，完成
当前 `unordered_map<ParentHandle, unordered_set<ChildHandle>>` 与紧凑连续 Child List 原型的 A/B 决策。第 8 周
只要求形成有数据支撑的“保留、延期或采用”结论，不强制重写已获得显著收益的 Hierarchy Index。

### Agent 固定验收

Fake Provider 与真实 Provider 分开运行普通 Golden、Adversarial、Failure Injection、Crash Recovery、
Forbidden Tool、长上下文和 Provider 对比。输出 CompletionRate、Steps、ToolCalls、RepairRate、FailureDistribution、
Token/Latency、ForbiddenSideEffect 和 RecoverableFailureRecoveryRate。

### 阶段完成门槛

- Runtime 优化均有可复现 Benchmark 和前后数据；
- Memory Tracker 分类可解释主要 Runtime 缓存的当前、保留和峰值成本，Profiler 长会话内存有明确上限；
- Agent 通用层不存在 PicoSandbox 类名/属性名硬编码；
- Editor、Play、网络、Graph、GAS 和 Package 无行为回归；
- 形成“采用了什么、拒绝了什么、为什么”的工程报告；
- 只有门槛全部通过后，才恢复 Agent 游戏搭建闭环。

## 冻结范围

8 周内暂缓：

- ECS 和全面数据导向迁移；
- 新渲染效果、RHI 扩展和光线追踪；
- 更完整 GAS、GameplayTask 和新项目玩法组件；
- SIMD Intrinsics、全面 SoA、自定义通用 Allocator、全局 `new/delete` 替换和完整 Binned 分配器；
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
