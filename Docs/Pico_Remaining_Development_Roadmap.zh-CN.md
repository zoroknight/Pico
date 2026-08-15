# Pico 剩余开发路线

本文档是 Pico 后续开发的长期基准，用于避免因对话上下文压缩、计划迭代或项目月份混淆而遗忘关键目标。

计划中的“月份”均指项目开发月份，不是自然月。项目第 5 月主线与 Montage Lite/人物装配已经验收完成；
进入项目第 6 月网络主线前的角色控制与摄像机策略加固已经完成。已经完成的工作只记录为基线，
不重复列入剩余任务。

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
- `PCharacter`、Capsule 和 CharacterMovement：Controller -> 输入缓存 -> 确定性移动模拟 -> MoveComponent。
- 第三人称模板基础链：ControlRotation 驱动 SpringArm，WASD 转换为世界移动意图，角色按移动方向平滑转身。
- UE 风格 SpringArm TargetRotation：ControlRotation 与组件 RelativeRotation 分离，支持 Pitch/Yaw/Roll
  继承开关；角色转身只移动摄像机跟随原点，不改变自由视角 Camera Yaw。
- `ControlRotation/ActorRotation/World` 移动参考系、FreeLook/Strafe、UE 风格旋转优先级、CameraActor 与 ViewTarget。
- `.pblueprint` Data-Only Actor 资产、动态生成 `PClass`、独立 CDO/默认子对象模板、场景生成类身份，以及
  Components/Preview/Details 角色装配编辑器。
- Actor World、Component Relative、Character Profile Visual 三层 Transform；模型源坐标修正不污染
  Gameplay/Physics/Replication 使用的 Actor Transform。
- `PicoPhysicsCore/PicoPhysicsJolt` 后端隔离、固定 60 Hz 物理步、查询、Trigger、动态刚体和角色推动。
- Pico 原生 Skeleton/SkeletalMesh/AnimationClip、AnimInstance、CPU 蒙皮、Idle/Walk/Jump 和可碰撞 Root Motion。
- Assimp glTF/GLB 与实验性 FBX 骨骼导入、命令行导入工具，以及独立临时 Preview World 中的动画预览。
- CDO、默认子对象、Native Delegate、`PFunction/ProcessEvent`、PicoHeaderTool 和 Stop-the-world Mark-Sweep GC。
- Dynamic Multicast Delegate、签名校验、弱目标清理，以及 `.pworld` v4 稳定引用和动态绑定恢复。
- 反射属性 Pre/Post 变化通知、ValueSet/Interactive/Load/UndoRedo 来源和 PicoInspector 可视化实验。

项目输入由 PlayerController/Gameplay Policy 转换为世界空间移动意图，Pawn 只缓存输入，MovementComponent
消费后经统一移动函数修改 Transform。输入参考系属于 Gameplay 策略，不写死在 Engine；Jolt、Root Motion
和网络移动不得新增旁路。详细设计见
[`Month08_8_CharacterControlAndCameraPolicy.md`](Month08_8_CharacterControlAndCameraPolicy.md)。

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
- 可视化脚本第一版只实现 `PicoGraph Lite` Event Graph、受限节点、编译和字节码 VM；完整 UE Blueprint、
  Kismet、热重载和调试器不进入 MVP。
- 已完成的 Data-Only Actor Blueprint 是“资产 -> GeneratedClass -> CDO/组件模板 -> 实例”的数据层，
  不等同于 PicoGraph；PicoGraph 后续复用这条生成类链，只增加行为图和执行表示。

### 长期扩展边界

现有对象、World、Gameplay、编辑器和项目边界适合继续扩展；增加新的 Component、资产类型、玩法类和
编辑器面板不应要求重写基础对象系统。后端替换能力则取决于具体模块是否隔离第三方类型，后续实现必须
遵守以下约束：

Pico 从 UE5 学习的是“高层使用引擎自己的稳定概念，第三方后端在模块边界内适配”，不是要求所有后端
都能无成本、运行时热切换。UE 的渲染通过 RHI 高度隔离 D3D12/Vulkan 等图形 API；物理通过 PhysicsCore、
PhysicsInterface 和 Handle 控制 Chaos/历史 PhysX 的影响范围，但更换物理后端仍是大型工程；动画则重点
分离 `SkeletalMeshComponent`、`AnimInstance/AnimInstanceProxy`、Pose 求值和渲染数据，而不是提供一个
可任意替换整个动画系统的万能接口。

Pico 的隔离分为三级：

| 层级 | 要求 | 当前决策 |
| --- | --- | --- |
| 模块隔离 | Engine/Gameplay 依赖 Pico 接口，第三方库由独立适配模块实现 | 物理、动画导入和未来图形 API 必须遵守 |
| 类型隔离 | 公共接口、反射、序列化和网络只使用 Pico 描述、Handle 和纯数据 | 从第 5 月开始作为硬门槛 |
| 任意后端热切换 | 插件发现、多后端并存、运行时切换和覆盖每个后端函数的统一虚接口 | 不进入 MVP，没有第二种实现和真实需求时不提前抽象 |

抽象只建立在稳定概念上，不为第三方库的每个函数机械增加包装。当前必须隔离物理后端；动画采用数据
分层而不设计 `IAnimationBackend`；完整 RHI 延后到复杂渲染阶段，届时由第二种图形 API 或 Render Graph
的真实需求校验接口设计。

- 物理公共接口只暴露 Pico 自己的 Shape/Body 描述、`FPhysicsBodyHandle`、查询参数和 `FHitResult`；
  Jolt 的类型、对象地址和生命周期只能存在于 Physics 适配层内部。
- 动画分为资产数据、Pose/Root Motion 计算和渲染数据三层；`SkeletalMeshComponent` 不持有 OpenGL
  资源，`AnimInstance` 不直接修改 Actor Transform。
