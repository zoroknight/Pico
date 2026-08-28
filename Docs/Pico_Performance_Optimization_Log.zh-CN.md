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
| Object Registry | 100K Destroy 仍然很慢 | `HasChildObjects` 为维护 Outer 生命周期扫描 Live Slot，逐个销毁形成近似二次增长 | Destroy Benchmark、代码路径分析 | 高 | 已排入第 4 周后置门 |
| Tick Scheduler | 依赖顺序和 Group 可能重复重建 | 每帧缺少 Generation 驱动的稳定 Schedule Cache | `Tick.Schedule` / `Tick.Execute` Scope、Tick Benchmark | 高 | 第 4 周 |
| Replication | Actor、Schema 和字段存在重复遍历或编码 | 每连接查找、Schema 构建、Dirty 状态缺少足够缓存 | Replication Benchmark、BytesPerFrame、分项 Scope | 中高 | 第 5 周 |
| GC | 标记和清扫存在临时工作量 | 反射引用布局、Mark Buffer、Work Stack 尚未复用 | `GC.Mark` / `GC.Sweep` Scope、GC Benchmark | 中 | 第 6 周按基线决定 |
| Task System | 是否值得并行仍无证据 | 工作负载和 Game Thread 边界尚未证明存在稳定并行收益 | 后续任务基准与帧预算 | 低 | 暂不预设实现 |

## 已完成优化记录

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

## 已定位但尚未完成的优化

### Object Hierarchy Index

#### 为什么是瓶颈

当逐个销毁 100K 对象时，`HasChildObjects` 为确认 Outer 子对象是否仍然存在而反复扫描 Live Slot，导致工作量
随对象数量快速增长。Name Index 只能解决 `(Outer, Name)` 查找，无法直接回答“这个 Outer 是否还有子对象”。

#### 计划方法

在第 4 周 Tick Cache 完成后、进入第 5 周前，评估并实现：

```text
OuterHandle -> ChildHandle Set
```

Add、Destroy、GC Sweep、Outer 变更和 Registry Reset 必须同步维护该索引，并提供父子双向一致性检查。实现前后
都要测量索引内存、Add/Destroy 维护成本和层级销毁收益；如果收益不足以覆盖复杂度，应保留数据并否决实现。

#### 验收数据

- `HasChildObjects` 不再扫描完整 Live Slot；
- 单对象销毁、递归树销毁、GC Sweep、Slot 复用和 Outer 变更测试通过；
- 使用与第 3 周相同的 Release Full 合约复测 100K Destroy；
- 报告 Destroy P50/P95/Max、索引内存和维护耗时，并与 `7.573 s` P95 基线比较。

计划位置见[工程深度路线图](Pico_Engineering_Depth_Roadmap.zh-CN.md)。

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
- 第 3 周是目前唯一完成并有明确前后对比数据的 Runtime 优化；
- Tick Cache、Replication Scaling 和 GC 深化完成前，不能提前声称它们已经带来收益；
- 后续任何“变快”的结论都必须同时说明是否牺牲了内存、生命周期安全、可维护性或正确性。
