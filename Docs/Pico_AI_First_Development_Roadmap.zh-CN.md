# Pico AI 优先后续开发路线

本文档记录 Pico 在完成第 6 月网络学习型 MVP 后的新主线。8 周工程深度阶段已经完成并形成可复现基线；
碰撞与通用 Actor Replication 纵向切片以及 MCP 四周纵向切片均已完成；MCP 现作为可关闭、可替换的本地
Development Editor 基线保留。
当本文档与 [`Pico_Remaining_Development_Roadmap.zh-CN.md`](Pico_Remaining_Development_Roadmap.zh-CN.md) 中的
未完成排期冲突时，以新路线为准。旧路线继续保留已经完成的架构基线、风险记录和历史决策。

计划中的“月份”均指项目开发月份，不是自然月。已经完成的对象系统、编辑器、Gameplay、物理、动画、
打包和网络主链不再重复列为新任务。

## 调整原因

Pico 已经通过本机多进程和两台真实 Windows 电脑验证 UDP、Replication、RPC、Ownership、客户端预测、
服务器纠错和模拟代理平滑。网络核心学习目标已经成立，下一阶段不再以公网、重连和回放为主线。

在完成 Agent Harness、Mini GAS 和 PicoGraph Lite 后，项目从功能扩展转入工程深度阶段。后续目标调整为：

1. 学习现代 Agent Harness 的真实组成，而不只调用聊天 API。
2. 让 AI 安全使用 Pico 已有的反射、资产、事务、World、Gameplay、网络和打包能力。
3. 先建立 Profiler、Benchmark、Failure Taxonomy 和 Eval，让性能与 Agent 正确性可测量。
4. 加固 Object/Tick/GC/Replication 的扩展性，并解除 Agent 通用层与项目特例的耦合。
5. 工程深度阶段验收后，以“小型双人协作游戏”为北极星恢复 Agent 游戏搭建；先建立资产理解和可靠生产链，
   再依次推进渲染架构、AI 视觉资产、受控代码生产，ECS 与 Vulkan 继续由真实需求和准入门槛驱动。

## 新实施顺序

```text
网络核心收尾
 -> PicoTask / Game Thread Dispatcher
 -> Pico Agent Harness Lite
 -> 编辑器场景 Agent
 -> Mini GAS / AbilityTask
 -> PicoGraph Lite
 -> Profiler + Runtime Scalability（已完成工程基线）
 -> Agent Reliability + Decoupling（已完成工程基线）
 -> 通用 Actor Replication 与碰撞语义收尾（已完成）
 -> Pico MCP 四周纵向切片（已完成）
 -> 阶段 A 第 1～4 周：资产理解、规格、Build Plan 与恢复（已完成）
 -> ReAct 轻量化加固门（R1～R5 已完成）
 -> 资产理解与安全创作 S0～S3（已完成）
 -> Agent 输入缓存与长会话上下文减重门 CE0～CE2（S4 前，待实施）
 -> 资产理解与安全创作 S4～S6（待实施）
 -> 阶段 A 第 5～8 周：玩法积木、Graph、组装与真实验收
 -> 阶段 C：Render Architecture（提前，四周）
 -> 阶段 D：AI 视觉资产生产（两周）
 -> 阶段 E：受控 Code Harness（四周）
 -> 阶段 A.2：第二玩法综合验收（两周）
 -> PicoResearchKit：算法研究扩展（独立专项路线）
 -> 阶段 B：ECS 纵向切片（继续延后）
 -> 阶段 C.2：Vulkan Backend（通过 RHI 准入后）
```

固定优先级为：

```text
Harness 可靠性、上下文效率与资产安全创作
 > AI 双人协作游戏闭环
 > Render Architecture
 > AI 视觉资产与受控 Code Harness
 > ECS / Vulkan / Multi-Agent
```

已完成的 8 周工程基线、性能数据和取舍详见
[Pico 工程深度阶段路线](Pico_Engineering_Depth_Roadmap.zh-CN.md)。容器全面重写继续延期；后续仍不得以旧月份
排期为理由同时启动 ECS、Vulkan、更多 GAS、Multi-Agent 或不受控 Code Agent。

对象语义、Semantic Skill、World Model、Planner、AI Companion 和后续 Learned Motion 的研究工作统一遵循
[Pico 算法研究扩展落地路线](Pico_Algorithm_Research_Extension_Roadmap.zh-CN.md)。研究代码以可选扩展维护，不得
把 Python 训练依赖、实验资产或特定算法实现写入 Pico Runtime 默认构建。

## AI 架构决策

### 进程与信任边界

```text
PicoEditor（可信）
  -> Agent Chat Workspace
  -> AgentToolRegistry
  -> Permission / Approval Policy
  -> Editor Transaction / Undo
  -> Validation
  -> Local IPC

PicoAgentHost（半可信独立进程）
  -> IAgentRuntime
  -> IAIProvider
  -> Session Event Log / Checkpoint
  -> Context Builder
  -> Plan -> Tool -> Verify Loop
```

- 模型只提出结构化 Tool Call，不直接获得 `PObject*`、World 内存或任意进程执行能力。
- 所有对象创建、反射属性修改、World 替换和 UI 更新都回到 Game Thread。
- AgentHost、模型请求或第三方库崩溃不得带崩 PicoEditor。
- Editor 是工具权限、参数验证、事务和最终副作用的唯一权威。
- API Key 保存到 `<PicoEngineRoot>/Saved/Editor/Agent/ApiKeys.ini`，由同一编辑器副本下的项目共享；该路径被
  Git 与打包排除，并且不向 Agent 工具、Prompt、Session、Trace 或普通日志暴露。

### Harness 范围

第一版自研 `Pico Harness Lite`，重点掌握：

- 可替换 Agent Loop。
- 追加式 Session Event Log。
- Tool Schema、注册、执行和结果回传。
- Checkpoint、恢复、取消和幂等。
- 权限、人工审批、预算和审计。
- Context Engineering、错误修复和自动评测。

参考成熟 Harness 时只吸收机制，不复制完整通用平台。DeepSeek Harness 主要参考可替换 Loop、Event-Sourced
Session、受保护 Tool Pipeline 和能力隔离；Codex Harness 主要参考 Workspace、审批、Sandbox、MCP 与 Skill
进入统一策略层的方式；LangGraph 只在 Pico 原生 Runtime 稳定后用于 Checkpoint 和 Human-in-the-loop 对照实验。

第一版不引入 LangChain 作为核心依赖，不直接嵌入预发布的 DeepSeek Harness，也不实现动态装卸一切能力的
通用插件内核。

### ReAct 轻量化决策

阶段 A 前四周完成后，Agent 架构只沿单一 ReAct Loop 加固，不同时引入 Direct、Plan-Execute、GoT、Multi-Agent
或模式路由器。Build Plan/DAG 保留为复杂任务的结构化 Task State 与审批产物，不建立第二套执行 Runtime。
Task State、Context Assembler、Memory/RAG、Query Rewrite 和 Reflection 均采用轻量、按需和可关闭设计；简单
任务不得因此增加模型轮次。Harness 继续独占 Validate、Permission、Approval、Transaction、Execute、Verify、
Journal 和 Game Thread 语义，Agent 层只消费结构化事实并判断是否推进用户目标。

详细依赖边界、五周任务、性能门槛和明确延期项见
[Pico Agent ReAct 轻量化加固路线](AgentReActLightweightHardeningRoadmap.zh-CN.md)。MCP 继续使用已完成的本地
Streamable HTTP；stdio 暂不实现。

### 接口边界

只在已有第二种实现或明确替换需求的边界建立接口：

```text
IAIProvider
IAgentRuntime
IAgentSessionStore
IAgentToolProvider
IAgentContextProvider
IAgentSandbox
```

推荐实现：

```text
IAIProvider
  -> FakeAIProvider
  -> DeepSeekProvider
  -> KimiProvider

IAgentRuntime
  -> PicoMinimalAgentRuntime
  -> LangGraphRuntime（可选实验）
```

### 工具安全等级

只读工具可自动执行：

```text
ListClasses
DescribeClass
InspectWorld
FindAssets
GetProperties
ReadMessageLog
ValidateWorld
```

低风险修改工具必须经过参数验证和 Editor Transaction：

```text
CreateActor
AddComponent
SetProperty
AssignAsset
CreateActorBlueprint
SaveWorld
```

高风险工具必须显示参数并由用户批准：

```text
DeleteActor
DeleteAsset
OverwriteWorld
GenerateSourcePatch
BuildProject
RunExternalProcess
PackageProject
```

第一版禁止提供 `ExecuteShell(string)`、任意内存写入和无范围文件写入。权限策略只能逐步收紧，任何后置
策略不得推翻前置拒绝。

### Skill、RAG、MCP 与多 Agent

- Skill：建议实现 Pico 原生、版本化、可审核的工作流。Skill 声明说明、允许工具、前置条件和验收标准，
  但不能自行提升权限，也不能默认执行任意脚本。