- Render Thread、Physics Worker 和资源 Worker 不直接持有或修改 Game Thread 的 `PObject`；跨线程只
  传递不可变快照、稳定 Handle、命令和纯数据结果。
- 网络、物理、动画和渲染不得把本地 `FObjectHandle` 当作跨进程、磁盘或后端资源身份。
- 资产格式继续使用显式版本和依赖信息，复杂资产导入结果与具体导入库类型隔离。

当前渲染可以继续承载 OpenGL 下的基础阴影、蒙皮和后处理，但 `FSceneViewportRenderer` 仍集中承担
World 遍历、GPU 资源、Shader、Picking、辅助线和绘制。进入延迟渲染、多线程渲染、Render Graph、
Vulkan/DirectX 12 或硬件光线追踪前，必须先完成：

```text
PrimitiveComponent
 -> PrimitiveSceneProxy / MeshBatch
 -> RenderScene
 -> RenderPass / RenderGraph
 -> RHI
 -> OpenGL / Vulkan / DirectX 12
```

第 5 月动画接入只建立通用 SkeletalMesh Render Data 和蒙皮提交边界，不提前复制完整 RHI。复杂渲染功能
开始前再安排独立 Render Architecture 阶段，拆分 `FSceneViewportRenderer` 并消除公共接口中的 OpenGL
过程加载器、纹理 ID 和直接 GL 资源语义。

### 人物与动画资产架构

人物不是单个模型文件，而是由独立资产和运行时组件装配出的对象。Pico 固定采用与 UE5 相近的职责分离，
但第一版不复制完整 Persona、Animation Blueprint 和 Retargeting：

```text
Source glTF/GLB/FBX/PNG                    仅用于编辑器导入与 Reimport
  -> Skeleton (.pskeleton)                骨骼层级、参考姿势、逆绑定矩阵
  -> SkeletalMesh (.pskeletalmesh)        几何、蒙皮权重、Material Slots、Skeleton 引用
  -> AnimationClip (.panimation)          骨骼轨道、时长、Skeleton 引用
  -> Texture (.ptex)                      材质使用的图像/数据
  -> Material (.pmat)                     PBR 参数与 Texture 引用
  -> AnimationSet (.panimset，近期新增)   Idle/Walk/Jump 等移动动画映射
  -> Montage (.pmontage)                  Clip Segment、Section、Notify 和玩法结束语义

PCharacter                                World 中的 Gameplay Actor
  -> CapsuleComponent                     权威碰撞外形
  -> CharacterMovementComponent           移动、碰撞、预测与 Root Motion 执行
  -> SkeletalMeshComponent                Mesh、材质覆盖和动画播放
       -> AnimInstance                    读取移动状态，选择 AnimationSet/Montage 并求 Pose
```

资产关系必须遵守：

- Skeleton 是 SkeletalMesh 与 AnimationClip 的兼容契约；一个 Skeleton 可以被多个兼容 Mesh 和 Clip 引用，
  但没有 Retargeting 时不得仅凭“都是人形”就共享动画。
- SkeletalMesh 的目标结构保存 Material Slot 名称和可选默认材质引用；`PSkeletalMeshComponent` 按 Slot
  覆盖材质。当前实现只有 Section Slot 名称和组件单材质引用，需在最小人物配置阶段迁移。Material 与
  Texture 始终独立成资产，禁止把完整材质数据复制进每个 Mesh。
- AnimationClip 只保存骨骼动作数据；速度、MovementMode、当前播放时间、Actor/Component 指针属于运行时状态。
- AnimInstance 消费 CharacterMovement 的速度和模式，不让动画反向修改 Gameplay；所有 Root Motion 继续经
  CharacterMovement 和 Sweep 执行。
- Montage 引用 AnimationClip Segment，不重复保存关键帧；网络不复制最终 Pose、骨骼矩阵、纹理或材质数据。
- Character 的胶囊、移动参数、生命值和网络 Role 不属于 SkeletalMesh 资产，替换视觉角色不得改变移动主链。

Pico 中必须区分三类身份：

| 身份 | 示例 | 用途与持久化规则 |
| --- | --- | --- |
| 源/磁盘路径 | `Content/Source/Characters/Hero/Hero.glb` | 仅编辑器导入元数据受控保存，不写进 Gameplay/World 资产引用 |
| 虚拟资产路径 | `/Game/Characters/Hero/Hero.pskeletalmesh` | 资产、组件、场景、Montage 和 Cook 依赖的稳定身份 |
| 运行时指针/Handle | `PSkeletalMeshComponent*`、`FObjectHandle` | 只在当前进程解析，禁止写入磁盘或跨网络发送 |

推荐项目布局为：

```text
Content/
  Source/Characters/Hero/                 可重导入源文件，不进入 Shipping Stage
    Hero.glb
    Textures/
  Characters/Hero/                        Pico 原生运行时资产
    Hero.pskeleton
    Hero.pskeletalmesh
    Hero.panimset
    Animations/
    Montages/
    Materials/
    Textures/
```

`AssetRegistry` 发现类型、虚拟路径和依赖元数据；`AssetManager` 按 `/Game` 路径加载并缓存 CPU 资产；运行时
组件只保存稳定资产引用，不保存 Assimp 对象、OpenGL ID 或本机绝对路径。开发期 Assimp 将源数据转换为
Pico 原生资产，Shipping Runtime 和最终 Stage 不依赖 Assimp，也不读取 glTF/FBX。

近期新增轻量 `PAnimationSet`，第一版只保存 Skeleton、Idle、Walk 和 Jump，随后可增加 Run、JumpStart、
JumpLoop 与 Land。`PAnimInstance` 从 AnimationSet 取 Clip，替代 Sandbox 中硬编码的三条路径；这比当前
直接实现完整 AnimGraph 更适合学习阶段。

