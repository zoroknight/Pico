# 第 6 月第 4 周：角色网络移动、预测与插值

## 目标与 UE5 对照

本阶段完成 Pico 第一版服务器权威角色移动闭环。设计对应 UE5 `CharacterMovementComponent` 的核心思想，
但省略移动合并、复杂 Root Motion、动态刚体预测和 Iris 等生产级范围：

```text
AutonomousProxy
  采集输入 -> 本地 SimulateMovement -> 保存 SavedMove -> 不可靠发送
       ^                                              |
       | Ack 对应状态比较、必要时恢复并重演             v
  Correction <- Authority 按序去重并用同一 SimulateMovement 重演

SimulatedProxy
  接收服务器快照 -> 权威根组件立即更新 -> 限时外推根组件 -> 按配置平滑 Skeletal Mesh
```

网络没有复制第二套方向算法。项目 `PlayerController` 仍是轴值、摄像机/角色参考系到 `WorldInput` 的唯一转换点；
客户端、服务器和重演都调用已有 `PCharacterMovementComponent::SimulateMovement`。`.pcontrolprofile` 的 Hash
随移动发送，服务器发现策略不同会拒绝输入，客户端收到不匹配的权威状态时关闭预测。

## 实现内容

- `FCharacterNetworkMove` 保存输入序号、模拟 DeltaTime、世界输入、Jump、ControlYaw、策略 Hash，以及仅在本地
  使用的模拟前后状态。
- 自主代理立即预测；每个移动包最多携带最近 3 条未确认输入，Pending Move 上限为 32。
- 服务器按序去重移动，每帧最多重演 8 条，接收队列上限为 32；错误 Ownership、Role、可靠性、数值范围或
  策略 Hash 均在模拟前拒绝。
- Correction 返回 Transform、Velocity、MovementMode、服务器 Tick 和最后处理的输入序号。误差比较使用该
  Ack 对应 SavedMove 的预测结果，而不是客户端更靠后的当前位置；超过阈值才恢复权威状态并重演未确认输入。
- 模拟代理不运行完整权威移动。默认 `Exponential` 模式立即更新权威根组件，只在
  `PSkeletalMeshComponent::GetVisualWorldTransform` 保留并衰减视觉偏移；碰撞、摄像机、动画状态和组件相对
  变换不被网络平滑写入。
- `Disabled`、`Linear` 和 `Exponential` 模式会在两次快照之间按服务器下发的 Velocity 和 MovementMode
  推进 SimulatedProxy。外推执行 Sweep、墙面滑动、重力和落地，但不消费输入、不推动动态刚体，也不触发
  Authority Gameplay；默认最多外推 0.2 秒，断包后到达上限便停住，避免角色无限漂移。
- Character 的通用可靠 Transform 仍可承载初始 Spawn，但 AutonomousProxy/SimulatedProxy 的后续 Transform
  写入由专用移动通道接管，避免复制与 CharacterMovement 双写。
- `PWorld` 非拥有地引用当前 `FNetDriver`；地图替换前先解绑，避免旧 World 销毁后留下悬空指针。
- `PController::Pawn` 使用 `OnRep_Pawn` 在延迟对象引用修复后建立客户端双向 Possess；GameInstance 会在客户端
  Actor 晚于握手到达时继续寻找 AutonomousProxy。这样项目 `OnPossess` 能加载同一控制配置和策略 Hash，避免
  服务器拒绝全部移动并让角色停留在初始 Falling/Jump 动画。

## 网络模拟与诊断

编辑器 Play Session Settings 新增：

- `Target RTT (ms)`：目标往返延迟，范围 0～2000 ms；编辑器将其一半作为每个进程的出站单程延迟。例如填写
  150 ms 时，客户端到服务器约 75 ms，服务器到另一客户端再约 75 ms；
- `Jitter (+/- ms)`：每包抖动，范围 0～1000 ms；
- `Packet Loss (%)`：出站丢包率，范围 0～100%。

设置保存在 `Saved/Editor/PlaySettings.ini`，并通过 `-netlatency`、`-netjitter`、`-netloss` 传给每个 Pico
进程。运行时 `-netlatency` 仍表示该进程的出站单程延迟。模拟只作用于该进程自己的 NetDriver；延迟队列上限
4096，不修改 Windows 或其他程序的网络行为。

运行时按 `F1` 可观察移动消息、Correction、Snapshot、模拟设置、延迟队列、策略是否匹配、Last Sent/Ack、
Pending Move、重演次数、最大误差和快照数量。

### 网络平滑模式

