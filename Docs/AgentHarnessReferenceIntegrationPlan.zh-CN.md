# Agent Harness 参考项目吸收与轻量化落地计划

> 状态：本文局部编号 P0～P3 与全项目路线图中的优先级 P0/P1 无关。P0 的请求归因、外部快照契约和离线重建检查已实现；生产日志按隐私边界只存指纹，不能独立还原动态知识原文。P1 的受压只读结果投影及安全回归已实现，一组真实 Provider 受控对照通过，但正式版真实任务收益未判定。P2 仍按测量决定是否实施；P3 的脚本轨迹、Editor 安全回归和 H1 采集已接入，真实 Provider 产品质量门仍待更多人工评审。本文只规划 PicoEditor 现有 Agent Harness 的增量加固，不替换 ReAct Runtime，也不改变 S0～S6、CE0～CE2、H1～H3 或 AG1～AG3 的历史验收结论。

## 依据与目标

已阅读本地 `test_harness/pico-harness`、`test_harness/claw_code/claw-code` 和 `test_harness/deepseek-harness` 的运行时、工具、上下文及测试源码。三者分别提供 Turn 所有权与上下文组装、脚本化 Provider 与轨迹测试、可重建请求与分层压缩的参考；其多入口调度、通用 Shell 权限或全插件容器不是 PicoEditor 的迁移目标。参考仓库的存在不代表相应机制已经在 Pico 实现或通过验收。

Pico 已有完整 Session/Event Log、`FAgentRuntime`、`FAgentContextAssembler`、Task State、审批与 Revision、PlanHash、Undo/ChangeSet、Fake Provider、Golden Runner、逐响应 Provider 缓存用量和 H1/AG 在线用例。本计划不重新实现这些能力，而是补齐：实际请求的可核对证据、长输入的可恢复减重、缓存改动的同质量成本判断，以及产品入口的安全回归。

所有阶段坚持三条约束：完整审计事实不因发送视图压缩而删除；当前 World/Selection/资产 Revision 和有效计划只从当前权威来源确认；未获审批的写入不能由摘要、检索或工具发现旁路触发。

## P0：请求证据与重建检查

在现有 `Model.Generate` Trace 的 `request_profile` 基础上记录每次实际发送的 Provider/模型、工具定义列表版本与顺序、各片段指纹及大小、用于组装的 Session 事件序号范围、当前 Selection/World/资产证据 Revision、Compact 状态和请求序列号。工具过滤、Provider 序列化及重试之后的实际发送视图应可与组装前意图对照；不把仅有字节数或哈希误称为可完整重建。

提供本地诊断检查：在测试环境中用 Session 事件和有版本的外部观察重建一次模型请求，逐段比较实际请求；生产 Trace 默认只保留必要的指纹、来源和计量，不记录 API Key、原始隐藏推理或无关敏感原文。无法恢复的动态观察必须明确标成不可重建，不以旧聊天文本补缺。先审查当前有界历史裁剪对 ToolCall/ToolResult 配对和最新用户指令的影响，再决定修正点。

验收：新会话、长会话、Selection 连续变化、房间预览后继续、World 改变使计划失效、Editor 重启六类轨迹均能指出请求来源；计划有效性仍须通过 `editor.scene.describe_room_plan` 等当前工具核对。诊断本身不改变 Agent 决策、审批或文件内容。

已实现的观测字段随 `Model.Generate` Trace 的 `request_profile` 写入 JSONL：`agent_step`、Session 来源事件起止序号、`editor_snapshot`（Selection Revision、World/资产/Selection 内容指纹，不含路径原文）、Provider 自报的 `provider_family` / `provider_model`、`tool_names`、`serialized_fingerprint` 和 `http_attempts`。World 的 Actor 数量和资产描述符数量不是 Revision；前者即使不变，Actor 移动也会改变 World 内容指纹。`replay_contract` 明示外部快照未随生产 Trace 持久化，不能只凭哈希重建。真实 OpenAI-compatible Provider 的 body 指纹在序列化后计算，重试复用同一 body；Fake Provider 无实际 body，因此对应字段为空。本地可执行 `./Scripts/InspectAgentRequestProfiles.ps1 -SessionPath <session.jsonl> [-RunId <run_id>]` 只读查看逐步来源与 system/schema/history/tool result/knowledge/skill/task/ledger 字节数。旧日志没有新增字段时显示空值。离线测试用复制的持久 Session 种子及相同外部快照重建逐字节相同的 Provider body，改变知识快照则请求与分区指纹都改变。生产完整原文重建不启用；需要重建时必须显式提供当时的外部快照，不得用当前 World 或旧聊天补缺。