`PCharacterVisualProfile` 的需求已由真实 Knight 多材质角色验证，并以轻量 `.pcharprofile` 落地。它集中保存
SkeletalMesh、AnimationSet、DefaultMontage 和四个 Material Override，只承载稳定 `/Game` 引用，不保存
Actor 对象图、后端对象或绝对路径。`PSkeletalMeshComponent` 可反射选择 Profile，Sandbox 还可通过项目
`[Game] DefaultPawnProfile` 设置默认角色外观，因此导入新角色不再要求修改 Pawn CDO。相对 Transform 仍属于
角色类的组件布局；未来出现多种骨架布局时，再升级为完整 Actor Archetype/Prefab，而不把布局职责塞进 Profile。

### 人物资产导入、许可与 Reimport

人物导入固定分为两阶段：

```text
Quick Preview
  Source -> Assimp 内存结果 -> 临时 Rooted Preview World
  不落盘、不修改地图、不产生 Dirty

Import To Project
  校验 -> 生成独立 Pico 原生资产 -> 修复 /Game 引用
       -> 刷新 AssetRegistry/AssetManager -> 选择正式资产
```

完整 Reimport 实现时，每组导入结果至少保存：`SourceFile`（项目相对路径）、源文件 Hash、Importer Version、
单位/坐标转换、生成资产列表和 Material Mapping。Reimport 必须保持已有 `/Game` 身份与用户材质覆盖；骨架
层级、名称或参考姿势发生不兼容变化时必须中止并报告，不得静默破坏 AnimationSet、Montage 或场景引用。

首个人形验收角色优先使用许可清晰、结构简单、单骨架并自带 Idle/Walk/Jump 的 CC0 glTF/GLB；Manny/Quinn
仅作为第二个复杂兼容性用例，验证 UE Template 导出、辅助骨骼、权重和坐标转换。任何第三方或 Epic 资产
进入公开仓库前必须记录来源、作者、许可证和修改，并与 Pico 代码许可证分开；不得把 UE-Only、
Reference-Only 或来源不明资产提交到仓库。转换成 Pico 原生格式不会消除原资产许可证。

## 实施依赖顺序

```text
CDO / ObjectInitializer
 -> PFunction / Delegate
 -> PicoHeaderTool
 -> Mark-Sweep GC
 -> Dynamic Multicast Delegate
 -> Gameplay Framework
 -> Movement / Physics / Animation
 -> Montage Lite（当前 3～5 天收尾，不扩展为完整动画编辑器）
 -> AnimationSet + 最小人物场景配置（1～2 天，不阻塞网络底层）
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
| 第三方物理与动画后端隔离 | 第 5 月第 2～4 周 | Jolt Body 或 GPU 蒙皮资源进入 Gameplay/反射属性前 | 公共接口和序列化数据不包含 Jolt、Assimp 或 OpenGL 对象；更换后端只影响适配层和资源转换层 |
| Montage Lite 的 Section/Notify/Slot/Root Motion 边界 | 当前第 5.5 阶段，限时 3～5 天 | 第 8 月 PlayAnimationAndWait 前 | Section 跳转、NotifyWindow、中断清理和 Root Motion Sweep 可测；AbilityTask 不依赖动画内部裸指针 |
| AnimationSet 与最小人物场景配置 | Montage Lite 后 1～2 天 | 最终人形 Demo 与 Cook 依赖图前 | Details 可配置 Mesh、AnimationSet 和按 Slot 材质覆盖；`.pworld` 重载恢复；PAnimInstance 不硬编码项目资产路径 |
| 人物 Source Metadata 与 Reimport | 第 7 月 Cook 前 | Shipping Stage 收集前 | 保持 `/Game` 身份重导入；骨架不兼容时中止；用户 Material Override 和依赖引用不丢失 |
| `FNetObjectId`、NetRole、Ownership，与 ObjectHandle/SceneId 类型隔离 | 第 6 月第 1 周 | ActorChannel 和属性同步前 | 编译期和运行时均不能把本地/磁盘身份当作网络身份 |
| Replication Dirty Tracking 和网络对象引用 | 第 6 月第 2 周 | RPC 和预测前 | 权威属性、Spawn/Destroy 和对象引用可复制 |
| RPC 方向、Role、Ownership 和参数校验 | 第 6 月第 3 周 | Gameplay 网络交互前 | 非法客户端调用被拒绝且没有副作用 |
| 客户端预测、服务器重演与纠错 | 第 6 月第 4 周 | 网络模拟和公网验证前 | 高延迟下可预测、Ack、Correction 和重演 |
| `PicoTask` Worker Pool、Dispatcher、取消和安全关闭 | 第 7 月第 3 周前半 | 异步 Cook、构建和 AI 请求前 | Worker 只产生纯数据，Game Thread 应用对象结果，退出时无遗留线程 |
| 依赖裁剪 Cook 与正式 Stage | 第 7 月第 3 周后半；Development Stage V1 已完成 | 正式 Package 前 | 从当前全量原生资产 Stage 升级为地图根依赖闭包；EXE、Config、Content、项目模块和第三方库完整 |
| Shipping、Client/Server Package | 第 7 月第 4 周；单机 Development Package V1 已完成 | AI Build/Package 工具前 | Client/Server 独立程序不依赖源码、编辑器或仓库目录，并通过全新电脑验收 |
| PicoGraph 类型检查、字节码和执行预算 | MVP 后第 9 月 | AI 生成可执行玩法图前 | 非法连线无法编译；循环和单次执行受预算限制；Runtime 不递归遍历编辑器图 |
| RenderProxy、RenderScene 和 RHI 边界 | MVP 后复杂渲染阶段 | 延迟渲染、Render Graph、多线程渲染、Vulkan/DX12 或光线追踪前 | Renderer 不再直接遍历并绘制 `PObject`；公共接口不暴露 OpenGL ID，后端可通过 RHI 替换 |

顺序上的硬约束为：

```text
Tick/Gameplay
 -> MoveComponent
 -> Physics/CharacterMovement/Animation
 -> Montage Lite（限时收尾）
 -> AnimationSet/最小人物配置
 -> NetId/Replication/RPC
 -> Prediction
 -> Network Simulation/WAN
 -> PicoTask
 -> Cook/Stage/Package
 -> Async AI Tools
 -> PicoGraph Lite
