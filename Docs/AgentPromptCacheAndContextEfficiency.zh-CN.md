# Agent 输入缓存与长会话上下文减重

> 状态：CE0～CE2 已按 2026-09-25 的风险接受决议完成阶段验收，允许进入 S4；原定五组在线 A/B 未完成，端到端收益与部分真实模型回归仍是后续风险，不得写成已证明。
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

### 已实现与未验证的边界

- CE0：`Model.Generate` 的 Session JSONL TraceSpan 中新增 `request_profile`，逐请求记录系统提示、工具 schema、Skill、Knowledge、Task State、Ledger、历史与 Tool Result 的字节数/稳定指纹，以及请求序列化、历史重放和首步知识刷新的耗时。`request_profile.task_boundary_projection_enabled` 与 Run Metrics 的 `context_assembly.task_boundary_projection_enabled` 记录本轮实际使用的 Compact 值，发送后的 UI 切换不会改写它；AI Chat 的 Agent Metrics 显示 `This run Compact`。新增 Trace 不存原文或 API Key；原有 Session Message 仍按审计要求保存用户消息。`provider_usage` 继续记录实际 Token。`Metrics/<run_id>.json` 保留 Run 汇总，完整分区明细看 JSONL Trace。
- CE1：Editor AI Chat 的 Request Settings 默认开启并持久化 `Compact prior task history`，也可关闭做 A/B 或即时回退。当前任务的用户指令与调用/结果链保持原样；更早的成功工具结果只保留最多四条、合计至多 4 KiB 的历史事实、来源、Revision/Artifact Handle，过大事实标记为需重新查询。未配对旧调用、没有可用旧证据或当前任务超预算时回退原有历史裁剪；完整 Session/Event Log 不变。同会话不再经 Episode 二次召回。
- CE2：无变化的规范化 Knowledge Source 跳过 Snapshot 写入和索引重建；项目文本与跨会话 Episode 按文件大小/修改时间复用已读内容，扫描仍发现新增/删除。源变化时仍使用现有全量索引重建，**尚未引入复杂的索引增量更新器**。如果外部程序在保持大小和修改时间不变的情况下改写文件，需要重启 Editor/清除缓存以强制重读；安全写入仍以实时工具与 Revision 为准。
- 离线验证：`PicoAgentTests` 206/206、`PicoEditorTests` 174/174、`PicoEditor` Debug 构建通过。Golden Tasks 21/21 与 RAG Benchmark 7/7 均为离线夹具，不能替代真实 Provider/Editor 验收。

### 在线可视化验收

1. 固定同一项目/World 快照、Provider、模型和提示词。在 AI Chat 的 Request Settings 切换 `Compact prior task history` 开/关，分别准备独立新会话与至少十轮历史的可比长会话；每次测试记录 `Run` ID、实际工具名/次数、回答中的证据和是否修改了场景。不要在同一会话里直接重复发送并把 `tools=0` 的复述轮算成对照。
2. 在 AI Chat 的 **Agent Metrics** 查看 `Provider input cache`、`Provider usage`、`History projection` 和 Run ID。到 `Projects/PicoSandbox/Saved/Agent/Sessions/` 的对应 JSONL 找该 Run 的 `Model.Generate` TraceSpan，检查 `request_profile` 的分区大小、指纹和耗时；不要把 Context KB 当作完整请求 Token。
3. 至少取得五组工具轨迹与回答证据可比的独立新/长会话样本。对照 CE0 基线统计未命中 Token 中位数/P95、总输入、Provider 延迟、知识刷新耗时和 Verified Task Success Rate。长会话未命中中位数目标先定为下降至少 30%；若工具能力、Revision 证据、审批/恢复/回滚或回答质量退化，则不通过 CE 门。

### 固定长会话种子的手动 A/B 夹具

