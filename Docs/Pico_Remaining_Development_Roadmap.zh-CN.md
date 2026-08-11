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

## 阶段准入门槛

以下项目用于控制物理、动画、网络、多线程和打包的返工风险。建议时间用于安排实现，最晚时间是进入依赖系统前必须通过的硬门槛。

| 风险控制项 | 建议时间 | 最晚时间/准入条件 | 验收 |
| --- | --- | --- | --- |
| Game Thread 身份、`IsInGameThread/CheckGameThread` 和关键对象入口断言 | 第 4 月第 1 周 | 第 4 月结束前 | Worker 误调用对象创建、销毁、GC、反射写入或 World 修改时立即失败 |
| TickGroup、TickFunction 和 prerequisite | 第 4 月第 1 周 | Gameplay 类型开始参与 Tick 前 | Controller、Pawn、Movement 和未来 Physics 顺序确定且可测试 |
| Tick 分阶段调度与帧管线边界 | 第 5 月第 2 周 | Jolt 正式驱动 World 前 | `FTickTaskManager` 可单独运行 TickGroup；输入、网络接收、物理、网络发送和 GC 能插入确定的阶段边界 |
| Gameplay Framework 生命周期和所有权关系 | 第 4 月第 2～4 周 | Movement 与 Replication 前 | 本地玩家从 Login 到 Possess、MatchState 的链路完整 |
| 正式编辑器 Events/Bindings | 第 4 月第 4 周 | Gameplay 网络交互配置前 | 可配置签名匹配绑定，支持 Dirty、Undo/Redo、失效诊断和重载恢复 |
| 仓库外最小运行探针 | 第 4 月末 | 第 5 月结束前 | 在仓库外目录运行两帧并列出绝对路径、资源和 DLL 缺口；允许首次失败但必须形成问题清单 |
| `MoveComponent`、Sweep/Teleport、`FHitResult` 和统一移动语义 | 第 5 月第 1 周 | Jolt、Root Motion 和网络移动前 | Gameplay、物理修正、Root Motion 和网络纠错共用移动入口 |
| Jolt 查询与 PhysicsScene 边界 | 第 5 月第 2 周 | CharacterMovement 前 | Raycast、Sweep、Trigger 和刚体同步不绕过 SceneComponent/Movement |
| CharacterMovement 确定性输入与模拟 | 第 5 月第 3 周 | 客户端预测前 | 相同状态与输入可重演行走、跳跃和下落 |
| Skeleton/Pose/Root Motion 边界 | 第 5 月第 4 周 | 动画驱动 Gameplay 前 | 动画输出 Pose/Root Motion，Root Motion 仍经 MovementComponent |
| `FNetObjectId`、NetRole、Ownership，与 ObjectHandle/SceneId 类型隔离 | 第 6 月第 1 周 | ActorChannel 和属性同步前 | 编译期和运行时均不能把本地/磁盘身份当作网络身份 |
| Replication Dirty Tracking 和网络对象引用 | 第 6 月第 2 周 | RPC 和预测前 | 权威属性、Spawn/Destroy 和对象引用可复制 |
| RPC 方向、Role、Ownership 和参数校验 | 第 6 月第 3 周 | Gameplay 网络交互前 | 非法客户端调用被拒绝且没有副作用 |
| 客户端预测、服务器重演与纠错 | 第 6 月第 4 周 | 网络模拟和公网验证前 | 高延迟下可预测、Ack、Correction 和重演 |
| `PicoTask` Worker Pool、Dispatcher、取消和安全关闭 | 第 7 月第 3 周前半 | 异步 Cook、构建和 AI 请求前 | Worker 只产生纯数据，Game Thread 应用对象结果，退出时无遗留线程 |
| Cook/Stage 和仓库外 Runtime 布局 | 第 7 月第 3 周后半 | Package 前 | EXE、Config、Content、项目模块和第三方库形成完整 Stage |
| Development/Shipping Package | 第 7 月第 4 周 | AI Build/Package 工具前 | 独立程序不依赖源码、编辑器或仓库目录 |

