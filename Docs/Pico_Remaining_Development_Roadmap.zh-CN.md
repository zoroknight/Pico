# Pico 剩余开发路线

本文档是 Pico 后续开发的长期基准，用于避免因对话上下文压缩、计划迭代或项目月份混淆而遗忘关键目标。

计划中的“月份”均指项目开发月份，不是自然月。项目第 3 月已经验收完成，当前进入项目第 4 月；已经完成的工作只记录为基线，不重复列入剩余任务。

## 项目目标

Pico 是一个以学习 Unreal Engine 5 源码和完整游戏引擎链路为主要目的的小型 C++ 3D 引擎。

最终目标不是复制完整 UE，而是实现一套小而清晰、可运行、可联网、可打包、可继续扩展的架构，重点学习：

- UObject、UClass、CDO、反射、UHT 和 GC。
- World、Actor、Component 和 Gameplay Framework。
- GameMode、GameState、PlayerController、PlayerState、Pawn 和 MovementComponent。
- Replication、RPC、网络所有权和客户端预测。
- 物理、动画、委托与 Tick 调度的系统边界。
- Cook、Stage、Package 和独立游戏程序。
- Mini GAS、AbilityTask 和 AI Agent 工具调用。

## 当前完成基线

以下内容已经完成，不应再次作为新的月度任务规划：

- UE 风格 `PreInit -> Init -> Tick -> Exit` EngineLoop。
- 基础对象注册、反射属性、Outer、Handle、对象序列化和 `PostLoad`。
- `PWorld/PLevel/PActor/Component` 生命周期、场景层级和 Transform。
- `.pworld` 对象图保存、加载、事务式 World 替换和引用修复。
- Editor World 文档：New、Open、Save、Save As、Dirty 状态和未保存保护。
- AssetRegistry、Content Browser、OBJ 导入、StaticMesh、纹理、材质和基础 PBR。
- RenderScene 缓存、Camera、SpringArm、DirectionalLight 和 PointLight。
- Outliner、Details、选择、Gizmo、Undo/Redo、复制粘贴和编辑器事务。
- 独立 `PicoGame` Runtime、输入 Action/Axis 映射和地图覆盖。
- 编辑器绿色 Play、红色 Stop 和独立 Game World 进程。
- Game Module、GameInstance、通用 `PicoGameRuntime` 和项目专属 `PicoSandboxGame` Target。
- 临时 `PSandboxPawn` WASD 输入链路，用于证明项目代码可以进入 Runtime。
- CDO、默认子对象、Native Delegate、`PFunction/ProcessEvent`、PicoHeaderTool 和 Stop-the-world Mark-Sweep GC。
- Dynamic Multicast Delegate、签名校验、弱目标清理，以及 `.pworld` v4 稳定引用和动态绑定恢复。
- 反射属性 Pre/Post 变化通知、ValueSet/Interactive/Load/UndoRedo 来源和 PicoInspector 可视化实验。

当前临时 Pawn 直接读取 `FInputSystem` 并修改 Transform。这只是链路验证，后续必须由 PlayerController、PawnMovementComponent 和统一移动函数替代。

## 固定架构决策

### 系统职责

```text
Gameplay       决定想做什么
Movement       决定怎样移动
Physics        决定是否能移动以及碰撞结果
Animation      表现移动状态或提供 Root Motion
Replication    传递权威状态、输入命令和事件
Delegates      提供跨系统生命周期通知
Tasks          执行纯数据后台工作，并将对象修改投递回 Game Thread
```

- Gameplay 不直接依赖 Jolt、OpenGL 或具体动画后端。
- MovementComponent 是位移、碰撞和网络重演的唯一执行边界。
- Root Motion 必须经过 MovementComponent，动画不能直接设置 Actor Transform。
- 物理通过 `IPhysicsScene/IWorldCollisionQuery` 接口接入。
- 委托先修改内部状态再广播，核心一致性不能依赖监听顺序。
- 网络使用 NetId，不发送指针或本地 ObjectHandle。
- 服务器不信任客户端提交的位置、伤害、分数或其他权威结果。
- `PObject`、World、Actor、Component、反射属性和 GC 对象图只允许 Game Thread 修改。
- Worker 只处理文件、网络包、AI JSON、资源中间数据等纯数据；完成后通过 Game Thread Dispatcher 应用结果。

### 网络范围

半年 MVP 支持公网 Dedicated Server：客户端通过公网 IP 或域名连接服务器。

不自行实现 NAT 穿透、P2P 打洞、Relay 网络、账号平台、匹配系统、主机迁移和反作弊平台。开发顺序固定为：

```text
Loopback -> 本机多进程 -> 局域网 -> 网络模拟 -> 公网 Dedicated Server
```

