# 工程深度第 1 周：Profiler、Runtime Benchmark 与 Agent Metrics

## 本周目标

第 1 周只回答“系统现在花了多少时间、Agent 一次运行发生了什么”，不修改 Object、Tick、GC、Replication
的算法。固定顺序是先建立测量工具，第 2 周再保存 Release 基线并提出优化假设。

## PicoProfiler

Profiler 位于 `PicoCore`，因此 Object、Engine、Net、Physics 和 Render 都可以使用它，不会形成模块环依赖。

```cpp
PICO_PROFILE_SCOPE("World.Tick");
PICO_PROFILE_FUNCTION();
```

每条完成事件记录：

- Scope 名称和稳定事件 ID；
- Frame ID、Thread ID；
- Parent Scope ID 和嵌套深度；
- 相对开始时间和持续微秒数。

Profiler 默认关闭。关闭时 RAII Scope 只检查一个原子布尔值，不创建字符串、事件或动态容器。Release 中可通过
API 显式开启，也可以在 Engine 启动参数中使用：

```powershell
PicoGame.exe -profile
PicoGame.exe -profiletrace=Saved/Profiling/PicoFrame.json
```

第二种形式会在 Engine Exit 时原子写出 Chrome Trace。可在 Chrome/Edge 的 Trace Viewer 或 Perfetto 中打开。
`WriteSummaryJson` 另外导出按名称聚合的 Count、Total、Min 和 Max。

首批主干 Scope 已接入：

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

这组 Scope 的作用是还原帧主干，不等同于最终粒度。只有基线证明某个阶段昂贵后，才继续向下拆分。

## PicoRuntimeBenchmarks

独立 Target `PicoRuntimeBenchmarks` 直接调用真实 Object Registry、TickTaskManager、标记清扫 GC 和
ReplicationSystem，不复制一套“看起来像 Runtime”的假实现。

快速冒烟：

```powershell
BuildCodex\Release\PicoRuntimeBenchmarks.exe --quick `
  --output=BuildCodex\BenchmarkResults\Release
```

正式规模入口：

```powershell
BuildCodex\Release\PicoRuntimeBenchmarks.exe --full `
  --output=BuildCodex\BenchmarkResults\Full
```

`--quick` 覆盖 1K Object、1K Tick、三种 GC 图形、100 Actor 和三种 Dirty Ratio；`--full` 扩展到：

- Object：1K、10K、100K 的 Create/Find/Rename/Destroy；
- Tick：1K、10K 的静态调度与依赖变化；
- GC：10%/50%/90% 存活率、0/2/8 引用密度和 0/4/16 Outer 深度；
- Replication：100/1K Actor，1%/10%/100% Dirty Ratio。

输出固定为：

```text
PicoRuntimeBenchmarks.json       版本化机器报告
PicoRuntimeBenchmarks.csv        表格分析入口
PicoRuntimeBenchmarks.trace.json Chrome Trace
```

CTest 只运行 60 秒上限的 Quick Smoke。第 1 周没有提交当前机器的一次性数字，也没有运行完整基线；现有线性
Object 路径可能使 100K 档明显变慢，第 2 周需要按 Suite 分批采集并记录环境，而不是把超长单次运行伪装成稳定结果。

## Agent Metrics

现有 Agent 的 Run/Turn/Model/Approval/Tool/Validation Span 成为唯一计时来源。每次 `FAgentRuntime::Run`
结束后，会在 Session 日志旁写入：

```text
Metrics/<RunId>.json
```

`FAgentRunResult` 返回 Metrics 路径和本次发送给 Provider 的累计上下文字节。Schema v1 包含：

- Run、Turn、Tool Call、Tool Result 数；
- Provider、Approval、Tool、Validation 的 Count/Total/Max/Average 延迟；
- Context Bytes、Repair、Semantic Cache Hit；
- Permission 阶段拒绝形成的 Forbidden Tool 数；
- Completion Rate 和 Failure Class。

第 1 周失败类只稳定输出 `None`、`Cancelled` 或 `Unclassified`。完整 `EAgentFailureClass` 和确定性恢复策略属于
第 2 周，避免这周先用字符串猜测错误原因。Golden Task 报告已引用每个 Run 的 Metrics 文件。

## 验收结果

- Debug：`PicoCoreTests` 102/102，`PicoAgentTests` 101/101；
- Release：三个新增/受影响目标构建成功，`PicoAgentTests` 101/101；
- Debug/Release `--quick` 均成功生成 JSON、CSV 和 Chrome Trace；
- Profiler 自动测试覆盖关闭无事件、Frame、Parent、Depth、Thread 和报告导出；
- Agent 自动测试覆盖 Context Bytes、四类延迟、Turn 数和 Metrics 持久化；
- 本周没有提交 Object Index、Tick Cache、Dirty Mask 或 GC 缓冲复用等性能优化。

## 第 2 周入口

1. 固定硬件、构建参数和 Fixture，分开保存 Debug/Release 基线；
2. 为 Benchmark 增加重复采样后的 P50/P95/Max 和核心 `sizeof`；
3. 根据 Scope 和 Benchmark 列出前三个真实热区，再决定第 3～6 周假设是否需要调整；
4. 建立正式 `EAgentFailureClass` 与 Recovery Policy，不再保留 `Unclassified` 作为长期语义。