顺序上的硬约束为：

```text
Tick/Gameplay
 -> MoveComponent
 -> Physics/CharacterMovement/Animation
 -> NetId/Replication/RPC
 -> Prediction
 -> Network Simulation/WAN
 -> PicoTask
 -> Cook/Stage/Package
 -> Async AI Tools
```

## Tick 当前缺口与目标帧管线

当前 `FTickTaskManager::Tick` 会在一次调用中连续执行 `PrePhysics`、`DuringPhysics`、
`PostPhysics` 和 `PostUpdateWork`。这已经能够验证分组、启停、Tick interval 和 prerequisite，
但 World 无法在组与组之间插入 Jolt 物理步骤。当前 `PGameInstance::Tick` 还位于
`World::Tick + GC` 之后；如果以后在这里读取输入或接收网络数据，Actor 通常只能在下一帧使用结果，
而且 GC 也不再是真正的帧末安全点。

接入 Jolt 前必须完成：

- 将 `FTickTaskManager` 拆为 `BeginFrame/RunTickGroup/EndFrame`，或提供等价的分阶段接口。
- 由 `PWorld` 显式编排各 TickGroup，并在 `PrePhysics` 与 `PostPhysics` 之间插入 `IPhysicsScene`。
- 不再把输入、网络收发和所有玩法更新笼统塞进 `PGameInstance::Tick`；GameInstance 只处理进程级和跨地图逻辑。
- 将 GC 移到本帧对象修改、延迟销毁和网络发送都完成后的统一安全点。
- 第一版 Jolt 采用同步 Step：`DuringPhysics` 代表物理模拟阶段，普通 Gameplay Tick 暂不依赖并行物理；多线程物理以后再扩展。

推荐的最终帧顺序固定为：

```text
BeginFrame / 更新时间
 -> InputSystem 更新输入
 -> NetDriver TickDispatch（接收数据、RPC 和连接事件）
 -> GameInstance 进程级 PreWorld 工作
 -> PrePhysics（Controller -> Movement/Pawn -> 写入物理输入）
 -> DuringPhysics（Jolt Step）
 -> PostPhysics（物理结果 -> SceneComponent、碰撞事件和移动状态）
 -> PostUpdateWork（Animation -> SpringArm -> Camera -> RenderScene）
 -> NetDriver TickFlush（收集并发送 Replication/RPC）
 -> 处理延迟销毁对象
 -> GC 安全点
 -> EndFrame
```

同一 TickGroup 内由 prerequisite 进行拓扑排序；跨组顺序由帧管线保证。输入必须早于 Controller，
Movement 必须早于物理，物理结果必须早于动画、相机和 RenderScene。网络接收位于模拟前，网络发送位于
本帧权威状态稳定后。

### 物理阶段的渐进实现

Pico 参考 UE5 的阶段边界，但不在同步物理阶段提前复制完整的 `StartPhysics/EndPhysics` 复杂度：

```text
第一版同步 Jolt
PrePhysics
 -> DuringPhysics（阻塞式 Jolt Step）
 -> PostPhysics
 -> PostUpdateWork

未来异步/多线程物理
PrePhysics
 -> StartPhysics（提交物理任务）
 -> DuringPhysics（只运行不依赖最终物理结果的任务）
 -> EndPhysics（等待任务并形成同步屏障）
 -> PostPhysics
 -> PostUpdateWork
```

- `PrePhysics` 负责 Controller、Movement、速度、力和本帧物理输入。
- 同步版 `DuringPhysics` 由 `IPhysicsScene::Step` 占据，普通 Gameplay Tick 暂不放入该组。
- `PostPhysics` 负责刚体结果写回 SceneComponent、碰撞/Trigger 通知和落地状态。
- `PostUpdateWork` 负责动画、网络平滑、SpringArm、Camera 和 RenderScene 数据准备。
- 只有确认需要物理与其他任务并行时才增加 `StartPhysics/EndPhysics`，并补充屏障、关闭和确定性测试。

