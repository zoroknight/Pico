# 工程深度第 2 周：Runtime 基线与 Agent 失败语义

## 本周结论

第 2 周没有直接优化热点，而是先建立可复现的性能基线和确定性的 Agent 失败恢复规则。结果证明第 3 周应优先处理 Object Index，而不是提前改 GC、Replication 或引入并行任务。

基线原始数据位于：

- `Docs/Baselines/EngineeringDepthWeek02/DebugBuild/RuntimeBaseline.json`
- `Docs/Baselines/EngineeringDepthWeek02/ReleaseBuild/RuntimeBaseline.json`

Debug 使用 `quick`、7 个连续样本；Release 使用 `full`、5 个连续样本。P50/P95 使用 nearest-rank 算法，因此 5 个样本下 P95 等于最大值。样本在同一 Engine 进程中连续执行，既包含热运行影响，也会暴露长期创建/销毁后注册表膨胀的问题。

## Runtime 基线

### 端到端工作负载前三名

Release Full 的最高 P95 均来自 100K 对象规模：

| 排名 | Case | P50 | P95/Max |
|---|---:|---:|---:|
| 1 | Object Rename | 24.356 s | 28.335 s |
| 2 | Object Create | 25.080 s | 27.504 s |
| 3 | Object Find | 10.451 s | 11.957 s |

`Object Destroy` 的 P95 也达到 9.056 s。连续样本越往后越慢，说明 Handle Slot 的稳定性虽然保住了，但名称/Outer 查找仍受历史 Slot 数量影响。这不是 GC Pause 主导的问题，而是对象注册与查询索引缺失导致的增长曲线问题。

### Profiler Scope 前三名

按 Release Full 的累计时间：

| 排名 | Scope | Count | Total | Max |
|---|---:|---:|---:|---:|
| 1 | `Tick.Schedule` | 120 | 187.344 ms | 26.575 ms |
| 2 | `Tick.Execute` | 120 | 108.934 ms | 11.914 ms |
| 3 | `GC.Sweep` | 60 | 18.667 ms | 1.123 ms |

`GC.Mark` 累计 18.275 ms、单次最大 2.366 ms，与 `GC.Sweep` 接近。Object 操作目前由 Benchmark 外层计时，还没有细分成内部 Profiler Scope，所以必须同时阅读 Scope 排名和工作负载排名。

### Tick、GC 与 Replication

- 10K Tick：静态调度 P95 24.593 ms；依赖链变化 P95 39.752 ms。依赖图重建是 Object Index 之后的第二优先级。
- 10K GC：当前最重组合 P95 2.561 ms。现阶段没有证据支持立刻实现增量或并行 GC。
- 1K Replicated Actor：P95 最大 1.707 ms；1%/10%/100% Dirty 分别产生约 230/2300/23000 BytesPerFrame。当前 CPU 风险低于对象系统和 Tick 调度。

### 核心类型尺寸

Release 中：`PObject=112 B`、`PActor=272 B`、`PActorComponent=192 B`、`PClass=160 B`、`PProperty=296 B`、`FTickFunction=72 B`、`FNetConnection=560 B`。Debug STL/检查状态使部分尺寸更大，因此尺寸基线必须按构建配置分别比较。

## Agent Failure Taxonomy

`EAgentStatus` 继续只表示 Planning、AwaitingApproval、ExecutingTool、Repairing 等运行阶段。失败原因由正交的 `EAgentFailureClass` 表示：

| Failure Class | 默认恢复动作 | 自动重试 |
|---|---|---|
| None | Abort | 否 |
| ModelProtocol | Replan | 是 |
| InvalidArguments | Replan | 否 |
| PermissionDenied | AskUser | 否 |
| ApprovalRejected | WaitForApproval | 否 |
| PreconditionFailed | RefreshState | 是 |
| ExecutionFailed | Retry | 是 |
| VerificationFailed | Rollback | 否 |
| Infrastructure | Retry | 是 |
| BudgetExceeded | Abort | 否 |
| Conflict | RefreshState | 是 |
| Cancelled | Abort | 否 |

分类从 ToolRegistry 的 Validate/Permission/Approval/Preflight/Execute/Verify/Transaction 阶段进入 Tool Result，再持久化到 Session Event、Run Result、Metrics 和 Golden Task Report。Runtime 只对策略中明确标记为 retryable 的失败消耗 Repair Budget。

这意味着参数错误、权限拒绝和用户拒绝审批不会再让模型原地重复同一 Tool Call；验证失败也不会在状态未知时盲目重试，而是明确要求 Rollback。

## 验收命令

```powershell
cmake --build BuildCodex --config Debug --target PicoAgentTests PicoRuntimeBenchmarks -j 8
.\BuildCodex\Debug\PicoAgentTests.exe
.\BuildCodex\Debug\PicoRuntimeBenchmarks.exe --quick --samples=7 --output=<output>

cmake --build BuildCodex --config Release --target PicoAgentTests PicoRuntimeBenchmarks -j 8
.\BuildCodex\Release\PicoAgentTests.exe
.\BuildCodex\Release\PicoRuntimeBenchmarks.exe --full --samples=5 --output=<output>
```

本次 Debug/Release Agent Tests 均为 104/104。Debug Quick 和 Release Full 基线均成功输出。附加 Core CTest 在本机等待超过 5 分钟且无 CPU 活动后人工终止；本周没有修改 Core 行为代码，后续需要单独排查该测试等待点，不能把这次中止记作通过。

## 第 3 周输入

1. 增加 `(OuterHandle, FName) -> ObjectHandle` 索引，并验证 Slot churn 后查询复杂度不再随历史对象总数增长。
2. 给 Create/Find/Rename/Destroy 增加内部 Scope，区分名称冲突检查、Slot 分配、索引维护和析构成本。
3. 优化前后必须复跑相同 Release Full 合约；目标先看增长曲线，再看绝对耗时。
4. Object Index 稳定后再分析 Tick dependency graph；GC 和 Replication 暂不进行架构重写。
