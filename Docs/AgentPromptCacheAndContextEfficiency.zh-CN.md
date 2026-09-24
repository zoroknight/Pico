# Agent 输入缓存与长会话上下文减重

> 状态：诊断与首轮 Provider 统计/稳定前缀改动已完成；CE0～CE2 减重门待实施。
> 范围：内置 AI Chat 的模型输入、知识检索与会话投影。DeepSeek 实测；外部 Codex/MCP 客户端的模型用量不在 Pico Provider 统计内。

本文记录一次从服务商用量截图、AI Chat 面板到逐请求 Trace 的排查链路，并规定后续优化和验收。这里的 **Prompt Cache** 是服务商对相同输入前缀的复用；它不同于只读工具语义缓存，也不同于 S4 的资产预览/视觉推断缓存。

## 问题与排查过程

1. 服务商按天统计曾显示缓存命中 908,928、未命中 4,612,743 输入 Token，命中率约 16.5%。这是账户级汇总，可能包含不同任务和客户端，不能单独归因为 Pico。
2. 检查请求组装发现，旧顺序在稳定知识和对话历史前放置每步变化的 Task State、Progress Ledger；工具目录还按所选 Skill 重排。当前代码已固定工具 schema 顺序、将稳定 Skill/知识置于动态状态前，并停止在 RAG 中重复收录完整工具 schema。DeepSeek 流式响应及兼容的非流式响应已采集 `usage`；单次值在 `Model.Generate` TraceSpan，Run 汇总在 Metrics JSON/AI Chat 面板。
3. 第一轮在已有大量历史的会话里连续发送同一只读任务。长会话达到 48 条消息上限，裁掉约 184～196 条。随后只新建一次会话却在其中连续重发四遍：首轮调用工具，后三轮均直接复述，`tools=0`，因此后三轮不能作为相同工作量的缓存对照。
4. 最后每轮先点 `New Chat`，确认 `Conversation` 下方的会话文件名改变，再用同一世界、Provider、模型和提示词测试三轮。这三轮分别落在三个独立 JSONL 会话，均实际调用 3～4 个工具；逐步 `usage` 显示稳定前缀能复用，但工具路径和资产检查深度仍有差异。

固定测试提示词：

> 只读分析当前世界：列出主要 Actor，检查 PhysicsCrate 的组件配置及相关资产，最后总结；不要修改场景。

### 本地观测快照

以下 Run 数据来自 `Projects/PicoSandbox/Saved/Agent/Sessions/` 的 JSONL 与 `Metrics/<run_id>.json`。`Saved` 不进入版本控制，表中数字是本次本地观测快照，不是跨机器可重放的服务端缓存证据。`chat KB` 是各次 Context Assembly 累计的会话分区字节数，**不是**完整 Provider 请求大小。

| 场景 / Run ID | 步 / 工具 | 输入 Token | 命中 / 未命中 | 命中率 | 消息 / 裁剪 | 累计 chat KB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 长会话 `run_1790219786725707_0` | 4 / 4 | 135,759 | 32,896 / 102,863 | 24.2% | 48 / 184 | 233.2 |
| 独立新会话 `run_1790220745610767_16` | 4 / 4 | 63,342 | 37,888 / 25,454 | 59.8% | 9 / 0 | 30.3 |
| 独立新会话 `run_1790220717018384_0` | 3 / 3 | 43,613 | 27,520 / 16,093 | 63.1% | 7 / 0 | 16.5 |
| 独立新会话 `run_1790220771924255_37` | 3 / 3 | 43,936 | 27,648 / 16,288 | 62.9% | 7 / 0 | 16.5 |

4 工具的两轮都读取 World、PhysicsCrate 和资产，长会话输入多约 72,417 Token，未命中多约 77,409 Token；这强烈提示原始历史是主要成本之一，但两轮并非严格 A/B，服务商缓存状态、知识检索和具体响应仍可能不同。两轮 3 工具测试选择了 `editor.asset.describe_catalog`，4 工具测试逐个调用两次 `editor.asset.describe`；不能仅凭较低 Token 宣称质量等价。独立新会话中的逐步命中大致从首步 6,784 增至后续约 10,368 Token，说明稳定前缀确实被复用，不能据此推断服务商缓存了哪个具体字段。

