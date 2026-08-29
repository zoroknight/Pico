# 工程深度第 6 周：GC 深化与 Agent 对抗评测

## 本周目标

Runtime 侧只做低风险 GC 优化：缓存反射强引用布局、复用临时容器，并把 Pause 拆成可解释阶段。Agent 侧不增加
新工具，而是用对抗任务检验已有 Harness 的权限、幂等、预算和事实边界，同时建立无需 Embedding 的确定性 RAG
Benchmark。

## GC 原瓶颈

旧 Mark/Sweep 每轮都会：

1. 重新创建 `Marked`、`WorkStack`、`Unreachable` 和 `FReferenceCollector`；
2. 对每个可达对象遍历完整 `PClass` 继承链；
3. 对每个反射属性重复判断它是否为 Strong Object Reference；
4. 只记录 `GC.Mark` 与 `GC.Sweep`，无法区分 Root Scan、排序和销毁成本。

这意味着引用密度越高、存活对象越多，重复元数据筛选越明显；反之大量不可达对象的场景主要由 BeginDestroy、
索引移除和对象析构决定，反射缓存不会带来同等级收益。

## GC 实现

### PClass 强引用布局缓存

`PClass::FinalizeMetadata` 现在按基类到派生类顺序缓存所有 Strong Object Reference 的 `PProperty*`。属性存储于
稳定的 `deque`，且 Metadata Finalize 后禁止再添加属性，因此缓存指针不会因容器增长失效。

GC Mark 对每个对象直接遍历 `GetStrongReferenceProperties()`，不再重复扫描完整属性集合。Native
`AddReferencedObjects` 仍然先执行，反射引用、Native 引用、Outer 方向和 Weak 引用语义没有改变。

### Scratch 复用

Object Registry 在 Game Thread 上复用：

- `vector<bool> Marked`；
- `vector<FObjectHandle> WorkStack`；
- `vector<FUnreachableObject> Unreachable`；
- `FReferenceCollector` 内部 Handle Buffer。

每轮开始清空内容并保留 capacity；每轮结束再次清空 Handle 和 Mark 状态，因此不会把旧对象现场保留到下一轮，
但存储容量可以复用。`ScratchGrowthCount` 只在容量真实增长时增加。

### 分阶段观测

`FGarbageCollectionResult` 与 Benchmark 新增：

```text
RootScanNanoseconds
MarkNanoseconds
UnreachableSortNanoseconds
DestroyNanoseconds
StrongReferenceLayoutCount / PropertyVisitCount
ScratchPeakBytes / ReservedBytes / GrowthCount
```

Profiler Scope 对应 `GC.RootScan`、`GC.Mark`、`GC.UnreachableSort` 和 `GC.Destroy`。Memory Tracker 先发布本轮
Peak，再发布 `Current=0 + retained Reserved`，避免把保留容量误报为泄漏，也不会丢失峰值。

## Release Full 数据

第 5、6 周均为 Release Full、5 个连续样本：

| Scale / 存活 / 密度 | 旧 P50/P95 | 新 P50/P95 | P50 变化 | 新主要阶段 P50 |
|---|---:|---:|---:|---|
| 1K / 10% / 0 | 278/311 us | 271/307 us | 1.03x | Destroy 66.2 us |
| 1K / 50% / 2 | 281/293 us | 234/259 us | 1.20x | Destroy 60.8 us |
| 1K / 90% / 8 | 496/1147 us | 214/228 us | 2.32x | Mark 37.3 us |
| 10K / 10% / 0 | 1285/1486 us | 1030/1507 us | 1.25x | Destroy 788.3 us |
| 10K / 50% / 2 | 1603/1766 us | 1265/1680 us | 1.27x | Destroy 945.6 us |
| 10K / 90% / 8 | 2432/2875 us | 812/1168 us | 3.00x | Mark 346.9 us |

结论不是“GC 全面提速”：缓存明确改善高存活、高引用密度的 Mark-heavy 场景；低存活场景仍由 Destroy 主导，
本轮没有优化对象析构、Name/Hierarchy Index 移除，因此 Destroy-heavy 场景收益较小，10K/10% Case 的 P95
还有约 1.4% 波动。后续若要继续优化，必须先证明 Destroy P95 是真实帧预算问题，不能用增量 GC 或并行 GC
掩盖慢销毁路径。