```

## Tick 当前缺口与目标帧管线

第 5 月已经完成 `FTickTaskManager::BeginFrame/RunTickGroup/EndFrame`，`PWorld` 现在显式运行
PrePhysics、DuringPhysics、同步 `IPhysicsScene::Step`、物理结果/事件写回、PostPhysics 和
PostUpdateWork。Jolt 使用固定 60 Hz、每个 World 帧最多 4 个子步；同组 prerequisite 和跨组顺序均有测试。

进入第 6 月后仍需完成的是 World 外围帧编排：当前 `FGameEngine::Tick` 先执行包含 GC 安全点的
`FEngineLoop::Tick`，然后才调用 `PGameInstance::Tick`。网络接入时必须把 `NetDriver::TickDispatch` 放到
World 模拟前，把延迟销毁、GameInstance 帧末工作、`TickFlush` 和 GC 放到权威状态稳定后。GameInstance
只承担进程级、跨地图和明确的 PreWorld/PostWorld 工作，不能成为输入、网络和所有玩法 Tick 的收纳箱。

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
 -> 处理延迟销毁并形成网络 Destroy 记录
 -> GameInstance 进程级 PostWorld 工作
 -> NetDriver TickFlush（收集并发送 Replication/RPC）
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

1. 已完成：同步 Jolt、固定 60 Hz、最大 4 个 Substep、角色长帧迭代上限，以及碰撞/Overlap 去重测试。
2. 第 6 月第 1 周先按每个渲染帧一次 `TickDispatch` 和一次 `TickFlush` 建立正确阶段边界。
3. Replication 稳定后引入独立网络发送频率和带宽预算，不要求每个渲染帧都发包。
4. 客户端预测阶段增加输入序号、回滚重演、模拟代理插值和高延迟测试。
5. 只有性能数据证明必要时才接入异步物理；多线程不能改变 Gameplay 可观察的阶段顺序。

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

## 第 5 月（已完成）：Movement、物理与动画

目标：形成可被服务器重演、可接物理和动画的统一角色移动。

| 周次 | 任务 | 月末验收 |
| --- | --- | --- |
| 第 1 周 | `MoveComponent`、Sweep/Teleport、`FHitResult`、MovementComponent 的 UpdatedComponent/MoveUpdatedComponent/SafeMoveUpdatedComponent、PawnMovementComponent、FloatingPawnMovement | 输入经 Controller 和唯一 Gameplay 移动入口驱动 Pawn；编辑器/加载使用显式 Teleport |
| 第 2 周 | 拆分 TickGroup 分阶段执行；通过 `IPhysicsScene/IWorldCollisionQuery` 接入 Jolt Shape、Body、Raycast、Sweep、Trigger 和 Transform/Body 同步规则 | Jolt Step 位于 Pre/PostPhysics 之间；公共接口不暴露 Jolt 类型，刚体、查询和碰撞事件不绕过移动边界 |
| 第 3 周 | Character、CharacterMovement，确定性输入、地面检测、行走、跳跃、下落和沿墙滑动 | 角色移动稳定，相同状态与输入可供服务器重演 |
| 第 4 周 | 分层实现 Skeleton、SkeletalMesh、AnimationClip、AnimInstance、Pose、通用蒙皮 Render Data、Idle/Walk/Jump 和基础 Root Motion；通过开发期导入层接入 Assimp，glTF/GLB 为正式验收输入，FBX 为实验性输入 | 动画由移动状态驱动，Root Motion 经 MovementComponent；动画资产和组件不持有 OpenGL、Assimp 或源格式对象；同一 Pico 原生资产链可承载 glTF/GLB 与受控 FBX |

第 1 周已完成：新增独立 `PicoPhysicsCore` 数据边界、`FHitResult`、Collision Shape/Query、Sweep/Teleport
语义、`UpdatedComponent`、`MoveUpdatedComponent/SafeMoveUpdatedComponent/SlideAlongSurface`、Pawn 输入缓存、
PawnMovement/FloatingPawnMovement 继承链和 Controller Tick prerequisite。Sandbox 已迁移到统一移动入口，
Gameplay Debug 可观察输入、速度、移动模式和命中结果。详见
[`Month08_1_MovementFoundation.md`](Month08_1_MovementFoundation.md)。Debug/Release 均为 13/13 CTest
通过，Movement 聚焦测试为 23/23 断言通过。

第 2 周已完成：`FTickTaskManager` 已拆为显式 `BeginFrame/RunTickGroup/EndFrame`，`PWorld` 在
Pre/PostPhysics 之间执行同步 Jolt Step；新增 `IPhysicsScene`、稳定 Body Handle、Body 描述/状态、
Raycast/Sweep/Overlap、Box/Sphere/Capsule Shape、Static/Kinematic/Dynamic Transform 权威规则、Trigger 与
Hit 委托，以及 `PicoPhysicsJolt` 后端隔离模块。Sandbox 运行时包含地面、墙、动态刚体和 Trigger，F1
Gameplay Debug 可观察 Step、Body 和查询结果。详见
[`Month08_2_JoltPhysics.md`](Month08_2_JoltPhysics.md)。Physics 聚焦测试为 18/18 断言通过，并覆盖
序列化 World 替换后的物理服务重建与逻辑 Overlap 去重。
当前 Debug/Release 均为 14/14 CTest 通过。

第 3 周已完成：新增 `PCharacter` 与 `PCharacterMovementComponent`，通过继承的 CDO 默认子对象模板
统一创建 Capsule 和移动组件；实现确定性移动输入/状态、地面 Sweep、可行走坡度、行走加减速、跳跃、
下落、落地、离开平台、沿墙滑动和动态刚体推动。角色长帧模拟受最大迭代次数约束，Jolt 后端改为固定
60 Hz 且每个 World 帧最多 4 个子步。Sandbox 已迁移至 Character，`Space` 接入 `Action.Jump`，F1
Gameplay Debug 可观察移动模式、地面结果、法线、跳跃请求和模拟迭代次数。详见
[`Month08_3_CharacterMovement.md`](Month08_3_CharacterMovement.md)。新增 CharacterMovement 聚焦测试
覆盖默认子对象、行走/跳跃/下落/落地、确定性重演、长帧上限和推动刚体；第 4 周进入动画数据与运行边界。

第 4 周已完成：新增 Pico 原生 `.pskeleton`、`.pskeletalmesh`、`.panimation` 格式及严格校验，
`PAnimInstance` 根据 CharacterMovement 的地面速度与 MovementMode 选择 Idle/Walk/Jump，
`PSkeletalMeshComponent` 输出通用 CPU 蒙皮 Render Data，`PicoRender` 按组件 Revision 动态上传顶点。
Standalone 的 F1 Gameplay Debug 已将 GroundSpeed、AnimationState、MovementMode、当前 Clip 和播放时间
分开显示，可直接验收静止时 `Idle + Walking`、移动时 `Walk + Walking` 和腾空时 `Jump + Falling` 的关系。
Root Motion 被写入可重放的 `FCharacterMoveInput` 并经 Sweep/MoveComponent 执行；自动化测试覆盖资产往返、
Pose、蒙皮、状态切换和撞墙 Root Motion。Sandbox 提供不依赖外部模型的双骨骼可视化样例。Assimp 适配源码与
`PicoAssetTool import-skeletal` 已接入；本地 Assimp 6.0.4 源码会由 CMake 自动发现，并只启用 glTF 与 FBX
Importer。已经使用 Assimp 官方蒙皮 glTF 和骨骼动画 FBX 样例完成真实文件导入，Debug 与 Release 自动化
测试均通过。详见
[`Month08_4_SkeletalAnimation.md`](Month08_4_SkeletalAnimation.md)。

第 4 周收尾已补齐 UE Persona 思路的简化编辑器链路：Content Browser 可直接选择 glTF/GLB/FBX，Assimp
先导入内存并在独立、RootSet 持有的临时 Preview World 中显示；确认后自动计算 Content 物理路径与
`/Game` 引用，保存 Skeleton/SkeletalMesh/AnimationClip、刷新 AssetRegistry 并选中新资产。双击正式
SkeletalMesh 或 AnimationClip 可再次预览，不创建、不修改也不保存地图 Actor。详见
[`Month08_5_SkeletalAssetPreview.md`](Month08_5_SkeletalAssetPreview.md)。

月末 Demo 已验收 WASD、跳跃、碰撞、推动刚体和 Idle/Walk/Jump 动画。Debug 与 Release 均为
16/16 CTest 通过，第 5 月原定主线完成度为 100%。

### 第 4 周动画导入范围

动画导入采用“第三方库只解析源文件，Pico 负责运行时”的固定边界：

```text
glTF/GLB 或 FBX
 -> PicoAssetImport + Assimp
 -> 坐标系/单位/骨骼/权重/关键帧校验与转换
 -> Pico 原生 Skeleton/SkeletalMesh/AnimationClip
 -> AssetManager/AnimInstance/Pose/Renderer