命中率按 `hit / (hit + miss)` 计算。优化目标是 **未命中 Token、总输入、延迟和可验证的任务质量**，而不是单独最大化命中率；较短的高质量请求即使命中率降低，也可能更经济。

## 源码定位与判断边界

| 设计 | 已确认事实 | 影响与边界 |
| --- | --- | --- |
| 原始会话窗口 | `FAgentRuntime` 每个模型步骤从 Session 重建历史，`FAgentContextAssembler` 按最近消息数和字节数裁剪；AI Chat 上限为 48 条、256 KiB | 长会话的最近窗口仍可含大量 Tool Result，且窗口滑动使先前消息前缀不再完整匹配。实测长会话累计 chat 233.2 KB，新会话 30.3 KB。历史构建发生在 Assembler 计时开始前。 |
| 工具结果与运行状态 | `FAgentSession::BuildMessageHistory` 恢复 Tool Result 的模型 JSON；Progress Ledger 再包含最近 Observation、已完成动作、目标和规则；Task State 也保存目标与证据 | 同一事实可能以不同形式重复出现。是否可删必须逐字段验证：审批、Revision、失败恢复和完成证据不是冗余装饰。 |
| 知识源刷新 | AI Chat 每次发送收集项目文件、编辑器知识和最多八个会话 Episode；对每种来源调用 `ReplaceSource`，后者保存 Snapshot 并重建索引 | 存在重复处理与历史/RAG 重叠的设计风险；尚无覆盖整个 RefreshKnowledge 的耗时证据，不能直接称为已测得的 CPU 瓶颈。 |
| 工具目录 | 完整工具 schema 每轮发送，现已固定顺序；当前约 45 个 Editor 工具 | 稳定目录有可复用成本，不应为抬高命中率就删工具。后续仅在测得其真实占比和任务成功率后考虑按需发现。 |
| 指标覆盖 | `Assembly` 只量组装函数；Context KB 不包括完整工具 schema 和 Provider 静态系统提示 | 面板上的 `Assembly 0.00 ms` 不能代表检索、历史重放或整个请求的耗时；字节数也不能直接换算为服务商 Token。 |

源码入口：[AgentChatWorkspace.cpp](../Source/Editor/PicoEditor/Private/AgentChatWorkspace.cpp)、[AgentRuntime.cpp](../Source/Developer/Agent/Private/AgentRuntime.cpp)、[AgentSession.cpp](../Source/Developer/Agent/Private/AgentSession.cpp)、[AgentContext.cpp](../Source/Developer/Agent/Private/AgentContext.cpp)、[AgentKnowledgeStore.cpp](../Source/Developer/Agent/Private/AgentKnowledgeStore.cpp)、[OpenAICompatibleProvider.cpp](../Source/Developer/Agent/Private/OpenAICompatibleProvider.cpp)。

## 方案如何确定

1. 先排除错误对照：账户日报混合了其他请求；同一新会话内的后续复述轮没有工具调用；3 工具与 4 工具轮的资产检查路径也不同。能支持的结论是长会话输入明显更重，不能把目前的差值直接归功于某一项代码改动。
2. 当前稳定前缀已有可观命中，且完整工具目录让 Agent 保有发现和调用能力。因此先补完整请求计量（CE0），不以提高百分比为由删工具、强制服务端缓存或加入 Provider 专有参数。
3. 长会话聊天分区与未命中 Token 同时增大，优先在现有 Assembler 做**任务边界投影**（CE1）；只改变发送给模型的视图，不删除审计历史。直接把消息上限从 48 降到个位数可能丢失审批、证据和 ToolCall/Result 配对，不作为默认方案。
4. Knowledge 每轮 ReplaceSource 的额外成本目前属于源码推断，未有端到端耗时实测；故放在计量之后的 CE2。只有在无变更跳过与增量刷新通过跨会话、Revision 和检索质量回归时，才算完成。若真实瓶颈不在此，应按 CE0 数据调整优先级。

## CE0～CE2：S4 前的独立减重门

这是对现有 ReAct Runtime/Context Assembler/Knowledge Store 的加固，**不另建 Harness、Memory Service 或 Agent 模式**。S0～S6 的既有编号与完成状态不变；CE0～CE2 通过后再开始 S4 资产预览。