- RAG：先实现反射、AssetRegistry、当前 World、选中对象、文档、Message Log 和构建错误的确定性检索；
  语料规模证明需要后，再增加关键词与 Embedding 混合召回。检索内容始终是不可信数据。
- MCP：参考 UE 实验性 MCP 插件的 Toolset 与本地 Server 组织方式，在共享执行服务稳定后增加本地
  Streamable HTTP Server Adapter。MCP 是互操作协议，不是 Agent Runtime 或安全边界；Pico 内部架构不得
  依赖 MCP，内置聊天也不得绕过统一 Harness 去调用另一套工具链。
- LangGraph：后期通过 `IAgentRuntime` 增加可选后端，使用相同工具和评测集比较，不作为第一版前置依赖。
- 多 Agent：主线暂不实现。一个 Agent、确定性工具、验证器和有限修复循环足以完成首个 Demo。
- Memory：从第一版保存显式 Session Event 和摘要，不使用不可审计的供应商隐式记忆作为事实来源。

## 已完成基线：Pico MCP 四周纵向切片

### 定位与时机

本阶段在通用 Actor Replication、碰撞 Profile 与网络语义收尾后开始，优先于 AI 完整游戏搭建、ECS、复杂渲染、
更多 GAS、Multi-Agent 和 Code Agent。底层容器全面替换没有被重新提上日程；只有 Profiler 和固定 Benchmark
证明现有容器形成真实瓶颈时，才建立单独优化任务。

Pico MCP 的目标不是再造一个 Agent Harness，也不是让外部客户端直接获得编辑器权限，而是把已经存在的工具、
审批、事务、验证和日志能力以标准协议安全地开放给 Codex、Claude、MCP Inspector 等外部客户端。参考 UE 的
方向是“嵌入式本地 Server + Toolset”，Pico 保留自身 C++ Harness、反射、Game Thread 和编辑器事务边界。

```text
外部 MCP Client
  -> 本地 Streamable HTTP
  -> Pico MCP Server
  -> Toolset Adapter
  -> FEditorAgentExecutionService
  -> AgentToolRegistry
  -> Validate / Permission / Approval / Transaction / Execute / Verify
  -> Game Thread
```

`FEditorAgentToolExecutor` 是底层工具入口；Game Thread 投递、交互审批、Operation Journal 和异步操作等待已在
第 1 周提取到共享执行服务。内置聊天和后续 MCP Adapter 都依赖该服务。禁止让 MCP 直接调用 Editor Executor、
复制第二套审批管线或绕过 Game Thread。

### 第 1 周：共享执行服务

状态：**已完成**。实现与自动化验收见
[MCP 第 1 周：共享 Editor Agent 执行服务](McpWeek01_SharedEditorAgentExecutionService.zh-CN.md)。

任务：

- 新增 `FEditorAgentExecutionService`，统一 Game Thread Dispatch、Approval Queue、Operation Journal、取消、
  Trace、并发控制以及 Play/Package 等异步操作的最终完成等待；
- 为一次外部调用分配稳定的 `SessionId/RunId/ToolCallId`，并与现有 Event Log、Journal 和 Tool Result 对齐；
- 将内置 DeepSeek/Kimi Chat Workspace 迁移到共享服务，UI 只负责会话展示、用户审批和取消，不再拥有执行语义；
- 为只读并发、修改串行、取消和编辑器关闭增加确定性测试。

周末验收：内置聊天功能与审批体验无回归；同一 Tool Call 在 UI 与无 UI 测试入口得到相同结构化结果；任何
编辑器对象修改只发生在 Game Thread；关闭会话或编辑器后不会留下延迟副作用。

### 第 2 周：传输无关的 MCP Core

状态：**已完成**。实现与自动化验收见
[MCP 第 2 周：传输无关协议核心](McpWeek02_TransportIndependentCore.zh-CN.md)。

任务：

- 新增独立 `PicoMcpCore`，实现 JSON-RPC 2.0 请求、响应、Notification、批次拒绝策略和结构化错误；
- 以当前 `2026-07-28` 无状态规范为主，实现每请求 `_meta`、`server/discover`、`ping` 和取消；同时提供有界的
  `2025-11-25` `initialize` / `notifications/initialized` 兼容入口；
- 实现最小工具协议：`tools/list` 与 `tools/call`，并对请求体、参数深度、字符串长度、超时和会话数设置上限；
- 以 Fake Transport 覆盖乱序、重复 ID、未知方法、非法 JSON、取消、超时和 Session 关闭，不在本周接真实端口。

周末验收：MCP Core 不依赖 Editor、HTTP 或具体 Tool；协议错误稳定映射为 JSON-RPC Error；工具执行失败保留为
MCP Tool Result 的 `isError=true`，不会错误地变成传输失败。

### 第 3 周：Toolset Adapter 与安全闭环

状态：**已完成**。实现与自动化验收见
[MCP 第 3 周：Toolset Adapter 与安全闭环](McpWeek03_ToolsetAdapterAndSafety.zh-CN.md)。

任务：

- 将现有 `IAgentCapabilityProvider` 组合为可版本化 Toolset，第一版默认只暴露
  `list_toolsets`、`describe_toolset`、`call_tool` 三个元工具；
- 将 `FAgentToolSchema` 映射为 MCP `inputSchema`，将 `FAgentToolResult` 的 Facts、Artifacts、Diagnostics、
  StateChanges、RevisionChanges、RecoveryHint 和 Trace 映射为结构化结果；
- 读写调用继续经过 Validate、Permission、Approval、Transaction、Execute、Verify 和 Journal，不把 MCP 当作
  身份认证或权限边界；
- 默认不自动暴露所有 `PFunction`。新增能力仍需 Capability Provider、Schema、权限、Verifier 和 Eval；
- 为只读世界查询、属性修改、拒绝审批、Undo/Redo、未知 Toolset 和 Prompt Injection 增加固定测试。

补充实现决策：JSON-RPC ID 不作为持久化幂等键；默认调用分配新 ToolCall ID，需要重试恢复时由客户端显式提供
`operation_id`。MCP 取消通过外部取消查询桥继续传入 Agent 与共享 Editor 执行服务。

周末验收：外部调用与内置聊天共享完全相同的工具结果和副作用规则；审批拒绝时 Revision 不变且零副作用；批准
后可通过 Undo 撤销；Toolset 可以独立启停而不修改 MCP Core。

### 第 4 周：本地 Streamable HTTP 与真实验收

状态：**已完成**。实现、安全边界、自动化和真实客户端验收见
[MCP 第 4 周：本地 Streamable HTTP 与真实验收](McpWeek04_LocalStreamableHttpAndAcceptance.zh-CN.md)。

任务：

- 通过隔离的成熟 HTTP Server 依赖实现 Streamable HTTP，不手写 HTTP Parser；实现本地 MCP Endpoint 的
  `POST`、现代每请求协议 Header、事件投递、取消和有界输出；仅旧协议兼容路径保留有界 Session；
- 在 Editor Settings 中提供启动/停止、端口、Endpoint、已连接 Session、复制客户端配置和诊断状态；
- 默认关闭且只绑定 `127.0.0.1`，校验 `Host/Origin`，使用保存在 Git 忽略本地配置中的随机 Bearer Token；
- API Key、Credential、本地私有配置和任意文件系统内容不得作为 Resource 或 Tool Result 暴露；
- 使用 MCP Inspector 和至少一个真实外部客户端完成查询 World、审批修改、Undo、取消、Play/Package 最终结果
  回传的端到端验收。

周末验收：Server 关闭时没有监听端口和持续帧开销；默认客户端只看到三个元工具；中文审批可用；请求取消或
Transport 断开后未执行修改不会迟到生效；Play/Package 返回最终完成状态而非“已启动”；内置 DeepSeek/Kimi
行为无回归。

### 固定安全边界

- 第一版仅用于本地 Development Editor，不进入 Shipping Runtime，不支持公网、局域网或远程 Host；
- 修改操作串行执行，只读操作也必须遵守快照、Revision 与 Game Thread 访问规则；
- 限制请求大小、并发请求、旧协议 Session、单调用时间、返回结果和 Artifact 生命周期，所有取消必须可审计；
- MCP Token 与 Provider API Key 分离，均不得提交 Git；外部客户端不能查询、替换或打印 Provider API Key；
- MCP Adapter 只做协议映射，安全判断归现有 Harness。任何新客户端都不能获得比内置聊天更高的权限；
- UE MCP 仍属于实验性参考，因此 Pico 的协议层、HTTP 层和 Editor Adapter 必须独立模块化，可单独替换或关闭。

### 明确不做

- MCP Client、Sampling、Multi-Agent 协调、远程公网 Server、OAuth、多租户和云端部署；
- 自动把全部反射函数变成工具、模型任意执行 Shell、绕过审批的批量写入；
- 用 MCP 替换 Harness、Skill、RAG、Tool Registry、事务、验证器或 Project Knowledge Store；
- 第一版不扩展完整 Resources/Prompts。后续若有真实需求，只优先把 Project Knowledge Store 作为只读、可审计
  Resource 暴露。

