# Pico 网络开发风险登记

本文档是第 6 月 Replication、RPC 与客户端预测阶段的持续风险登记。每周开始时确认风险状态和预防任务，
每周验收时根据关闭条件销项。风险不能仅凭“目前没有复现”关闭，必须有自动化测试或多进程证据。

## 风险等级

| 等级 | 含义 |
| --- | --- |
| 高 | 会破坏协议、对象生命周期或权威模型，通常导致后续层整体返工 |
| 中高 | 可能阻塞月末 Demo，或在高延迟/丢包下造成明显错误 |
| 中 | 不阻塞基础联网，但会降低诊断、扩展或打包质量 |

当前总体判断：基础联网无法完成的风险低；架构返工风险中高；一个月内完成稳定预测体验的风险中高。

## 第 1 周复查结果

- NET-R01：已建立强类型 `FNetObjectId`，没有与 `FObjectHandle`/SceneId 的隐式转换；Actor 映射仍需第二周验证。
- NET-R02：第一周范围已关闭。固定测试覆盖丢包、重复、乱序、回绕、重发、恰好一次、有序交付和队列归零；
  Windows Socket 已关闭每 Socket 的 `SIO_UDP_CONNRESET`，启动竞态产生的 `10054` 不再污染连接错误状态。
- NET-R08：第一周范围已关闭。GameEngine 测试证明 Dispatch 在 World 前、Flush 在 World 后且早于 GC。
- NET-R09：可靠发送队列、接收缓冲、重发次数、单帧发送和历史 Packet 均有硬上限；后续 Channel/快照另行复查。
- NET-R10：已部分缓解。F1 Network Debug 显示模式、连接、RTT、Packet 与可靠队列；属性和预测指标后续增加。
- NET-R11：远端角色的固定平滑窗口可能在网络 RTT 之外继续增加视觉拖尾。先增加 Move/Server/Snapshot/Render
  阶段计时，再用服务器时间同步和快照间隔驱动动态平滑；不得以无条件缩短 Smooth Time 代替抖动测试。
- NET-R12：简化 SimulatedProxy 仅携带 Transform、Velocity 和 MovementMode，急转、移动基座与复杂状态可能
  外推错误。外推保持 0.2 秒硬上限；后续逐项加入 Acceleration、Rotation 和 Movement Base，不复制第二套玩法逻辑。
- NET-R12：Winsock 静态系统依赖位于 `PicoNetCore`，Standalone 不打开 Socket；Client/Server Receipt 留到第 7 月。

## 第 2 周复查结果

- NET-R01：已关闭本月基础范围。服务器分配 `FNetObjectId`，双端注册表只通过本地 Handle 解析；引用 Wire Format
  仅携带 NetId，并验证目标反射类。
- NET-R03：已关闭本月基础范围。每个 ActorChannel 保存独立 Pending/ACKed 基线；两个连接独立初始同步，未变化
  字段零 Delta，可靠 ACK 后才提交基线。
- NET-R04：部分缓解。普通 Actor Transform 在 World Tick 后捕获；Character 模拟代理插值和自主代理写入所有权
  仍由第 4 周关闭，当前可靠 Transform 只用于基础权威同步。
- NET-R05：已关闭基础 Actor 范围。Channel 保存 Handle，Destroy 可靠有序，断线按 Connection 清理，延迟引用不
  保存裸指针；Component/Subobject 生命周期不在本月范围。
- NET-R08：已关闭复制范围。Dispatch 应用远端状态，World Tick 后捕获服务器最终状态，Flush 早于 GC。
- NET-R10：继续缓解。F1 已显示 NetObject、Channel、未解析引用、复制消息、拒绝和 OnRep 计数；网络模拟与预测
  指标留到第 4 周。

## 风险总表

| 编号 | 风险 | 等级 | 主要触发信号 | 负责周次 |
| --- | --- | --- | --- | --- |
| NET-R01 | `FNetObjectId` 与 ObjectHandle/SceneId/资产路径混用 | 高 | 销毁重生后旧引用指向新对象，跨进程 ID 无法对应 | 第 1～2 周 |
| NET-R02 | UDP 可靠层确认、重发或有序交付错误 | 高 | RPC 重复、漏执行，可靠队列持续增长 | 第 1 周 |
| NET-R03 | 每连接属性基线或 Delta 错误 | 高 | 丢包后属性永远不更新，或每帧都发送全量状态 | 第 2 周 |
| NET-R04 | Transform、Movement、Jolt、Root Motion 与网络状态双写 | 高 | 位置来回覆盖、穿墙、跳跃重复或持续抖动 | 第 2～4 周 |
| NET-R05 | Channel、Actor 生命周期与 GC 冲突 | 高 | 悬空引用、Actor 泄漏、Destroy 未送达或断开崩溃 | 第 2 周 |
| NET-R06 | RPC 方向、Role、Ownership 或参数校验不足 | 高 | 客户端可控制他人 Actor 或调用服务器内部函数 | 第 3 周 |
| NET-R07 | 客户端与服务器移动模拟不一致 | 高 | 高频 Correction、明显拉回、未确认输入无法清空 | 第 4 周 |
| NET-R08 | 网络接收/发送放错帧阶段 | 中高 | 收包晚一帧、发送旧状态、GC 早于 Destroy 记录 | 第 1～2 周 |
| NET-R09 | 队列、快照或重演没有上限 | 高 | 延迟升高后内存和单帧耗时持续增长 | 全月 |
| NET-R10 | 缺少网络模拟和可视化诊断 | 中 | 只能看到不同步，无法定位 Connection、Role 或属性 | 全月 |
| NET-R11 | 过度复制 UE5/Iris 复杂度导致范围失控 | 中高 | 长时间停留在抽象层，没有三进程 Gameplay 闭环 | 全月 |
| NET-R12 | Socket、配置和 Target Receipt 破坏后续打包扩展 | 中 | 开发目录可运行，Stage 中缺依赖或端口配置 | 第 1、4 周 |

