# 工程深度第 7 周：Revision、上下文与可视化

## 本周目标

本周把前六周已经测量出的数据接成可长期使用的开发闭环：Profiler 不再无限保存事件；Agent 缓存只受相关资源
Revision 影响；长会话请求有固定上限；Runtime 与 Agent 的关键指标能在编辑器中直接查看。

## Runtime

### Profiler 三种存储模式

- `Disabled`：不采样，不保存事件；
- `AggregateOnly`：只累计 Scope 的 Count/Total/Min/Max，编辑器和 `--profile` 默认使用；
- `BoundedTrace`：固定容量 Ring Buffer 保存最近事件，被覆盖事件计入 `DroppedEventCount`；只有显式
  `-profiletrace` 或要求 Trace 的 Benchmark 才使用。

`PicoRuntimeBenchmarks --no-trace` 现在明确使用 `AggregateOnly`。第 6 周 Full、5 样本中
`Profiler.Events` 为 `602,348,472 B current / 701,574,544 B reserved`；第 7 周相同 Full 合约为 `0 / 0`，
同时 JSON 中仍保留 12 个聚合 Scope。这说明内存下降来自停止保存逐事件历史，不是关闭性能观测。

### 安全点 Compact

`FProfiler::Compact` 与 `FObjectRegistry::CompactStorage` 只在显式按钮、World 切换和 Engine Exit 等生命周期
安全点执行。Object Slot 只收缩 capacity，不重排 Slot 或修改 Handle；Name/Hierarchy Index 只重建 bucket；GC
Scratch 释放历史峰值容量。完成后立即校验 Name Index 和 Hierarchy Index。禁止每帧 `shrink_to_fit()`。

### 帧统计与开发面板

`FFrameTimer` 使用固定 240 帧窗口提供 P50/P95/P99，并累计超过 16.67 ms 的长帧。编辑器 `View >
Development Metrics` 显示：

- Frame P50/P95/P99 与长帧；
- Object、TickFunction、Schedule Rebuild、Task Queue；
- 最近一次 GC；
- Replication Actors/Dirty Actors/Dirty Fields/Bytes；
- Top CPU Scopes；
- Object/GC/Profiler/Replication 的 Current/Reserved/Peak Bytes；
- Profiler 模式、Ring 容量与覆盖事件数，以及显式 Compact 按钮。

该窗口是开发工具，不进入正式游戏 HUD。Present/VSync 与 Idle/Pacing 的独立 CPU/GPU 时间、Editor 后台
10～20 FPS、不可见资产预览自动暂停尚未伪装成完成项，留到第 8 周结合真实编辑器复测决定。

## Agent

### 多域 Revision

Tool Descriptor 已有 `RevisionReadSet/RevisionWriteSet`，本周 Runtime 正式消费这些信息。缓存键由 Tool 名、规范化
参数和 Reads 中各 Domain 的版本组成。Graph 写入只推进 `Graph.Revision`，不会让只读
`Asset.Revision` 查询失效。当前域支持工具自由声明，约定核心域为 World、Asset、Graph、Config、Knowledge。

成功 Tool Result 持久化每个 Domain 的 Before/After；下一次 Agent Runtime 从 Session JSONL 重建 Revision
Snapshot，不会在新聊天轮次回到零。没有声明集合的旧 Executor 使用 `State.Revision` 兼容域。

### 有界上下文

Provider 每次请求默认最多使用最近 48 条 Message 和 256 KiB Message 内容。完整 JSONL 仍是审计事实源，裁剪只
影响送给模型的窗口。裁剪边界保持 Assistant `tool_calls` 与后续 Tool Result 的协议组完整；如果预算容不下前置
Assistant，则丢弃该组旧 Tool Result，避免向 OpenAI-Compatible Provider 发送孤立的 `role=tool`。以下控制状态
不依赖旧 Message：

- `CurrentGoal`；
- 最近 12 个 Progress Action；
- 各资源域 Revision；
- 最近错误；
- Pending Approval；
- Budget、上下文保留数与裁剪数；
- 大 Tool Result 的 Artifact Handle（沿用第 4 周外部化机制）。

AI Chat 的 `Agent Metrics` 显示最近运行 Steps、Tool Calls、Cache Hits、Context Messages、Trimmed Messages 和
累计 Context Bytes。

## 自动验收

- Debug：Core/Object/Agent/Engine/Editor 5 组测试通过；
- Release：Core/Object/Agent 3 组测试通过；
- 新增测试覆盖 AggregateOnly、BoundedTrace 顺序与覆盖计数、显式 Compact、Object Handle/索引保持、Graph 写入
  不失效 Asset 缓存、多域 Revision 持久恢复、有界 Message History 与 ToolCall 协议组裁剪；
- Release Full 基准：
  `Docs/Baselines/EngineeringDepthWeek07RevisionContextVisualization/ReleaseBuild/`。

关键样本：100K Object Destroy P50/P95 `30.926/31.853 ms`；10K Tick StaticSchedule P50/P95
`2.772/3.210 ms`；1K Replication 1% Dirty P50/P95 `145/211 us`。这些数据用于第 8 周综合报告，不把单次机器
波动写成普遍提升。

## 可视化验收

1. 打开 `BuildCodex/Release/PicoEditor.exe`，选择 `View > Development Metrics`。
2. 观察 Frame 分位数、Object/Tick 数和 Top CPU Scope 随编辑器运行更新。
3. 启动双客户端 Play，确认 Replication 行出现 Actors、Dirty Fields 和 Bytes。
4. 停止 Play，点击 `Compact At Safe Point`，Message Log 应显示成功，场景与选择仍可继续使用。
5. 在 AI Chat 执行一次包含查询和修改的任务，展开 `Agent Metrics`，观察工具数、缓存命中和上下文指标。

## 剩余风险

- Profiler Aggregate 使用 `std::map<string,...>`，Scope 名数量通常很小；若未来动态 Scope 名膨胀，需要 Name
  Interning，而不是重新开放无界 Event；
- 多域 Revision 的正确性依赖 Tool Descriptor 声明准确，第 8 周应审计 Editor Tool Catalog 的 Reads/Writes；
- Context 当前采用“保留最近消息 + 独立 Ledger”，尚未加入模型摘要；在有确定性 Eval 证明收益前不引入摘要模型；
- Present/Idle 与可见性降频需要真实 Editor/Play 测量后再实现。