### 综合完成门槛

1. Server 默认关闭，关闭时没有监听、后台线程和可测量的持续帧成本；
2. MCP Inspector 与真实客户端均能完成现代 `server/discover`（或旧版兼容初始化）、列举 Toolset、查询 World
   和调用受控工具；
3. 修改工具必须触发中文审批，拒绝零副作用，批准后可验证并可 Undo；
4. 工具执行保持 Game Thread 正确性，请求取消或 Transport 关闭后不会产生迟到修改；
5. Play/Package、错误、Artifact、Trace 和 Revision 使用结构化结果返回，不依赖解析自然语言；
6. 敏感配置不会进入 Tool Schema、Resource、日志或响应；
7. MCP Core、Adapter、安全故障注入、真实 Editor Smoke 和现有 Agent Golden Tests 全部通过。

## 第 7 月：Pico Agent 基础

目标：建立安全、可恢复、可测试的 Agent Harness，让 AI 能操作 Pico 编辑器中的场景和资产，但暂不承诺
生成复杂玩法逻辑。

| 周次 | 任务 | 周末验收 |
| --- | --- | --- |
| 第 1 周（已完成） | `PicoTask` Worker Pool、Game Thread Dispatcher、任务取消/状态/异常/安全关闭和 Fake 异步结果回投 | Worker 与 Game Thread 分离；回调按帧预算执行；退出无残留线程；Debug/Release 测试通过 |
| 第 2 周（已完成） | `PicoAgentCore`、`PicoAgentHost` 文件协议、Agent 接口、Fake Provider、状态机、追加式 Event Log、Session 保存/恢复、Checkpoint、预算、幂等和有限修复 | Fake Provider 驱动确定流程；重启可恢复会话；重复 ToolCall 不产生重复副作用；任务可取消；不完整日志尾部可恢复 |
| 第 3 周（已完成） | `AgentToolRegistry`、确定性 JSON Schema Catalog、Validate -> Permission -> Approval -> Transaction -> Execute -> Verify 管线；首批 2 个只读和 2 个场景修改工具 | 非法类型、越界路径、权限拒绝和用户拒绝零副作用；验证失败自动回滚；Agent 修改进入原有 Undo；Trace 持久化 |
| 第 4 周（已完成） | AI Chat Workspace、Tool Call/Approval/Trace UI、DeepSeek/Kimi Provider、超时和限流恢复、场景 Agent 验收 | 中文创建和修改场景，操作可查看、审批、撤销，会话可保存和恢复 |

Agent 状态机固定为：

```text
Idle
 -> Planning
 -> AwaitingApproval
 -> ExecutingTool
 -> Validating
 -> Repairing
 -> Completed / Failed / Cancelled
```

第 7 月最终验收指令示例：

```text
创建一个地面、两面墙、一个可推动箱子、一个角色和一个点光源，
配置好材质后保存场景并运行。
```

验收要求：模型离线、超时、输出非法 JSON、调用未知工具、重复调用、用户拒绝或编辑器重启均不能损坏项目。

第 1 周实现与线程规则见
[`AIPhase01_PicoTasksAndGameThreadDispatcher.md`](AIPhase01_PicoTasksAndGameThreadDispatcher.md)。
第 2、3、4 周实现分别见
[`AIPhase02_PicoAgentCoreAndSessions.md`](AIPhase02_PicoAgentCoreAndSessions.md) 和
[`AIPhase03_AgentToolPipeline.md`](AIPhase03_AgentToolPipeline.md)，以及
[`AIPhase04_ChatWorkspaceAndProviders.md`](AIPhase04_ChatWorkspaceAndProviders.md)。

第 7 月已经完成安全 Agent 基础闭环，并追加两个纵向切片。通用反射属性工具可发现 Actor/Component 的实际
`PProperty`、当前值和语义元数据，并在一次审批和 Undo 事务中批量修改基础可编辑类型；新增普通 `Editable`
属性不需要再手写工具，详见 [`AIPhase05_ReflectedPropertyTools.md`](AIPhase05_ReflectedPropertyTools.md)。游戏搭建
竖切进一步开放 AssetRegistry 查询、碰撞房间、第三人称角色模板、Gameplay 校验、World 保存、内容型项目创建
和正式打包服务，并补充 Blueprint Actor 创建/删除与真实 Play/Stop。Play 与 Package 使用独立工具，执行器会在
副作用前拒绝和用户原始意图冲突的调用，详见 [`AIPhase06_GameAssemblyVerticalSlice.md`](AIPhase06_GameAssemblyVerticalSlice.md)。
AI Chat 已升级为按 Provider 隔离的多会话工作区，支持新建/删除、重启恢复、最新消息定位、气泡复制和 MD4C
Markdown/格式化 JSON 代码块；长代码块可展开且不抢占聊天滚轮。
Agent 创建项目后已支持受控新进程交接和同一 Session 恢复；Harness 还会发送结构化 Progress Ledger，在相同
StateRevision 下缓存等价只读查询，并以分类预算和连续无进展检测阻止 DeepSeek 重复检查。详见
[`AIPhase07_ProjectHandoffAndLoopControl.md`](AIPhase07_ProjectHandoffAndLoopControl.md)。材质实例创建和任意新玩法
逻辑仍未开放，模型输出也不能直接映射为任意 PFunction。

进入 Mini GAS 前已经追加完成 SSE Streaming、可审计 Project Knowledge Store、确定性 RAG Lite 和 Pico Skill
v0。当前来源覆盖项目文本、World、AssetRegistry、选择对象反射、工具 Schema 与 Message Log；四个内置 Skill
同时裁剪 Provider Schema 并在执行器侧限制工具。该阶段当时延期了 Embedding、MCP 与跨项目长期知识；
本地 MCP Server 现已按本文四周纵向切片重新排入下一阶段，其他两项继续延期。详见
[`AIPhase08_StreamingKnowledgeRagAndSkills.md`](AIPhase08_StreamingKnowledgeRagAndSkills.md)。
Agent 阶段收尾已将 Intent Router 提取到 `PicoAgentCore`，当前已冻结 39 条中英文生产提示评测，覆盖 Play/Package
否定语义、场景/角色 Skill 和多 Skill 组合。后续 Gameplay 模块必须通过扩展该评测集接入 Agent，不能在聊天
窗口中增加另一套临时意图判断。

## Harness 成熟度加固路线

Pico 保留自研 C++ Harness，不为了功能数量迁移到 LangChain/LangGraph。后续重点是把现有纵向链路变得可观察、
可评测、可恢复和可扩展；基础能力已伴随 GAS、PicoGraph 增量完成，进一步解耦与可靠性加固转入当前 8 周
工程深度阶段，不另起一套 Agent Runtime。

| 优先级与时机 | 任务 | 验收标准 |
| --- | --- | --- |
| P0：已完成基础版 | 统一 `SessionId/RunId/TurnId/SpanId`，为 Model、Tool、Approval、Validation 建立父子 Span | Session JSONL 可还原调用顺序、耗时、结果和失败位置；旧日志兼容；编辑器时间线 UI 后续补充 |
| P0：已完成离线基线 | 扩展路由 Eval；建立端到端 Eval Runner 和首批 Golden Tasks；增加原子批量属性/删除与冲突保护的 Run ChangeSet | 确定性测试不访问真实 Provider；生成带 RunId、耗时、计数和证据路径的报告；真实 Editor Fixture 在工程深度阶段加固后进入 Agent 游戏搭建阶段 |
| P0：第 8～9 月伴随主线 | 建立上下文分区预算、摘要快照和大结果 Artifact/Handle，避免把完整日志和 Tool Result 反复送入模型 | 超预算时可解释地裁剪；关键指令、审批状态、引用来源和最近错误不丢失 |
| P0：第 8 月持续 | 完善 Provider 超时、取消、可重试/不可重试错误、指数退避、限流和单 Run Token/费用预算 | Provider 断网、限流或非法响应不会破坏项目，也不会形成无限重试 |
| P0：第 8～9 月伴随主线 | 在 Durable Operation Journal 上增加轻量 `RunCheckpoint`，记录 Skill、已完成 ToolCall、待审批调用、预算和下一步 | 仅从 Tool 边界恢复；先 reconcile 再重试；不承诺从工具函数内部某一行继续 |
| P0：第 9 月 Skill 增长时 | Skill Registry 返回带匹配 Trigger/Tag、分数和排除理由的确定性候选列表 | 候选结果可解释、可固定测试，不增加权限，也不依赖模型或网络 |
| P1：Agent 游戏搭建阶段 | 对歧义候选启用结构化模型路由 Shadow Mode，并扩展到至少 20 个端到端 Golden Tasks | 模型结果先只记录比较；达到固定准确率、误触发和禁止副作用门槛后才处理歧义路由 |

伴随开发规则：