### 网络阶段的渐进实现

网络阶段是 World 模拟外围的系统边界，不为每个网络步骤新增公共 Actor TickGroup：

```text
NetDriver TickDispatch
 -> World Gameplay/Physics Tick
 -> NetDriver TickFlush
```

- `TickDispatch` 在模拟前解析连接、Actor Spawn/Destroy、Replication、OnRep、RPC、Ack 和 Correction。
- `TickFlush` 在本帧权威状态稳定后执行相关性判断、Dirty 属性收集、RPC、Spawn/Destroy、Ack/Correction 和发包。
- 服务器在 Dispatch 后消费客户端输入，完成权威移动和物理，再在 Flush 中发送结果。
- 自主代理在 Dispatch 中处理 Ack/Correction 和必要的回滚重演，在 PrePhysics 生成 `SavedMove` 并本地预测。
- 模拟代理在 Dispatch 中接收快照，主要在 PostUpdateWork 做插值和平滑，不运行完整的权威移动链。

### 时间步与频率演进

渲染帧、物理步和网络发送频率最终应彼此独立，但按以下顺序渐进实现：

1. 第一版每个渲染帧执行一次同步 Jolt Step、一次 TickDispatch 和一次 TickFlush。
2. 角色移动稳定后增加最大 `DeltaSeconds` 限制和固定物理步长。
3. 长帧稳定后增加物理 Substep，并验证碰撞事件不会重复或丢失。
4. Replication 稳定后引入独立网络发送频率和带宽预算，不要求每个渲染帧都发包。
5. 客户端预测稳定后增加输入序号、回滚重演、模拟代理插值和高延迟测试。
6. 只有性能数据证明必要时才接入异步物理；多线程不能改变 Gameplay 可观察的阶段顺序。

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

## 第 4 月（已完成）：Gameplay Framework

目标：完成 UE 风格本地游戏启动、玩家创建和控制流程。

最终完成度：100%。Debug/Release 均为 12/12 CTest 通过，且无源码 Release Stage 两帧探针通过。

| 周次 | 任务 | 月末验收 |
| --- | --- | --- |
| 第 1 周 | Game Thread 身份与关键对象入口断言；TickGroup、TickFunction、prerequisite 和 GameInstance World 生命周期 | 非主线程对象访问可立即发现；Controller、Movement、Pawn 顺序确定 |
| 第 2 周 | LocalPlayer、GameModeBase、GameStateBase、Controller、PlayerController、PlayerState、Pawn、PlayerStart | Gameplay 类型和关系完整 |
| 第 3 周 | Standalone Login、PostLogin、RestartPlayer、Possess、UnPossess、重生和旁观基础 | 本地玩家通过 GameMode 获得 Pawn |
| 第 4 周 | GameMode/GameState Match 状态机、Gameplay 委托和正式编辑器 Events/Bindings 面板 | Waiting、InProgress、PostMatch 可运行；场景可配置签名匹配的动态绑定 |

第 1 周已完成并通过 Debug 验收：Game Thread 身份与关键对象入口检查、Actor/Component
`FTickFunction`、四级 `TickGroup`、同 World prerequisite、Tick interval、运行时启停，以及反射化
`PGameInstance` 的 `Init/WorldInitialized/Tick/WorldCleanup/Shutdown` 生命周期。实现与测试说明见
[`Month07_1_GameThreadTickAndGameInstance.md`](Month07_1_GameThreadTickAndGameInstance.md)。第 2 周在这些
基础设施之上继续搭建 Gameplay 类型和所有权关系，不重复实现本周内容。

第 2 周已完成：新增 `PLocalPlayer`、`PGameModeBase`、`PGameStateBase`、`PController`、
`PPlayerController`、`PPlayerState`、`PPawn` 和 `PPlayerStart`；GameMode CDO 提供默认 Gameplay
类配置，GameInstance 持有跨地图 LocalPlayer，World 持有运行时 GameMode/GameState，PlayerStart 可由
编辑器创建并持久化。详见
[`Month07_2_GameplayFrameworkTypes.md`](Month07_2_GameplayFrameworkTypes.md)。

