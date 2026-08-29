# Pico 性能优化总览与追踪记录

## 文档目的

本文档记录 Pico 性能优化的完整闭环：

```text
待优化点
 -> 瓶颈成因
 -> 监控与定位
 -> 优化假设
 -> 最小实现
 -> 正确性回归
 -> 优化前后数据
 -> 代价与后续决定
```

它是性能工作的长期总账，不替代每周实现文档、源码注释或原始 Benchmark 文件。后续每完成一项性能工作，
必须在这里增加一条记录，并链接到对应的周文档和机器报告。

## 固定测量规则

所有优化都遵循相同流程：

1. 固定构建配置、硬件、场景、对象规模和采样次数；
2. 使用 `PicoProfiler` 找到实际昂贵的 Scope；
3. 使用 `PicoRuntimeBenchmarks` 记录 Release 基线；
4. 写出可证伪的优化假设和预期代价；
5. 只修改最小的 Runtime 边界；
6. 重跑相同 Benchmark，并执行对象、GC、序列化、编辑器和 Sandbox 回归测试；
7. 同时记录 P50、P95、Max、吞吐、内存、维护成本和正确性结果；
8. 收益不稳定或代价不合理时，保留实验记录但不合入主线。

性能结论以 Release 为主，Debug 只用于发现断言、生命周期错误和回归。当前阶段的微基准目标为约 30～60 秒，
不要求额外进行长时间压力测试。

## 当前待优化点

| 子系统 | 当前问题 | 主要成因 | 发现方式 | 优先级 | 当前状态 |
|---|---|---|---|---|---|
| Object Registry | Find/Create/Rename 随历史 Slot 数量退化 | 查询和冲突检查依赖完整 Slot 扫描 | Object Full Benchmark、P95 增长曲线 | 已处理 | Name Index 已完成 |
| Object Registry | 100K Destroy 仍然很慢 | `HasChildObjects` 为维护 Outer 生命周期扫描 Live Slot，逐个销毁形成近似二次增长 | Destroy Benchmark、代码路径分析 | 已处理 | Hierarchy Index 已完成 |
| Tick Scheduler | 依赖顺序和 Group 重复重建 | 每帧缺少 Generation 驱动的稳定 Schedule Cache，RegistrationId 线性查找放大拓扑成本 | `Tick.Schedule` / `Tick.Execute` Scope、Tick Benchmark | 已处理 | 第 4 周完成 |
| Frame Pacing | Release Game 约 28 FPS，关闭软件 MaxFPS 后约 59.2 FPS | `WaitForMaxFPS(60)` 在 Render/Present 前等待，随后 GLFW VSync 再次等待 | Runtime FPS/Frame Time 状态窗口、`-maxfps=0` A/B 测试 | 已处理 | 第 4 周收尾门完成 |
| Memory Observability | 只有局部结构估算，无法按子系统解释 Current/Reserved/Peak 和增长次数 | 尚无统一轻量内存分类与机器可读 Benchmark 字段 | 现有 Hierarchy 估算边界、Profiler Trace 大文件、代码路径审计 | 已处理 | 第 5 周前置门完成 |
| Replication | Actor、Schema 和字段存在重复遍历或编码 | 每连接查找、Schema 构建、Dirty 状态缺少足够缓存 | Replication Benchmark、BytesPerFrame、分项 Scope | 已处理 | 第 5 周完成 |
| GC | 标记和清扫存在重复反射筛选与临时分配 | 强引用布局未缓存，Mark/WorkStack/Unreachable/Reference Scratch 尚未复用 | 分阶段 GC Scope、GC Benchmark、Memory Tracker | 已处理 | 第 6 周完成；Destroy-heavy 场景继续观察 |
| Profiler Memory | Full Trace 随事件数量持续增长，长 Benchmark 曾产生超大 Trace | 逐事件历史无默认容量上限，聚合统计与完整 Trace 未分模式 | Trace 文件大小、Profiler.Events Peak/Reserved Bytes | 中 | 第 7 周有界化 |
| Runtime Capacity | Slot 和子系统 Scratch 可能长期保留历史峰值 | 缺少只在生命周期安全点执行的显式 Trim 策略 | 地图切换前后 Reserved/Peak、真实项目复测 | 中低 | 第 7 周安全点 Trim |
| Object Hierarchy Storage | 嵌套 `unordered_*` 节点存在未计入估算的分配头和指针跳转 | 当前实现优先解决复杂度，尚无真实保留容量与 Child 分布数据 | Memory Tracker、Parent Child 分布、A/B Benchmark | 观察 | 第 8 周只做决策 |
| Task System | 是否值得并行仍无证据 | 工作负载和 Game Thread 边界尚未证明存在稳定并行收益 | 后续任务基准与帧预算 | 低 | 暂不预设实现 |