- GAS、PicoGraph、ECS 每新增一组 Tool/Skill，必须同时注册 Knowledge Source、Verifier、路由 Eval 和至少一个端到端任务。
- Trace、Session、Journal、Checkpoint 和 Eval 报告使用同一组 Run/ToolCall 标识，避免形成彼此无法关联的日志系统。
- 离线确定性测试验证 Runtime、Tool Pipeline、Skill、Guardrail 和恢复；真实 DeepSeek/Kimi 测试只验证外部模型行为和协议兼容。
- AI 代码/文件生成正式开放前，先增加固定工作区、路径白名单、Diff 审批、进程边界和生成后构建/测试验证。

明确延期：

- 多 Agent、Handoff 和并行子 Agent：单 Agent 的 Gameplay/资产/构建职责出现可测量瓶颈后再引入。
- LangChain/LangGraph 运行时迁移：只参考 Checkpoint、Interrupt 和 State Graph 思想，不替换 Pico 的 C++/Game Thread 集成。
- MCP Client、远程公网接入、跨项目长期记忆、Embedding/向量数据库：先由真实用例和 Eval 证明需要；本地
  MCP Server 纵向切片按下文四周计划实施。
- 云端 Trace、分布式任务队列、多租户与远程 Worker：本地单用户编辑器阶段不实现。
- 任意 Shell、无约束脚本和模型直接写字节码：不作为成熟度升级方向。

## 第 8 月：Mini GAS 与 AbilityTask

目标：学习 UE GAS 的核心职责和对象关系，为 PicoGraph 的异步节点与 AI 可组合 Gameplay 工具提供稳定能力。
本月实现的是可运行、可验证、可联网的 Mini GAS，不追求一次复制 UE GAS 的全部复杂度。

### 模块与依赖边界

新增独立 Runtime 模块，依赖方向固定为：

```text
PicoGameplayAbilities
 -> PicoEngine
 -> PicoObject
 -> PicoCore
```

- `PicoGameplayAbilities` 不依赖 PicoEditor、Agent、Provider 或 Inspector；编辑器和 AI 只能从外层调用 Runtime API。
- Ability、Effect 和 Attribute 的状态修改只发生在 Game Thread；Worker 只能处理不持有 `PObject*` 的纯数据。
- Class/CDO 保存 Ability 与 Effect 的默认配置；每个角色的运行状态保存在 Spec、Handle 和 Active Effect 中，不能修改 CDO。
- 跨帧、GC、网络和序列化身份使用稳定 Handle、NetId 或对象引用扫描，不长期保存未经跟踪的裸指针。

首批核心类型：

```text
FGameplayTag / FGameplayTagContainer
FGameplayAttributeData / PAttributeSet
PGameplayAbilitySystemComponent
PGameplayAbility / FGameplayAbilitySpec / FGameplayAbilitySpecHandle
PGameplayEffect / FGameplayEffectSpec / FActiveGameplayEffectHandle
PAbilityTask / FGameplayEventData / FPredictionKey
```

### UE5 源码学习对照

| Pico 范围 | UE5 重点参照 | 本月学习重点 |
| --- | --- | --- |
| Tag | `FGameplayTag`、`FGameplayTagContainer`、`UGameplayTagsManager` | 层级 Tag、容器查询和确定性注册 |
| Ability | `UAbilitySystemComponent`、`UGameplayAbility`、`FGameplayAbilitySpec` | CDO 定义与每实例 Spec 状态分离，授予、激活、提交、取消和结束 |
| Attribute | `UAttributeSet`、`FGameplayAttributeData` | Base/Current 值、修改入口和变更通知 |
| Effect | `UGameplayEffect`、`FGameplayEffectSpec`、`FActiveGameplayEffectsContainer` | 配置、运行时 Spec、持续时间、周期、叠加和清理 |
| Task | `UAbilityTask`、WaitDelay、WaitGameplayEvent、PlayMontageAndWait | 异步等待、委托、取消和生命周期归属 |
| Prediction | `FPredictionKey`、`FScopedPredictionWindow`、`ServerTryActivateAbility` | 客户端预测、服务端确认/拒绝和最小纠错闭环 |

参照的是职责划分、数据流和生命周期，不照搬 UE 的模板、宏体系、Fast Array 或完整 Aggregator。

### 四周交付计划

| 周次 | 任务 | 周末验收 | 月度累计 |
| --- | --- | --- | --- |
| 第 1 周（已完成） | GameplayTag、AttributeSet、ASC、Ability、Spec/Handle、反射与 GC 接入 | Inspector 可授予、激活、取消和移除 Ability；属性与引用生命周期正确 | 25% |
| 第 2 周（已完成） | GameplayEffect、Cost、Cooldown、Duration、Infinite、Periodic、Stack 和 Tag 条件 | Damage、Regen、Stun 可组合；属性与 Effect 委托可观察 | 55% |
| 第 3 周（已完成） | AbilityTask、WaitDelay、WaitGameplayEvent、PlayAnimationAndWait | Task 可完成、取消、清理弱委托并响应 Montage 事件 | 78% |
| 第 4 周（已完成） | Gravity、Burn、Freeze 三色权威投射物、状态面板、独立预测实验、GAS Agent 工具和评测 | 双客户端结果一致；三端可直接观察属性、Effect 和冷却；AI 可配置已有能力 | 100% |

### 第 1 周：Tag、Attribute 与 Ability 骨架

- 实现层级 GameplayTag、精确匹配、父级匹配、容器包含/追加/移除，并保证注册与序列化结果确定。
- `FGameplayAttributeData` 区分 BaseValue 与 CurrentValue；所有修改经过统一入口并广播旧值、新值和来源。
- 首个 `PAttributeSet` 提供 `Health`、`MaxHealth`、`Mana`、`MoveSpeed`，接入反射、序列化、GC 引用扫描和调试描述。
- ASC 明确 OwnerActor 与 AvatarActor；负责 AttributeSet、AbilitySpec、GameplayTag 和后续 ActiveEffect 容器。
- Ability 提供 `CanActivate/Activate/Commit/Cancel/End`；CDO 保存默认 Cost、Cooldown 和 Tag 配置。
- Spec 保存稳定 Handle、Ability Class、Level、InputId 和实例激活状态，避免把角色运行状态写回 CDO。
- 新增独立 `PicoGameplayAbilitiesTests`，并在 PicoInspector 增加 GAS Lab，而不是依赖完整 3D 场景才能验证。

周末验收：Inspector 显示 Health=100、MoveSpeed=600；同一 Ability 可授予、激活、取消和移除；销毁 Owner 或
切换 World 后 ASC、Spec 和委托不残留，GC 测试通过。

完成记录：独立 `PicoGameplayAbilities` Runtime 模块已经落地。层级 Tag、确定性容器文本、Base/Current
Attribute、统一变更委托、Ability CDO、Spec/Handle、ASC Owner/Avatar、强弱反射引用和销毁清理均由
`PicoGameplayAbilitiesTests` 覆盖；PicoInspector 新增 `GAS Lab`，可以在无 3D 场景时完成授予、激活、取消、
移除和属性广播验收。具体实现和可视化流程见
[Mini GAS Foundation](GameplayAbilitiesPhase01_Foundation.zh-CN.md)。

### 第 2 周：GameplayEffect 与组合规则

- Effect DurationPolicy 支持 Instant、Duration、Infinite；Modifier 首版支持 Add、Multiply、Override。
- Runtime Spec 保存来源、目标、等级、持续时间、周期、Tag 和计算后的 Modifier；ActiveEffect 使用稳定 Handle。
- 支持 Periodic、最小可解释 Stack 规则、Application/Granted/Blocked Tag 条件和统一移除路径。
- Cost 与 Cooldown 复用 GameplayEffect，不在 Ability 中建立第二套扣费和计时系统。
- 暴露 AttributeChanged、EffectApplied、EffectRemoved 和 GameplayTagChanged 委托，为 UI、动画、网络和任务接入。

周末验收：Damage 立即扣血，Regen 周期恢复，Stun 在持续期内授予 `State.Stunned`；Mana 不足或 Cooldown
存在时激活失败；Effect 到期、取消或目标销毁后属性和 Tag 恢复且无重复回调。

完成记录：`PGameplayEffect` CDO、运行时 Spec、ActiveEffect Handle、三类 DurationPolicy、三种 Modifier、
周期执行、Stack Key/上限、Tag 条件和统一移除已经接入 ASC。Cost/Cooldown 通过同一 Effect 管线提交；Effect
Tick 对委托重入移除采用延迟队列保护。GAS Lab 可以直接组合 Damage、Regen 和 Stun，观察 Active Effects、
属性、Tag 及生命周期广播。具体实现和验收流程见
[GameplayEffect 与组合规则](GameplayAbilitiesPhase02_Effects.zh-CN.md)。

### 第 3 周：AbilityTask 异步生命周期

AbilityTask 生命周期固定为：

```text
Created -> ReadyForActivation -> Active -> Finished / Cancelled -> Destroyed
```

Ability 强引用活动 Task；Task 的事件绑定使用弱对象委托；Ability 结束、预测被拒绝、Actor 销毁、World
替换或引擎退出时必须取消 Task 并解除绑定。

