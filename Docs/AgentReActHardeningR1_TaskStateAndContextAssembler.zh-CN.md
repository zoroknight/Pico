# ReAct 加固 R1：Task State 与 Context Assembler

## 目标

R1 在不增加 Agent 执行模式、不修改 Harness 安全管线、也不增加简单任务模型轮次的前提下，建立最小可恢复
Task State、统一上下文预算和可测量的装配基线。

## 已完成

- 新增版本化 `FAgentTaskState`：保存 Goal、SuccessCriteria、Constraints、CurrentStep、可选 RemainingSteps、
  EvidenceRefs、OpenQuestions 和 Revision。
- Task State 随 Session JSONL Checkpoint 增量保存；重启时优先恢复最新状态，旧日志没有 Task State 时继续从
  最近 User Message 恢复 Goal。
- 新增无状态 `FAgentContextAssembler`，统一装配 Skill 指令、Task State、Conversation、Project Knowledge
  与最新 Progress Observation。
- 总上下文遵守 `MaxContextBytesPerRequest`；预留至少一半预算给最近对话，外部上下文超限时按优先级整块丢弃，
  不截断 JSON，也不制造孤立 Tool Result。
- 将原有消息裁剪算法提取为共享纯函数，Session 旧入口继续复用，保持 OpenAI ToolCall/Tool Result 配对规则。
- 新增 `bTaskState`、`bContextAssembler`、`bContextMetrics` 三个运行时 Feature Flag。关闭后回退到 R1 前路径，
  不需要第二套 Runtime。
- Provider Request 增加独立 Task State 分区；Progress Ledger 继续承载预算、Revision、最近失败和已完成行动。
- Metrics JSON 与 Editor Agent Metrics 面板增加装配次数、总/最大耗时、各分区字节和 DroppedBytes。

## 固定边界

~~~text
Agent Runtime
 -> Task State / Context Assembler
 -> Provider Request

Harness
 -> Validate / Permission / Approval / Transaction
 -> Execute / Verify / Journal / Game Thread
~~~

本周没有修改 Tool Provider、审批、事务、Undo、Operation Journal、MCP Toolset 或 Game Thread 语义。
Context Assembler 是纯函数，不拥有 Store、不调用模型、不执行工具，也不修改 Task State。

当前分区指标只统计 Runtime 注入内容：

- `instructions_bytes`：激活 Skill 上下文；
- `task_state_bytes`：最小任务状态；
- `conversation_bytes`：有界消息历史；
- `memory_bytes`：Project Knowledge；
- `observation_bytes`：Progress Ledger。

Provider 私有 System Prompt 和 Tool Catalog 尚不在 Runtime 指标内，不能把当前 `context_bytes` 解读为 HTTP
请求的完整 Token 或完整字节数。后续若需要端到端 Token 成本，应由 Provider 返回独立 Usage 指标，不能混入口径。

## 自动化验收

新增测试覆盖：

1. Task State 版本化 JSON 往返；
2. 低优先级超大 Knowledge 被丢弃，总上下文不越界且最近对话保留；
3. 简单 ReAct 请求仍只有一次 Provider 调用；
4. Context Assembler 简单 Fixture 最大耗时低于 `5 ms` 门槛；
5. Task State 随 JSONL Checkpoint 重启恢复；
6. Metrics 报告包含装配耗时和分区大小；
7. Feature Flag 关闭后使用旧路径且不增加模型轮次。

Debug 验收命令：

~~~powershell
cmake --build BuildCodex --config Debug --target PicoAgentTests PicoEditorTests -j 2
ctest --test-dir BuildCodex -C Debug --output-on-failure -R "^(PicoAgentTests|PicoEditorTests)$"
~~~

结果：`PicoAgentTests` 与 `PicoEditorTests` 全部通过。

## 后续

R2 已在此基础上实现 Harness Result 到 Observation 的统一映射、成功条件与 Evidence 绑定、最近行动指纹和
Revision 进展判断，详见
[ReAct 加固 R2：Observation、Evidence 与行动振荡控制](AgentReActHardeningR2_ObservationEvidenceAndOscillation.zh-CN.md)。
R2 未新增另一种 Agent 模式，也没有把 Agent Progress Check 下沉成第二套 Harness Verifier。