`PCharacterMovementComponent` 通过反射暴露四种 `Network Smoothing Mode`，可以在 Actor Blueprint 的
`CharacterMovement` 组件中切换：

- `Disabled`：收到快照后立即更新，不产生视觉缓冲；延迟最低，但低更新率或抖动下会跳变。
- `Linear`：权威根组件立即更新，Skeletal Mesh 在 `NetworkSimulatedSmoothLocationTime` 内线性追到目标。
- `Exponential`：默认模式；权威根组件立即更新，Mesh 视觉偏移按指数方式衰减，对应 UE5 默认思路。
- `Snapshot Interpolation`：保留旧实现，最多缓存 32 个快照并落后
  `SnapshotInterpolationDelayTicks` 插值，作为教学对照和高抖动备选。

默认平滑时间为 0.1 秒，最大平滑距离为 256，超过 384 的误差直接 Snap。F1 的 Network 区域会显示首个
SimulatedProxy 的模式、平滑时间、当前 Mesh Offset、快照缓冲数量和 Snapshot 延迟 Tick；下一行显示快照年龄、
本轮外推时间、最大外推时间和触顶次数。UE5 的 `UCharacterMovementComponent::SimulatedTick/SimulateMovement`
同样会让 SimulatedProxy 在更新间隔内继续模拟，然后由 `SmoothClientPosition` 处理 Mesh 视觉偏移。Pico 保留了
这条核心分层，但暂未实现 UE5 的服务器时间戳同步、网络时间差修正和完整代理移动状态机。

## 自动化验收

- `PicoCharacterMovementTests`：SavedMove 去重、策略 Hash 拒绝、四种平滑模式、权威胶囊与 Mesh 视觉分离、
  模拟代理限时外推与触顶、快照上限和原有确定性移动回归。
- `PicoReplicationTests`：所属代理移动协议、服务器 Ownership/策略验证、权威 Correction 下发，以及复制
  `Controller -> Pawn` 后的双向 Possess 对账。
- `PicoGameTests`：每进程网络模拟配置及原有双客户端连接。
- `PicoEditorTests`：网络模拟设置持久化和多进程命令行参数。

Debug 全量 19 项测试必须保持通过。人工最终门槛仍是一个服务器加两个客户端，在目标 RTT 100～150 ms、约 5%
丢包下连续运行至少 10 分钟；三个窗口应保持角色状态收敛，Pending Move、快照、延迟包和可靠队列不能持续增长。

## 当前边界

本阶段不预测 Root Motion、动态刚体、Montage 位移或跨平台确定性物理。它们仍由服务器权威；后续扩展应继续
通过 `CaptureMoveState/ApplyMoveState/SimulateMovement` 边界接入，不能从网络层直接新增第二个物理写入口。

## 下一轮低延迟优化

Pico 当前采用与 UE5 常规 Gameplay 相同方向的服务器权威状态同步，而不是全局锁步帧同步。现有实现已经解决
“能同步”和“本地角色可预测”，但尚不能宣称达到 UE5 `CharacterMovementComponent` 的成熟度。固定 0.1 秒
Mesh 平滑、缺少服务器/客户端统一时间轴，以及不完整的 SimulatedProxy 状态会在起步、急停和转向时产生可见拖尾。

下一轮必须按可测量顺序推进：

1. 为 Move、服务器处理、Snapshot 发送/接收和最终显示增加时间与序号诊断，拆分 Client-to-Server、Server Queue、
   Server-to-Client、Snapshot Age 和 Render Offset，禁止继续只凭画面猜测瓶颈。
2. 建立轻量服务器时间同步，用平滑后的时钟偏移把服务器 Snapshot 时间映射到客户端时间轴。
3. 参考 UE5 的 Smoothing Server/Client Timestamp，根据真实快照间隔和抖动动态计算平滑窗口；保留上下限，避免
   直接把固定时间调小后在丢包环境中抖动。
4. 为 AutonomousProxy 的权威校正增加独立 Mesh 视觉平滑；逻辑胶囊仍立即完成恢复与 SavedMove Replay。
5. 扩充 SimulatedProxy 状态到加速度、朝向和移动基座；最后再评估 Headless Server、Actor 更新频率和带宽调度。

人工验收至少覆盖目标 RTT 0/40/80/150 ms、抖动 0/10 ms 和丢包 0/1/5%。本地所属角色应立即响应；远端角色
在稳定移动时保持连续；方向突变的延迟不得被误认为可由平滑完全消除，因为客户端在快照到达前不可能知道新的输入。
