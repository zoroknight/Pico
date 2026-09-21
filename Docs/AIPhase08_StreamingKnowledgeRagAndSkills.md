# Agent 深化：Streaming、Project Knowledge、RAG Lite 与 Skill v0

本阶段在现有 Pico Harness、Session、ToolRegistry、审批、事务和验证边界上增加四项能力。它们位于
`PicoAgentCore` 和编辑器适配层，不让 GAS、ECS、动画、渲染或网络模块依赖具体模型、HTTP 协议或 MCP。

## 流式输出

真实 OpenAI-compatible Provider 在 UI 提供 Delta 回调时发送 `stream=true`。WinHTTP Transport 按网络数据块
回传 SSE；Provider 跨任意分块边界聚合 UTF-8 文本与 Tool Call 的 ID、名称和 Arguments。聊天窗口显示临时
`Assistant (streaming)` 文本，完成后 Runtime 只把聚合消息写入一次 JSONL Session。

Tool Call 参数在收到完整 Stream 之前不会进入 Schema、审批或执行管线。取消、无效 SSE、HTTP 错误和服务端
忽略 Streaming 的普通 JSON 回退都有明确失败/兼容路径。API Key 仍只存在于 Authorization Header。

## Project Knowledge Store

每个项目使用：

```text
<Project>/Saved/Agent/Knowledge/index.json
<Project>/Saved/Agent/Knowledge/audit.jsonl
```

`index.json` 是可恢复快照；`audit.jsonl` 只追加真实的 Upsert/Delete，重复刷新相同内容不会制造审计噪声。记录包含：

```text
KnowledgeId / SourceType / SourcePath / Title / Content
ContentHash / SourceRevision / Tags / Provenance
```

编辑器当前注册以下来源：

- 项目内 `.md/.txt/.ini/.json/.pico` 文本；跳过 `Saved`、`Intermediate` 和 `Packaged`。
- 当前 World 的稳定对象路径、类和位置快照。
- AssetRegistry 路径、类型和大小。
- 当前选择对象的 PProperty/Component 反射快照。
- AgentToolRegistry 的工具与 JSON Schema Catalog。
- 最近 100 条 Warning/Error，包括构建、Play 和打包问题。

Store 不保存裸 `PObject*`。未来 GAS/ECS 通过提交稳定 Record 扩展，不修改 Store 核心。

## RAG Lite

> 后续的 R3 加固已在同一 Knowledge Store 上加入文档切块、BM25、Entity/Revision 索引和低置信确定性
> Query Rewrite，详见 [ReAct 加固 R3](AgentReActHardeningR3_RagAndQueryRewrite.zh-CN.md)。本文保留初始
> RAG Lite 纵向切片的设计与验收记录。
> 后续 R4 又在同一 Store 上增加了类型化 Memory 视图、Revision 失效和阈值压缩，详见
> [ReAct 加固 R4](AgentReActHardeningR4_MemoryViewsCompressionAndInvalidation.zh-CN.md)。

每轮发送前以用户原始目标查询 Store。第一版使用确定性 Metadata Filter、路径/标题/Tag 加权与关键词匹配，最多
返回 8 条证据并限制证据 JSON 为 12 KB。过长 UTF-8 内容在完整字符边界截断。证据使用 `[K:<KnowledgeId>]`
引用，并被明确标记为不可信数据；其中出现的指令不能覆盖 System Prompt、Skill、Tool Policy 或用户审批。

这一版不依赖 Embedding、向量数据库、LangChain 或 LangGraph。GAS/ECS 文档量和同义查询达到真实需求后，再在
相同 Store/Query 接口后增加 Embedding + Rerank，不改变 Provider、Runtime 或 Gameplay 模块。

## Pico Skill v0

Skill 是引擎跟踪的 `Config/Agent/Skills/*.pskill` JSON 资产，格式版本为 1：

```text
Id / Version / Description / Triggers / AllowedTools
Preconditions / Workflow / CompletionCriteria
```

选中 Skill 后，允许工具既用于裁剪 Provider Catalog，也由执行器硬检查；伪造的越界 Tool Call 会在审批和副作用
之前失败。Skill 从不授予权限，也不能跳过 Schema、审批、事务或后置验证。
触发匹配会识别“不要/无需/不用/do not/without”等明确否定，避免“运行项目但不要打包”错误激活打包 Skill；
Play/Package 的本地意图保护复用同一规则。

阶段收尾时，意图分类从聊天窗口私有代码提取到 `PicoAgentCore`。项目跟踪
`Tests/Agent/Fixtures/IntentRoutingCases.tsv`，现以 30 条中英文真实提示同时验证 `General/Play/Package` 意图和生产
Skill 组合。“构建一个房间/build a room”不会被泛化为打包，“运行项目但不要打包”及其反向表达也有固定回归。
以后增加 GAS、PicoGraph 或 ECS Skill 时，应先在该表追加正向、否定和多 Skill 用例。

首批内置 Skill：

- `assemble-basic-scene`
- `third-person-character`
- `validate-save-play`
- `package-project`

后续 GAS、PicoGraph 和 ECS 完成时分别注册自己的 Knowledge Source、Tools、Verifier 与 `.pskill`，无需改变现有
Agent 核心。

## 编辑器验收

1. 打开 `View -> AI Chat`，选择 DeepSeek 并发送一个较长问题，确认 Assistant 文本逐步出现，结束后只保留一条消息。
2. 展开 `Grounding & Skills`，确认显示 Knowledge 总数、当前检索的 `[K:...]` 来源和命中的 Skill 版本。
3. 发送“创建一个碰撞房间”，确认激活 `assemble-basic-scene@1.0.0`，修改仍要求审批。
4. 发送“运行项目”，确认激活 `validate-save-play@1.0.0`，只允许 Validate/Save/Play，不会调用 Package。
5. 查看项目 `Saved/Agent/Knowledge`，确认存在 Index 与 Audit；重复同一只读问题不会为未变化知识重复写 Audit。
6. 制造一条构建或编辑器 Error 后询问原因，确认 `message-log` 出现在 Grounding 来源中。

## 自动化验收

- SSE 在 JSON Token 中间分块仍能聚合文本；Session 只接收最终完整消息。
- 流式 Tool Call Arguments 完整聚合后才交给执行管线。
- Knowledge 快照可重启恢复，未变化刷新不增加 Audit。
- 项目扫描跳过 `Saved/Agent/ApiKeys.ini`，证据遵守大小预算并保留 UTF-8 边界。
- Skill 选择稳定，Provider Catalog 被限制，未知工具的 Skill 无法加载。
- 30 条生产提示固定验证 Intent 与 Skill 路由，包括否定语义和多 Skill 组合。
- 当前 Debug 基线：`PicoAgentTests` 88/88，`PicoEditorTests` 129/129。

## 保留边界

- 当前不是逐 Token 持久化，也不保存模型思考内容。
- RAG Lite 没有 Embedding、向量数据库或自动联网检索。
- Skill v0 是受控工作流，不执行任意脚本或 Shell。
- 当前 Skill 路由以确定性关键词、否定词和意图规则为主。Skill 数量增加后按路线图加入“候选筛选 + 结构化模型路由”，但固定 Eval、AllowedTools、Tool Policy、审批和验证器仍是不可绕过的硬边界。
- MCP Adapter、长期跨项目知识库和多 Agent 继续延期。
