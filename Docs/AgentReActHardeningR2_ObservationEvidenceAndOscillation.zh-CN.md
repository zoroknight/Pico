# ReAct 加固 R2：Observation、Evidence 与行动振荡控制

## 目标

R2 将 Harness 返回的结构化 Tool Result 统一映射为 Agent Runtime 可消费的 Observation，并把“工具执行成功”
与“用户任务已经完成”分开。它同时使用有界行动指纹和 Revision 进展判断阻止原地重复，不新增第二套验证器、
Planner 或模型调用。

## 已完成

- 新增 `FAgentObservation`，统一承载 CallId、Tool、规范化行动指纹、Facts、Artifacts、Diagnostics、
  StateChanges、RevisionChanges、验证状态、复用状态和进展状态。
- 行动指纹由 Tool 名称与规范化 JSON 参数生成稳定 FNV-1a 标识，不包含 CallId；语义相同但字段顺序不同的
  调用会得到同一指纹。
- `FAgentTaskState` 升级为版本 2，新增 SuccessCriteria 到 EvidenceRefs 的显式绑定；版本 1 Checkpoint 仍可读取。
- Task State 同时保存 ObservationCount，重启恢复后仍不能绕过 Evidence Completion Gate。
- 只有包含 Facts、Artifact、状态变化、Revision 或失败诊断的可信 Observation 才能成为 Evidence。单独的
  `succeeded=true` 不能支撑完成声明。
- 最终回答前执行 Evidence Completion Gate：任务未使用工具时保持原路径；使用工具后必须存在绑定到成功条件的
  可审计证据。失败、拒绝和回滚诊断也可作为“未完成原因”的有效证据。
- Progress Ledger 增加最近 Observation、成功条件绑定、Revision Epoch 和 R2 计数；最近 Observation 固定保留
  8 条，行动指纹固定保留 12 条。
- 新增 Revision 感知的 A-A / A-B-A 振荡检测。只有重复行动没有产生进展且 Revision 未变化时才停止；不同参数
  的读取、第一次获得新事实、幂等重放一次以及状态变化后的重新读取不会被误杀。
- 新增 `bObservationMapping`、`bEvidenceCompletionGate`、`bActionOscillationGuard` Feature Flag，关闭后仍使用
  既有单 ReAct Runtime。
- Session Checkpoint 和 Metrics 增加 Observation、Evidence Binding 与 Oscillation 计数，旧日志缺失字段时按
  0 恢复。

## 架构边界

~~~text
Harness Tool Result
 -> FAgentObservation（Agent 层事实映射）
 -> Evidence Binding / Revision Progress
 -> 下一轮 ReAct 或最终完成门
~~~

- Harness 仍负责 Validate、Permission、Approval、Transaction、Execute、Verify 和 Journal。
- Observation 不再次执行后置条件，也不改变 Harness 的成功/失败结论。
- Progress Check 只判断这些已验证事实是否推进当前 Goal；它不是第二个 Tool Verifier。
- MCP、内置 AI Chat 和未来外部 Agent 都消费同一 Tool Result 合同，不添加协议专用证据语义。

## 进展与振荡规则

一次行动满足以下条件时视为推进：

1. Observation 来自已完成的 Harness 边界；
2. 不是缓存或旧 CallId 的复用结果；
3. 提供了新 Facts、Artifact、状态变化或 Revision，或者是一次新执行的 Mutation。

检测器仅检查最近有界窗口：

- A-A：连续两个不推进的相同行动；
- A-B-A：回到相同行动，且中间行动和当前行动都未推进；
- 任意 Mutation 推进 Revision Epoch 后，旧指纹不再构成同一停滞区间。

语义缓存仍负责避免重复读取 Handler；振荡检测负责在模型继续消费缓存却不收敛时受控停止。两者职责不同。

## 自动化验收

新增或扩展测试覆盖：

1. JSON 参数字段顺序不同仍生成同一行动指纹；
2. Observation 结构化序列化并绑定 SuccessCriteria/Evidence；
3. 空的成功 Tool Result 不能支持最终完成声明；
4. A-B-A 在 Revision 不变时停止，重复 Handler 不执行；
5. 不同参数的连续读取正常完成；
6. 原有语义缓存、幂等 CallId、跨 Revision 重读和 Golden Tasks 不回归；
7. R2 计数进入 Checkpoint 和 Metrics。

Debug 与 Release 验收命令：

~~~powershell
cmake --build BuildCodex --config Debug --target PicoAgentTests PicoEditorTests -j 2
ctest --test-dir BuildCodex -C Debug --output-on-failure -R "^(PicoAgentTests|PicoEditorTests)$"
cmake --build BuildCodex --config Release --target PicoAgentTests PicoEditorTests -j 2
ctest --test-dir BuildCodex -C Release --output-on-failure -R "^(PicoAgentTests|PicoEditorTests)$"
~~~

## 后续

R3 在该证据链上补文档切块、精确字段/BM25、Entity/Revision 索引和低召回 Query Rewrite。R3 不改变原始用户
指令、授权范围或本周建立的 Harness/Observation 边界。
