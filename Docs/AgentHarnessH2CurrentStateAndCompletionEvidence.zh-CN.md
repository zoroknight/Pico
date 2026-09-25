# H2 当前状态与完成证据

H2 沿用现有 ReAct Runtime、Knowledge Store、Tool Registry 和审批/事务链路，不新增规划模型或固定工具 DAG。

## 实现

- AI Chat 发送请求时从编辑器实时知识记录抽取独立的 `current_editor_state`：World 路径与 Actor 数、Selection 主对象/全部对象路径及 Selection Revision。它带 `capture_scope=send_start` 和来源说明；World 没有可靠的单调 Revision 时保留 `null`，不把 Actor 数量冒充 Revision。空 Selection 显式是空路径数组。
- 该快照进入本轮 Task State，而不是混入历史 RAG 命中。每次新发送重新采集；Checkpoint 恢复不会把上轮快照当成当前状态。它只证明发送瞬间的身份，运行中的工具结果优先；涉及当前属性、资产内部参数或修改后的状态仍需精读/读回。
- Provider 和 Editor 指令明确要求逐项对照用户目标：本轮已验证事实、仅见引用与未验证内容分开；检索、Episode 和压缩历史只能提供线索，不能证明当前 World/Selection。资产的一处引用不能证明其视觉属性或项目内唯一性。
- 失败的工具结果不再绑定为成功完成证据，也不算新进展。审批拒绝 Golden 夹具改为真实的 `ApprovalRejected`/`Failed` 语义，继续验证零副作用；没有通过“把失败当成功”维持旧断言。

## 范围推断回归

后续真实 Editor 测试发现两种越界回答：把当前 World 中 `M_Accent` 的一处引用说成修改它只影响一个对象，以及仅凭三面墙的 Actor 列表称房间封闭。`editor.asset.describe` 和 `editor.asset.find_references` 现在给出 `reference_scope`：资产引用来自已注册项目资产，World 引用只来自已加载的活动 World，未扫描其他 World 或间接引用。Editor 指令要求排他性、完整性和影响范围结论不得超出实际检查范围；不规定查询顺序或回答长度。

Runtime 不再将任意成功观察自动标记为满足所有自然语言成功条件。完成证据门只确认本轮存在成功且有内容的已验证工具观察；修改后的独立读回门仍照常生效。它不承担语义正确性证明。`H1-13`/`H1-14` 增加针对性的真实 Provider 评测，并允许在证据足够时得出明确结论；改动后的在线质量仍待人工复测。

## 验证与边界

Debug 构建 `PicoEditor`、`PicoAgentTests`、`PicoEditorTests` 通过；两套 CTest 通过。Agent 测试新增跨轮 Selection 快照和失败读回不满足证据门的回归断言。现有 Golden/RAG、审批、Mutation Readback、Revision、Checkpoint 测试继续通过。

这不是语义正确性的形式化证明。通用自然语言请求无法靠一个工具成功标志机械验证每个子要求；Runtime 保留本轮证据门、Mutation Readback 和有限反思，模型仍需按回答契约逐项检查并诚实报告未知。不能因离线 Fake 测试通过就宣称真实模型已经解决 `Cow_1` 跨轮比较或 `M_Accent` 唯一性过度推断。

## 真实 Editor 复测

1. 使用 [H1 用例](../Tests/Agent/Fixtures/RealTaskEvalH1.json) 的 H1-03～05，在同一会话依次选 `PhysicsCrate`、`Cow_1`、三个 Actor。每轮发送对应原文，核对当前选择、主对象与数量；不能把上一轮对象当成当前对象。
2. 用 H1-01/02 分别检查“资产引用”和“资产内部特性”。没有 `editor.asset.describe` 的回答应明确不知道内部参数；有读回时仍不能仅凭一处引用声称材质在全项目唯一。
3. 在临时未保存 World 执行 H1-08～10：拒绝无副作用、批准后精确读回、Undo 后对象消失且无关对象不变。再做 H1-12 重启后重新读取当前 World/Selection。
4. 每轮按 [H1 采集说明](AgentHarnessH1RealTaskEval.zh-CN.md) 保存 Run/Trace，用相同判据比较 H2 前后漏答、无证据断言和状态准确性。真实 Provider 复测尚未执行，因此 H2 的在线验收仍待完成。
