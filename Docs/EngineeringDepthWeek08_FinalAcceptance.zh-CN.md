# 工程深度第 8 周：综合验收与工程决策

## 本周目标

本周不新增玩法系统，而是把前七周的 Runtime Scalability 与 Agent Reliability 工作收束成可重复执行的工程证据。
统一入口为：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File Scripts/RunEngineeringDepthWeek08.ps1 `
  -Configuration Release -BuildDirectory BuildCodex -Samples 5
```

Runner 会构建项目、运行完整 CTest、执行固定 Runtime Matrix、归档 Fake Provider 评测、启动真实 PicoEditor 两帧、
打包 PicoSandbox 并启动暂存 Runtime 烟测，最后生成 JSON/Markdown 汇总。Core 的进程管理测试依赖 Windows Child
Process 与 Job Object，因此完整验收不能在禁止创建子进程的沙箱中运行。

## 自动验收结果

- Release CTest：`24/24` 通过；
- Fake Provider Golden Tasks：`21/21` 后置条件通过，41 Turns、39 Tool Calls、1 次 Repair；
- 故障注入：4 个可恢复场景全部恢复，恢复率 `100%`；
- Forbidden Side Effect：`0`；通用 `Source/Developer/Agent` 中 PicoSandbox 硬编码：`0`；
- 真实 PicoEditor 初始化烟测：通过；
- PicoSandbox Development Stage：66 个文件，Packager 与暂存 Runtime smoke 通过；
- 真实 Provider：未自动执行。它需要本地 API Key、网络和明确费用授权，不能用 Fake 结果冒充；
- 交互式 Play、镜头和手感：仍属于人工可视化验收，不用两帧 smoke 冒充。

## Runtime 固定矩阵

配置为 Release、5 样本、AggregateOnly Profiler。时间单位为微秒。

| Case | P50 | P95 | 关键正确性/容量 |
|---|---:|---:|---|
| 100K Object Create | 112205 | 118373 | 全部进入统一 Registry |
| 100K Object Find | 51198 | 57191 | found=100000 |
| 100K Object Destroy | 35819 | 48244 | Name/Hierarchy Index 保持一致 |
| 100K Object GC，50% 存活 | 45826 | 53753 | collected=50000 |
| 10K Tick StaticSchedule | 2875 | 3401 | 静态缓存帧 rebuild=0 |
| 10K Tick DependencyChange | 3559 | 4793 | 动态依赖使调度失效并重建 |
| 1K Actor，2 Client，1% Dirty | 382 | 410 | 20 Dirty/Encoded，460 B/frame |
| 1K Actor，2 Client，10% Dirty | 403 | 514 | 200 Dirty/Encoded，4600 B/frame |
| 1K Actor，2 Client，100% Dirty | 1695 | 1949 | 2000 Dirty/Encoded，46000 B/frame |

与第 2 周 Release 基线相比，100K Find P50 从 `10.451 s` 降到 `51.198 ms`，约 `204x`；100K Destroy P50
从 `8.750 s` 降到 `35.819 ms`，约 `244x`；10K Tick StaticSchedule 从 `24.056 ms` 降到 `2.875 ms`，约
`8.37x`。Replication 第 8 周改为两个 Connection，不能直接把 `382 us` 与旧单客户端 `145 us` 当成回归；
它同时处理两套 Channel，且 BytesPerFrame 正好为单客户端的两倍。

Memory Tracker 在基准结束时仍报告 Object Slot、Name/Hierarchy Index、GC Scratch、Profiler 与 Replication
分类。AggregateOnly 下 `Profiler.Events` 为 0，说明长会话逐事件内存不再线性增长。

## Hierarchy A/B 决策

原型比较当前 `unordered_map<Parent, unordered_set<Child>>` 与“每 Parent 连续 Child 数组 + Child Slot 位置索引”。
位置索引是为了让任意 Child 销毁仍可 swap-remove，避免用不真实的 `pop_back` 美化结果。

| 100K 关系分布 | Hashed/Compact Reserved | Build P95 | Traverse P95 | Remove P95 |
|---|---:|---:|---:|---:|
| Wide：1 x 100K | 1.45/1.20 MB | 3633/193 | 411/54 | 1461/1448 |
| Typical：10K x 10 | 2.49/1.61 MB | 5860/1219 | 870/96 | 3381/1717 |
| Deep：100K x 1 | 14.65/5.05 MB | 18792/7349 | 1403/570 | 20936/5447 |

结论：**延期采用，保留原型与门槛**。Compact 在多 Parent 分布中很有潜力，但当前真实 100K 宽层级端到端
HierarchyDestroy P95 为 `51.809 ms`，其中 Hashed/Compact 原型 Remove P95 只差 `13 us`；立即修改 Object Slot
ABI、序列化诊断和索引校验，收益不足以覆盖风险。出现大量“小 Parent”、Hierarchy Reserved 成为主要内存类别，
或 Child 遍历进入帧热点时，再把该原型升级为 Runtime 实现。

## Agent 结论

Fake Provider 已覆盖普通任务、对抗输入、Forbidden Tool、冲突、预算耗尽、错误后置条件、长上下文、故障注入与
崩溃恢复；FailureDistribution 为 None 15、InvalidArguments 1、PermissionDenied 2、Conflict 1、
BudgetExceeded 1、VerificationFailed 1。失败任务也必须满足“无越权副作用”的验证条件，因此 Golden 的
`100%` 是工程后置条件通过率，不代表所有 Runtime 状态都应为 Completed。

通用 Agent 层通过 Revision Domain、有界上下文、结构化 Tool Result、Operation Journal、Recovery Policy 与
Capability Provider 保持解耦。真实 Provider 的质量、Token 与公网延迟必须单独运行并单独报告，不能与 Fake 的
确定性微秒延迟合并。

## 采用、延期与拒绝

采用：Object Name/Hierarchy Index、Tick Schedule Cache、Replication Dirty/Schema/Channel Cache、GC Layout/Scratch
复用、Profiler 有界存储、统一 Metrics、Agent Revision Context 与恢复链、统一验收 Runner。

延期：Compact Child List Runtime 替换、增量/并行 GC、Task Graph、ECS、Allocator/Pool、MCP、多 Agent 与 Code
Harness。它们需要新的真实热点，而不是旧路线上的名称。

拒绝：为了单一 Demo 增加专用 Tool、把 API 调用成功当成任务成功、把 Fake Provider 或两帧 Smoke 当成交互质量。

原始证据位于 `Docs/Baselines/EngineeringDepthWeek08FinalAcceptance/ReleaseBuild/`。