- `WaitDelay` 使用 World 时间和现有 Tick/Timer 调度，不创建线程，也不依赖编辑器帧率。
- `WaitGameplayEvent` 支持精确 Tag 和父级 Tag 匹配，并携带可反射的 `FGameplayEventData`。
- `PlayAnimationAndWait` 复用 Montage Lite，区分 Completed、Interrupted、Cancelled 和 Notify/Event 输出。
- `EndTask`、Ability 结束和外部取消必须幂等，不能重复广播、重复移除或访问已经销毁的对象。
- Inspector GAS Lab 展示 Active Tasks、等待条件、完成原因和清理结果，允许手动触发事件。

周末验收：Delay 到时只广播一次；GameplayEvent 可推进 Ability；Montage 完成、中断和取消进入正确分支；
销毁 Actor、替换 World 或退出 Play 后 Active Task 数量归零。

完成记录：`PAbilityTask`、稳定 TaskHandle、ASC 强引用与待清理队列已经落地；WaitDelay 使用 ASC/World
DeltaSeconds，WaitGameplayEvent 支持父级和精确 Tag，PlayAnimationAndWait 复用 AnimInstance 的 Montage
结束与 Notify 委托。Ability 结束、ASC 销毁和回调重入均有统一清理边界；GAS Lab 可在无项目资产时观察活动
Task、推进时间、发送事件、完成、中断和取消。具体实现与验收流程见
[AbilityTask 异步生命周期](GameplayAbilitiesPhase03_AbilityTasks.zh-CN.md)。

### 第 4 周：联网 Demo 与 Agent 适配

- Gravity、Burn、Freeze：客户端只请求激活，三色投射物生成、移动、命中和 Effect 结算均由服务端权威执行。
- Gravity 产生一次受控上抛；Burn 在 8 秒内周期扣血；Freeze 在 6 秒内阻止移动、跳跃和 Ability。
- 网络首版同步 Attribute、GameplayTag、Ability 激活结果和基础 ActiveEffect 摘要；右上角固定面板在三端显示关键状态。
- PredictionKey、确认、拒绝和回滚保留在 PicoInspector 独立实验中，预测投射物与预测伤害延期。
- Agent 增加 ASC 描述、授予/移除已有 Ability、应用已有 Effect、设置数据化默认值等受控工具。
- 新增 GAS Knowledge Source、`configure-character-abilities.pskill`、确定性 Verifier、路由 Eval 和 Golden Task。
- Agent 仍经过 Validate、Permission、Approval、Transaction、Execute、Verify；不允许模型直接生成任意 Ability C++。

联网验收：一台服务器加两个客户端分别发射 Gravity、Burn 和 Freeze；三端最终 Health、Tag、Effect 和 Cooldown 一致。
AI 验收指令为“给这个角色配置重力、灼烧和冻结投射物能力”，结果必须引用已有资产、
保存项目并通过 Gameplay 验证器，而不是仅由模型宣称成功。

完成记录：`PSandboxPawn` 现在以默认子对象持有 ASC，并用可保存的反射 Mini GAS Profile 组合和配置三个现有
Ability；启用开关、消耗、冷却、射程、弹速/颜色、持续时间与效果强度均可由 World 实例或 Actor Blueprint
保存。`AbilityLoadoutBits` 仅作为旧数据兼容字段，Cost/Cooldown 通过每个 Pawn 的 AbilitySpec Override 生效，
不污染 Ability CDO。
收尾加固将持久化范围明确分成 World Instance 与 Actor Blueprint Defaults：GameMode 生成的玩家由后者提供配置；
`InitialHealth/InitialMana` 是输入，Replicated Attribute/Tag/Remaining 字段统一为只读运行时输出。Agent 新增通用
Blueprint Defaults 读写工具，并禁止用瞬时复制镜像宣称持久配置成功。

迁移债务（必须在 PicoGraph Lite 完成后清偿）：Gravity/Burn/Freeze 的玩法实现可以继续属于 PicoSandbox，
但当前 Editor/Agent 中针对 `PSandboxPawn`、固定属性名和历史 Ability 类名的识别只是第 8 月纵向切片适配。
不得把这些项目名固化为 Pico 引擎 API；第 9 月完成 Graph Runtime 后，按下文“反射驱动迁移门槛”替换。
`FGameplayPredictionLedger` 管理快照、PredictionKey、确认与拒绝，并由 Inspector 独立验收。联网 Demo 的通用投射物 Actor
复制颜色类型，命中后由服务器应用 Gravity、Periodic Burn 或 Freeze Effect。Attribute、已知 Tag 和基础 Effect 摘要通过
Pawn 反射属性、ActorChannel 和 RepNotify 收敛到客户端；Runtime Launch 固定绘制右上角 Mini GAS 面板。Agent 新增 ASC 描述、
受控 Loadout 配置、Knowledge Source、Skill、Verifier、2 条路由用例
和第 11 个 Golden Task。具体结构与验收流程见
[联网 Demo、预测与 Agent](GameplayAbilitiesPhase04_NetworkedDemoAndAgent.zh-CN.md)。

### 月末质量门槛

- Debug/Release 构建通过，新增模块单元测试与 Inspector 可视化测试同时存在。
- 现有 21 个测试模块不回归；涉及网络的用例至少覆盖服务器接受、拒绝、断开与 World 清理。
- 新增 GAS Skill 必须同时具备 Knowledge、Verifier、Routing Eval 和 Golden Task，不能只增加 Tool。
- Ability/Task/Effect 在取消、Actor 销毁、World 替换和引擎退出时均能完成清理。
- Spec、Effect、Task 和网络对象使用稳定 Handle/NetId，不把裸指针当作持久身份。
- 先证明 Runtime API，再接编辑器和 Agent；UI 或模型失败不得破坏底层 Gameplay 状态。

### 本月明确不实现

- 完整 GameplayCue、Attribute Capture/Aggregator、MMC 与 Execution Calculation。
- 完整 TargetData/TargetActor、UE 的全部 Replication Mode 与 Fast Array 兼容实现。
- 预测投射物、预测伤害、复杂回滚和多 Ability 连锁预测。
- 蓝图 Ability 编辑器、AI 生成任意 C++ Ability，以及 ECS 与 GAS 的深度整合。

### 风险降级顺序

若第 4 周时间不足，优先级固定为：服务端权威与校验 > Attribute/Tag/Effect 同步 > 一个 Dash
预测与拒绝闭环 > 服务端 Fireball > Agent 配置已有 Ability。不得为了功能数量牺牲生命周期、GC、权威边界和测试。

## 第 9 月：PicoGraph Lite

目标：补齐 AI 从“摆放场景”到“创建玩法逻辑”所缺少的安全行为表示。

| 周次 | 任务 | 周末验收 |
| --- | --- | --- |
| 第 1 周（已完成） | `.pgraph`、稳定 Node/Pin/Link ID、变量、Entry Event、版本、序列化、Undo/Redo 和基础节点编辑器 | 保存重开后图结构与布局恢复 |
| 第 2 周（已完成） | Graph Schema、Pin 类型、控制流/数据流校验、Typed IR、字节码编译和诊断 | 非法图不能编译；合法图产生确定字节码 |
| 第 3 周（已完成） | `FPicoScriptVM`、执行上下文、指令/循环/调用深度预算、`PScriptComponent`、PFunction/Property/Delegate 节点 | 原生 Actor 可运行图；错误图不能卡死 Game Thread |
| 第 4 周（已完成） | Delay、WaitGameplayEvent、PlayMontageAndWait、ActivateAbility、Cook/Package、AI Graph Tools、可视化状态与反射驱动入口 | AI 可生成受限图；仓库外 Runtime 执行 Cook 后字节码；新增 Graph Skill、Verifier、42 条路由 Eval 和第 12 个 Golden Task；反射属性/函数自动进入 Details、Graph 菜单和 Agent Schema |

固定管线：

```text
.pgraph Editor Asset
 -> Graph Schema + Validator
 -> Typed IR
 -> Bytecode Compiler
 -> Cooked Script Asset
 -> FPicoScriptVM + FScriptExecutionContext
 -> PFunction / PProperty / Delegate / AbilityTask
```

AI 只能通过 `CreateGraph/AddNode/ConnectPins/SetDefaultValue/CompileGraph/ValidateGraph` 创建行为，不能直接
写字节码。PicoGraph 复用 Data-Only Actor Blueprint 的 GeneratedClass、CDO、默认子对象和实例构造链，
不得创建第二套对象系统。

第 1 周完成记录：新增独立 `PicoGraph` Runtime 数据模块和 `.pgraph` AssetRegistry 类型。Graph、Node、Pin、
Link、Variable 均拥有保存后不变的稳定 ID；版本 1 资产采用确定性 JSON，加载时拒绝重复 ID、缺失 Pin、方向/
类型不兼容和一个输入多连接。PicoEditor 的 Content Browser 可创建或双击打开 Graph；独立节点窗口支持 Entry
Event、Sequence、变量、拖动、平移、连线、断线、节点删除、64 步 Graph 专用 Undo/Redo 和保存重开。当前只
表达并保存图结构，不提前承诺 Schema、编译或运行语义。自动化覆盖确定性 round-trip、稳定身份、结构拒绝、
级联 Link 清理、事务恢复和 Registry 扫描。详见
[PicoGraph 第 1 周](PicoGraphPhase01_AssetsAndEditor.zh-CN.md)。

