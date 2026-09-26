# P3 Harness 质量门与回放

## 当前状态

P3 的离线轨迹回归已接入现有 `PicoAgentTests` 和 `PicoEditorTests`，没有新增生产 Runtime、第二套 Harness 或固定工具顺序约束。Debug/Release 均通过 Agent 242/242、Editor 193/193。真实 Provider 的 H1 人工质量门仍为 2/4 已评审 Run 通过，覆盖 2/14 任务；不能将离线通过写成产品质量门全通过。

## 离线轨迹

| 风险 | 断言位置 | 通过条件 |
| --- | --- | --- |
| 重启后 Selection 改变且 Compact 开启 | `Tests/Agent/Private/Main.cpp` 的 `TestP3ScriptedContinuationTrajectories` | 重开同一 Session 后再次执行只读 Selection 工具；新结果为 Cow_1，旧 PhysicsCrate 证据仍在 Session，但不能替代本轮结果；历史投影保持当前工具结果。 |
| 审批拒绝与批准 | 同一脚本轨迹；Editor `TestAgentRoomPlan` | 拒绝后零 World 写入、不自动重试；后续独立批准只执行一次。真实 Editor 工具在批准后创建五个部件，可一次 Undo。 |
| 旧 PlanHash 与 World 变化 | `Tests/Editor/Private/Main.cpp` 的 `TestAgentRoomPlan` | World 改变后旧预览标记失效，旧 PlanHash 被拒绝且不创建半个房间。 |
| 工具实例边界 | 同一 Editor 测试新建 `FEditorAgentToolExecutor` | 新工具实例无上一实例的有效计划；旧哈希不能恢复为可执行授权，也不产生部分几何。进程级重启仍由在线路径验收。 |
| 压缩中的旧计划 | `Tests/Agent/Private/Main.cpp` 的 `TestContinuationHints` 和 `TestTaskBoundaryProjection` | 历史 PlanHash 不进入发送上下文，只留下实时 `editor.scene.describe_room_plan` 查询提示；拒绝审批不关闭仍可查询的提示。 |
| 取消、失败工具和重复调用 | Agent 现有取消、失败分类、幂等与预算测试 | 有界停止、无重复写入、不绕过审批。 |

这些是脚本模型响应及可处置 Editor World 的确定性回归，不评估真实模型选择工具的概率，也不自动判定自然语言事实真伪。Agent 脚本审批轨迹检查 Runtime 编排；World 的实际事务/几何由 Editor 测试独立核验。

## 执行

在 `E:\UnrealSourceCode\Pico` 运行：

```powershell
.\Build\Release\PicoAgentTests.exe
.\Build\Release\PicoEditorTests.exe
.\Scripts\TestAgentRealTaskEval.ps1 -Action Summary
```

真实 Provider 和 Release Editor 继续使用 `Docs/AgentHarnessH1RealTaskEval.zh-CN.md` 的逐项采集与人工评审。建议下一项只做一个未覆盖的 H1-11 只读资产证据用例，不要求唯一工具顺序或固定回答长度；检查本轮证据、未知边界、无无关编辑，并保留 Run ID。采集和评审未完成时标记 Pending，不以缓存命中率或离线测试替代质量通过。审批、旧计划与重启的既有在线验收记录仍有效；若生产代码之后改变，才需针对受影响路径重测。

## 可视化观察（2026-09-26）

同一 AI Chat 会话的两轮：`run_1790432723265683_0` 选中 PhysicsCrate，`run_1790432748406287_11` 随后选中 Cow_1。两轮 Compact 均开启，Selection Revision 为 2 与 4，内容指纹分别为 `15522859095274794962` 与 `2065610397445500484`。第一轮实际调用 `editor.selection.describe`、`editor.object.describe`；第二轮重新调用 `editor.selection.describe`，结果为 `StarterWorld.PersistentLevel.Cow_1`，无结果复用。两轮均 Completed、无失败或写工具；第二轮回答明确把 PhysicsCrate 标为上一轮历史观测，以 Cow_1 为本轮当前选择。

这证明该具体切换路径可视化通过；提示词并非 H1-03/04 的逐字用例，未按 H1 Capture 归档，不增加 H1 正式任务覆盖数。Session 中此前存在 Debug P1 夹具历史，但本次请求使用普通 Editor 工具目录；旧合成记录没有替代本轮 Selection 读取。