### 简化范围

- CDO 第一版实现类默认对象、默认属性和默认子对象模板，不复制完整 UE Archetype 系统。
- PicoHeaderTool 只解析受约束的 `PCLASS/PPROPERTY/PFUNCTION`，不实现 Blueprint 和热重载。
- GC 第一版使用 Stop-the-world Mark-Sweep，不实现 UE 的并行、增量和 Cluster GC。
- 多线程第一版只实现固定 Worker Pool、任务状态/取消、Game Thread Dispatcher 和安全关闭；不拆分 Render Thread、Physics Thread、动画任务图或并行 GC。
- CharacterMovement 第一版只实现行走、跳跃、下落、地面检测和基础滑动。
- GAS 只实现 Mini GAS，不复制完整 GameplayTask、TargetActor 和复杂 Effect Aggregator。

## 实施依赖顺序

```text
CDO / ObjectInitializer
 -> PFunction / Delegate
 -> PicoHeaderTool
 -> Mark-Sweep GC
 -> Dynamic Multicast Delegate
 -> Gameplay Framework
 -> Movement / Physics / Animation
 -> Replication / RPC
 -> Client Prediction
 -> PicoTask / Game Thread Dispatcher
 -> Cook / Package
 -> Mini GAS / AbilityTask
 -> AI Tool Registry
```

不得在统一移动函数、网络所有权和 RPC 稳定前提前实现客户端预测。

## 第 3 月（已完成）：对象系统核心

目标：补齐 Gameplay、RPC、AI 和编辑器共同依赖的 UE 式对象基础。

| 阶段 | 任务 | 月末验收 |
| --- | --- | --- |
| A | CDO、`GetDefault<T>`、ObjectInitializer、SpawnParams、默认属性继承 | 实例继承 CDO 默认值，父子类 CDO 正确 |
| B | 默认子对象模板和简化 `CreateDefaultSubobject` | Character CDO 可声明 Capsule、Mesh、Movement |
| C | 类型安全委托、DelegateHandle、弱对象绑定 | 对象销毁后回调失效，无悬空绑定 |
| D | `PFunction`、参数/返回值描述、调用 Thunk 和函数标记 | 可通过元数据安全调用 C++ 函数 |
| D.1 | PicoInspector Developer Sandbox：通用函数调用面板、Native Delegate 实验、Event Log 和 Reset | 无需项目即可可视化调用 `PFunction`，添加/移除/销毁监听并观察广播 |
| E | PicoHeaderTool、生成代码和 CMake 依赖 | 项目类无需手写大部分注册模板 |
| F | 强弱对象引用、Root Set、Stop-the-world Mark-Sweep GC | 循环引用可回收，强引用和根对象不会误回收 |
| G | Dynamic Multicast Delegate、签名校验、反射函数绑定和失效监听清理 | 可通过对象引用与函数名绑定多个 `PFunction`，并经 `ProcessEvent` 安全广播 |
| H | 动态委托稳定引用序列化、引用修复、属性变化通知和编辑器事务适配 | `.pworld` 加载后恢复动态绑定；CDO、实例和反射属性修改均能正确通知 |

阶段 A、B、C、D、D.1、E、F、G、H 已完成。CDO 与统一构造链见
[`Month03_13_ClassDefaultObjects.md`](Month03_13_ClassDefaultObjects.md)，默认子对象模板、继承、
World 重建复用和编辑器限制见
[`Month03_14_DefaultSubobjects.md`](Month03_14_DefaultSubobjects.md)，Native Delegate、广播变更语义和
弱对象绑定见 [`Month03_15_NativeDelegates.md`](Month03_15_NativeDelegates.md)，函数元数据、调用 Thunk、
`ProcessEvent` 和 RPC Flags 边界见
[`Month03_16_ReflectedFunctions.md`](Month03_16_ReflectedFunctions.md)。PicoInspector 的通用函数调用、
Native Delegate 实验、Event Log、Reset 和可调 UI Scale 见
[`PicoInspector Developer Sandbox`](PicoInspector_DeveloperSandbox_Plan.zh-CN.md)。PicoHeaderTool 的
Token 解析、生成文件、CMake 增量依赖和项目类迁移见
[`Month03_17_PicoHeaderTool.md`](Month03_17_PicoHeaderTool.md)。Stop-the-world Mark-Sweep、强弱对象引用、
Root Set、原生引用上报和 Inspector GC 实验见
[`Month03_18_GarbageCollection.md`](Month03_18_GarbageCollection.md)。动态多播委托、签名校验、
广播快照、弱绑定清理和Inspector综合实验见
[`Month03_19_DynamicMulticastDelegates.md`](Month03_19_DynamicMulticastDelegates.md)。稳定引用序列化、
动态绑定修复、属性变化通知和编辑器事务适配见
[`Month03_20_StableReferencesAndPropertyNotifications.md`](Month03_20_StableReferencesAndPropertyNotifications.md)。
第 3 月对象系统核心已经完成，下一阶段进入第 4 月 Gameplay Framework。

