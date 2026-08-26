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
Agent 阶段收尾已将 Intent Router 提取到 `PicoAgentCore`，当前已冻结 39 条中英文生产提示评测，覆盖 Play/Package
否定语义、场景/角色 Skill 和多 Skill 组合。后续 Gameplay 模块必须通过扩展该评测集接入 Agent，不能在聊天
窗口中增加另一套临时意图判断。

## Harness 成熟度加固路线

Pico 保留自研 C++ Harness，不为了功能数量迁移到 LangChain/LangGraph。后续重点是把现有纵向链路变得可观察、
可评测、可恢复和可扩展；加固工作伴随 GAS、PicoGraph 主线增量完成，不另起一套 Agent Runtime。

| 优先级与时机 | 任务 | 验收标准 |
| --- | --- | --- |
| P0：已完成基础版 | 统一 `SessionId/RunId/TurnId/SpanId`，为 Model、Tool、Approval、Validation 建立父子 Span | Session JSONL 可还原调用顺序、耗时、结果和失败位置；旧日志兼容；编辑器时间线 UI 后续补充 |
| P0：已完成离线基线 | 将路由 Eval 从 18 条扩展到 37 条；建立端到端 Eval Runner 和首批 10 个 Golden Tasks；增加原子批量属性/删除与冲突保护的 Run ChangeSet | 确定性测试不访问真实 Provider；生成带 RunId、耗时、计数和证据路径的报告；真实 Editor Fixture 留到第 10 月 |
| P0：第 8～9 月伴随主线 | 建立上下文分区预算、摘要快照和大结果 Artifact/Handle，避免把完整日志和 Tool Result 反复送入模型 | 超预算时可解释地裁剪；关键指令、审批状态、引用来源和最近错误不丢失 |
| P0：第 8 月持续 | 完善 Provider 超时、取消、可重试/不可重试错误、指数退避、限流和单 Run Token/费用预算 | Provider 断网、限流或非法响应不会破坏项目，也不会形成无限重试 |
| P0：第 8～9 月伴随主线 | 在 Durable Operation Journal 上增加轻量 `RunCheckpoint`，记录 Skill、已完成 ToolCall、待审批调用、预算和下一步 | 仅从 Tool 边界恢复；先 reconcile 再重试；不承诺从工具函数内部某一行继续 |
| P0：第 9 月 Skill 增长时 | Skill Registry 返回带匹配 Trigger/Tag、分数和排除理由的确定性候选列表 | 候选结果可解释、可固定测试，不增加权限，也不依赖模型或网络 |
| P1：第 10 月 | 对歧义候选启用结构化模型路由 Shadow Mode，并扩展到至少 20 个端到端 Golden Tasks | 模型结果先只记录比较；达到固定准确率、误触发和禁止副作用门槛后才处理歧义路由 |

伴随开发规则：

- GAS、PicoGraph、ECS 每新增一组 Tool/Skill，必须同时注册 Knowledge Source、Verifier、路由 Eval 和至少一个端到端任务。
- Trace、Session、Journal、Checkpoint 和 Eval 报告使用同一组 Run/ToolCall 标识，避免形成彼此无法关联的日志系统。
- 离线确定性测试验证 Runtime、Tool Pipeline、Skill、Guardrail 和恢复；真实 DeepSeek/Kimi 测试只验证外部模型行为和协议兼容。
- AI 代码/文件生成正式开放前，先增加固定工作区、路径白名单、Diff 审批、进程边界和生成后构建/测试验证。

明确延期：

- 多 Agent、Handoff 和并行子 Agent：单 Agent 的 Gameplay/资产/构建职责出现可测量瓶颈后再引入。
- LangChain/LangGraph 运行时迁移：只参考 Checkpoint、Interrupt 和 State Graph 思想，不替换 Pico 的 C++/Game Thread 集成。
- 完整 MCP 生态、跨项目长期记忆、Embedding/向量数据库：先由真实检索 Eval 证明现有 RAG Lite 不足。
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
| 第 1 周 | `.pgraph`、稳定 Node/Pin/Link ID、变量、Entry Event、版本、序列化、Undo/Redo 和基础节点编辑器 | 保存重开后图结构与布局恢复 |
| 第 2 周 | Graph Schema、Pin 类型、控制流/数据流校验、Typed IR、字节码编译和诊断 | 非法图不能编译；合法图产生确定字节码 |
| 第 3 周 | `FPicoScriptVM`、执行上下文、指令/循环/调用深度预算、`PScriptComponent`、PFunction/Property/Delegate 节点 | 原生 Actor 可运行图；错误图不能卡死 Game Thread |
| 第 4 周 | Delay、WaitGameplayEvent、PlayMontageAndWait、ActivateAbility、Cook/Package、AI Graph Tools、可视化状态；PicoGraph 验收后执行项目专用蓝图/GAS 适配的反射驱动迁移 | AI 可生成受限图；仓库外 Runtime 执行 Cook 后字节码；新增 Graph Skill、Verifier、路由 Eval 和 Golden Task；新反射 Gameplay 类无需修改 Editor 即可生成 Details、Graph 节点和 Agent Schema |

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