`Scripts/TestAgentCacheAB.ps1` 只负责测试数据的准备、恢复和报告，不驱动桌面窗口，也不发送模型请求。`Init` 将已有长会话复制到 `Projects/PicoSandbox/Saved/Agent/ABTests/<CaseName>/seed.jsonl`，并创建新的测试会话 ID；原会话、场景与凭据不被改写。`Reset` 必须在 Editor 关闭后运行，每轮用同一份种子重建**同一个**测试会话并设置 `CompactPriorTaskHistory`。同一时刻只保留一个测试会话，避免 ON 分支作为跨会话 Episode 进入 OFF 分支。`Capture` 保存该轮 JSONL 和 Metrics，`Compare` 检查工具轨迹与 Token，`Restore` 将测试会话移出会话列表并恢复原 Compact 设置。所有测试数据在本地 `Saved` 下，不提交仓库。

每组都从仓库根目录执行，且仅在 Editor **关闭后**运行脚本；交替使用 On-first / Off-first 顺序：

1. `./Scripts/TestAgentCacheAB.ps1 -Action Init -CaseName <id> -SeedSession ./Projects/PicoSandbox/Saved/Agent/Sessions/<seed>.jsonl -Prompt '<固定提示词>'`，随后 `-Action Reset -CaseName <id> -Variant On`（或 Off）。
2. 打开 `Build/Debug/PicoEditor.exe` 与 `Projects/PicoSandbox/PicoSandbox.pico`。在 AI Chat 选择该组测试会话，确认 Provider/模型和 Compact 状态；只发送一次固定提示词，等待完成后关闭 Editor。
3. `-Action Capture -CaseName <id> -Variant On`（或 Off），再 `-Action Reset -CaseName <id> -Variant Off`（或 On）。重复上一步，确保两轮的项目、会话种子和提示词相同。
4. 采集第二轮，执行 `-Action Compare -CaseName <id>`；人工核对两份回答的资产、Revision 与场景证据，最后 `-Action Restore -CaseName <id>`。

`Compare` 会显示实际 Compact 值、步数、工具序列、输入/命中/未命中 Token、Provider 耗时与 Run ID，并在工具路径、参数或世界文件不一致时警告。`Capture` 读取 Run Metrics 中发送时固定的 Compact 值并核对 ON/OFF 标签，不用采集时的复选框状态或投影条数猜测；旧版本没有该字段的 Run 无法作为新 A/B 样本采集。它还会核对 Provider/模型。仍需人工确认回答证据和场景状态；一次先 ON 后 OFF 的对照会受服务商尽力而为的缓存和顺序效应影响，只用于冒烟检查。正式归因需要至少五组、交替 ON/OFF 顺序、相近工具轨迹与质量的样本。其他机器或新的测试组可用 `./Scripts/TestAgentCacheAB.ps1 -Action Init -CaseName <id> -SeedSession <jsonl> -Prompt <text>` 建立独立夹具。

### 2026-09-24 验收进度