第 3 周已完成：新增 `PPlayer` 抽象并让 `PLocalPlayer` 通过 GameMode 完成 `Login/PostLogin`；
`PController` 提供成对的 `Possess/UnPossess`，`RestartPlayer` 通过场景 `PlayerStart` 创建并占有默认 Pawn，
`Logout` 和地图替换会按所有权顺序清理当前 World 对象。Sandbox 输入已迁移到项目 PlayerController，
运行时新增可切换的 Gameplay Debug 面板，编辑器新增 PlayerStart 可视化和 Play 前校验。实现和逐步验收见
[`Month07_3_StandaloneLoginPossessAndGameplayDebug.md`](Month07_3_StandaloneLoginPossessAndGameplayDebug.md)。

第 4 周已完成：`PGameModeBase` 负责合法 MatchState 转换，`PGameStateBase` 保存可观察状态和仅在
`InProgress` 累加的比赛时间；PostLogin、Logout、Possess/UnPossess 和 MatchState 均提供 Native
生命周期委托。`PPlayerStart::OnPlayerSpawnedEvent` 作为正式动态事件示例，编辑器 Details 新增
`Events & Bindings`，支持同 World 目标选择、签名匹配函数过滤、添加/删除、Dirty、Undo/Redo 和
`.pworld` 重载修复。Gameplay Debug 可观察状态、计时、绑定数和广播数。实现与逐步验收见
[`Month07_4_MatchStateGameplayEventsAndBindings.md`](Month07_4_MatchStateGameplayEventsAndBindings.md)。
第 4 月 Gameplay Framework 主链完成，下一项进入第 5 月第 1 周统一移动入口。

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

第 4 月结束时执行一次早期仓库外运行探针：把 `PicoSandboxGame`、最低 Config/Content 和必要 DLL 放入
仓库外临时目录并运行两帧。首次探针允许失败，但必须记录编译期绝对路径、默认项目定位、缺失资源、
第三方库和 Runtime 对 Editor/源码目录的隐式依赖；正式 Cook/Package 仍在第 7 月完成。

首次探针发现源码树识别依赖后已立即加固：`FPaths` 正式区分 Development、Installed 和 Staged；
安装版使用 `PicoEngine.root`，Stage 使用 `PicoStage.manifest`，并支持互斥的 `-engineroot/-stageroot`。
显式无效路径不得回退，Manifest 内 Engine/Project 相对路径不得逃逸 Stage，Engine 与项目名称/版本会在
PreInit 校验。第二次 Release 探针不携带 Source、CMakeLists 或项目参数，从仓库外工作目录自动定位项目，
加载 13 个资产、运行两帧并以 0 退出。正式 Cook、Stage 文件收集和 Package UI 仍按第 7 月执行，但源码树
运行依赖已经解除。详见
[`Month07_5_DevelopmentInstalledAndStagedLayouts.md`](Month07_5_DevelopmentInstalledAndStagedLayouts.md)。

## 第 5 月：Movement、物理与动画

目标：形成可被服务器重演、可接物理和动画的统一角色移动。

| 周次 | 任务 | 月末验收 |
| --- | --- | --- |
| 第 1 周 | `MoveComponent`、Sweep/Teleport、`FHitResult`、MovementComponent、PawnMovementComponent、FloatingPawnMovement | 输入经 Controller 和唯一移动入口驱动 Pawn |
| 第 2 周 | 拆分 TickGroup 分阶段执行；接入 Jolt PhysicsScene、Shape、Body、Raycast、Sweep、Trigger 和 Transform/Body 同步规则 | Jolt Step 位于 Pre/PostPhysics 之间；刚体、查询和碰撞事件可用且不绕过移动边界 |
| 第 3 周 | Character、CharacterMovement，确定性输入、地面检测、行走、跳跃、下落和沿墙滑动 | 角色移动稳定，相同状态与输入可供服务器重演 |
| 第 4 周 | Skeleton、SkeletalMesh、AnimationClip、AnimInstance、Idle/Walk/Jump、基础 Root Motion | 动画由移动状态驱动，Root Motion 经 MovementComponent |