第 2 周完成记录：新增独立 Graph Schema Registry，并让编辑器 Add Node 与编译器共用 Entry、Sequence、Branch、
Bool/Float Literal 和 Add Float 契约。语义校验覆盖节点/Pin Schema、变量和 Pin 默认值、Entry、Exec 扇出、
不可达节点与控制流环；错误诊断携带稳定 NodeId/PinId。合法 Graph 转换为包含变量、Entry、控制目标和类型化
Operand 的 `FPicoGraphIR`，再确定性编码为内存 `PGRB v1` 字节码；语义值变化会改变产物，节点布局变化不会。
编辑器提供 Validate/Compile、默认值编辑、诊断定位和编译摘要。VM、派生 Cook 资产和运行时执行仍属于后续阶段。
详见 [PicoGraph 第 2 周](PicoGraphPhase02_SchemaCompiler.zh-CN.md)。

第 3 周完成记录：`PGRB` 升级到 v2，为控制边保留输出 Pin 名并为数据 Operand 保留稳定 Pin ID；
`DecodeGraphBytecode` 在执行前检查 Magic、版本、记录边界、枚举和跳转。`FPicoScriptVM` 通过显式 Context
运行 Entry/Sequence/Branch 和按需数据节点，并以总指令、单指令重复次数和数据求值深度三类预算阻止损坏图
占住 Game Thread。首批反射节点以 Actor Owner 为 `Self`，通过 `ProcessEvent`、`PProperty::SetValue` 和
`FDynamicMulticastDelegate::Broadcast` 调用既有对象系统。`PScriptComponent` 接入 Actor BeginPlay，保存 Graph
资产和预算，并返回结构化执行报告。Actor Blueprint 新增的 ScriptComponent 会保存反射类元数据；简化版
`FActorBlueprintReinstancer` 在 `Compile & Save` 后向当前地图实例同步新增默认组件和未被覆盖的新默认值，且不
替换 Actor 身份。组件树右键菜单可删除蓝图自有组件，并从资产、CDO 和已放置实例同步移除；继承、Root 和
仍承载子节点的组件受保护。蓝图预览 World 只注册渲染所需组件，不 Tick 且不派发 BeginPlay，避免 Graph
运行结果在 `Compile & Save` 时污染资产默认值和 GeneratedClass CDO。当前 Release 验收为 21 项 Graph 测试与
669 项 Engine 测试。
第 3 周同步节点有意限制为无参 Callable、基础属性类型和零参动态委托；第 4 周已补齐受限异步节点与 Cook
资产，参数化 PFunction 和更完整的节点库仍留在后续增量。
详见 [PicoGraph 第 3 周](PicoGraphPhase03_VMAndScriptComponent.zh-CN.md)。

第 4 周完成记录：`PGRB` 升级到 v3，VM 可返回带成功/失败续点的 `Suspended` 报告；`PScriptComponent`
保存执行代次并在组件注销或新执行开始时取消旧等待。Delay 使用 World Tick，GameplayEvent 和 Montage 等待复用
现有 AbilityTask，ActivateAbility 通过 ASC SpecHandle 分支。Package 会校验并将 `.pgraph` Cook 为
`.pgraph.pgrb`，Stage 不保留编辑 JSON。Agent 新增 create/describe/add/connect/set/validate/compile 七个受控
Graph 工具和 `edit-picograph` Skill；Knowledge Store 同步 Graph Schema 与实时反射 Schema。Graph 编辑器从
PClass 注册表生成 Callable 和基础 Property 节点，只读/Transient 属性不会暴露 Set。Release 验收为 Graph
24/24、Engine 691/691、GameplayAbilities 73/73、Packaging 13/13、Agent 100/100；Sandbox 真实 Development
打包 Smoke 通过，2 个 Graph 均只以 PGRB 进入 Stage。详见
[PicoGraph 第 4 周](PicoGraphPhase04_LatentCookAgent.zh-CN.md)。

### 反射驱动迁移门槛（PicoGraph Lite 完成后执行）

目标不是把 Gravity/Burn/Freeze 玩法搬进引擎，而是让项目只负责声明玩法类、属性、函数和元数据；引擎根据
反射自动生成编辑与 Agent 接口。第 9 月已完成通用 Details、基础 Graph 节点和 Agent Schema 的纵向切片；
以下更丰富的元数据与旧项目适配清理作为兼容性加固继续维护，不阻塞 PicoGraph Lite 验收。

- 扩展 `PCLASS/PPROPERTY/PFUNCTION` 元数据，至少支持 DisplayName、Category、范围/步长、Gameplay 语义 Tag、
  Graph 暴露策略和安全权限；PHT 将这些信息写入统一反射描述。
- Actor Blueprint Details、PicoGraph Node Registry、Knowledge Source 和通用属性工具读取同一份反射描述；
  不再各自维护项目属性列表或 Ability 名称映射。
- 删除 PicoEditor 中针对 `PSandboxPawn`、`GravityManaCost`、`BurnEffectDuration`、`FreezeShot` 以及历史
  `Dash/Fireball/Stun` 类名的硬编码描述和 Tool Schema；项目专用 Skill 可按 Gameplay Tag 组合通用工具，
  但不能要求引擎认识具体项目类名。
- 保持现有 `.pblueprint`、CDO、默认子对象和场景实例数据兼容；迁移只替换“如何发现并呈现能力”，不重写
  对象构造、序列化或网络权威链路。
- 建立独立 Fixture：新增一个仓库内此前不存在的反射 Gameplay Actor/Ability，不修改 PicoEditor 源码，仍能
  自动出现在 Actor Blueprint Details、PicoGraph 可调用节点和 Agent 可审计 Schema 中，并可保存、Play、打包。
- 为自动生成结果加入重复名称、非法类型、只读/Transient、权限越界和旧资产迁移测试；失败时拒绝暴露节点，
  不能退回静默硬编码。

当前验收：增加基础类型反射属性或零参数 Callable 时，只需重新运行 PHT/构建，不需要新增 Details 控件、
Graph 节点类或 Agent 属性 Tool。PicoSandbox 早期 Mini GAS 语义工具暂作为旧 Skill 兼容适配器保留；后续扩展
DisplayName、Category、范围和权限元数据后，再将该适配器完全移出通用 Editor 模块。

## 后续阶段 A（进行中，八周）：AI 游戏搭建闭环

目标：让 AI 在受控工具、Skill、上下文检索和验证器的约束下完成一个简单可玩、可联网、可打包的双人协作
3D 游戏。首个纵向切片固定包含两个角色、差异化能力、环境机关、玩家专属交互、共享目标和服务器权威胜利判定；
它是对 Agent 生产链的工程验收，不宣称复刻完整商业游戏的内容规模。
本阶段不是开放任意代码生成，而是补齐“自然语言需求 -> 结构化规格 -> 可复用玩法积木 -> 自动运行验收 -> 打包证据”的
可靠链路。详细架构、数据契约、边界和验收以
[Agent 游戏制作链路规划](AgentGameCreationPipeline.zh-CN.md) 为准。

阶段 A 第 1～4 周已经完成。在继续第 5～8 周前，先执行
[ReAct 轻量化加固门](AgentReActLightweightHardeningRoadmap.zh-CN.md)：它强化现有 Agent Runtime，但不计为
第二套 Harness 或新的执行模式。加固门通过前，不以继续增加玩法 Tool、独立 Memory Service 或额外模型调用掩盖
上下文、证据、振荡和恢复问题。

ReAct R1～R5 已通过，资产理解与安全创作的 S0～S3 也已完成。在 S4 前先通过
[Agent 输入缓存与长会话上下文减重门](AgentPromptCacheAndContextEfficiency.zh-CN.md) 的 CE0～CE2，再执行 S4～S6：
[Agent 资产理解与安全创作纵向切片](AgentAssetUnderstandingAndSafeAuthoring.zh-CN.md)。该七周切片是阶段 A
第 5 周前的最高优先级门：先补齐稳定目标、Revision 校验、字段级更新、引用影响分析、复合事务和读回验证，再依次
开放 Material、通用组件、Actor 装配、语义元数据、标准预览和可选视觉分析。视觉 Provider 只提供只读候选语义，
不成为第二套 Agent，不直接写项目，也不改变现有 Build Plan、Tool Policy、审批、Checkpoint 和验证边界。