- `pair1` 因开关与发送时序无法可靠确认而作废。`pair2`（On-first）和 `pair3`（Off-first）都从同一长会话种子出发，World 哈希不变，均调用相同的 4 个只读工具及参数；回答覆盖 Cow_2 组件、网格、材质、贴图和位移。`pair2` 使用旧版 Metrics，未记录本轮 Compact 实际值，只作为参考；`pair3` 由 Run Metrics 明确确认 On/Off。
- `pair2`：On 18,857 / Off 141,407 未命中 Token，下降 86.7%；`pair3`：On 14,867 / Off 141,377，下降 89.5%。这些是单组结果，不代表至少五组后的中位数，也未证明其他任务质量。
- 两组四次冷启动的首步 `knowledge_refresh_us` 为 2.37～2.81 秒；后续步为 0，因为刷新只在 Send 时发生。尚无同进程无变更连续查询与 CE2 前的同条件对照，不能据此声称知识刷新端到端收益已通过。
- `pair4`（On-first，Cow_1）：Run Metrics 明确记录 On/Off，World 哈希不变，4 个只读工具及参数完全一致；两份回答均依据材质 Descriptor 正确指出基础色贴图、无法线/高度贴图、无顶点位移。On 23,910 / Off 172,152 未命中 Token，下降 86.1%；Provider 耗时 7.25 / 10.97 秒。至此严格在线对照为 **2 组**（`pair3`、`pair4`），仍不能宣称达到五组质量门。
- `pair5`（Off-first，Rock_1）：首次仅输入 `Rock_1` 的 Off 轮被脚本拒绝并单独归档，不计入对照；从原始种子重测后，On/Off 均有明确开关记录，4 次只读调用及参数一致，World 哈希不变。两份回答依据本轮 Descriptor 正确覆盖基础色贴图、法线/高度贴图和顶点位移。On 19,413 / Off 176,925 未命中 Token，下降 89.0%；Provider 耗时 7.49 / 12.38 秒。严格在线对照累计 **3 组**（`pair3`～`pair5`）。
- `pair6` 原拟以 PhysicsCrate 做不同目标；用户决定不再重复同类 A/B 后，在发送任何消息前归档，未计入样本。三组严格样本的 On/Off 未命中 Token 中位数分别为 19,413 / 172,152，按中位数计算下降 88.7%；样本量和任务类型仍不足以满足原定五组正式门槛。这足以支持继续采用可回退的 CE1 Compact 默认值，但**不等于 CE0～CE2 质量门全部通过**。
- 2026-09-25 重启正式采样后，`pair6b`（PhysicsCrate）On/Off 分别为 19,226 / 91,255 未命中 Token，但 On 只查引用路径、Off 另查网格与材质内部，工具 3/4 次且回答深度不同，**不计入**。随后 `pair6c` 明确要求四份本轮证据：On/Off 分别为 22,052 / 91,951 未命中 Token，均调用相同的四个只读工具及参数、回答覆盖同一事实；但对象与资产查询的顺序不同，当前严格脚本警告工具路径不匹配，仍**不计入**。这两组证明仅比较 Token 容易掩盖任务深度或轨迹差异，不能为凑足五组放松已定判据。
- 后续不为凑数重复当前只读查询。`pair6d` 仅初始化、未发送消息，已归档且不计入样本。原定五组在线样本门槛及其未满足的事实保留在记录中；后文的风险接受决议明确说明本次为何允许先进入 S4。

### CE2 同进程刷新核验（2026-09-25）

在同一个 Debug PicoEditor 进程、同一项目和会话内连续发送三次只读请求；`StarterWorld.pworld` 的 SHA-256 始终为 `247A655D272FAF0E8B162C5FF205ADC9A4F08B46E6FD4DA35853B9AD3635CC37`。每轮耗时取该 Run 首个 `Model.Generate` Trace 的 `request_profile.knowledge_refresh_us`，不是模型网络耗时。

| 轮次 | 选中项与证据 | 知识刷新 | Snapshot/审计 |
| --- | --- | ---: | --- |
| 首轮 | `editor.selection.describe` 返回 `StarterWorld` | 1.740 秒 | 当前会话从跨会话 Episode 中排除，索引更新，审计删除两条旧 Episode |
| 同选中项暖轮 | 再次返回 `StarterWorld` | 0.268 秒 | `index.json` 哈希、写入时间和 `audit.jsonl` 长度均不变 |
| Selection 变化轮 | `editor.selection.describe` 和 `editor.object.describe` 返回 `Cow_1`、组件与材质引用；Selection revision 1→2 | 0.316 秒 | 索引更新，审计删除旧 Selection 并写入新 Selection |

这证明无变化来源没有重复落盘，Selection 变化能够使证据失效并更新；刷新耗时暖轮较首轮低约 84.6%。首轮还包含会话 Episode 去重等冷启动工作，**不是**旧实现与新实现的受控性能 A/B，不能据此声称 CE2 端到端收益已被严格归因。第三轮回答的当前对象事实正确，但附带“与上一轮一致”的跨轮比较错误（上一轮实际为 `StarterWorld`）。随后在任务边界投影中明确标识先前工具证据可能过期、当前任务读回优先，增加 `StarterWorld`→`Cow_1` 的 Selection 回归夹具；离线夹具通过，但真实模型是否不再作错误跨轮比较仍待复测。

另增 `PicoAgentTests` 的同进程 `ReplaceSource` 基准：48 条约 2 KB 的记录，12 次交替测量。2026-09-25 Debug 本机结果：无变更路径中位数 1.81 ms，变更路径中位数 16.64 ms，约 9.2 倍；无变更轮索引写入时间与审计长度不变，变更轮审计增长，重载后 Revision 正确。原始逐次结果写到 `%TEMP%/PicoAgentTests/KnowledgeFastPathBenchmark.txt`。这只对比**当前实现的无变更与必须刷新路径**，没有旧版同条件构建，也没有 Provider 网络和整轮 Agent 耗时，不能视为 CE2 端到端收益归因。