## P1：按风险分层的上下文减重

先量化旧 Tool Result、会话历史、知识、Ledger 和工具 schema 对未命中 Token 的贡献。优先对超过阈值的旧工具文本结果制作有界发送投影，保留结果身份、错误、来源、Revision 和可取回句柄；完整结果留在 Session/Artifact 中。随后仅针对已验证的旧任务历史优化 CE1 投影，不增加默认内部 LLM 摘要调用。

当前任务的最新用户指令、未闭合工具配对、待审批/待读回状态、失败恢复所需信息必须保留。摘要或续接提示只能提示取回入口，不能携带可执行的旧 PlanHash 作为权威状态；取回后的 Revision/World 指纹不匹配时拒绝执行。触发依据为本次请求对实际模型窗口及输出预留的压力，不能用累计会话 Token 代替。裁剪后若缺少证据，模型应重新查询或明确未知。

验收：在相同 World/Provider/模型条件下，长会话任务的未命中 Token 与总输入下降；H1-03～05、H1-07、H1-08～10、H1-13～14、AGCompact 有效/失效/重启以及保存重开用例不退化。发现工具配对断裂、陈旧计划被执行、无证据推断或任务质量下降时回退该投影，不删除日志来追求指标。

已实现：Compact 开启时，仅在本次消息字节数超过应用级消息预算的 65% 后，投影当前任务中较早且超过 4 KiB 的成功只读 Tool Result；最近两个 Tool Result 原样保留，单条投影必须小于 2 KiB 且比原文短。投影保留 Call ID、工具名、原文指纹与“重新只读查询当前事实”的标记；Session/Artifact 原文不改，Ledger 不再重复注入已省略事实。写工具、失败结果、Revision/状态变更、未闭合 ToolCall、待变更读回一律不投影。旧任务 CE1 投影遇到任意结构化 `PlanHash` 则省略哈希并提示实时查询。该阈值按**本次应用请求预算**而非累计 Token 触发；它不是 Provider 声明的精确模型窗口，需用真实请求用量校准。离线与 Fake Provider 回归已覆盖行为、持久回读和 Trace 计数；上述真实任务成本与安全验收仍待执行。

测试接入：Debug Editor 已提供独立于 CE1 的 P1 开关及固定五条只读大结果夹具，Release 不编入；请求 Trace 显示开关与投影字节，`InspectAgentRequestProfiles.ps1` 可按 Run ID 提取。操作和判据见 `AgentHarnessP0P1Validation.zh-CN.md`。离线 On/Off 回归与一组真实 Provider 受控对照已通过：On 的序列化请求与未命中 Token 下降、回答事实一致；单次延迟和命中率未改善。夹具结果不能外推为正式版真实任务收益，也不足以判定 P2 必须实施。

## P2：缓存前缀与工具目录实验

仅当 P0 证明工具 schema 是主要未命中来源时，才试验常用工具稳定常驻、其他工具按需发现。是否启用按实际 schema Token 占比和任务覆盖决定，不按工具数量硬触发；默认保持现有完整且顺序稳定的工具目录。工具搜索只做能力发现，具体调用仍经原 Tool Registry、Tool Policy、审批、Revision 与幂等链。上下文预算使用最终发送的工具定义，不能一边隐藏 schema 一边按完整目录预留。