## 已完成优化记录

### 6. GC 强引用布局缓存与 Scratch 复用

状态：已完成低风险部分

基线：第 5、6 周 Release Full，1K/10K Object，10%/50%/90% 存活与 0/2/8 引用密度，均为 5 样本。

瓶颈与根因：每个可达对象重复遍历 PClass 继承链并筛选 Strong Property；四个临时容器每轮重建。旧 Scope 无法
区分 Root Scan、Unreachable Sort 和 Destroy。

优化方法：Metadata Finalize 时缓存 Strong Property 指针；Game Thread 复用 Mark、WorkStack、Unreachable 与
ReferenceCollector 容量；结束时清除全部 Handle 和 Mark 内容；新增四阶段纳秒计时与 Scratch 指标。

结果：10K、90% 存活、密度 8 的 P50/P95 从 `2432/2875 us` 降至 `812/1168 us`，P50 约 `3.00x`；
1K 同类 Case 从 `496/1147 us` 降至 `214/228 us`。10K、10% 存活 Case 从 `1285/1486 us` 变为
`1030/1507 us`，P50 约提升 `1.25x`，但 P95 仍有约 1.4% 波动，说明 Destroy-heavy 路径收益有限。

内存代价：Scratch Peak `156501 B`、Reserved `352440 B`、GrowthCount `6`；Current 在 GC 后为 0。旧版本清空
Reserved 报告，无法做同口径保留容量比较。

结论：缓存与复用合入；不宣称全面提速。增量/并行 GC 继续延期，Destroy 优化必须先拆分索引移除和析构成本。

相关文档：[第 6 周 GC 深化与 Agent 对抗评测](EngineeringDepthWeek06_GcAndAdversarialEvaluation.zh-CN.md)。

### 5. Replication Schema/Channel Index 与 Push Dirty

状态：已完成

基线：Release Full，1000 Actor，1%/10%/100% Dirty；新基线 5 样本，旧 Memory Gate 参照仅 1 样本。

瓶颈现象：旧路径即使只有 1% Actor 变化，也会按连接重建 Schema、线性定位对象和 Channel，并捕获比较全部属性。
1% 与 100% Dirty 的 CPU 差距远小于实际编码字段差距。

根因与监控：新增 Gather/Compare/Serialize/Queue 分项，以及 Cache Hit、Skipped Actor、Dirty/Encoded Property
计数后，确认稀疏更新的主要浪费发生在编码之前。

优化方法：按 `PClass` 缓存 Schema，建立 Channel 与 NetObject 双索引，FieldId 直接定位；Actor 使用
ReplicationGeneration 与 Dirty Mask，每条连接独立记录 LastObservedGeneration。固定 8 条历史解决多连接进度差，
超出窗口时保守全量同步。

正确性保护：保留 `FullPollingValidation` 检测漏标 Dirty；反射编辑、Transform 和内置复制 Setter 自动标脏；
测试覆盖两连接延迟观察、值回到 Baseline、直接写漏标与 Channel ACK。

结果：1%/10%/100% Dirty 的新 P50/P95 为 `144/173`、`179/274`、`846/1278 us`；旧 P50 为
`1032/1052/1367 us`，P50 约提升 `7.2x/5.9x/1.6x`。Bytes/Frame 保持 `230/2300/23000 B`。

代价：Replication Schema/Channels 结构 Peak 从前置门的 `24/200000 B` 增至 `72/272000 B`。该数据为主动
报告的结构估算，不含标准库节点分配器额外开销。