月末 Demo 必须支持 WASD、跳跃、碰撞、推动刚体和 Idle/Walk/Jump 动画。

## 第 6 月：Replication、RPC 与预测

目标：一个服务器和两个客户端完成 Gameplay 同步。

| 周次 | 任务 | 月末验收 |
| --- | --- | --- |
| 第 1 周 | `INetTransport/FUdpTransport`、平台 Socket、NetDriver、Connection、握手、Packet Sequence、Ack/AckBits、心跳/超时、可靠队列、NetMode、NetRole、Ownership、`FNetObjectId` 与身份类型隔离 | 本机客户端可通过 UDP 连接服务器；可靠消息可确认/重发；网络身份不复用 ObjectHandle/SceneId |
| 第 2 周 | ActorChannel、Spawn/Destroy、网络对象引用、Dirty Tracking、Replication Condition、OnRep、每连接属性基线和 Delta | Actor 和属性可复制；未变化字段不重复发送；客户端确认的基线可用于后续增量 |
| 第 3 周 | Server/Client/Multicast RPC、可靠/不可靠、函数反射调用、方向/Role/Ownership/参数校验 | 合法交互 RPC 可执行，非法调用零副作用 |
| 第 4 周 | SavedMove、输入序号、服务器重演、Ack/Correction、纠错快照、未确认输入回滚重演、模拟代理快照缓冲与插值 | 高延迟下所属角色可预测和纠正；其他角色可平滑显示；重演和快照缓冲均有上限 |

网络职责固定为：GameMode 仅服务器；GameState/PlayerState 对所有客户端；PlayerController 仅服务器和所属客户端；Pawn 对相关连接复制。

### 网络模型决策

Pico 采用 UE5 常规 Gameplay 网络相近的“UDP + 服务器权威状态同步 + 局部客户端预测”，不采用要求整个
World 跨机器确定性运行的全局锁步帧同步。

```text
服务器负责权威状态
所属客户端负责输入预测、Ack/Correction 和未确认输入重演
模拟代理客户端负责快照缓冲、插值和视觉平滑
```

传输层和复制层保持分离：

```text
Platform Socket
 -> INetTransport / FUdpTransport
 -> NetConnection（Sequence、Ack、可靠队列、超时）
 -> NetDriver / ActorChannel
 -> Replication、RPC、Prediction
```

- UDP 不直接等于“不可靠 Gameplay”：可靠 RPC 在 UDP 上通过序号、确认、重发和有序交付实现；移动输入与高频状态允许不可靠发送并用新数据覆盖旧数据。
- 第一版限制单包和 RPC 大小；只有实际 Demo 需要时才加入分片，禁止把任意大对象直接塞进可靠队列。
- `FNetObjectId` 是网络身份；不能发送裸指针、本地 `FObjectHandle` 或磁盘 `SceneId` 代替网络引用。
- 状态同步使用服务器 Actor/属性的网络序列化结果，绝不复制 `PObject` 内存布局。
- 属性快照基线记录某连接已经确认的 Replicated 状态，用于 Dirty Tracking 和 Delta；它不是每帧发送完整 World 快照。
- 纠错快照至少包含服务器 Tick、权威 Transform/Velocity、移动模式和 `LastProcessedInput`，用于自主代理回滚重演。
- 插值快照按远端 Pawn 保存有限长度的时间序列；模拟代理在较旧的两个样本之间插值，不运行完整权威 CharacterMovement。
- “追帧”仅指固定物理步补步和客户端未确认输入重演，不让整个 World 追赶服务器渲染帧。
- 固定步补帧必须限制 `MaxSubsteps/MaxAccumulatedTime`；预测重演必须限制 Pending Move 数量和单帧重演次数，避免慢帧形成无法追平的循环。

### 快照、重连与回放边界