同一固定任务集比较未命中 Token、总输入、Provider 延迟、额外搜索轮次、工具选择错误率和 Verified Task Success Rate；不能仅以缓存命中率上升为成功。除只读且显式标注可并发的工具外，不改变现有执行顺序；World/资产写工具保持串行。若总成本上升、能力遗漏或安全回归，关闭实验开关。

## P3：产品质量门与回放

复用现有 Fake Provider、Golden Runner、H1 脚本与 Debug/Release 测试目标，不建立第二套 Harness。补充可编排的模型响应脚本，覆盖旧 PlanHash、审批拒绝/批准、取消、重复或失败工具调用、Selection 变化、重启和压缩前后续接。离线测试断言工具轨迹允许的语义集合、Session 事件、审批次数、World/资产真实状态和禁止副作用，不锁死回答长度或等价只读工具的顺序。

真实 Provider 与 Release Editor 验收分开记录：Run ID、请求指纹、任务必需事实、证据来源、最终 World/资产状态、未命中/总输入 Token、延迟及同条件静止 FPS。一个测试只能证明它覆盖的路径；真实 API 缓存命中、端到端质量和性能不能由单测或 Agent 自述替代。出现不同工具深度的两轮不得作为缓存收益的严格配对。Release 不链接 Fake/Golden 测试支撑，沿用 H3 隔离。

现有 `TestAgentRealTaskEval.ps1 -Action Capture` 的新报告使用 `schema_version: 2`，附带每个 `Model.Generate` 的来源事件序号、Provider body/工具 schema 指纹、实际工具顺序、history/tool result/knowledge 字节数及发送时 Editor 快照 Revision；旧 JSONL 缺字段时保持 `null`，不伪造已验证请求。`Summary` 继续读取旧报告。此项只是采集能力，不代表新的真实模型质量门已通过。

已补充跨轮脚本回放：重启 Session 后 Selection 变化与 Compact 并存、审批拒绝/批准的执行次数；真实 Editor 工具回归覆盖旧 PlanHash 在 World 变化和工具实例重建后的失效与零部分副作用。Debug/Release Agent 和 Editor 测试均通过，具体矩阵与剩余产品入口见 `AgentHarnessP3QualityGate.zh-CN.md`。H1 汇总仍只有 2/14 个任务覆盖，不能宣称 P3 真实 Provider 质量门已全部放行。

## 顺序与放行

1. P0 先行，建立请求证据和裁剪安全基线；P1 依赖 P0 的诊断与回归集。
2. P2 只有在 P0/P1 的测量显示工具目录值得优化时才启动；可以判定为“不实施”，不影响其他阶段完成。
3. P3 的离线夹具随每阶段增量建设，真实模型与产品入口质量门在 P1 后集中执行，不等到 P2 才开始。
4. CE0～CE2 既有风险接受决议已允许 S4 按现有基线启动；P0/P1 的新代码若合入，须先通过自身相关安全回归，但不反向增加 S4 的启动门槛，也不为凑齐固定数量的相似缓存 A/B 无限阻塞。任何 S4 写入和视觉推断仍受原计划的审批、来源和 Revision 约束。

## 明确不做

- 不移植 Cordis 全插件树、多渠道 Turn 调度、通用 Shell 沙箱或额外 Agent；PicoEditor 保留单 Agent ReAct 主链。
- 不让 LLM 摘要、旧聊天或资产路径恢复当前 World、Selection、PlanHash 或审批状态。
- 不以固定回答模板、固定工具顺序或单次缓存命中率作为质量门。
- 不把完整原始工具结果、Session/Event Log 或用户已有 World 内容当成可丢弃的性能缓存。

关联基线：[输入缓存与长会话减重](AgentPromptCacheAndContextEfficiency.zh-CN.md)、[H1 真实任务评测](AgentHarnessH1RealTaskEval.zh-CN.md)、[H3 上下文与测试隔离](AgentHarnessH3ContextAndTestIsolation.zh-CN.md)、[AG 验收](AgentAGValidation.zh-CN.md)、[资产理解与安全创作](AgentAssetUnderstandingAndSafeAuthoring.zh-CN.md)。
