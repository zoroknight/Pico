# Pico AI 优先后续开发路线

本文档记录 Pico 在完成第 6 月网络学习型 MVP 后的新主线。它是后续任务规划、实现顺序和阶段验收的
首要依据；当本文档与 [`Pico_Remaining_Development_Roadmap.zh-CN.md`](Pico_Remaining_Development_Roadmap.zh-CN.md)
中的未完成排期冲突时，以本文档为准。旧路线继续保留已经完成的架构基线、风险记录和历史决策。

计划中的“月份”均指项目开发月份，不是自然月。已经完成的对象系统、编辑器、Gameplay、物理、动画、
打包和网络主链不再重复列为新任务。

## 调整原因

Pico 已经通过本机多进程和两台真实 Windows 电脑验证 UDP、Replication、RPC、Ownership、客户端预测、
服务器纠错和模拟代理平滑。网络核心学习目标已经成立，下一阶段不再以公网、重连和回放为主线。

后续目标调整为：

1. 学习现代 Agent Harness 的真实组成，而不只调用聊天 API。
2. 让 AI 安全使用 Pico 已有的反射、资产、事务、World、Gameplay、网络和打包能力。
3. 逐步从“创建场景”推进到“创建可执行玩法”和“完成简单 3D 游戏”。
4. 保持模型、Harness、工具协议和编辑器执行层可以独立替换。
5. 在 AI 主线形成完整纵向切片后，再推进 ECS 和复杂渲染架构。

## 新实施顺序

```text
网络核心收尾
 -> PicoTask / Game Thread Dispatcher
 -> Pico Agent Harness Lite
 -> 编辑器场景 Agent
 -> Mini GAS / AbilityTask
 -> PicoGraph Lite
 -> AI 完整游戏搭建
 -> MCP / Skill / RAG 增强
 -> ECS 纵向切片
 -> Render Architecture
```

固定优先级为：

```text
PicoTask
 > Harness 安全基础
 > 场景 Agent
 > Mini GAS / AbilityTask
 > PicoGraph
 > AI 完整游戏
 > MCP / LangGraph 对照实验
 > ECS
 > 深入渲染
```

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
- MCP：`AgentToolRegistry` 稳定后增加本地 `stdio` Server Adapter。MCP 是互操作协议，不是 Agent Runtime
  或安全边界；Pico 内部架构不得依赖 MCP。
- LangGraph：后期通过 `IAgentRuntime` 增加可选后端，使用相同工具和评测集比较，不作为第一版前置依赖。
- 多 Agent：主线暂不实现。一个 Agent、确定性工具、验证器和有限修复循环足以完成首个 Demo。
- Memory：从第一版保存显式 Session Event 和摘要，不使用不可审计的供应商隐式记忆作为事实来源。

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
同时裁剪 Provider Schema 并在执行器侧限制工具。Embedding、MCP 与跨项目长期知识仍延期。详见
[`AIPhase08_StreamingKnowledgeRagAndSkills.md`](AIPhase08_StreamingKnowledgeRagAndSkills.md)。
Agent 阶段收尾还将 Intent Router 提取到 `PicoAgentCore`，并冻结 18 条中英文生产提示评测，覆盖 Play/Package
否定语义、场景/角色 Skill 和多 Skill 组合。后续 Gameplay 模块必须通过扩展该评测集接入 Agent，不能在聊天
窗口中增加另一套临时意图判断。

## 第 8 月：Mini GAS 与 AbilityTask

目标：学习 UE GAS 的核心职责，为 PicoGraph 的异步节点和 AI 可组合 Gameplay 工具提供稳定能力。

| 周次 | 任务 | 周末验收 |
| --- | --- | --- |
| 第 1 周 | GameplayTag、AttributeSet、AbilitySystemComponent、AbilitySpec | 属性和 Ability 可反射、授予和撤销 |
| 第 2 周 | GameplayEffect、Cost、Cooldown、Duration、Periodic 和 Tag 条件 | Effect 生命周期、叠加边界和属性修改可测试 |
| 第 3 周 | AbilityTask、WaitDelay、WaitGameplayEvent、PlayAnimationAndWait | Task 可完成、取消、清理弱委托并响应 Montage 事件 |
| 第 4 周 | Dash、Fireball、Stun、网络预测与拒绝；向 AI 暴露受控 GAS 工具 | 双人网络 Demo 一致；AI 能配置已有能力 |

AbilityTask 生命周期固定为：

```text
Created -> ReadyForActivation -> Active -> Finished / Cancelled -> Destroyed
```

Ability 强引用活动 Task；Task 的事件绑定使用弱对象委托；Ability 结束、预测被拒绝、Actor 销毁、World
替换或引擎退出时必须取消 Task 并解除绑定。

## 第 9 月：PicoGraph Lite

目标：补齐 AI 从“摆放场景”到“创建玩法逻辑”所缺少的安全行为表示。