Pico 只使用结构化网络快照，禁止发送或恢复 `PObject` 的原始内存镜像。快照字段必须经过网络 Schema、
量化和序列化，不能包含虚表、裸指针、容器内部地址或本地 Handle。第一版区分三类内存状态：

```text
属性基线：某连接已确认的 Replicated 字段，用于 Delta
纠错快照：ServerTick、Transform、Velocity、MovementMode、LastProcessedInput
插值快照：远端 Pawn 的有限时间序列，用于模拟代理平滑
```

短线重连采用“重新建立连接并恢复当前权威状态”，不采用“重放断线期间所有帧”：

```text
断线
 -> 销毁旧 Connection/PlayerController
 -> 服务器保存有限时长 InactivePlayerRecord
 -> 客户端凭 StablePlayerId + SessionToken 重新握手
 -> 创建新 Connection/PlayerController
 -> 恢复 PlayerState，并重新关联或重生 Pawn
 -> 重新复制当前相关 Actor
 -> 重建快照缓冲并恢复正常模拟
```

- `InactivePlayerRecord` 至少保存稳定玩家身份、允许恢复的 PlayerState 数据、Pawn 恢复策略和过期时间。
- 第一版重连窗口建议 30～60 秒，最终数值通过公网测试确定；过期记录必须安全清理。
- 不能用旧 UDP 地址识别玩家，也不能复用已经关闭的 Connection 或 PlayerController。
- 重连后直接接受服务器当前状态；不使用 8x/16x 追赶整个 World，也不补发断线期间所有瞬时 RPC。
- 需要跨断线保留的结果必须进入权威持久状态，例如 PlayerState、GameState 或可重新复制的 Actor 属性。

完整 Replay/DemoNetDriver 不进入 MVP。MVP 后可复用网络 Schema 记录 Actor Spawn/Destroy、Replicated
属性、RPC/Gameplay Event 和输入流，并周期性保存结构化 Checkpoint。跳转时加载最近 Checkpoint，再在时间
预算内批量处理到目标时间；`1x/2x/4x/8x` 仅作为可选播放速度，在线预测重演和断线重连不绑定固定倍率。

MVP 明确不实现完整 Iris、Replication Graph、全局确定性锁步、跨平台确定性物理、Replay/DemoNetDriver、
P2P/NAT 穿透和商业级拥塞控制。对应概念保留扩展点，但不得阻塞双人 Demo。

网络验收必须同时覆盖：正常连接、可靠消息丢包重发、重复/乱序包、非法 RPC、Actor Spawn/Destroy、
属性 Delta、对象引用、客户端预测纠错、模拟代理插值，以及连接断开后 Channel/对象引用的安全清理。

## 第 7 月：广域网、PicoTask、Cook 与 Package

目标：产出仓库外可运行的公网双人 Demo。

| 周次 | 任务 | 月末验收 |
| --- | --- | --- |
| 第 1 周 | 延迟、抖动、丢包、乱序模拟，带宽预算和移动平滑 | 网络模拟环境稳定 |
| 第 2 周 | Dedicated Server Target、公网连接、超时、基于 StablePlayerId/SessionToken 的短线重连、InactivePlayerRecord、版本校验和限流 | 公网双客户端可连接；断线后在窗口期内恢复 PlayerState 和当前权威状态，无需追赶断线帧 |
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
- 短线重连后恢复 PlayerState、重新获得 Pawn，并从当前权威状态继续游戏。
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
- 基于网络 Schema 的 Replay：结构化 Checkpoint、时间跳转、事件重放和可选倍速播放。
- 语音 Agent 和自动生成复杂 C++ 游戏代码。

若进度延期，首先削减 AI 和可选增强，不削减 CDO、GC、Gameplay Framework、Movement、Replication/RPC、客户端预测和 Package 主链。

## 文档维护规则

- 每完成一个阶段，更新对应表格和“当前完成基线”。
- 新计划不得重新列入已经验收完成的任务。
- 改变系统边界、网络范围或最终 Demo 时，必须同步修改“固定架构决策”。
- 后续对话和实施均以本文档为优先依据。