### 反射驱动迁移门槛（PicoGraph Lite 完成后执行）

目标不是把 Gravity/Burn/Freeze 玩法搬进引擎，而是让项目只负责声明玩法类、属性、函数和元数据；引擎根据
反射自动生成编辑与 Agent 接口。迁移完成前，第 9 月不能视为完全收尾。

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

验收定义：通用 Editor/Agent 模块不再引用 PicoSandbox 类名；增加新的项目反射属性或可调用函数时，只需重新
运行 PHT/构建并配置元数据，不需要为该属性编写新的 Details 控件、Graph 节点类或 Agent Tool。

## 第 10 月：AI 游戏搭建闭环

目标：让 AI 在受控工具、Skill、上下文检索和验证器的约束下完成一个简单可玩、可联网、可打包的 3D 游戏。

| 周次 | 任务 | 周末验收 |
| --- | --- | --- |
| 第 1 周 | 在已完成 Skill v0 上增加拾取、触发门、GAS 和 PicoGraph Skills，并做版本迁移/禁用 UI；加入“候选筛选 + 结构化模型路由” | 明确输入继续走确定性规则；歧义输入只返回通过 Schema 校验的候选 Skill ID；Gameplay Skill 可审核、禁用和固定版本，不提升权限 |
| 第 2 周 | 扩展已完成 RAG Lite：加入 GAS/PicoGraph Schema、验证结果和固定检索评测；按真实数据决定是否做 Embedding 对照 | 回答和工具规划引用新增 Gameplay 真实数据 |
| 第 3 周 | 自动验证、Graph 编译、资产引用检查、Play、日志读取和最多两轮修复 | 失败保留现场并报告，不无限循环或掩盖错误 |
| 第 4 周 | AI 完整游戏 Demo、Windows Package、端到端 Golden Tasks 和回归评测；可选本地 MCP Adapter | 从中文需求到场景修改、保存、Play、验证和可运行 Stage 形成可审计闭环；已有能力无回归 |

最终指令示例：

```text
制作一个第三人称收集游戏。玩家收集三个能量块后打开大门，
敌人碰到玩家会造成伤害，支持两个客户端联机，最后打包为 Windows 程序。
```

AI 可以分步规划和申请审批，但完成定义必须由确定性验证器判定，不能由模型自行宣布成功。

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

现有 39 条 `IntentRoutingCases.tsv` 和 11 个离线 Golden Tasks 继续负责快速验证 Harness；第 10 月在其上增加真实
Editor Fixture，并扩展到至少 20 个固定端到端
Golden Tasks，覆盖“自然语言需求 -> Agent 规划 -> Tool Pipeline -> 场景修改 -> 保存 -> Play/验证 -> Package”的真实链路。

- 每个任务固定输入需求、初始项目/World、允许副作用、禁止副作用和确定性验收条件。
- 场景结果由对象、组件、反射属性和资产引用检查判定；不能以 Assistant 声称“已完成”作为成功依据。
- Play 由进程状态、运行日志和玩法验证器判定；Package 由退出码、Stage 清单和可执行文件启动结果判定。
- 记录任务成功率、路由准确率、工具调用数、Token、耗时、审批次数、恢复/修复轮次和禁止副作用。
- Golden Tasks 使用可重建的 Fixture 项目或临时副本运行，失败时保留 Journal、Session、Tool Trace 和验证报告，避免污染开发项目。
- 修改 Provider、Prompt、Skill、RAG、Tool Schema 或 Agent Runtime 后必须重跑；路由 Eval 负责快速定位，端到端 Eval 负责发现跨模块回归。

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
| 跨进程持久操作 | 第 7 月收尾并已加固 | Journal 区分 Applied/Committed；项目和 Package 使用 Staging；Play/Package 子进程归属 Job Object；Package 最终退出码、报告和完成标记回写原 Tool Result |
| Skill 路由 | 第 10 月第 1 周 | 候选筛选后才允许结构化模型选 Skill；未知、低置信度和越权结果无副作用；固定 Eval 覆盖误路由 |
| Graph 类型和执行预算 | 第 9 月第 3 周 | 非法图不可运行；超预算终止当前执行而不阻塞 World |
| AI 完成判定 | 第 10 月第 3 周 | 编译、引用、Play、日志和 Package 由验证器判定 |
| 端到端 Agent Eval | 第 10 月第 4 周 | 至少 20 个 Golden Tasks 可重复运行；覆盖场景修改、保存、Play、验证、Package 和禁止副作用 |

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