结论：合入。未来 Replication Graph 只负责相关性筛选，不能替代当前单 Actor Dirty/Generation 层。

相关文档：[第 5 周 Replication Scaling 与 Agent 故障注入](EngineeringDepthWeek05_ReplicationScalingAndFailureInjection.zh-CN.md)。

### 1. Object Name Index

#### 瓶颈

第 2 周基线显示，100K 对象的 Create、Find、Rename 和 Destroy 受到 Object Registry 完整 Slot 扫描影响。
其中 Find、Rename 和 Create 的退化最明显。

#### 优化方法

增加可重建的查询加速层：

```text
(OuterHandle, FName) -> ObjectHandle
```

稳定 Handle Slot Registry 仍是对象所有权和生命周期的唯一事实来源，Name Index 不保存第二份对象状态。索引在
Add、Rename、Destroy、GC Sweep 和 Registry Reset 中同步维护，并提供双向一致性检查。

#### 数据

第 2、3 周使用相同的 Release Full、5 个连续样本和 100K 规模：

| Case | 优化前 P95 | 优化后 P95 | 约提升 |
|---|---:|---:|---:|
| Create | 27.504 s | 140.873 ms | 195.2x |
| Find | 11.957 s | 65.831 ms | 181.6x |
| Rename | 28.335 s | 184.505 ms | 153.6x |
| Destroy | 9.056 s | 7.573 s | 1.2x |

完整 Full Benchmark 从约 371 秒下降到约 55 秒。Destroy 提升很小，说明 Name Index 没有解决对象树查询的
主要成本，不能把所有 Object 性能收益归因于同一个索引。

#### 正确性与代价

- 覆盖不同 Outer 下的同名对象、Rename、Destroy、Slot 复用、GC、序列化加载和 Object System 重启；
- Object、GC、Engine/Serialization、Editor 和 Sandbox 测试保持通过；
- 增加了索引维护和一致性检查成本；
- 索引只是加速层，Handle Serial、Outer 规则和 GC 生命周期没有被绕过。

详细实现见[工程深度第 3 周总结](EngineeringDepthWeek03_ObjectIndexAndAgentCapabilities.zh-CN.md)，原始数据见：

- `Docs/Baselines/EngineeringDepthWeek02/ReleaseBuild/RuntimeBaseline.json`
- `Docs/Baselines/EngineeringDepthWeek03/ReleaseBuild/RuntimeBaseline.json`

### 2. Tick Schedule Cache 与 RegistrationId Index

#### 瓶颈

静态 Tick 图每帧仍重复扫描、线性查找、构建依赖图和拓扑排序。第 2 周 10K StaticSchedule P95 为
`24.593 ms`，DependencyChange P95 为 `39.752 ms`，`Tick.Schedule` 是主要 Profiler 热点之一。

#### 优化方法

- 使用 `RegistrationId -> FRegisteredTick` 哈希索引替代重复线性查找；
- 每个 TickGroup 保存 Required/Built Generation 和 OrderedIds；
- Register、Unregister、Group 与 Prerequisite 变化只标脏相关 Group；
- 普通帧复用 Cached Schedule，执行时仍检查 Enabled、Owner、Interval 和帧注册上限；
- 增加 `Tick.Schedule.Build`、BuildCount 和结构化依赖环诊断。

#### 数据

| 10K Case | 优化前 P95 | 优化后 P95 | 约提升 |
|---|---:|---:|---:|
| 首次静态调度 | 24.593 ms | 4.800 ms | 5.1x |
| 依赖链变化 | 39.752 ms | 4.674 ms | 8.5x |

新增的 60 帧 Cached Case P50/P95 为 `20.135/20.647 ms`，即 P95 折算约 `0.344 ms/帧`，并记录
`schedule_rebuilds=0`。折算值只用于说明连续静态帧的摊销成本，不作为逐帧 P95。

#### 正确性与代价

- 缓存只保存拓扑，不缓存对象存活、Enabled 或 TickInterval 状态；
- 动态注册、注销和依赖变化仍在下一次目标 Group 执行前重建；
- 每个 Manager 增加 Registration Index、四组 OrderedIds/Generation 和诊断存储；
- Debug/Release Engine `695/695`，Agent `109/109`，Editor `142/142`。

