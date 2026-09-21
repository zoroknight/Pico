# ReAct 加固 R5：条件式 Reflection、恢复阶梯与评测门

R5 完成 R1～R4 之后的可靠性收尾。它没有引入第二套 Planner、Reflection Service 或 Harness，而是在现有
`FAgentRuntime` 循环中增加一个有上限、可关闭的恢复分支。

## 交付内容

### 条件式 Reflection

`bConditionalReflection` 默认启用，只在以下情况触发：

- 最终答案缺少绑定到成功条件的验证证据；
- 工具后置条件验证失败；
- A-A 或 A-B-A 在相同 Revision 上振荡；
- 连续工具调用没有产生新事实或 Revision；
- Provider 返回既不是最终答案也不是工具调用的空动作。

Reflection 不要求模型输出或保存原始 CoT，也不额外调用独立模型。Runtime 将触发原因、失败分类、简短诊断和
四条恢复约束写入下一次正常 ReAct 请求的 `progress_ledger.reflection`。每个 Run 最多触发一次；一旦不同动作
产生进展，记录转为 resolved。再次出现同类失败时直接升级并停止，避免“反思死循环”。关闭 Feature Flag 后，
行为退回 R4 基线。

### 恢复阶梯

```text
结构化失败分类
 -> 可安全自动恢复：Retry / RefreshState / Replan（受 Repair Budget 限制）
 -> 语义停滞：一次 Conditional Reflection
 -> 仍失败：AskUser 语义提示或 Abort，不再重复工具
```

权限拒绝、审批拒绝和非法参数仍然不能被 Reflection 绕过。事务、审批、Verifier、Journal 和 MCP Adapter 均未
改变；Reflection 只能影响下一步决策，不能直接执行副作用。

### 可观测性

Session Checkpoint、Progress Ledger、Run Metrics 和 Golden Task Report 新增：

- `reflection_attempts` / `reflections`；
- `recovery_escalations`；
- Reflection trigger、failure class、diagnosis 和 resolved revision；
- Golden Task 汇总成功率、平均耗时、平均上下文字节、平均工具数和语义缓存命中率。

当前 Provider 契约没有统一 Token Usage，因此报告使用精确 `context_bytes`，不伪造 Token 数。未来 Provider
提供统一 usage 后可在同一 Metrics 契约中追加，不改变 Harness。

## 回归覆盖

`PicoAgentTests` 固定验证：

1. 正常简单任务仍然只有一次模型轮次；
2. 首次无进展振荡只获得一次结构化 Reflection；
3. Agent 可以利用已有 Evidence 完成，不重复执行只读 Handler；
4. Reflection 后继续振荡会升级并停止；
5. Feature Flag 关闭后恢复 R4 的确定性停止行为；
6. Provider timeout 与非法响应仍走有限 Repair Budget；
7. 原有 Golden Tasks、RAG Benchmark、故障注入和中断恢复全部回归。

真实工程门继续复用 `Scripts/RunEngineeringDepthWeek08.ps1`：它运行完整 CTest、固定 Runtime Benchmark、真实
PicoEditor 两帧初始化、PicoSandbox Package/Smoke，并收集 Golden、RAG 与故障注入报告。R5 不提前实现阶段 A
第 8 周的玩法 Runtime Probe 和三进程脚本；当前双客户端门由 `PicoGameTests`、`PicoReplicationTests` 和现有
真实 UDP/Play Session 测试承担。等玩法积木完成后，再由阶段 A 第 8 周 Scenario Runner 验证具体游戏目标。

## 验收命令

```powershell
cmake --build BuildCodex --config Debug --target PicoAgentTests
ctest --test-dir BuildCodex -C Debug --output-on-failure -R "^PicoAgentTests$"

cmake --build BuildCodex --config Release --target PicoAgentTests PicoEditorTests `
  PicoPackagingTests PicoReplicationTests PicoGameTests PicoRuntimeBenchmarks PicoEditor `
  PicoSandboxGame PicoPackager
ctest --test-dir BuildCodex -C Release --output-on-failure `
  -R "^(PicoAgentTests|PicoEditorTests|PicoPackagingTests|PicoReplicationTests|PicoGameTests|PicoRuntimeBenchmarksSmoke)$"

.\Scripts\RunEngineeringDepthWeek08.ps1 -Configuration Release -BuildDirectory BuildCodex -SkipBuild
```

## 结论

R1～R5 共用一套 ReAct Runtime、Context、Session、Knowledge Store 和 Harness。R5 的新增状态有固定上限，默认
路径不增加简单任务轮次，且可独立关闭。下一阶段可以进入资产理解与安全创作纵向切片，不再继续扩张通用 Agent
架构。

## 2026-09-22 实测记录

- Debug `PicoAgentTests`：190 项断言通过；
- Release 核心门：Agent、Runtime Benchmark、Packaging、Replication、Game、Editor 共 6/6 通过；
- Release 完整 CTest：27/27 通过；
- Golden Tasks：21/21，验证成功率 `100%`，平均上下文 `4406.95 bytes`，平均工具调用 `1.76`，平均耗时
  `55.67 ms`；该组显式关闭 Reflection 作为 R4 兼容基线；
- R5 定向用例：首次振荡触发 1 次 Reflection，底层只读 Handler 仍仅执行 1 次；再次振荡产生 1 次恢复升级并停止；
- 真实 PicoEditor 两帧初始化通过；PicoSandbox Release 打包与 Smoke 通过，共 74 个 Stage 文件，完成标记与
  Package Report 均存在；
- 固定 Runtime Matrix 55 条记录、Hierarchy A/B 18 条记录均通过；
- 真实付费 Provider Eval 未运行，因为需要 API Key 与显式成本批准；交互式 Editor Play 仍保留为人工验收，
  具体玩法的 Server + Client 1 + Client 2 自动 Scenario Runner 仍按阶段 A 第 8 周实施。

机器证据位于 `BuildCodex/EngineeringDepthWeek08/EngineeringDepthWeek08Summary.json` 及其 `Agent`、`Runtime`、
`Package` 子目录；构建产物不纳入源码提交。
