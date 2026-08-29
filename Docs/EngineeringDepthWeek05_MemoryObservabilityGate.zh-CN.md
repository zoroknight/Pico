# 工程深度第 5 周前置门：Memory Observability Gate

## 目标

本门先让 Pico 能按子系统回答“当前使用、保留容量、历史峰值和容量增长次数”，再进入 Replication Scaling。
它不拦截全局 `new/delete`，不改变对象所有权，也不提前实现 Binned Allocator、GC Scratch 复用或 Replication Cache。

```text
子系统主动发布结构快照
 -> PicoMemoryTracker 按固定 Tag 聚合
 -> Runtime Benchmark v3 输出 JSON/CSV
 -> 后续优化使用同一口径复测
```

## PicoMemoryTracker

`PicoCore` 新增线程安全的 `FMemoryTracker`。关闭时 `Report` 立即返回；开启后每个固定分类保存：

- `CurrentBytes`：最近一次发布时仍在使用的结构字节；
- `ReservedBytes`：最近一次发布时容器已经保留的结构容量；
- `PeakBytes`：Reset 后观察到的最大 Current；
- `ElementCount`：该分类最近一次发布的逻辑元素数；
- `GrowthCount`：Reserved 相对上一次发布增长的次数。

第一批固定分类为：

```text
Object.Slots
Object.NameIndex
Object.HierarchyIndex
GC.Scratch
Profiler.Events
Replication.Schema
Replication.Channels
```

固定枚举避免 Tracker 自己为了 Tag 查询创建字符串哈希节点。不同分类由唯一子系统发布，当前不支持多个 Provider
向同一 Tag 并发累加。

## 接入边界

### Object

`FObjectRegistry::PublishMemoryStatistics` 显式采样 Slot、Name Index 和 Hierarchy Index。Slot 使用 `size/capacity`；
哈希索引报告可见 Payload 和 Bucket 指针估算。Hierarchy 采样需要遍历 Parent Entry，因此只在 Benchmark 或未来
开发面板采样时调用，不进入每次 Add/Destroy 热路径。

### GC

一次 GC 内统计 Mark Bit、Work Stack、Unreachable List 和单对象 Reference Collector 的观察峰值；收集结束后
Current/Reserved 归零，但 Peak/Growth 保留。当前 Scratch 仍是局部容器，因此多轮 GC 会重复出现增长；第 6 周
再将其改为可复用 Context。

### Profiler

Profiler 只在 Events `vector` 容量真正变化时发布，并在 `EndFrame` 补最终 size，避免每个 Scope 都获取第二把锁。
第一版只计算 `FProfileEvent` 结构数组，不猜测 `std::string` 的 SSO 或堆分配成本。

### Replication

当前 Schema 每次构建后销毁，因此记录一次 Replication 调用中的最大临时 Fields 容量并在退出时归零。Channel
由显式快照统计 Channel、Baseline、Pending Value、Transform Buffer 和内部 Value Data。Schema Cache、Channel
Index、Dirty Mask 与 Generation 仍属于第 5 周主任务。

## Benchmark v3

`PicoRuntimeBenchmarks` 开启 Tracker，并在原有报告中新增顶层 `memory_categories`。同时输出独立
`PicoRuntimeMemory.csv`。Smoke/Full 运行会校验：

- 七个分类完整且顺序稳定；
- Tag 名称已注册；
- `CurrentBytes <= ReservedBytes`；
- 每个固定 Case 都实际观察到 Peak 和 Growth。

## Release Full 基线

配置：Release、Full、单样本、100K Object/10K Tick/1K Replicated Actor、关闭 Trace 文件写出。单样本只用于
建立内存规模和验证分类完整性，不用于宣称 CPU 性能提升。

| Category | Final Current | Final Reserved | Peak | Elements | Growth |
|---|---:|---:|---:|---:|---:|
| Object.Slots | 1,600,048 B | 2,212,080 B | 1,600,048 B | 100,003 | 3 |
| Object.NameIndex | 40 B | 1,048,616 B | 2,000,060 B | 2 | 6 |
| Object.HierarchyIndex | 80 B | 131,216 B | 800,152 B | 1 | 7 |
| GC.Scratch | 0 B | 0 B | 156,501 B | 0 | 24 |
| Profiler.Events | 120,467,512 B | 138,582,664 B | 120,467,512 B | 1,368,949 | 36 |
| Replication.Schema | 0 B | 0 B | 24 B | 0 | 8 |
| Replication.Channels | 0 B | 136,448 B | 200,000 B | 0 | 8 |

最明确的新证据是 Full Profiler 即使使用 `--no-trace` 不写 Chrome Trace，仍会在内存保留约 120.47 MB 事件、
约 138.58 MB 容量。第 7 周的 `AggregateOnly/BoundedTrace` 因此是已测量风险，不再只是理论优化。Name Index 和
Channel 在元素清空后仍保留 Bucket/Vector 容量，也为第 7 周安全点 Trim 提供了观测基础。

## 数据解释边界

- 这些是容器结构估算，不是进程 RSS、Commit Size 或 Allocator 实际占用；
- `unordered_*` 节点头、Allocator 元数据和碎片没有计入；
- Profiler Event Name 的潜在字符串堆内存没有计入；
- Peak 是观察到的 Current 峰值，不等于 Reserved 历史峰值；
- Growth 表示发布序列中的容量增长，不是全局 `malloc` 调用次数；
- Tracker 自身固定数组和互斥量未反向计入分类。

## 验证

- Debug/Release Core：`113/113`；
- Debug/Release Object：`185/185`；
- Debug/Release GC：`42/42`；
- Debug/Release Replication：`28/28`；
- Debug Quick 与 Release Full Benchmark v3 均通过七分类自校验；
- Release 原始报告位于 `Docs/Baselines/EngineeringDepthWeek05MemoryGate/ReleaseBuild`。

## 下一步

进入第 5 周主任务：Replication Schema Cache、Channel/NetObject Index、Field Delta 查找消除、Dirty Mask 与
Generation。后续报告必须同时解释 CPU、内存、缓存跳过次数和 `BytesPerFrame`。