详细实现见[工程深度第 4 周总结](EngineeringDepthWeek04_TickCacheAndStructuredToolResult.zh-CN.md)，原始数据见：

- `Docs/Baselines/EngineeringDepthWeek04/ReleaseBuild/RuntimeBaseline.json`

### 3. Frame Pacing Correctness

#### 瓶颈与证据

Pico 原先在 `EngineLoop::Tick` 尾部执行 `WaitForMaxFPS(60)`，Editor/Game Host 又通过
`glfwSwapInterval(1)` 启用 VSync。软件等待发生在 Render/Present 前，可能错过显示刷新窗口，随后 Present
再次等待。相同 Release Game 与 StarterWorld 中，默认路径约 `28 FPS / 35.7 ms`；保留 VSync 并追加
`-maxfps=0` 后为 `59.2 FPS / 16.89 ms`，角色移动明显更平滑。

#### 优化方法

- EngineLoop 只维护帧时钟和配置，不在 Tick 内主动等待；
- Game、Editor 和无窗口 Launch 在完整帧末尾解析 VSync、Software 或 Unlimited；
- VSync 开启时忽略软件 MaxFPS；VSync 关闭且 MaxFPS 大于0时才软件等待；
- 最小化窗口不能可靠 Present 时回退到软件上限，避免后台空转；
- Windows 使用高精度 Waitable Timer 加0.5ms短尾段校准，避免 `sleep_for` 每帧约8ms的稳定过睡；
- `[Display] VSync/MaxFPS` 为新配置入口，旧 `[Engine] MaxFPS` 保持兼容，命令行优先。

#### 数据

Release `PicoLaunch` 固定30帧、每档3样本的中位数：

| 模式 | 实测中位耗时 | 29个目标帧间隔理论耗时 |
|---|---:|---:|
| Software 30 | 979.10 ms | 966.67 ms |
| Software 60 | 495.05 ms | 483.33 ms |
| Software 120 | 253.59 ms | 241.67 ms |
| Unlimited | 9.67 ms | 无等待 |

真实 Release Game、StarterWorld、640x360、固定60帧的进程总耗时：

| 模式 | 总耗时 |
|---|---:|
| Default VSync | 1607.46 ms |
| Software 30 | 2561.16 ms |
| Software 60 | 1514.87 ms |
| Software 120 | 1004.96 ms |
| Unlimited | 555.44 ms |

Unlimited 结果包含约0.55秒启动、资产和场景加载固定成本，因此总进程时间只用于验证档位顺序和等待量，不作为
逐帧 P95。原始机器可读记录位于
`Docs/Baselines/EngineeringDepthWeek04FramePacing/ReleaseBuild/FramePacingBaseline.json`。

#### 正确性与边界

- Core `108/108`、Engine `699/699`、Editor `142/142`、Game `53/53`、Packaging `13/13`、Sandbox
  `58/58` 在 Debug/Release 保持通过；
- Fixed Physics Step、Movement Delta、动画时间和网络发送频率没有改为固定渲染帧率；
- Dedicated Server 独立 TickRate 尚未实现，当前无窗口 Host 会在 VSync 不可用时使用软件 MaxFPS 兜底；
- Frame P50/P95/P99、Present/Idle 分项、Editor 后台降频和不可见预览暂停留到第 7 周。

详细实现见[Frame Pacing 收尾总结](EngineeringDepthWeek04_FramePacingCorrectness.zh-CN.md)。

### 4. Object Hierarchy Index

#### 瓶颈

Name Index 完成后，100K 平铺对象逐个销毁的 P95 仍为 `7.573 s`。每次 `DestroyObject` 都调用
`HasChildObjects`，即使对象没有子节点，也要扫描完整 Live Slot；连续销毁使总工作量接近二次增长。对象树递归
销毁和 Shutdown 的叶节点搜索也存在相同问题。

#### 优化方法

