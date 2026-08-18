# 第 6 月前网络准入基线

本文档记录 Pico 在进入 Replication、RPC 和客户端预测之前已经锁定的运行时边界，避免网络代码重新绕过
World、Movement、物理、动画和对象系统。

## 当前帧顺序

```text
Game Runtime 采样 Input
 -> FEngineLoop BeginFrame / 更新时间
 -> BeforeWorldTick
      -> PGameInstance::Tick
      -> 未来 NetDriver::TickDispatch
 -> PWorld::Tick
      -> PrePhysics
      -> DuringPhysics / Jolt Step
      -> PostPhysics
      -> PostUpdateWork
 -> AfterWorldTick
      -> 未来延迟销毁与 NetDriver::TickFlush
 -> GC 安全点
 -> 帧率限制 / EndFrame
```

`FEngineFrameCallbacks` 是 World 外围的最小阶段接口。当前 `FGameEngine` 已把 GameInstance 放入
`BeforeWorldTick`；测试同时断言 `BeforeWorldTick -> World -> AfterWorldTick` 的顺序。第 6 月接入网络时应扩展
回调中的具体职责，不应让 NetDriver 自己驱动 World，也不应在 Actor 或 MovementComponent 内直接收发 Socket。

## 已锁定边界

- Game Thread 是 `PObject`、World、Actor、Component 和 Gameplay 状态的唯一写入线程。
- Controller 产生移动意图，Pawn 缓存输入，CharacterMovement 模拟并通过统一 MoveComponent 边界移动。
- Jolt、Assimp 和 OpenGL 类型不进入 Gameplay 反射属性、场景文件或网络协议。
- GameMode 只存在于服务器；GameState、PlayerState、PlayerController 和 Pawn 的网络可见性由第 6 月 NetRole、
  Ownership 和连接相关性实现。
- `FNetObjectId` 必须独立于本地 `FObjectHandle`、场景对象 ID 和资产路径。
- 网络复制 Gameplay 状态、移动命令和必要动画事件，不复制最终 Pose、Jolt Body 或 GPU 资源。

## 自动化测试隔离

`PicoEditorTests` 不再直接打开 `Projects/PicoSandbox`。测试会把 `.pico`、`Config` 和 `Content` 复制到系统
临时目录，在副本上执行 World Open/New/Save 相关验证，结束后删除副本。这样运行 CTest 不会覆盖开发者正在
编辑的 `StarterWorld.pworld`，也不会把测试产生的 Saved、布局或会话状态写进真实项目。

## 第 6 月第 1 周完成状态

1. 已完成 `INetTransport/FUdpTransport`、Connection、三步握手、Sequence、Ack/AckBits 和有界可靠队列。
2. 已定义 `ENetMode/ENetRole` 与独立 `FNetObjectId`；Actor Ownership 和角色语义在第二周 ActorChannel 落地。
3. 网络接收已进入 `BeforeWorldTick`，发送已进入 `AfterWorldTick`，并由 GameEngine 顺序测试固定。
4. Loopback、同进程真实 UDP 和三个独立 PicoSandboxGame 进程均完成一个服务器加两个客户端验收。

第 6 月按“连接和身份 -> Actor/属性复制 -> RPC/Gameplay -> 预测和插值”四个纵向阶段推进。每一阶段都必须
同时提供自动化测试和可视化/多进程验收，不能把三进程联调推迟到月末。详细周计划见
[`Pico_Remaining_Development_Roadmap.zh-CN.md`](Pico_Remaining_Development_Roadmap.zh-CN.md)，风险控制见
[`NetworkRiskRegister.zh-CN.md`](NetworkRiskRegister.zh-CN.md)。

## 范围与降级顺序

本月必须交付连接、网络身份、Actor Spawn/Destroy、基础属性 Delta、Server RPC 权限校验、角色状态同步和
模拟代理插值；自主代理预测、Correction 和未确认输入重演为重点目标。若进度受阻，先降低预测平滑精度，不能
删除身份隔离、权限校验、队列上限和生命周期清理。

Dedicated Server 独立 Target、公网、短线重连、Replay、Root Motion 预测、动态刚体预测、Dormancy、分片、
Iris 和 Replication Graph 不属于第 6 月范围。

## 本阶段不代表已经完成

- ActorChannel、Replication、RPC 和客户端预测尚未实现；NetDriver、Socket 与 Connection 已完成第一版。
- 正式依赖裁剪 Cook、Shipping、Client/Server Target 和全新电脑打包验收仍按后续计划执行。
- Jolt 工厂的实现选择仍位于 Engine 私有代码；公共接口已经隔离，但未来增加第二物理后端前需改为注册式工厂。