| 阶段 | 工作与工程边界 | 完成判据 |
| --- | --- | --- |
| CE0：完整计量与固定基线 | 保留已完成的 Provider 逐响应 usage；补充各请求系统提示、工具 schema、Skill、Knowledge、Task State、Ledger、会话历史和 Tool Result 的字节数/稳定指纹，以及知识刷新、历史重放、请求序列化的耗时。不记录 API Key 或原文 Prompt。固定同一项目快照、Provider、模型和多步任务，分别跑新会话与至少十轮历史的长会话。 | 对每个 Run 可解释输入组成、逐步缓存命中和工具路径；至少五组可比较样本，按相近工具轨迹和回答证据分组，不能用账户级日报或 `tools=0` 的复述轮做基线。 |
| CE1：任务边界的历史投影 | 追加式 Session/Event Log 保持完整；在现有 Context Assembler 为模型构造视图：当前任务的 User 指令、完整 ToolCall/Tool Result 配对、待审批/待读回状态及有效证据必须保留；更早轮次只保留经验证的简要事实、来源、Revision 和 Artifact Handle。去除本会话同时经原始历史和 Episode 进入同一请求的重复内容；渐进式实验，不直接把 48 条硬改为 8 条，也不默认调用模型生成摘要。 | 长会话匹配轨迹的未命中 Token 中位数相对 CE0 下降，目标先定为至少 30%；新会话与 Golden Tasks 的成功率、资产引用完整度、审批/恢复/回滚不退化；裁剪不产生孤立 Tool Result 或遗失最新用户指令。 |
| CE2：知识源增量更新与去重 | `ReplaceSource` 对内容/Revision 不变的来源直接跳过 Snapshot/索引重建；项目文件和 Episode 按变更增量刷新；同会话即时历史默认不再以 Episode 重复检索。仅在评测证实重复时压缩 Ledger/Task State 重叠字段，保留审计所需的完整原始记录。 | 无变更的连续查询不重复写 Knowledge Snapshot；Revision 变化、跨会话引用和陈旧证据过滤仍正确；测量 RefreshKnowledge 耗时及端到端延迟改善，且质量门与 CE1 相同。 |

**条件性后续，不进入 CE 门默认范围：**若完整工具 schema 经计量仍是主要成本，再研究稳定工具分组或显式工具发现与回退。DeepSeek 当前 Chat Completions 不能假设支持 OpenAI 专有的 `allowed_tools`、`defer_loading`、显式缓存断点；没有覆盖“非推荐工具仍可被找到”的 Golden Tests 前，不收窄默认能力。

### 回归与安全门

- 离线 `PicoAgentTests`、`PicoEditorTests`、RAG Benchmark 和真实 Editor Golden Tasks；有修改行为的用例继续经相同 Tool Policy、审批、事务、Undo、Revision 校验、Verifier 和 Checkpoint。
- 保留原始 JSONL 作为审计与恢复事实源；只缩减发送给模型的视图，不把模型摘要写成用户确认的正式资产事实。旧证据随 Revision 失效；大结果仍通过 Artifact Handle 按需取回。
- 同时报告未命中 Token 中位数/P95、总输入、Provider 延迟、工具轨迹、Verified Task Success Rate 和无证据完成率。缓存命中比例提高但任务质量下降，判为未通过。
- 若 CE1/CE2 在匹配场景无稳定收益或使模型找不到必要资产/工具，回退该阶段策略，不继续叠加新的抽象。

## 外部资料与取舍

- [DeepSeek Context Caching](https://api-docs.deepseek.com/guides/kv_cache/)：前缀缓存默认开启、完整匹配且尽力而为，返回命中/未命中 Token。Pico 不实现一套替代服务商的 KV Cache。
- [OpenAI Prompt Caching](https://developers.openai.com/api/docs/guides/prompt-caching)：稳定工具定义/顺序和静态前缀。这里只借鉴原则，不把 OpenAI 专有参数硬编码进 DeepSeek Provider。
- [OpenAI Compaction](https://developers.openai.com/api/docs/guides/compaction/) 与 [Agents SDK Sessions](https://github.com/openai/openai-agents-python/blob/main/docs/sessions/index.md)：长会话需要保留继续执行所需状态；Pico 优先采用本地确定性的投影和 Revision 证据，不把 Responses API 服务端压缩作为前置依赖。