```

- `PicoAssetImport` 是唯一允许包含 Assimp 头文件和对象的模块；`PicoAsset`、`PicoEngine`、Gameplay、
  反射、序列化、网络和 `PicoRender` 不得暴露 `aiScene/aiNode/aiMesh/aiAnimation`。
- 第一版正式保证一个受控 glTF/GLB 动画角色可以稳定导入、预览并保存为 Pico 原生资产；当前不宣称
  Skeletal Reimport，完整 Source/Reimport 元数据仍是后续编辑器易用性任务。
- 第一版使用同一导入器尝试一个简单 FBX 的 Skeleton、Skin Weights 和 AnimationClip；通过坐标、单位、
  Bind Pose 和关键帧验收后标记为“实验性支持”，不承诺兼容任意 FBX 版本、DCC 导出设置、Morph、LOD
  或复杂材质。
- 不自行解析 FBX，也暂不直接接入 Autodesk FBX SDK；只有 Assimp 无法满足经过记录的真实 FBX 用例时，
  才允许新增独立 `PicoAssetImportFBX` 适配模块，上层 Pico 原生资产格式保持不变。
- 导入测试至少包含一个程序生成的双骨骼确定性样例和一个许可证清晰的可视化角色；测试资产来源、许可、
  单位和导出设置必须随项目记录。

## 第 5.5 阶段（已完成）：Pico Montage Lite

Montage Lite 不属于第 5 月基础动画 MVP，但现在作为进入第 6 月前的 3～5 天限时收尾。目标是补齐
Section、Notify、完成/中断和 Root Motion 的运行时语义，为网络状态边界和第 8 月
`PlayAnimationAndWait` 提供稳定接口；到期后不继续扩展完整动画编辑器，立即回到网络主线。

| 时间 | 任务 | 当日验收 |
| --- | --- | --- |
| 第 1 天 | `.pmontage` 数据、Segment、Section、Slot、Notify/NotifyWindow、版本化保存加载与严格校验 | 程序化双 Clip Montage 往返后数据一致，非法 Section 链和时间范围被拒绝 |
| 第 2 天 | `FActiveMontageInstance`、Play/Stop/JumpToSection、播放速率、Section 跳转/循环 | 可完成一次 Idle -> Action -> Idle，Section 跳转不会重复或漏采样 |
| 第 3 天 | Completed/Interrupted、Notify Begin/End 弱对象委托及销毁/切图清理 | 正常完成、中断、目标销毁和 World 重载均只产生一次终止结果且无悬空监听 |
| 第 4 天 | 单 DefaultSlot 淡入淡出和 Montage Root Motion -> CharacterMovement -> Sweep | Action 能平滑进入/退出，Root Motion 撞墙并保留可重演输入边界 |
| 第 5 天 | PicoInspector 或 Skeletal Preview 可视化控制、自动化回归和文档 | 可选择 Montage、播放/停止/跳 Section，并观察时间、Notify 和结束原因；Debug/Release 全量测试通过 |

完成记录：`.pmontage`、Section/Notify/NotifyWindow、淡入淡出、结束原因、Root Motion 移动边界及
Skeletal Preview Montage Lab 已实现；Debug/Release 16/16 CTest 与聚焦动画测试 13/13 均通过。实现与可视化验收见
[`Month08_6_MontageAndCharacterAssembly.md`](Month08_6_MontageAndCharacterAssembly.md)。

第 3 天结束时做一次范围检查：若数据、生命周期和委托已经稳定但 Blend UI 尚未完成，只允许压缩第 4～5 天
的可视化表现，不能延长阶段去实现多 Slot、骨骼遮罩或完整时间轴编辑器。

首版数据与运行实例：

```text
PAnimationMontageAsset
  -> AnimationClip Segments
  -> named Sections and next-section links
  -> one DefaultSlot
  -> Notify / NotifyWindow timeline