动态多播委托安排在 HeaderTool 与 GC 之后：运行时绑定保存“弱对象引用 + 函数名”，广播时通过
`PClass::FindFunction` 和 `PObject::ProcessEvent` 调用；持久化不得保存本次运行的 `FObjectHandle`，
而应在对象图重建后通过 SceneId/ObjectPath 修复稳定引用。第一版不要求 Dynamic Single-cast。

必须覆盖 CDO 继承、默认子对象、反射函数错误参数、强弱引用、循环引用、Root GC、Native/动态委托销毁安全、动态绑定签名不匹配、序列化引用修复和 HeaderTool 增量生成测试。

## 第 4 月：Gameplay Framework

目标：完成 UE 风格本地游戏启动、玩家创建和控制流程。

| 周次 | 任务 | 月末验收 |
| --- | --- | --- |
| 第 1 周 | TickGroup、TickFunction、prerequisite 和 GameInstance World 生命周期 | Controller、Movement、Pawn 顺序确定 |
| 第 2 周 | LocalPlayer、GameModeBase、GameStateBase、Controller、PlayerController、PlayerState、Pawn、PlayerStart | Gameplay 类型和关系完整 |
| 第 3 周 | Standalone Login、PostLogin、RestartPlayer、Possess、UnPossess、重生和旁观基础 | 本地玩家通过 GameMode 获得 Pawn |
| 第 4 周 | GameMode/GameState Match 状态机、Gameplay 委托和正式编辑器 Events/Bindings 面板 | Waiting、InProgress、PostMatch 可运行；场景可配置签名匹配的动态绑定 |

目标链路：

```text
GameInstance
 -> LocalPlayer
 -> GameMode Login
 -> PlayerController + PlayerState
 -> RestartPlayer
 -> Spawn Pawn
 -> Possess
 -> GameState / MatchState
```

正式编辑器的 Events/Bindings 面板只承担游戏内容配置：显示动态委托绑定，选择 World 内目标对象，
过滤签名匹配的 `PFunction`，支持添加、精确删除、失效目标诊断、Dirty 和 Undo/Redo，并验证 `.pworld`
重新加载后仍可广播。PicoInspector 继续承担 Pre/Post、GC 和 Handle 等底层实验，不把开发调试界面复制进正式编辑器。

## 第 5 月：Movement、物理与动画

目标：形成可被服务器重演、可接物理和动画的统一角色移动。

| 周次 | 任务 | 月末验收 |
| --- | --- | --- |
| 第 1 周 | MovementComponent、PawnMovementComponent、FloatingPawnMovement | 输入经 Controller 和 Movement 驱动 Pawn |
| 第 2 周 | Jolt PhysicsScene、Shape、Body、Raycast、Sweep、Trigger、`FHitResult` | 刚体、查询和碰撞事件可用 |
| 第 3 周 | Character、CharacterMovement，地面检测、行走、跳跃、下落和沿墙滑动 | 角色移动和碰撞稳定 |
| 第 4 周 | Skeleton、SkeletalMesh、AnimationClip、AnimInstance、Idle/Walk/Jump、基础 Root Motion | 动画由移动状态驱动 |

月末 Demo 必须支持 WASD、跳跃、碰撞、推动刚体和 Idle/Walk/Jump 动画。

## 第 6 月：Replication、RPC 与预测

目标：一个服务器和两个客户端完成 Gameplay 同步。

| 周次 | 任务 | 月末验收 |
| --- | --- | --- |
| 第 1 周 | NetDriver、Connection、Transport、握手、NetMode、NetRole、Owner、NetId | 本机客户端可连接服务器 |
| 第 2 周 | ActorChannel、Spawn/Destroy、对象引用、Dirty Tracking、Replication Condition、OnRep | Actor 和属性可复制 |
| 第 3 周 | Server/Client/Multicast RPC、可靠/不可靠、函数反射调用、Ownership 校验 | 交互 RPC 可验证 |
| 第 4 周 | SavedMove、输入序号、服务器重演、Ack/Correction、回滚重演、模拟代理插值 | 高延迟移动可预测和纠正 |

网络职责固定为：GameMode 仅服务器；GameState/PlayerState 对所有客户端；PlayerController 仅服务器和所属客户端；Pawn 对相关连接复制。