## 第 4 周代码复查结果

- NET-R04：代码层已关闭 Character Transform 双写。初始 Spawn 可应用通用 Transform；后续自主代理和模拟代理
  忽略通用 Transform 写入，分别由 Correction/Replay 和快照插值更新。Root Motion 与动态刚体预测仍明确排除。
- NET-R07：代码与自动化范围已缓解。输入序号、DeltaTime、ControlYaw、策略 Hash、Ack 对应状态比较和未确认
  输入重演已落地；首次三进程验收发现复制 Pawn 指针未触发客户端 Possess，导致策略 Hash 为 0、移动被拒绝且
  动画停在 Falling。现已增加 `OnRep_Pawn` 双向关系修复、晚到 Actor 的客户端绑定补偿和自动化回归；仍需
  100～150 ms、约 5% 丢包的 10 分钟人工证据后关闭。
- NET-R09：Pending Move 32、服务器移动 32、每帧服务器重演 8、快照 32、每包移动 3、延迟包 4096；F1
  可观察当前数量，未发现无界新增路径。
- NET-R10：Play Session 与命令行均可配置延迟、抖动、丢包；F1 已显示移动消息、Correction、Snapshot、
  Sent/Ack、Pending、重演、最大误差和模拟队列。本风险的代码范围关闭。
- NET-R12：网络模拟和移动协议仍位于 Runtime 模块，不依赖编辑器；Client/Server Receipt 继续留在第 7 月。

## NET-R01：网络身份混用

服务器和客户端中的同一个 Actor 拥有不同内存地址和本地 `FObjectHandle`。网络必须由服务器分配独立、连接期
稳定且不会立即复用的 `FNetObjectId`，并通过每端映射表解析。

- 预防：使用强类型 `FNetObjectId`；禁止网络序列化裸指针、本地 Handle、磁盘 SceneId 和 `/Game` 资产路径作为对象身份。
- 降级：第一版只允许 Actor 网络引用，不支持任意子对象复制。
- 关闭条件：销毁旧 Actor 后生成新 Actor，旧 NetId 在服务器和客户端均不能解析到新对象；编译期不能把 Handle 传给 NetId API。

## NET-R02：可靠 UDP 协议错误

UDP Socket 本身不是主要难点，Sequence 回绕、Ack/AckBits、重复过滤、重发和有序交付才是风险中心。

- 预防：先使用可注入丢包、重复和乱序的 Loopback Transport；限制单包大小、可靠窗口、队列长度、重发次数和超时；
  Windows UDP 按 Socket 禁止 ICMP Port Unreachable 转换为 `WSAECONNRESET`，接收路径仍对残留 `10054` 容错。
- 降级：本月不实现任意大消息和分片；高频移动使用可覆盖旧值的不可靠消息。
- 关闭条件：固定种子测试覆盖丢包、重复、乱序、延迟、Sequence 回绕和超时，可靠消息恰好交付一次且队列最终归零。

## NET-R03：属性基线与 Delta 错误

服务器“上一帧的值”不等于客户端“已经确认的值”。若以全局旧值为基线，数据包丢失后可能永久漏掉变化。

- 预防：为每个 Connection 保存已确认属性基线；类注册后生成稳定 Replication Schema 和属性编号。
- 降级：只支持 Pico 当前反射类型和少量复制条件，不在首版扩展容器、任意结构体或热变更 Schema。
- 关闭条件：丢失一次属性包后仍能通过后续重发/新基线收敛；未变化属性不发送；两个客户端拥有独立基线。

## NET-R04：状态双写

角色 Transform 可能同时被 CharacterMovement、Jolt、Root Motion、复制和预测纠错修改。网络不能增加绕开统一移动
入口的新路径。

| 角色类型 | 唯一状态策略 |
| --- | --- |
| 服务器权威角色 | CharacterMovement + Physics 形成最终状态 |
| 自主代理 | 本地预测；Correction 时恢复权威状态并重演未确认输入 |
| 模拟代理 | 消费网络快照并插值，不运行完整权威 CharacterMovement |
| 动态刚体 | 服务器物理权威，客户端只显示同步结果 |

