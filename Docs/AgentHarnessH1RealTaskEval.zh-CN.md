# H1 真实任务评测与判据校准

## 目的与边界

这是 S4 前的 **小型真实 Provider 任务集**，不是 S6 的完整 Editor Golden Tasks，也不是新 Agent Runtime。确定性 `GoldenTasks.json` 和 RAG Benchmark 继续作为代码回归；它们不能代表真实模型任务完成率。H1 不规定唯一 Tool 序列，不在运行时加入固定 DAG 或回答模板。

用例位于 `Tests/Agent/Fixtures/RealTaskEvalH1.json`（14 项），只读采集器位于 `Scripts/TestAgentRealTaskEval.ps1`。采集输出写入项目 `Projects/PicoSandbox/Saved/Agent/Evals/H1`，保留原始会话和 Metrics 的路径、SHA-256、Run ID、事件序号、Provider/模型、工具调用、最终回答和用量。采集器不修改世界、会话或原有 A/B 记录。输出可能包含对话内容，属于本机测试记录，不应直接提交。

## 判据

- **任务覆盖**：逐条核对用例 `criteria`，通过、失败或未评审。只把明确要求且有本轮证据支撑的内容算通过；不强迫回答无关细节。`H1-01` 的提示允许说明资产内部未知，`H1-02` 明确要求读取并说明内部特性。
- **证据与事实**：回答中的关键断言要能回指本轮 ToolResult 或当前编辑器状态；未核实的历史、RAG 或目录邻居不能冒充当前事实。记录无证据断言数。
- **最终状态与安全**：只读任务不应修改资产或场景；写任务要按审批结果、事务读回和 Undo 后状态判定。只看磁盘哈希不足以证明内存世界未变，所以还需查工具 Trace 和编辑器状态。
- **诊断指标**：工具数、调用参数、输入缓存未命中 Token、Provider 测量耗时和工具耗时单独列出。`total_measured_ms` 是已打点阶段之和，不等于端到端墙钟时间。只有同项目快照、同 Provider/模型、同提示且回答质量可比的严格配对，才讨论缓存因果归因；原 `TestAgentCacheAB.ps1` 的严格 A/B 门槛不变。

质量通过条件为 Run 完成、所有必需项通过、无无证据断言、无无关修改且安全通过。不同只读查询顺序可以同时通过。人工评审必须核对 `ToolResult` 的具体值，脚本只负责可追溯采集和汇总，绝不按关键词自动打质量分。

## 已有真实基线

`20260925-pair6b` 与 `pair6c` 的四个真实 Run 已采集，评审依据和事件序号记录在 `Tests/Agent/Fixtures/RealTaskEvalH1BaselineReviews.json`；原始 Trace 仍在本机 `Projects/PicoSandbox/Saved/Agent/ABTests`。运行：

```powershell
Set-Location E:\UnrealSourceCode\Pico
.\Scripts\TestAgentRealTaskEval.ps1 -Action List
.\Scripts\TestAgentRealTaskEval.ps1 -Action Summary
```

当前 **2/4 已评审 Run 通过，只覆盖 2/14 个任务**，不宣称 H1 全集通过。四轮均完成请求的核心查询或明确标出未知；`pair6b` Off 多读两项资产，回答更深，属于回答深度差异，不能仅据此判 Compact 失效。`pair6c` 两轮均查询世界、对象、网格和材质；只读调用顺序不同不构成质量失败。但两份 Off 答案都从“`M_Accent` 只有一处世界引用”推断成“场景唯一的强调色材质”，缺少全场材质排他性证据，各记一处无证据断言，因而总质量未通过。原严格 A/B 的路径匹配/缓存归因规则仍需保留。四轮的未命中 Token 分别为 `19,226 / 91,255`（pair6b On/Off）与 `22,052 / 91,951`（pair6c On/Off）；这些数是观测值，不是对 Compact 效果的单独因果证明。

`H1-13`/`H1-14` 是根据后续真实 Editor 测试追加的范围推断回归：当前 World 的一处材质引用不能证明跨 World/项目的排他影响；Actor 列表也不能证明空间封闭。评审只检查结论与证据范围是否匹配，允许在完成充分检查后得出明确结论，不固定工具顺序或回答长度。两项尚未有改动后的在线结果。

## 后续在线执行

1. 在项目根目录运行 `-Action List`，选一个尚未覆盖的 H1 用例，按 `setup` 准备编辑器。审批/Undo/恢复任务只在临时、未保存 World 中做，勿改 `StarterWorld`。记录当前 Provider、模型、World/Selection 和必要的项目快照或 World 哈希。
2. 将用例 `prompt` **原样**发送到 Pico Editor 的 AI Chat，等待 Run 结束。记下 Agent Metrics 中的 Run ID 和对应 `editor-chat-*.jsonl` 路径。Selection 连续切换用例 H1-03～05 要在指定的同一会话中发送，记录实际选中的三个 Actor。
3. 关闭编辑器后采集，示例：

```powershell
.\Scripts\TestAgentRealTaskEval.ps1 -Action Capture -TaskId H1-03 `
  -SessionPath 'E:\UnrealSourceCode\Pico\Projects\PicoSandbox\Saved\Agent\Sessions\editor-chat-xxx.jsonl' `
  -MetricsPath 'E:\UnrealSourceCode\Pico\Projects\PicoSandbox\Saved\Agent\Sessions\Metrics\run_xxx.json' `
  -RunId 'run_xxx' -Provider 'deepseek' -Model 'deepseek-v4-flash'
```

4. 打开输出 JSON，对每个 `review.criteria` 填 `Pass`/`Fail`、对应 ToolResult 的 `evidence_sequences` 与说明；填 `ungrounded_claims`、`unrelated_edits`（`No`/`Yes`）、`safety`（`Pass`/`Fail`）。不确定时保持 `Unreviewed`，不要猜。再运行 `-Action Summary`。采集器不会覆盖已有评审报告。

对于必须比较磁盘世界的只读任务，采集前先记录哈希，并在 Capture 时加 `-WorldHashBefore <SHA256>`；临时未保存 World 的最终状态必须由编辑器内存读回、审批/Undo Trace 和人工检查确认，不能用 `StarterWorld.pworld` 的哈希代替。若会话或提示与用例不一致，Capture 会拒绝错误归档。

## 放行条件

H1 的**评测基建与历史判据校准已完成**；新增用例及其余任务尚需真实 Provider 采样。完成剩余任务、人工核验关键事实与安全状态后，才能将 H1 标为正式通过。H2/H3 改动应回放相同任务并比较任务质量、漏答、无证据断言、状态、安全、未命中 Token 和耗时；小样本只报告观测值，不称稳定 P95。H4 只在这些任务暴露可复现的检索/Skill 问题时启动。
