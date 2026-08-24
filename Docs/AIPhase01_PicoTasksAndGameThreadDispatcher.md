# 第 7 月第 1 周：PicoTasks 与 Game Thread Dispatcher

本阶段为 AI Agent、异步 Cook、构建和资源处理建立最小线程基础。它不实现聊天、模型 Provider、Agent Loop
或 Tool Registry，只负责把耗时纯数据工作移出 Game Thread，并把结果安全送回主线程。

## 实现范围

新增独立运行时模块 `PicoTasks`：

```text
FTaskSystem
  -> 固定 Worker Pool
  -> 有界生命周期
  -> Submit / Shutdown

FTaskHandle
  -> Id / Name / State / Error
  -> Wait / RequestCancel

FCancellationToken
  -> Worker 协作式检查取消

FGameThreadDispatcher
  -> Worker Post 纯数据结果
  -> Game Thread 按数量和时间预算 Pump
```

任务状态为：

```text
Invalid
Queued -> Running -> Succeeded
                 -> Failed
                 -> Cancelled
Queued ----------------> Cancelled
```

`FTaskSystem::Shutdown` 先停止接收任务，再请求取消已排队和运行中的任务，最后等待全部 Worker 退出。任务函数
抛出的异常不会越过线程边界，而是转换为 `Failed` 状态和可读取的错误文本。

## 帧阶段

PicoEditor 在已有 `FEngineFrameCallbacks::BeforeWorldTick` 中泵送 Dispatcher：

```text
BeginFrame
 -> GameThreadDispatcher.Pump
 -> World.Tick
 -> AfterWorldTick
 -> GC Safe Point
 -> Editor UI / Render
```

因此 Worker 返回的对象修改会在 World 模拟开始前执行，不会插入 Actor/Component Tick 或 GC 扫描中间。
默认每帧最多执行 64 个回调或使用 2 ms，任一预算先到即停止，剩余工作留到下一帧。

## 固定线程规则

- Worker 只处理 HTTP、JSON、磁盘读取、资源中间数据和其他纯数据。
- Worker 不得创建、销毁、保存或修改 `PObject`、World、Actor、Component、反射属性和编辑器 UI。
- 跨线程关联对象时只传稳定 Handle；回到 Game Thread 后重新解析并验证。
- Dispatcher 回调必须短小，不执行阻塞网络、进程等待或大文件读取。
- `Pump` 在非 Game Thread 调用时拒绝执行，回调继续留在队列中。
- Dispatcher 回调异常被隔离并记录，不阻止后续帧继续处理。

取消是协作式的。C++ 不能安全强制终止任意线程，因此长任务必须定期检查 `FCancellationToken`，阻塞 I/O
也必须使用可取消或带超时的 API。引擎关闭阶段禁止 Worker 等待某个 Game Thread 回调完成，否则会形成：

```text
Game Thread 等待 Worker 退出
Worker 等待 Game Thread 回调
 -> 死锁
```

正确形式是 Worker 投递结果后结束，由下一帧或正常关闭流程决定结果是否仍需应用。

## 编辑器生命周期

`FPicoEditorApp` 拥有 TaskSystem 和 Dispatcher：

```text
Editor 构造
 -> 启动 1～4 个 Worker

Editor 每帧
 -> BeforeWorldTick Pump

Editor 析构
 -> TaskSystem.Shutdown
 -> Dispatcher.Shutdown
 -> 保存 Editor Session
 -> 停止 Play / Package
 -> EngineLoop.Exit 销毁 World
```

默认 Worker 数量为 `hardware_concurrency - 1`，并限制在 1～4 个。当前 Agent 和 Cook 的任务规模不需要
创建与 CPU 核心数相同的大型线程池。

## 自动化验收

新增 `PicoTaskTests`，覆盖：

- Worker Pool 初始化和普通任务完成。
- Worker 确实不在 Game Thread 运行。
- Worker 结果通过 Dispatcher 回到 Game Thread。
- 排队任务取消后不会执行。
- 运行任务协作响应取消。
- Worker 异常转换为任务失败和错误文本。
- Shutdown 取消任务、等待 Worker 并拒绝新任务。
- Dispatcher 的数量预算、跨回调异常隔离和下一帧继续处理。
- Dispatcher 关闭时丢弃未应用结果并拒绝新回调。

Debug 全部 20 个测试和 Release `PicoTaskTests` 已通过。

## 当前边界

- 没有优先级、依赖图、Work Stealing 或 UE TaskGraph 风格的 Named Thread。
- 没有强制终止忽略 Token 的任务。
- 没有让 Render、Physics 或 Animation 脱离 Game Thread。
- 没有 Agent消息、JSON协议、Session、Provider或ToolCall。
- 没有可视化任务调试面板；第2至4周的Agent UI会显示其实际后台任务状态。

这些限制是刻意的。第一版只建立安全异步边界，不提前复制完整 UE TaskGraph。

## 下一步（已完成）

第 7 月第 2 周在此基础上实现：

```text
PicoAgentCore
PicoAgentHost
IAIProvider / FakeAIProvider
Agent State Machine
Append-only Session Event Log
Checkpoint / Resume / Cancel / Budget / Idempotency
```

Provider 执行模型请求时使用 `FTaskSystem`，返回纯数据后通过 IPC 和 Dispatcher 进入 Editor；Agent协议不会
反向依赖 World、ImGui 或具体模型 SDK。

上述 Agent Core、Fake Provider、状态机和 Session 持久化已经在第 7 月第 2 周完成，见
[Pico Agent Core 与可恢复 Session](AIPhase02_PicoAgentCoreAndSessions.md)。Editor IPC 和 Dispatcher
接线仍随第 4 周 Chat Workspace 完成。