### 真实 Editor 审批与撤销核验（2026-09-25）

在 Debug PicoEditor 的**未保存新 World `GameWorld`** 中执行，未对 `StarterWorld` 做测试性修改。`StarterWorld.pworld` 的 SHA-256 在各阶段均为 `247A655D272FAF0E8B162C5FF205ADC9A4F08B46E6FD4DA35853B9AD3635CC37`。

| 阶段 | Run / 结果 | 核验 |
| --- | --- | --- |
| 拒绝 | `run_1790305906858257_35`：`editor.world.describe` 返回 0 Actor；`editor.actor.spawn` 在真实 UI 点击“拒绝”后返回 `ApprovalRejected` | Operation Journal 记录 `User denied tool call`，无 `state_changes` / `revision_changes`；下一轮只读 World 仍为 0 Actor |
| 批准及读回 | `run_1790306049657166_47`：再次只读确认 0 Actor 后，点击“批准一次”，`editor.actor.spawn` 成功创建 `GameWorld.PersistentLevel.CEGateApprovedCube` | 随后两次 `editor.object.describe` 读回 Actor `PActor`、根组件 `PCubeComponent` 及属性；未调用保存工具 |
| 编辑器 Undo | 通过 **Edit > Undo** 撤销一次，`run_1790306228039173_69` 的 `editor.world.describe` 返回 0 Actor / 0 组件 | 测试方块不存在，原 `StarterWorld.pworld` 未变 |

上述 UI 验收时离线 `PicoEditorTests` 174/174 与 `PicoAgentTests` 206/206 同时通过；新增 Selection 投影夹具和 Knowledge 快路径基准后，`PicoAgentTests` 为 208/208。离线测试分别覆盖拒绝零副作用、事务 Undo、Revision 冲突、Run Revert、Checkpoint/故障恢复等确定性路径。上述真实 UI 用例验证了审批与 Undo，但**未实测编辑器崩溃后的交互式恢复**，不可把离线故障注入等同于真实重启演练。此前用户确认维持原定**至少五组**严格在线 A/B 的正式门槛；截至本次决议仍只有三组。跨轮比较的真实模型复测、CE2 旧/新同条件端到端归因也尚未完成。

### 阶段验收决议（2026-09-25）

用户明确要求停止为凑齐五组而继续重复提示词实验，并决定直接验收。**本次为有记录的风险接受，不是将三组改称五组，也不是证明了完整端到端因果收益。**据此，CE0～CE2 的当前实现作为 S4 前的工程阶段门放行；不继续用强制工具调用顺序的提示词制造匹配样本。

- 接受依据：三组严格匹配的真实 Provider A/B 均有相同只读工具与参数、世界文件不变、回答证据可比；On/Off 未命中 Token 中位数为 19,413 / 172,152。另有同进程 Selection/Knowledge 刷新核验、Knowledge 快路径隔离基准、离线 `PicoAgentTests` 208/208 和 `PicoEditorTests` 174/174，以及真实 Editor 的审批拒绝、批准读回与 Undo。
- 不计入依据：`pair6b` 的 On/Off 查询深度不同；`pair6c` 虽然工具与参数集合相同，但调用顺序不同；`pair6d` 未执行。三者均不能补足原定五组。
- 保留风险：Provider 缓存为尽力而为，三组样本不足以给出稳定的跨任务 P95 或端到端因果结论；CE2 缺旧/新构建同条件对照；跨轮 Selection 的错误比较只有离线防回归、尚无真实模型复测；编辑器崩溃后恢复未做真实 UI 演练。
- 放行边界：允许开始 S4 资产预览，CE1/CE2 保留现有回退能力；若真实任务出现必要工具/资产证据缺失、无依据的当前状态断言、审批/回滚退化或明显延迟反弹，应重新打开此质量门并修复，不把风险转嫁为 S4 已完成。

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