```text
ReAct R5
 -> S0 Safety Foundation（已完成）
 -> S1 Material / Reference Tools（已完成）
 -> S2 Component / Actor Assembly（已完成）
 -> S3 Semantic Metadata（已完成）
 -> CE0 完整计量与固定基线（待实施）
 -> CE1 任务边界历史投影（待实施）
 -> CE2 知识源增量刷新与去重（待实施）
 -> S4 Preview Artifacts
 -> S5 Optional Vision Provider
 -> S6 Editor Golden Tasks
 -> 阶段 A 第 5～8 周
```

CE0～CE2 是独立的上下文效率质量门，不冒充 S4 的资产预览缓存，也不改变 S0～S6 的原编号。已完成的 Provider 逐响应缓存 Token 采集、固定工具顺序和稳定前缀顺序是 CE0 的现有基础；CE0 尚需补齐完整请求组成、知识刷新与历史重放耗时及可比基线。长会话 4 工具本地测试为 135,759 输入 / 102,863 未命中 Token，新会话同类测试为 63,342 / 25,454；该差异提示历史成本，但不是已完成的因果 A/B。CE1/CE2 必须在任务质量不退化的前提下降低长会话未命中输入；工具目录按需加载须另有测量与能力回归证据，不进入默认实施范围。

| 周次 | 任务 | 周末验收 |
| --- | --- | --- |
| 第 1 周（已完成） | `AssetDescriptor`、Capability Descriptor/Catalog 和统一来源元数据；覆盖 Mesh、Texture、骨骼动画、Actor Blueprint、PicoGraph 与 World | Agent 可用结构化事实说明用户资产和现有积木；结果带来源、版本、依赖、预览证据和 Verifier，不读取二进制后猜测 |
| 第 2 周（已完成） | Gameplay Recipe、版本化 `PicoGameSpec`、Requirement Parser 和支持度诊断 | 同一需求稳定生成 Spec，并清楚列出已支持、缺失和不支持项；真正阻塞的问题才要求用户澄清 |
| 第 3 周（已完成） | Build Plan DAG、Dry Run、PlanHash 审批、项目级事务、幂等批处理工具和暂存工作区 | 执行前可预览完整修改；批准后只执行绑定计划；故障可回滚且不污染已有项目 |
| 第 4 周（已完成） | Checkpoint、Artifact Handle、大结果外置、失败分类、上下文预算和有限恢复策略 | 中断后可从已验证边界恢复；重复查询和无进展调用被确定性停止；证据不依赖聊天上下文存活 |
| 第 5 周 | Agent Game Starter Template、双角色/差异化能力、收集/交互/Owned Door/机关/目标/比赛状态/HUD 组件和首批 Recipes | 人工只用现有积木即可组装并运行双人协作 Demo；通用引擎层不出现项目专用类名 |
| 第 6 周 | PicoGraph 补齐 Input/Overlap/RepNotify/Custom Event、事件参数、Int/比较/布尔、参数化 PFunction 和 Authority Policy | 不新增项目专用 C++ 也能表达收集、能力协作、条件判断、专属交互和服务器权威调用 |
| 第 7 周 | 端到端 Game Assembly Skill、World/Blueprint/Graph 联合生产和最多两轮定向修复 | Agent 从中文需求自动搭建首个双人协作关卡；所有修改可审计、保存、重开并由失败断言驱动修复 |
| 第 8 周 | Development/Test Runtime Probe、Input Action 注入、Server + Client 1 + Client 2 Scenario Runner、Package Validator 和真实 Editor Golden Tasks | 自动验证移动、能力、机关、错误玩家拒绝、共享胜利、双端 UI 和 Windows Stage，形成结构化证据 |

最终指令示例：

```text
制作一个第三人称收集游戏。玩家收集三个能量块后打开大门，
敌人碰到玩家会造成伤害，支持两个客户端联机，最后打包为 Windows 程序。
```

AI 可以分步规划和申请审批，但完成定义必须由确定性验证器判定，不能由模型自行宣布成功。

本阶段统一使用以下产物生产边界：`ExistingAssetProducer`、`ActorBlueprintProducer`、
`PicoGraphProducer` 和 `WorldProducer`。`CodeModuleProducer` 在本阶段保持禁用，待阶段 E 通过独立准入后接入同一
Build Plan；它不能绕过 Tool Policy、Diff 审批、事务、构建测试或验证器。阶段 A 完成后先进入阶段 C，不直接
进入 ECS；第二玩法复用由阶段 A.2 独立验收。

### Skill 路由演进原则

Skill 数量增加后采用分层路由，而不是用模型替换现有规则：

```text
确定性意图/否定/安全规则
    -> 关键词、Tag 和 Skill 元数据筛选候选
    -> 仅在候选存在歧义时调用结构化模型路由
    -> Skill Registry 校验 ID、版本和启用状态
    -> AllowedTools、Tool Policy、审批、事务和验证器硬检查
```

- “运行但不要打包”等明确意图、否定词和安全边界继续由确定性代码处理，不能交给模型自由解释。
- 模型只能从候选集合中返回符合 Schema 的 Skill ID、置信度和简短理由；不能创建未知 Skill、扩大 AllowedTools 或授予权限。
- 无候选、低置信度或候选冲突时，回退到安全的澄清/拒绝路径，不猜测执行具有副作用的工作流。
- 固定 Agent Eval 增加同义表达、否定表达、相似 Skill、多 Skill 组合、未知 Skill 和越权 Tool Call 用例；比较路由准确率、误触发率和禁止副作用。
- 先保留当前轻量关键词路由。仅当 GAS、PicoGraph 等 Skill 增多并出现真实歧义后启用模型层，避免过早增加延迟、成本和不确定性。

### 端到端 Golden Tasks

现有 `IntentRoutingCases.tsv` 和离线 Golden Tasks 继续负责快速验证 Harness；本阶段在其上增加真实
Editor Fixture、三进程 Scenario Runner，并扩展到至少 20 个固定端到端
Golden Tasks，覆盖“自然语言需求 -> Agent 规划 -> Tool Pipeline -> 场景修改 -> 保存 -> Play/验证 -> Package”的真实链路。

- 每个任务固定输入需求、初始项目/World、允许副作用、禁止副作用和确定性验收条件。
- 场景结果由对象、组件、反射属性和资产引用检查判定；不能以 Assistant 声称“已完成”作为成功依据。
- Play 由进程状态、运行日志和玩法验证器判定；Package 由退出码、Stage 清单和可执行文件启动结果判定。
- 记录任务成功率、路由准确率、工具调用数、Token、耗时、审批次数、恢复/修复轮次和禁止副作用。
- Golden Tasks 使用可重建的 Fixture 项目或临时副本运行，失败时保留 Journal、Session、Tool Trace 和验证报告，避免污染开发项目。
- 修改 Provider、Prompt、Skill、RAG、Tool Schema 或 Agent Runtime 后必须重跑；路由 Eval 负责快速定位，端到端 Eval 负责发现跨模块回归。

## 后续阶段 C（提前）：Render Architecture

阶段 A 首个双人协作切片通过后，先拆清渲染职责，再开放 AI 编排后处理或增加第二图形后端：

```text
PrimitiveComponent
 -> PrimitiveSceneProxy / MeshBatch
 -> RenderScene
 -> RenderPass / RenderGraph
 -> RHI
 -> OpenGL Backend
```

| 周次 | 任务 | 周末验收 |
| --- | --- | --- |
| 第 1 周 | PrimitiveSceneProxy、MeshBatch 和 Game/Render 数据快照 | 渲染不遍历和持有可变 Gameplay 对象 |
| 第 2 周 | RenderScene、资源句柄、上传/销毁命令和缓存生命周期 | OpenGL 类型不进入 Gameplay/序列化边界；失效句柄和延迟销毁可检测 |
| 第 3 周 | RenderPass/RenderGraph Lite、声明式资源、读写依赖、拓扑排序、生命周期验证和调试视图 | 现有 PBR、蒙皮、Picking 和辅助线迁移；非法依赖在执行前失败 |
| 第 4 周 | 最小 RHI 与 OpenGL Backend、截图回归和 CPU/GPU 性能基线 | 高层渲染不调用 OpenGL；现有编辑器与打包游戏画面一致，为第二 Backend 留下真实边界 |

第一版不复制 UE5 RDG 的异步计算、资源别名、并行录制和复杂 Barrier，也不在拆分期间同时实现 Vulkan。

## 后续阶段 D（新增，两周）：AI 视觉资产生产

| 周次 | 任务 | 周末验收 |
| --- | --- | --- |
| 第 1 周 | `TextureProducer`：生成/编辑源图、平铺与尺寸检查、颜色空间和通道校验、预览审批、来源与 Prompt 记录 | Agent 可生成并导入简单地面、墙体、机关和道具贴图；未经审批的源图不进入正式 `/Game` 资产 |
| 第 2 周 | 参数化几何与 `External3DProducer`：外部生成、glTF/GLB 暂存、比例/拓扑/UV/材质/骨骼/碰撞检查 | Agent 可生产或导入简单环境道具；复杂角色生成失败时明确降级为用户资产或受控外部工具 |

生成器只产出暂存 Artifact，Pico 的 Importer、Validator 和审批负责是否发布。首版不承诺任意高质量角色、自动
重拓扑、自动绑定或不可追踪的云端模型下载。