FActiveMontageInstance
  -> playback time/rate
  -> current section
  -> blend in/out state
  -> playing/completed/interrupted state
```

首版必须支持：

- `PlayMontage`、`StopMontage`、`JumpToSection`、`IsMontagePlaying`；
- 多个 AnimationClip Segment、命名 Section、Section 跳转/循环和播放速率；
- 一个 `DefaultSlot`、基础淡入淡出，以及 Completed/Interrupted Native 弱对象委托；
- Notify 与 NotifyWindow 的 Begin/End 广播，跨越多关键点或循环边界时不漏发、不重复；
- Root Motion 从 Montage 提取后交给 CharacterMovement，再经 MoveComponent/Sweep 执行；
- 暴露足够的完成、中断、取消和 Notify 委托，使后续 `AbilityTask::PlayAnimationAndWait` 无需依赖动画内部裸指针；
- 自动化验证连击 Section 跳转、换弹循环、NotifyWindow、撞墙 Root Motion 和中断清理。

首版明确不做完整 Montage 时间轴编辑器、多 Slot Group 并发、骨骼遮罩/上半身分层、Sync Group、
Branching Point 精确任务语义、复杂 Montage 优先级和任意 Montage 网络复制。网络阶段不复制每根骨骼或
最终 Pose；将来确有玩法需求时只同步权威 Montage 资产身份、Section、服务器时间/位置、播放速率与必要
事件，Root Motion 继续服从服务器移动与客户端纠错。

当前动画易用性边界也固定记录如下，避免把运行时能力和编辑器完成度混为一谈：

- 已支持外部骨骼模型导入、独立 Preview World、原生资产保存和 C++ Gameplay 配置播放。
- 正式场景编辑器尚未提供完整的 SkeletalMeshComponent 创建、资产拖放、Clip/状态映射 Details 和重导入。
- 最小场景配置可在 Montage Lite 后用 1～2 天补齐，但不作为第 6 月网络准入条件；复杂 Persona、AnimGraph、
  Retargeting、PhysicsAsset 和材质自动导入继续延后。
- 当前已完成 Windows Development Package V1：Target Receipt、原生资产/运行时依赖 Contributor、原子 Stage、
  校验、PackageReport、编辑器 Package 命令和仓库外两帧冒烟测试均已落地。依赖图 Cook、Shipping Profile、
  Client/Server Target、归档和全新电脑验收仍按第 7 月执行。
- Package V1 已补齐显式同名覆盖、自定义 Stage 名称、唯一内部工作目录与失败清理；Windows Game Target
  使用 GUI 子系统。第三人称相机已补齐可配置 Pitch 限制和可关闭的 SpringArm 球形 Sweep，但 Camera Lag
  仍属于可选手感增强，不是网络准入条件。

## 第 5.6 阶段（代码完成，等待真实人形资产验收）：AnimationSet 与最小人物配置

Montage Lite 后安排 1～2 天完成，不延伸为完整 Persona：

| 时间 | 任务 | 验收 |
| --- | --- | --- |
| 第 1 天 | 新增版本化 `.panimset`，保存 Skeleton、Idle、Walk、Jump；AssetRegistry/AssetManager 支持；PAnimInstance 改为读取 AnimationSet | 替换 AnimationSet 即可改变移动动画；组件不再硬编码三个项目 Clip 路径；保存加载与非法 Skeleton 引用测试通过 |
| 第 2 天 | 正式编辑器创建/选择 SkeletalMeshComponent，Details 配置 Mesh、AnimationSet、按 Slot Material Override 与相对 Transform | 导入的简单 CC0 人形可装配到 Character；保存 `.pworld`、关闭重开后全部引用恢复；Play 中完成 Idle/Walk/Jump |

本阶段不实现 VisualProfile、动画重定向、AnimGraph、PhysicsAsset、自动复杂材质、完整骨骼树或动画时间轴。
如果按 Slot 覆盖需要修改资产版本，旧的单材质场景必须保持向后兼容。完成后使用简单 CC0 GLB 做正式验收，
再将 Manny/Quinn 作为独立兼容性测试，不让复杂 UE 资产阻塞第 6 月网络。

代码完成记录：`.panimset`、Registry/Manager、组件反射引用、导入时按 Idle/Walk/Jump 名称自动生成、
四个 Section 材质覆盖、分 Section 渲染以及 AnimationSet/Montage 预览入口已经实现。待办只剩选择许可证
清晰且包含 Idle/Walk/Jump 的人形 GLB，完成导入、场景保存重开和 Standalone Play 的视觉验收；该外部资产
不作为进入第 6 月网络底层的阻塞项。

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

进入网络主线前的人物装配缺口已经关闭：地图可以持久保存一个 `Auto Possess Player 0` 的可玩 Pawn；编辑器提供 Pawn Class/Profile 创建器和 Project Settings 资源选择器；运行时具备 ControlRotation、鼠标第三人称视角、视角相对移动、移动朝向旋转与 8 槽材质覆写。网络实现应复制 Pawn/Actor 的权威 Transform 和 Controller 的必要视角状态，不复制仅用于修正源资产坐标的 SkeletalMesh Relative Transform。

编辑器观察与装配体验也已经完成一次加固：主视口具有可关闭的世界 XYZ 原点轴线和可拖拽方向控件；
Actor Blueprint 以编辑器同进程的独立原生窗口打开，并用带箭头的局部轴明确 Actor `+X/+Y/+Z`。
Standalone Play 仍保持独立游戏进程，不能与资产编辑器的平台窗口生命周期混为一谈。

项目入口与会话恢复也已完成：无参数启动进入 Project Browser，用户级设置保存最近 8 个 `.pico`；
项目级 `Saved/Editor/EditorSession.ini` 恢复最后 World、Actor Blueprint 与 Skeletal Preview。项目切换
通过新 PicoEditor 进程完成，不在已注册项目类、CDO 和 Game Module 的进程内热替换项目。

PicoSandbox 已增加持久化 `StarterWorld` 与 `/Game/StarterContent` 基础网格/材质。地面、墙、动态箱子、PlayerStart 和灯光属于关卡场景对象，不再由 GameMode 在 BeginPlay 临时生成；这保证编辑器预览、Standalone、网络复制与未来 Cook/Package 使用同一份 World 数据。

Windows Development Package V1 已提前完成。`PicoPackager` 与编辑器 File 菜单读取 Target Receipt，收集全部
Pico 原生资产和声明式 Runtime Dependency，在临时目录完成校验及仓库外冒烟测试后原子替换成功 Stage；
`Content/Source`、Saved、Intermediate、FBX/glTF/GLB/OBJ 和 Assimp 不进入包。该基线按
`Game/Client/Server + Development/Shipping + IPackageContributor` 扩展，网络和后端替换不应重写 StageBuilder。
同名输出必须显式选择 Replace，自定义 Package Name 只改变 Stage 文件夹名；唯一内部工作目录在成功或失败后
清理。Game Target 使用 Windows GUI 子系统，后续 Dedicated Server Target 仍可单独保留控制台日志行为。

目标：产出仓库外可运行的公网双人 Demo。

| 周次 | 任务 | 月末验收 |
| --- | --- | --- |
| 第 1 周 | 延迟、抖动、丢包、乱序模拟，带宽预算和移动平滑 | 网络模拟环境稳定 |
| 第 2 周 | Dedicated Server Target、公网连接、超时、基于 StablePlayerId/SessionToken 的短线重连、InactivePlayerRecord、版本校验和限流 | 公网双客户端可连接；断线后在窗口期内恢复 PlayerState 和当前权威状态，无需追赶断线帧 |
| 第 3 周 | `PicoTask` Worker Pool、任务状态/取消、Game Thread Dispatcher、安全关闭；在现有 Package V1 上接入 Cook 依赖图和人物 Source Metadata/Reimport | 耗时纯数据任务不阻塞编辑器；Reimport 保持资产身份；Stage 从“全部原生资产”升级为地图根依赖闭包 |
| 第 4 周 | Shipping 编译配置、Client/Server Receipt、归档、全新电脑测试 | Client 与 Server 包均可分发运行；Development Package V1 保持兼容 |

`PicoTask` 的第一版线程规则固定为：后台任务不得直接保存或修改裸 `PObject*`；需要关联对象时保存
`FObjectHandle`，回到 Game Thread 后重新 `ResolveObject`。资产扫描/解码、Cook、外部构建进程和 AI HTTP
请求可以在 Worker 执行，创建对象、设置反射属性、替换 World 和更新编辑器 UI 必须回到 Game Thread。
关闭引擎时必须停止接收任务、取消未开始任务并等待 Worker 退出。

## 第 8 月：Mini GAS、AbilityTask 与 AI

| 周次 | 任务 | 月末验收 |
| --- | --- | --- |
| 第 1 周 | GameplayTag、AttributeSet、AbilitySystemComponent、AbilitySpec | 属性和 Ability 可授予 |
| 第 2 周 | GameplayEffect、Cost、Cooldown、Duration、Periodic 和 Tag 条件 | Effect 生命周期正确 |
| 第 3 周 | 基于已完成的 Montage Lite 实现 AbilityTask、WaitGameplayEvent、WaitDelay、PlayAnimationAndWait 和 Ability 网络预测 | 异步 Ability 可等待、取消和预测；Montage 完成/中断/Notify 能可靠结束或推进 Task |
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
- 一个许可清晰的外部人形角色通过 Skeleton、SkeletalMesh、AnimationSet、Material/Texture 独立资产完成装配，场景重载和 Package 后仍可运行。
- Montage Lite 的 Section/Notify/中断，以及 AbilityTask 对动画完成或事件窗口的等待。
- Dash、Fireball、Stun 以及 AbilityTask 事件等待。
- 中文创建对象、修改属性、保存并触发打包。
- 资产/Cook/AI 等耗时任务可在后台运行、取消并安全回到 Game Thread 应用结果。
- 输出不依赖编辑器、源码和仓库目录的独立程序。

## 第 9 月（MVP 后）：PicoGraph Lite 可视化脚本

目标：以 UE Blueprint 的“反射暴露、强类型图、编译、生成运行表示和事件驱动”为学习重点，让已有原生
Actor 可以挂载并执行可保存、可 Cook、可由 AI 安全生成的 Event Graph。PicoGraph 不进入当前半年 MVP
验收，不得反向阻塞网络、Package、Mini GAS 或 AI Tool 主链。

| 周次 | 任务 | 周末验收 |
| --- | --- | --- |
| 第 1 周 | `.pgraph` 资产、稳定 Node/Pin/Link ID、变量、Entry Event、图版本、序列化与基础节点编辑器 | 创建、连线、断线、Undo/Redo、保存、重开后图结构和布局完全恢复 |
| 第 2 周 | Graph Schema、Pin 类型系统、控制流/数据流校验、Typed IR、Pico Bytecode Compiler 和编译诊断 | 非法类型、缺失输入、执行环和失效函数给出定位到节点/Pin 的错误；合法图产生确定字节码 |
| 第 3 周 | `FPicoScriptVM`、Execution Context、指令/循环预算、`PScriptComponent`、BeginPlay/Custom Event、PFunction/Property/Delegate 节点 | 原生 Actor 可执行 Graph；函数和属性经反射访问；错误图不会卡死 Game Thread |
| 第 4 周 | Latent Continuation、Delay、WaitGameplayEvent、PlayMontageAndWait、Cook/Package、AI Graph Tools 和可视化调试状态 | 开门/拾取/延迟/Montage 等待 Demo 可运行；AI 可生成受限图并经校验保存；Stage 外 Runtime 可执行 Cook 后字节码 |

### PicoGraph 固定边界

```text
.pgraph Editor Asset
 -> Graph Schema + Validator
 -> Typed IR
 -> Bytecode Compiler
 -> Cooked Script Asset
 -> FPicoScriptVM + FScriptExecutionContext
 -> PFunction / PProperty / Delegate / AbilityTask
