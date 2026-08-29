# 工程深度第 5 周：Replication Scaling 与 Agent 故障注入

## 本周目标

Runtime 侧在不引入 Replication Graph 的前提下，消除每个连接对全部 Actor、Schema 和字段重复执行的工作，
并保留一条可发现漏标 Dirty 的正确性校验路径。Agent 侧把崩溃位置变成可控测试输入，验证副作用已经发生但
Tool Result 尚未持久化时，重启能够 Reconcile，而不是重复执行操作。

## Replication 瓶颈

第 5 周前的复制路径每帧、每连接都会重复：

1. 构建 `PClass -> FReplicationSchema`；
2. 线性查找 `(ConnectionId, NetObjectId)` Channel 和 Object Handle；
3. 捕获并比较 Actor 的全部复制属性；
4. 在 Field Delta 与 Baseline 合并时重复线性查找 FieldId。

因此即使 1000 个 Actor 中只有 10 个发生变化，仍要为 1000 个 Actor 支付大部分比较成本。原始
`BytesPerFrame` 已随 Dirty 比例变化，但 CPU 没有同比例下降，说明瓶颈位于“判断什么需要发送”之前。

## Runtime 实现

### 索引与缓存

- `FReplicationSchema` 按 `PClass*` 缓存，FieldId 使用连续表直接定位；
- Channel 使用 `(ConnectionId, NetObjectId)` 哈希索引；
- NetObject Registry 同时维护 Object Handle 和 NetId 索引；
- Delta 构建使用固定 FieldId 表，Baseline 合并使用有序查找。

索引只改变定位方式，不改变 NetId、Channel 生命周期、ACK 或销毁语义。Channel 删除后会同步维护索引，
Object 注销会同时清理两个 Registry 索引。

### Push Dirty 与多连接正确性

每个复制 Actor 现在持有单调递增的 `ReplicationGeneration`、属性 Dirty Mask 和 Transform Dirty 标记。
每个 Connection Channel 保存自己的 `LastObservedGeneration`：

```text
属性或 Transform 写入
 -> Actor 记录新 Generation 与 Dirty 信息
 -> 每条连接分别合并 LastObservedGeneration 之后的记录
 -> 只捕获、比较、编码对应字段
 -> ACK 后该 Channel 推进自己的 Generation
```

Dirty 状态不能由第一个连接消费后全局清空，否则较慢的第二个连接会漏同步。Pico 使用固定 8 条 Generation
历史；连接落后超过历史窗口时保守退回“全部属性 + Transform Dirty”，以额外 CPU 换取正确性。

引擎内置复制 Setter、`SetActorTransform` 和反射属性编辑会自动标脏。自定义 C++ 代码直接写复制字段时，必须
调用 `MarkReplicatedPropertyDirty(PropertyName)`；批量或无法定位字段时调用 `MarkReplicationDirty()`。

### 漏标校验模式

`EReplicationDirtyMode::PushModel` 是正常运行路径。`FullPollingValidation` 会继续捕获并比较全部字段；若字节变化
但 Actor 未标脏，`DirtyValidationMisses` 会增加。该模式用于测试和开发期排错，不作为发布时默认路径。

### 可观测指标

`FReplicationStatistics` 新增：

- Schema/Channel/NetObject Index Hits 与 Misses；
- Actors Considered、Dirty、Skipped Unchanged；
- Dirty/Compared/Encoded Properties 与 Validation Misses；
- Bytes Queued；
- Gather/Compare/Serialize/Queue 纳秒级分项。

Benchmark JSON/CSV 会保存这些运行指标，因此“更快”可以对应到明确的跳过量和阶段成本。

## Release Full 数据

新基线使用 Release Full、1000 Actor、5 个连续样本。旧 Memory Gate 基线只有 1 个样本，因此旧 P95 不能视为
严格百分位；下表只把旧值作为同规模前置门参照，提升倍数按 P50 计算。

| Dirty Ratio | 旧 P50 | 新 P50/P95 | P50 约提升 | Skipped / Dirty | Bytes/Frame |
|---|---:|---:|---:|---:|---:|
| 1% | 1032 us | 144/173 us | 7.2x | 990 / 10 | 230 B |
| 10% | 1052 us | 179/274 us | 5.9x | 900 / 100 | 2300 B |
| 100% | 1367 us | 846/1278 us | 1.6x | 0 / 1000 | 23000 B |

1% Case 的阶段 P50 为 Gather `44.5 us`、Compare `0.8 us`、Serialize `3.1 us`、Queue `0.3 us`；
100% Case 分别为 `201.5/70.8/296.1/26.4 us`。这说明稀疏更新已主要受 Actor Gather 固定成本限制，
全量更新则仍以序列化为主要成本。

结构内存存在明确代价：`Replication.Schema` Peak 为 `72 B`，`Replication.Channels` Peak 为 `272000 B`；
前置门分别为 `24 B` 和 `200000 B`。这是持久 Schema、Channel/Registry 索引换取 CPU 的结构估算，不含标准库
节点分配器开销。`Profiler.Events` 在 5 样本 Full 运行中增长到约 602 MB，属于第 7 周 Bounded Trace 的已知风险，
不归因于 Replication Channel。

原始报告：

- `Docs/Baselines/EngineeringDepthWeek05ReplicationScaling/ReleaseBuild/RuntimeBaseline.json`
- `Docs/Baselines/EngineeringDepthWeek05ReplicationScaling/ReleaseBuild/PicoRuntimeBenchmarks.csv`
- `Docs/Baselines/EngineeringDepthWeek05ReplicationScaling/ReleaseBuild/PicoRuntimeMemory.csv`

## Agent 故障注入

`FAgentRuntimeContext` 支持一次性确定性故障点：Provider Timeout、Provider Invalid JSON、Crash Before Execute、
Crash After Side Effect、Crash Before Persist 和 Session Append Failure。已有 Approval Rejection、Verification
Rollback 与 Duplicate ToolCall 测试继续覆盖其余计划场景。

关键恢复测试使用真实 `FAgentOperationJournal`：

```text
第一次运行：副作用完成 -> 注入崩溃 -> Tool Result 未落盘
重启运行：相同稳定 CallId -> Journal::FindApplied -> Reconcile
最终：副作用次数仍为 1，Journal 没有未完成记录
```

Provider Timeout 与 Invalid JSON 会各消耗一次修复预算后恢复，避免一次 Step 同时吞掉两个故障点。不可恢复错误
仍保留 Session、Journal 和 Trace 现场。

## 正确性与验收

- 无变化 Actor 在 PushModel 下不再捕获和比较属性；
- 同一 Dirty Generation 可被两个不同进度的连接分别观察；
- Dirty 后值回到 Baseline 时不发送无意义 Delta，但 Channel Generation 正确推进；
- FullPollingValidation 能发现绕过 Setter 的直接写入；
- Agent 在三个持久化边界故障后重启，副作用均只执行一次；
- Debug/Release Replication Tests `32/32`、Agent Tests `114/114` 通过；Debug/Release 全量 CTest 均为 `24/24`。

## 后续输入

1. 第 6 周 GC 可复用 Generation、缓存命中统计与“优化路径 + 校验路径”做法；
2. 第 7 周应优先处理 `Profiler.Events` 的 AggregateOnly/BoundedTrace，避免长期运行吞掉数百 MB；
3. 若未来实现 Replication Graph，当前 Dirty Generation 仍作为单 Actor 增量编码层，不需要推翻；
4. 新增复制属性时，测试应至少覆盖 PushModel 正常标脏与 FullPollingValidation 漏标检测。