## 后续阶段 E（新增，四周）：受控 Code Harness

只有阶段 A 已暴露出无法由 Tool、Recipe、PicoGraph 或现有 Component 合理表达的重复能力缺口时才进入本阶段。

| 周次 | 任务 | 周末验收 |
| --- | --- | --- |
| 第 1 周 | `CodeModuleProducer`、隔离 Worktree/临时项目、Patch Artifact 和项目源码白名单 | 模型不能直接写主工作区或引擎核心；生成结果可丢弃且不影响用户修改 |
| 第 2 周 | 文件/目录/依赖/代码量预算、结构化 Diff、PlanHash 与应用前二次审批 | 计划审批不等于源码审批；越权路径、密钥、二进制和未知依赖零副作用 |
| 第 3 周 | 格式化、静态检查、编译、测试、运行验证、有限修复和原子回滚 | 不可编译或验证失败的代码不能发布；失败保留诊断与 Patch，不污染项目 |
| 第 4 周 | 将通过验收的代码注册为 Component、反射函数、Graph Node 或 Recipe，并刷新 Catalog | 新能力随后可由普通生产链复用，不为每次需求重复生成一次性代码 |

Code Harness 是现有 Harness 下的新 Artifact Producer，不拥有任意 Shell 权限，不形成第二套 Agent Runtime，
默认禁止修改 `PicoCore`、对象生命周期、网络协议和渲染 Backend。

## 后续阶段 A.2（新增，两周）：第二玩法综合验收

使用不同于首个双人收集开门 Demo 的需求，例如“钥匙 + 双人压力板 + 差异化能力机关”，验证 Catalog、Recipe、
Graph 和新增代码能力没有绑定首个项目类名。第一周完成生成、运行和定向修复，第二周完成三进程、Package、迁移性
与指标对比。两个非同构玩法均通过后，才宣称 Agent 游戏制作链成立，并允许评估 ECS。

## 后续阶段 B（继续延后）：ECS 纵向切片

ECS 不替换 `PObject/Actor/Component`。Actor 继续管理身份、生命周期、Gameplay、网络和编辑器对象；ECS
用于大量同构、数据导向的运行时实体。

进入门槛：工程深度阶段、阶段 A、C、D、E 与 A.2 均通过，并且性能数据存在高密度同构实体用例。若未通过，
继续修正 Catalog、Recipe、Graph、Producer 或 Scenario Runner，不以 ECS 新功能掩盖游戏制作链路的缺口。

| 周次 | 任务 | 周末验收 |
| --- | --- | --- |
| 第 1 周 | Entity ID、Registry、组件类型注册和生命周期 | Entity 与组件可稳定创建销毁 |
| 第 2 周 | 稠密存储、Query、System 调度和结构变更缓冲 | 大量实体遍历无迭代期失效 |
| 第 3 周 | Actor/ECS 桥接、Transform 和 Render Instance 同步 | 少量 Actor 可拥有大量 ECS 表现实体 |
| 第 4 周 | 性能、序列化/网络边界和可视化验收 | 对比 Actor 与 ECS 用例并证明真实收益 |

没有第二个高密度用例前，不把 Gameplay Framework、Character 或现有 Component 全面迁移到 ECS。

## 后续阶段 C.2（准入后，四至六周）：Vulkan Backend

只有阶段 C 证明 OpenGL 已完全位于 RHI 后方、RenderGraph 承载现有效果、公共接口和资产不泄漏 OpenGL Handle，
并建立稳定截图与性能基线后才开始。阶段依次覆盖 Vulkan Device/Swapchain/资源、Shader 与 Pipeline、RenderGraph
Barrier 映射、OpenGL/Vulkan 画面一致性、编辑器/游戏/Package 回归。DX12、硬件光线追踪和 Render Thread 继续
独立评估，不与 Vulkan 首个 Backend 验收捆绑。

## 网络保留任务

以下内容从主线移入维护待办：

- Dedicated Server Target 完善。
- 协议版本、构建版本和内容版本校验。
- 断开后 Connection、Channel、NetId 和延迟引用清理。
- Client/Server 独立 Target Receipt 与打包回归。
- 延迟、抖动、丢包、乱序和资源上限测试。

以下内容暂缓，不阻塞 AI、GAS、PicoGraph、ECS 或渲染主线：

- 公网 Dedicated Server 实际部署。
- NAT 穿透、P2P、Relay、匹配和账号系统。
- 短线重连、InactivePlayerRecord 和主机迁移。
- Replay、时间跳转和倍速回放。
- 商业级拥塞控制与反作弊平台。

## 安全与稳定准入门槛

| 准入项 | 最晚完成时间 | 验收 |
| --- | --- | --- |
| Worker 与 Game Thread 边界 | 第 7 月第 1 周 | Worker 误写对象立即失败；返回结果通过 Dispatcher 应用 |
| Fake Provider 与固定评测 | 第 7 月第 1 周 | 不消耗 API 即可重放成功、失败、超时和取消 |
| Event Log 与幂等 | 第 7 月第 2 周 | 每个 Tool Call/Result 配对；重复 ID 零额外副作用 |
| Tool Policy 与事务 | 第 7 月第 3 周 | 非法输入、拒绝、路径穿越和未知工具零副作用 |
| Provider 隔离 | 第 7 月第 4 周 | 切换 DeepSeek/Kimi 不修改 Editor Tool 实现 |
| 跨进程持久操作 | 第 7 月收尾并已加固 | Journal 区分 Applied/Committed；项目和 Package 使用 Staging；Play/Package 子进程归属 Job Object；Package 最终退出码、报告和完成标记回写原 Tool Result |
| Skill 路由 | Agent 游戏搭建阶段第 2 周 | 候选筛选后才允许结构化模型选 Skill；未知、低置信度和越权结果无副作用；固定 Eval 覆盖误路由 |
| Graph 类型和执行预算 | 第 9 月第 3 周 | 非法图不可运行；超预算终止当前执行而不阻塞 World |
| AI 完成判定 | Agent 游戏搭建阶段第 3～4 周 | 编译、引用、Play、日志和 Package 由验证器判定 |
| 端到端 Agent Eval | Agent 游戏搭建阶段第 8 周 | 至少 20 个 Golden Tasks 可重复运行；覆盖场景修改、保存、Play、验证、Package 和禁止副作用 |

从第一版开始覆盖以下故障测试：

- 非法 JSON、未知 Tool、错误参数类型和缺失参数。
- 越界路径、路径穿越、未知对象和失效 Handle。
- 重复 ToolCall、模型循环、Provider 超时、限流和断网。
- 用户拒绝、任务取消、编辑器退出和会话恢复。
- 事务执行失败与回滚。
- 文档、资产名和日志中的 Prompt Injection 文本。
- 构建失败、Play 崩溃和 Package 失败。

建立至少 20 个固定 Golden Tasks，记录成功率、工具调用数、耗时、Token、审批次数、修复次数和禁止副作用。
模型版本、Prompt Hash、工具参数、结果、审批和事务 ID 必须可追踪；日志必须清除 API Key 和敏感数据。

## 范围控制与降级顺序

时间不足时按以下顺序保住完整学习链：

1. 必须保留 PicoTask、Game Thread Dispatcher 和安全关闭。
2. 必须保留自研最小 Harness、Event Log、Tool Policy、审批和评测。
3. 必须保留场景 Agent、Mini GAS 核心和 PicoGraph 编译/VM 纵向切片。
4. 优先完成 AI 创建简单单机场景和玩法，再扩展双人联网验收。
5. MCP Server 只保留下文定义的本地四周纵向切片；MCP Client、LangGraph、Embedding RAG 和 C++ 自动生成
   均可延期。
6. AI 视觉资产先保留纹理、参数化几何和受控 glTF/GLB 导入，不承诺任意高质量 3D 生成。
7. 受控 Code Harness 可以削减生成范围，但不能削减隔离、Diff 审批、构建验证和回滚。
8. ECS 只保留纵向切片，不改写现有 Actor 架构。
9. 深入渲染先完成架构边界，不同时追求多图形 API 和光线追踪。
10. 不得通过开放任意 Shell、删除审批、跳过事务或放宽执行预算换取表面进度。

## 最终验收结果

完成本路线主线后，Pico 应支持：

- 用户在 AI Chat Workspace 中用中文逐步描述游戏需求。
- Agent 检索真实反射、资产、World、Gameplay 和日志上下文。
- Agent 通过受控工具创建 Actor、组件、资产引用、GAS 配置和 PicoGraph 行为。
- 用户能查看计划、Tool Call、参数、审批、结果、错误和修复过程。
- 所有编辑可保存、重开、Undo/Redo，并经过确定性验证。
- 简单第三人称收集/战斗 Demo 可在两个客户端间同步并打包为独立 Windows 程序。
- Agent、模型、MCP、Skill、ECS 和渲染后端均保持清晰边界，不侵入 Pico 对象与 Gameplay 核心。