- 增加 `OuterHandle -> ChildHandle Set`，只保存稳定 Handle，不复制对象状态；
- Add 在 Slot 与 Name Index 发布后登记父子关系；
- Destroy 和 GC Sweep 在 `BeginDestroy` 完成、Handle 仍可解析时撤销 Name 与 Hierarchy 条目；
- `DestroyObjectTree` 从 Child Set 快照递归，保持 Serial 顺序和对子节点重入销毁的容忍；
- `DestroyAllObjects` 只扫描一次初始叶节点，随后通过父 Handle 推进叶节点队列；
- `ValidateHierarchyIndex` 双向检查 Live Object、Parent Entry、Child Handle 和关系总数；
- `GetHierarchyIndexStats` 报告 Parent/Relation 数与结构存储估算。

Pico 当前没有运行时 Reparent/SetOuter API，因此本次没有伪造 Outer 变更测试；未来开放该 API 时，必须同时原子
迁移 Name Key 与 Hierarchy 关系。

#### 数据

第 3 周与本后置门均使用 Release Full、5 个连续样本和 100K 规模：

| Case | 第 3 周 P50/P95 | Hierarchy Index P50/P95 | P95 约提升 |
|---|---:|---:|---:|
| 平铺 Destroy | 7100.452 / 7573.463 ms | 33.865 / 107.531 ms | 70.4x |

新增维护成本 Case：

| 100K 层级 Case | P50 | P95 | Max |
|---|---:|---:|---:|
| HierarchyCreate | 128.412 ms | 180.959 ms | 180.959 ms |
| HierarchyDestroy | 48.375 ms | 172.380 ms | 172.380 ms |

一个 Parent、100K Child Relation 的结构存储估算为 `1,848,648 bytes`，约 `18.5 bytes/relation`。这是基于
bucket、容器对象和 Handle 的结构估算，不包含 `std::unordered_*` 节点分配器的隐藏头部，因此不是进程 RSS。

#### 正确性与代价

- Object `185/185`、GC `42/42`，并覆盖递归销毁、拒绝带子节点的父对象销毁、Slot Serial 复用、GC Sweep、
  BeginDestroy 重入、Shutdown 和 Object System 重启；
- Debug/Release 下 22 组测试目标全部通过，包含 Engine、Replication、Movement、Physics、GAS、Graph、Agent、
  Editor、Packaging 与 Sandbox；
- 有 Outer 的对象新增哈希关系维护与内存开销，顶层对象不创建关系；
- Slot Registry 继续是生命周期唯一事实来源，Hierarchy Index 是可校验、可清空的查询加速层。

详细实现见[Object Hierarchy Index 总结](EngineeringDepthWeek04_ObjectHierarchyIndex.zh-CN.md)，原始数据见：

- `Docs/Baselines/EngineeringDepthWeek03/ReleaseBuild/RuntimeBaseline.json`
- `Docs/Baselines/EngineeringDepthWeek04HierarchyIndex/ReleaseBuild/RuntimeBaseline.json`

### 5. Memory Observability Gate

#### 问题

此前只能报告少数结构估算，无法统一比较 Current、Reserved、Peak、Element 和 Growth；CPU 优化增加的缓存成本
也无法进入机器报告。Profiler Full Trace 曾产生超大文件，但内存中的 Events 风险没有量化。

#### 实现

- `PicoCore` 增加固定 Tag、线程安全且可关闭的 `PicoMemoryTracker`；
- Object 采用显式快照，避免 Hierarchy 统计进入 Add/Destroy 热路径；
- GC 记录一次收集中的临时结构峰值并在退出后归零；
- Profiler 只在 Events 容量扩张及帧末发布，避免逐 Scope 监控锁；
- Replication 报告临时 Schema 峰值和 Channel 内部 Buffer；
- Benchmark 升级为 v3，JSON 增加 `memory_categories`，另写 `PicoRuntimeMemory.csv` 并执行七分类自校验。

#### Release Full 数据

单样本用于规模基线，不用于宣称 CPU 提升。100K Object/10K Tick/1K Replicated Actor 合约中：

