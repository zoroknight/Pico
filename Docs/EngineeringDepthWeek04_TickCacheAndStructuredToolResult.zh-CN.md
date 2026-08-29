# 工程深度第 4 周：Tick Cache 与结构化 Tool Result

## 本周目标

Runtime 侧不改变 Game Thread 串行 Tick 语义，只消除静态 Tick 图每帧重复收集节点、查找 RegistrationId 和
拓扑排序的成本。Agent 侧保留旧工具 Handler 的 `OutputJson` 兼容入口，但从 Registry 开始统一转换成结构化
Tool Result，使 Verifier、Session、Replay、UI 和模型历史不再分别猜测工具是否成功。

## Tick Schedule Cache

### 原瓶颈

旧实现每个 TickGroup、每一帧都会：

1. 扫描全部注册 TickFunction；
2. 为每个节点线性查找 RegistrationId；
3. 重建 InDegree 和 Dependents；
4. 重新执行拓扑排序。

即使注册关系和依赖图完全没有变化，也会重复完成相同工作。10K TickFunction 的第 2 周 Release 基线中，
StaticSchedule P95 为 `24.593 ms`，DependencyChange P95 为 `39.752 ms`。

### 实现

`FTickTaskManager` 现在持有：

```text
RegistrationId -> FRegisteredTick
TickGroup -> {RequiredGeneration, BuiltGeneration, OrderedIds, BuildCount}
```

以下结构变化只标脏受影响的 TickGroup：

```text
Register / Unregister / SetTickGroup / Add-Remove-Clear Prerequisite
 -> ScheduleGeneration++
 -> affected TickGroup dirty
 -> 下一次运行该 Group 时 BuildSchedule
 -> 普通帧直接执行 Cached OrderedIds
```

缓存只保存稳定的拓扑顺序。执行阶段仍逐项检查 RegistrationLimit、注册状态、Enabled、Owner Handle 和
TickInterval，因此禁用 Tick、对象销毁和一帧内注册边界没有被缓存绕过。`RegistrationId` 查询改为哈希索引，
避免拓扑构建中的重复线性查找。

依赖环不再只有一条日志：`FCycleDiagnostic` 保存 TickGroup、参与环的 RegistrationId 和诊断消息。Profiler 将
一次性构建记录为 `Tick.Schedule.Build`，普通执行继续记录为 `Tick.Execute`。

### Release Full 数据

第 2、4 周均使用 Release Full、5 个连续样本：

| 10K Case | 第 2 周 P95 | 第 4 周 P95 | 约提升 |
|---|---:|---:|---:|
| 首次静态调度 | 24.593 ms | 4.800 ms | 5.1x |
| 依赖链变化 | 39.752 ms | 4.674 ms | 8.5x |

第 4 周新增 `CachedStaticFrames` Case：10K TickFunction 连续运行 60 帧，P50 为 `20.135 ms`、P95 为
`20.647 ms`，参数明确记录 `schedule_rebuilds=0`。折算后的 P95 平均约为 `0.344 ms/帧`。它说明静态帧已经
不再支付拓扑构建成本；该折算值不是逐帧 P95 分布，不能冒充独立的单帧百分位。

原始报告位于：

- `Docs/Baselines/EngineeringDepthWeek02/ReleaseBuild/RuntimeBaseline.json`
- `Docs/Baselines/EngineeringDepthWeek04/ReleaseBuild/RuntimeBaseline.json`

## 结构化 Tool Result

### 统一契约

`FAgentToolResult` 新增：

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

旧 Handler 仍可返回 `CallId + bSucceeded + OutputJson + Error`。`NormalizeAgentToolResult` 会在 Registry 内将
旧结果转换为统一结构，失败结果自动生成 Failure Diagnostic 和确定性 RecoveryHint。Provider 声明的
Revision Write Set 会进入 Result，Runtime 在真实 StateRevision 前后填充版本值。

### 单一事实流

- Verifier 接收归一化后的 Result；
- Session JSONL 同时保存兼容 Payload 和 `structured_result`；
- `FindToolResult`、CallId 重用和语义缓存恢复同一结构；
- Operation Journal 保存结构化 Result，旧 v1 记录仍可读取；
- AI Chat 直接渲染 Session 中的结构化 Result；
- 模型历史通过 `BuildAgentToolResultModelJson` 读取相同事实，不解析 UI 文本或 Assistant 声明。

当 Facts 超过 `64 KiB` 时，Session 将完整 JSON 写入同目录 `Artifacts`，结构化结果和模型历史只保留稳定 Handle、
摘要、类型和字节数。模型侧不接收本机相对路径，避免把大结果和本地布局重新塞回上下文。

## 验收结果

- Debug/Release Engine Tests：`695/695`；
- Debug/Release Agent Tests：`109/109`；
- Debug/Release Editor Tests：`142/142`；
- Debug Quick 与 Release Full Benchmark 成功输出；
- 静态 Tick 图连续 60 帧的 `schedule_rebuilds=0`；
- prerequisite 变化只重建目标 Group，依赖环产生结构化诊断；
- 大 Tool Result 可外置并在 Session 重启后按 Artifact Handle 恢复摘要；
- 现有 12 个 Golden Tasks 与 42 条中英文 Intent/Skill 回归保持通过。

## 后续输入

1. 按路线进入 Object Hierarchy Index 后置门，处理 100K Destroy 的 Outer 子对象扫描；
2. 第 5 周 Replication Scaling 应沿用 Generation、Dirty 与可复现基准方法；
3. 第 7 周资源域 Revision 可直接扩展现有 `RevisionChanges`，不需要再次修改 Tool Result 外层协议；
4. Artifact 当前解决持久化和上下文体积，后续按实际需求增加受权限控制的 Artifact 读取工具。