```

- Runtime 执行 Cook 后字节码，不递归遍历编辑器 Node 对象；图布局、注释和编辑器选择不进入运行数据。
- Node/Pin/Link 使用稳定 ID，不能保存 UI 指针；对象引用沿用资产路径、SceneId 或运行时受控 Handle，
  不发明可跨磁盘/网络复用的裸地址身份。
- Pin 类型第一版复用 Pico 反射类型：Exec、Bool、Int32、Float、String、Name、Vector3、Rotator、Transform
  和受 `PClass` 约束的 Object；隐式转换必须由 Graph Schema 明确列出。
- `PFunction` 是 C++ 与脚本的唯一函数调用桥；只有显式 `Callable` 且满足上下文、线程、Role/Ownership
  和权限元数据的函数才能生成节点。属性节点同样服从 ReadOnly、Transient、网络权威和变更通知规则。
- VM 每次恢复都有最大指令数、循环次数和调用深度；超预算产生可诊断错误并终止当前执行，不阻塞 World。
- Delay、WaitGameplayEvent 和 PlayMontageAndWait 不创建第二套等待线程，统一复用 AbilityTask/Latent Handle；
  Actor 销毁、World 重载、网络预测拒绝和引擎退出时必须取消 Continuation 并清理弱委托。
- AI 只能通过 `CreateGraph/AddNode/ConnectPins/SetDefaultValue/CompileGraph` 等受控工具构建图；编译成功前
  不允许进入场景或 Package，模型输出不能绕过 Schema 直接写字节码或执行任意 Shell。

第一阶段节点范围固定为：

```text
Event BeginPlay / Tick / Custom Event
Call PFunction
Get / Set Property
Branch / Sequence / bounded ForLoop
Bind Delegate / Broadcast Delegate
SpawnActor / DestroyActor
MoveComponent
PlayMontageAndWait / ActivateAbility
Delay / WaitGameplayEvent
Log
```

第一阶段只让已有原生 Actor 或已完成的 Data-Only Actor Blueprint 通过 `PScriptComponent` 挂载 Graph。
动态类注册、GeneratedClass、独立 CDO、默认子对象模板、实例默认值和 `.pworld` 类身份已经由
`.pblueprint` 纵向切片验证；PicoGraph 必须复用这条链，并在其上增加脚本组件/行为类数据，不能创建第二套
生成类和对象构造系统。蓝图继承、父类失效传播和运行时热重载仍需在进入多层生成类继承前单独验证。

首版明确不实现完整 Kismet Compiler、Blueprint Interface、Macro/Function Library、任意递归、无界循环、
Construction Script、AnimGraph、节点热重载、Nativization、完整断点/单步调试、多人协作图编辑和任意
Editor Utility 权限。可视化调试只显示当前节点、最近错误、调用栈摘要和剩余执行预算。

第 9 月最终验收 Demo：在编辑器中用 Graph 创建 Trigger 开门、拾取加属性和播放攻击 Montage 的逻辑；
保存并重开后行为恢复；AI 用中文生成同类受限 Graph；编译错误可定位；Cook 后在仓库外 Runtime 中运行，
且网络权威操作仍由 Server RPC/Replication 规则约束。

## MVP 后任务

以下内容不进入当前主线，只有主计划提前完成时才开始：

- `PicoGraph Lite` 可视化脚本按上述第 9 月独立阶段执行；完整 Blueprint 生态继续延后。
- ECS Registry、稠密组件存储和 Actor/ECS 桥接。
- Render Architecture：PrimitiveSceneProxy、MeshBatch、RenderScene、RHI 和 RenderPass/RenderGraph；完成后再接入光线追踪。
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