| 周次 | 任务 | 周末验收 |
| --- | --- | --- |
| 第 1 周 | `.pgraph`、稳定 Node/Pin/Link ID、变量、Entry Event、版本、序列化、Undo/Redo 和基础节点编辑器 | 保存重开后图结构与布局恢复 |
| 第 2 周 | Graph Schema、Pin 类型、控制流/数据流校验、Typed IR、字节码编译和诊断 | 非法图不能编译；合法图产生确定字节码 |
| 第 3 周 | `FPicoScriptVM`、执行上下文、指令/循环/调用深度预算、`PScriptComponent`、PFunction/Property/Delegate 节点 | 原生 Actor 可运行图；错误图不能卡死 Game Thread |
| 第 4 周 | Delay、WaitGameplayEvent、PlayMontageAndWait、ActivateAbility、Cook/Package、AI Graph Tools 和可视化状态 | AI 可生成受限图；仓库外 Runtime 执行 Cook 后字节码 |

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

## 第 10 月：AI 游戏搭建闭环

目标：让 AI 在受控工具、Skill、上下文检索和验证器的约束下完成一个简单可玩、可联网、可打包的 3D 游戏。

| 周次 | 任务 | 周末验收 |
| --- | --- | --- |
| 第 1 周 | 在已完成 Skill v0 上增加拾取、触发门、GAS 和 PicoGraph Skills，并做版本迁移/禁用 UI | Gameplay Skill 可审核、禁用和固定版本，不提升权限 |
| 第 2 周 | 扩展已完成 RAG Lite：加入 GAS/PicoGraph Schema、验证结果和固定检索评测；按真实数据决定是否做 Embedding 对照 | 回答和工具规划引用新增 Gameplay 真实数据 |
| 第 3 周 | 自动验证、Graph 编译、资产引用检查、Play、日志读取和最多两轮修复 | 失败保留现场并报告，不无限循环或掩盖错误 |
| 第 4 周 | AI 完整游戏 Demo、Windows Package 和回归评测；可选本地 MCP Adapter | 从中文需求到可运行 Stage 形成可审计闭环 |

最终指令示例：

```text
制作一个第三人称收集游戏。玩家收集三个能量块后打开大门，
敌人碰到玩家会造成伤害，支持两个客户端联机，最后打包为 Windows 程序。
```

AI 可以分步规划和申请审批，但完成定义必须由确定性验证器判定，不能由模型自行宣布成功。

## 第 11 月：ECS 纵向切片

ECS 不替换 `PObject/Actor/Component`。Actor 继续管理身份、生命周期、Gameplay、网络和编辑器对象；ECS
用于大量同构、数据导向的运行时实体。

| 周次 | 任务 | 周末验收 |
| --- | --- | --- |
| 第 1 周 | Entity ID、Registry、组件类型注册和生命周期 | Entity 与组件可稳定创建销毁 |
| 第 2 周 | 稠密存储、Query、System 调度和结构变更缓冲 | 大量实体遍历无迭代期失效 |
| 第 3 周 | Actor/ECS 桥接、Transform 和 Render Instance 同步 | 少量 Actor 可拥有大量 ECS 表现实体 |
| 第 4 周 | 性能、序列化/网络边界和可视化验收 | 对比 Actor 与 ECS 用例并证明真实收益 |

没有第二个高密度用例前，不把 Gameplay Framework、Character 或现有 Component 全面迁移到 ECS。

## 第 12 月：Render Architecture

本阶段先拆清渲染职责，再增加复杂效果：

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
| 第 2 周 | RenderScene、资源句柄、上传/销毁命令和缓存生命周期 | OpenGL 类型不进入 Gameplay/序列化边界 |
| 第 3 周 | RenderPass/RenderGraph Lite、依赖和资源生命周期 | 现有 PBR、蒙皮、Picking 和辅助线迁移 |
| 第 4 周 | 最小 RHI 与 OpenGL Backend、回归和性能基线 | 为 Vulkan/DX12/光线追踪保留真实替换边界 |

延迟渲染、Vulkan、DX12、硬件光线追踪和 Render Thread 在上述边界验收后单独规划，不与架构拆分同时展开。

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
| 跨进程持久操作 | 第 7 月收尾 | Journal 区分 Applied/Committed；项目和 Package 使用 Staging；Play/Package 子进程归属 Job Object |
| Graph 类型和执行预算 | 第 9 月第 3 周 | 非法图不可运行；超预算终止当前执行而不阻塞 World |
| AI 完成判定 | 第 10 月第 3 周 | 编译、引用、Play、日志和 Package 由验证器判定 |

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
5. MCP、LangGraph、Embedding RAG 和 C++ 自动生成均可延期。
6. ECS 只保留纵向切片，不改写现有 Actor 架构。
7. 深入渲染先完成架构边界，不同时追求多图形 API 和光线追踪。
8. 不得通过开放任意 Shell、删除审批、跳过事务或放宽执行预算换取表面进度。

## 最终验收结果

完成本路线主线后，Pico 应支持：

- 用户在 AI Chat Workspace 中用中文逐步描述游戏需求。
- Agent 检索真实反射、资产、World、Gameplay 和日志上下文。
- Agent 通过受控工具创建 Actor、组件、资产引用、GAS 配置和 PicoGraph 行为。
- 用户能查看计划、Tool Call、参数、审批、结果、错误和修复过程。
- 所有编辑可保存、重开、Undo/Redo，并经过确定性验证。
- 简单第三人称收集/战斗 Demo 可在两个客户端间同步并打包为独立 Windows 程序。
- Agent、模型、MCP、Skill、ECS 和渲染后端均保持清晰边界，不侵入 Pico 对象与 Gameplay 核心。