`GC.Scratch` 本轮 Peak 为 `156501 B`、Reserved 为 `352440 B`、GrowthCount 为 `6`。旧报告在每轮结束把
Reserved 清零，无法进行同口径保留容量对比；新语义明确表示当前元素为 0、容量保留供后续 GC 复用。

原始报告：

- `Docs/Baselines/EngineeringDepthWeek06GcAndAdversarialEval/ReleaseBuild/RuntimeBaseline.json`
- `Docs/Baselines/EngineeringDepthWeek06GcAndAdversarialEval/ReleaseBuild/PicoRuntimeBenchmarks.csv`
- `Docs/Baselines/EngineeringDepthWeek06GcAndAdversarialEval/ReleaseBuild/PicoRuntimeMemory.csv`

## Agent 对抗评测

Golden Tasks 从 12 个扩展到 21 个，新增九类固定攻击或失败场景：

1. Knowledge Prompt Injection 只作为不可信数据；
2. 未知 Tool 失败关闭；
3. Skill Forbidden Tool 不产生打包副作用；
4. 审批后的 CallId 不能更换参数；
5. 旧 CallId 重放只执行一次；
6. 重复只读查询由 No Progress Budget 停止；
7. 修改验证失败后不能用最终文本虚假完成；
8. Play 请求不能混入 Package；
9. Project Skill 不能越权写 Engine Scope。

所有任务通过真实 `FAgentRuntime`、Session JSONL、预算、CallId 匹配与 Tool Result 失败语义执行。Release 报告为
`21 passed / 0 failed`，失败型任务的“通过”表示 Harness 按预期拒绝并验证零副作用，不表示工具执行成功。

## 确定性 RAG Benchmark

新增版本化 Fixture 和通用 `FAgentRagBenchmarkRunner`，输出：

- Recall@1/3/8；
- Mean Reciprocal Rank；
- ContextBytes；
- RetrievalLatency；
- ForbiddenSourceRate；
- 每个 Case 的排序结果和命中记录。

首批 6 条知识、5 个 Query 的 Release 结果为 Recall@1/3/8 `1.0/1.0/1.0`、MRR `1.0`、平均上下文
`431 B`、平均检索约 `7.20 us`、ForbiddenSourceRate `0.0`。Fixture 中包含 Credential Prompt Injection，查询通过
Source Policy 排除它。

该小规模结果只能证明确定性检索在当前受控 Schema/文档上足够，不能代表大型自然语言语料。当前没有证据支持
引入 Embedding、向量数据库或 LangChain，因此继续延期；未来真实 Eval 的 Recall@3 或 MRR 明显下降时再评估。

Agent 原始报告：

- `Docs/Baselines/EngineeringDepthWeek06GcAndAdversarialEval/ReleaseBuild/GoldenTaskReport.json`
- `Docs/Baselines/EngineeringDepthWeek06GcAndAdversarialEval/ReleaseBuild/RagBenchmarkReport.json`

## 验收

- GC 生命周期、强弱引用、Outer、CDO、重入保护和索引一致性测试 `47/47`；
- 连续等价 GC 的 Scratch Reserved 与 GrowthCount 保持不变；
- Agent 测试 `117/117`，21 个 Golden Tasks 与 5 个 RAG Case 全部通过；
- Debug/Release 全量 CTest 均为 `24/24`；
- 未引入增量 GC、并行 GC、GC Cluster、写屏障或 Embedding。

## 后续输入

1. 第 7 周优先解决 `Profiler.Events` 无界增长，不把 GC Scratch 的合理保留容量误当泄漏；
2. 安全点 Trim 只能在 World Transition/Engine Exit 等生命周期边界执行；
3. 若后续优化 Destroy，先细分 BeginDestroy、Name Index、Hierarchy Index 与析构阶段；
4. 新增 Knowledge Source 或 Skill 时必须追加 RAG Case 或对抗 Golden Task。