- Object Slots Peak `1,600,048 B`、Reserved `2,212,080 B`；
- Name Index Peak `2,000,060 B`，结束后仍 Reserved `1,048,616 B`；
- Hierarchy Index Peak `800,152 B`；
- GC Scratch Peak `156,501 B`；
- Profiler Events Peak `120,467,512 B`、Reserved `138,582,664 B`；
- Replication Channels Peak `200,000 B`。

这组数据确认第 7 周 Profiler 有界化是当前最明确的内存任务。所有值都是结构估算，不包含哈希节点头、Allocator
元数据、碎片、Profiler Name 潜在堆存储，也不等于进程 RSS。

详细实现见[Memory Observability Gate](EngineeringDepthWeek05_MemoryObservabilityGate.zh-CN.md)，原始数据见：

- `Docs/Baselines/EngineeringDepthWeek05MemoryGate/ReleaseBuild/RuntimeBaseline.json`
- `Docs/Baselines/EngineeringDepthWeek05MemoryGate/ReleaseBuild/PicoRuntimeMemory.csv`

## 已定位但尚未完成的优化

### 内存与缓存优化排期

当前不建立独立的“内存重写”支线，而是把观测、缓存和容量治理嵌入现有第 5～8 周：

```text
第 5 周前置门：PicoMemoryTracker 最小分类与 Benchmark 字段（已完成）
 -> 第 5 周：Replication Schema/Channel/Dirty/Generation Cache
 -> 第 6 周：GC Reference Layout Cache 与 Scratch 复用
 -> 第 7 周：Profiler AggregateOnly/BoundedTrace 与安全点 Trim
 -> 第 8 周：统一内存报告与 Hierarchy 紧凑布局 A/B 决策
```

`PicoMemoryTracker` 第一版由子系统主动报告，不拦截全局 `new/delete`。统一指标为
`CurrentBytes/ReservedBytes/PeakBytes/ElementCount/GrowthCount`，首批分类覆盖 Object、GC、Profiler 和
Replication。所有缓存优化必须同时报告 CPU 收益、额外内存、命中或跳过次数与正确性代价。

第 6 周默认实施低风险 GC 优化，但增量 GC 仍由 Pause P95 决定；并行 GC 不进入本阶段。第 8 周对 Hierarchy
Index 只要求形成有数据支撑的“保留、延期或采用”结论，不以容器形式为理由强制重写。

自定义通用 Allocator、完整 Binned 分配器、全局 `new/delete` 替换、全面 SoA/ECS 和每帧自动容器收缩均延期。
只有完成第 8 周综合报告后，且分配次数、碎片或 Cache Miss 被证明为剩余主瓶颈，才允许规划局部 Pool/Arena 或
紧凑 Child List 实验。

## 后续优化记录模板

后续新增条目时，至少填写以下内容：

```text
### N. <子系统>/<问题名称>

状态：计划中 / 实施中 / 已完成 / 否决
基线：构建配置、硬件、场景、规模、样本数、Benchmark 报告路径
瓶颈现象：哪个指标如何退化
根因假设：为什么会退化，如何被验证
监控证据：Profiler Scope、Benchmark Case、日志或 Trace
优化方法：数据结构、缓存、调度或边界调整
正确性保护：生命周期、GC、反射、网络、事务和线程约束
结果：P50/P95/Max、吞吐、内存、帧时间或 BytesPerFrame
代价：额外内存、维护耗时、复杂度、编译或调试成本
结论：合入、继续观察、延期或否决
相关文档：实现总结和原始报告
```

## 当前数据解释边界

- 第 1 周完成了测量基础，但没有用一次性数字宣称性能提升；
- 第 2 周建立了 Debug/Release 基线和热点排序；
- 第 3 周 Object Name Index 与第 4 周 Tick Schedule Cache 均已完成相同合约下的前后对比；
- Frame Pacing 已完成版本化节拍与真实 Game 进程复测；`28 -> 59.2 FPS` 仍明确标记为人工 A/B；
- Object Hierarchy Index、Replication Scaling 与 GC 深化已完成同合约前后对比；GC 的 Mark-heavy Case
  收益明显，Destroy-heavy Case 的较小收益与 P95 波动继续保留；
- 后续任何“变快”的结论都必须同时说明是否牺牲了内存、生命周期安全、可维护性或正确性。