- 预防：通过 `CaptureMoveState/ApplyMoveState` 或专用网络移动接口进入现有 Movement/Physics 边界。
- 降级：本月不预测动态刚体和 Root Motion；它们只服从服务器状态。
- 关闭条件：连续移动、跳跃、落地和碰墙时没有网络写入被下一物理帧恢复为旧值。

## NET-R05：生命周期与 GC 冲突

建议的销毁顺序固定为：

```text
标记网络销毁
 -> AfterWorldTick 形成并发送 Destroy
 -> 关闭 ActorChannel
 -> 删除 NetId 映射
 -> 释放网络强引用
 -> GC 安全点回收对象
```

- 预防：网络表保存 `FObjectHandle` 并在使用时解析；Connection/World 清理必须幂等；延迟对象引用有超时和容量上限。
- 降级：第一版 World 切换时整体关闭旧连接和 Channel，不实现跨地图 Seamless Travel。
- 关闭条件：Actor Destroy、连接超时、主动断开、World Exit 和 GC 压力测试后对象数、Channel 数和映射表均回到预期。

## NET-R06：RPC 权限错误

`Callable` 或 `PFunction` 存在不代表允许远程调用。服务器在 `ProcessEvent` 前必须校验目标、方向、Role、Ownership、
参数类型、对象参数可见性和调用频率。

- 预防：远程入口只接受显式 Server/Client/NetMulticast 标记；权限失败记录结构化原因且不执行函数。
- 降级：第一个 Gameplay 闭环只实现一个 Server RPC 和必要 Client/Multicast 示例。
- 关闭条件：非拥有者、错误方向、未知函数、参数错误、失效对象和超频调用全部零副作用。

当前状态：方向、Role、Ownership、可靠性、参数类型和每连接每帧 32 次上限已落地；自动化测试覆盖合法调用、
非拥有者、可靠性不匹配和超频零副作用。待一个服务器加两个客户端的开门流程人工验收后关闭本风险。

## NET-R07：预测不一致

客户端和服务器即使调用同一 `SimulateMovement`，仍可能因 DeltaTime、输入顺序、碰撞场景、浮点误差、动态刚体和
Root Motion 不同而产生偏差。目标不是跨平台完全确定，而是误差可检测、可纠正、可重演。

- 预防：输入携带序号和模拟 DeltaTime；服务器返回 Transform、Velocity、MovementMode 和 LastProcessedInput。
- 降级：先完成服务器权威 Transform 同步和模拟代理插值，再加入自主代理重演；排除动态刚体和 Root Motion 预测。
- 关闭条件：100～150 ms 延迟和约 5% 丢包下 Pending Move 能持续清空，Correction 频率可观察且不会形成永久拉回。

## NET-R08～R12：阶段、资源和范围控制

- 帧阶段：`TickDispatch` 只进入 `BeforeWorldTick`，`TickFlush` 只进入 `AfterWorldTick`；Destroy 记录早于 GC。
- 有界资源：可靠队列、Pending Move、单帧重演、插值快照、延迟引用和 RPC 频率全部设置硬上限并统计峰值。
- 可观测性：Network Debug 至少显示 NetMode、Role、Connection、Ping、Loss、Reliable Queue、Channel、Correction 和 Pending Move。
- 范围：本月不实现 Dedicated Server Target、公网、重连、Replay、Dormancy、分片、Iris、Replication Graph、Root Motion 预测和动态刚体预测。
- 打包：Socket 平台实现位于独立模块；地址、端口和模式来自命令行/配置；运行时依赖通过现有 Target Receipt 扩展。

## 每周风险门槛

| 周次 | 不满足时禁止进入下一周的条件 |
| --- | --- |
| 第 1 周 | 可靠消息不能在丢包/重复/乱序下恰好交付一次；队列不能归零；NetId 与本地身份仍可混用 |
| 第 2 周 | Actor/属性不能在丢包后收敛；Destroy、断开或 GC 后仍有 Channel、映射或对象泄漏 |
| 第 3 周 | 任一非法 RPC 能产生副作用；三进程开门/拾取结果不一致 |
| 第 4 周 | 网络模拟下存在无界队列、连续硬拉回、崩溃或十分钟稳定性测试失败 |

## 进度降级顺序

时间不足时按以下顺序保住完整 Demo：

1. 必须保留连接、身份隔离、Actor Spawn/Destroy 和基础属性 Delta。
2. 必须保留 Server RPC、Ownership 校验和三进程 Gameplay 闭环。
3. 必须保留角色权威状态同步与模拟代理插值。
4. 优先完成自主代理预测、Ack 和 Correction。
5. 可降低完整回滚重演和视觉平滑精度。
6. 不得通过删除权限校验、资源上限、生命周期清理或测试来换进度。