## 第 7 月：广域网、PicoTask、Cook 与 Package

目标：产出仓库外可运行的公网双人 Demo。

| 周次 | 任务 | 月末验收 |
| --- | --- | --- |
| 第 1 周 | 延迟、抖动、丢包、乱序模拟，带宽预算和移动平滑 | 网络模拟环境稳定 |
| 第 2 周 | Dedicated Server Target、公网连接、超时、重连、版本校验和限流 | 公网双客户端可连接 |
| 第 3 周 | `PicoTask` Worker Pool、任务状态/取消、Game Thread Dispatcher、安全关闭；接入 Cook 依赖图和 Stage Runtime | 耗时纯数据任务不阻塞编辑器，并生成完整 Stage 目录 |
| 第 4 周 | Development/Shipping Profile、编辑器 Package 命令、仓库外测试 | 独立 EXE 可运行 |

`PicoTask` 的第一版线程规则固定为：后台任务不得直接保存或修改裸 `PObject*`；需要关联对象时保存
`FObjectHandle`，回到 Game Thread 后重新 `ResolveObject`。资产扫描/解码、Cook、外部构建进程和 AI HTTP
请求可以在 Worker 执行，创建对象、设置反射属性、替换 World 和更新编辑器 UI 必须回到 Game Thread。
关闭引擎时必须停止接收任务、取消未开始任务并等待 Worker 退出。

## 第 8 月：Mini GAS、AbilityTask 与 AI

| 周次 | 任务 | 月末验收 |
| --- | --- | --- |
| 第 1 周 | GameplayTag、AttributeSet、AbilitySystemComponent、AbilitySpec | 属性和 Ability 可授予 |
| 第 2 周 | GameplayEffect、Cost、Cooldown、Duration、Periodic 和 Tag 条件 | Effect 生命周期正确 |
| 第 3 周 | AbilityTask、WaitGameplayEvent、WaitDelay、PlayAnimationAndWait、Ability 网络预测 | 异步 Ability 可等待、取消和预测 |
| 第 4 周 | 基于 PicoTask 的异步 DeepSeek/Kimi Provider、Tool Registry、中文命令、保存、构建和打包 | AI 请求不阻塞编辑器，并可驱动受控编辑器命令 |

Mini GAS Demo 包含：

- Dash：Stamina、Cooldown 和本地预测。
- Fireball：Server RPC 生成投射物并应用伤害。
- Stun：持续 Effect 和 `State.Stunned`。
- WaitGameplayEvent：等待命中或动画窗口事件。

AbilityTask 生命周期固定为：

```text
Created -> ReadyForActivation -> Active -> Finished/Cancelled -> Destroyed
```

Ability 强引用 ActiveTasks；Task 的事件绑定使用弱对象委托；Ability 结束或预测被拒绝时必须清理对应 Task。

AI 只能调用受控工具，例如：

```text
CreateActor
SetProperty
AssignAsset
SaveWorld
BuildProject
PackageProject
```

AI 不直接执行模型生成的任意 Shell 命令，也不直接修改未知内存。

## 最终验收 Demo

- 编辑器创建、修改、保存和重新打开完整场景。
- 公网 Dedicated Server 加两个客户端。
- GameState、PlayerState、Pawn、移动、跳跃、拾取和开门同步。
- 客户端预测、服务器纠错和模拟代理平滑。
- Jolt 刚体、碰撞查询和 Trigger。
- Idle、Walk、Jump 和基础 Root Motion。
- Dash、Fireball、Stun 以及 AbilityTask 事件等待。
- 中文创建对象、修改属性、保存并触发打包。
- 资产/Cook/AI 等耗时任务可在后台运行、取消并安全回到 Game Thread 应用结果。
- 输出不依赖编辑器、源码和仓库目录的独立程序。

## MVP 后任务

以下内容不进入当前主线，只有主计划提前完成时才开始：

- ECS Registry、稠密组件存储和 Actor/ECS 桥接。
- 光线追踪。
- 完整 GameplayTask Scheduler 和 TargetData 系统。
- 更完整的 GAS Effect Aggregator 和复杂叠层。
- NAT 穿透、P2P、Relay、匹配和账号平台。
- 语音 Agent 和自动生成复杂 C++ 游戏代码。

若进度延期，首先削减 AI 和可选增强，不削减 CDO、GC、Gameplay Framework、Movement、Replication/RPC、客户端预测和 Package 主链。

## 文档维护规则

- 每完成一个阶段，更新对应表格和“当前完成基线”。
- 新计划不得重新列入已经验收完成的任务。
- 改变系统边界、网络范围或最终 Demo 时，必须同步修改“固定架构决策”。
- 后续对话和实施均以本文档为优先依据。
