# 第 6 月第 3 周：Gameplay RPC、Ownership 与网络玩家链

本周把 Actor 属性复制推进为可交互的 Gameplay 网络闭环。客户端现在可以通过自己拥有的
`PlayerController` 请求服务器操作门；服务器验证方向、Role、Ownership、参数和调用频率后执行操作，再通过
属性复制、Client RPC 和 Multicast RPC 把结果送回对应端。

## UE 思路与 Pico 对应关系

```text
UE UPlayer / UNetConnection / APlayerController / Role / ProcessEvent
                 |
Pico PNetPlayer / FNetConnection / PPlayerController / ENetRole / ProcessEvent
```

服务器为每条已打开连接创建 `PNetPlayer`，再沿用现有 `Login -> PostLogin -> RestartPlayer -> Possess` 链生成
PlayerController、PlayerState 和 Pawn。`PNetPlayer` 保存 ConnectionId，ReplicationSystem 保存 Actor 所属连接。
Ownership 会沿 Actor Owner 链查询，因此 Pawn 和 PlayerState 可以继承 PlayerController 的网络拥有者。

## Gameplay 对象可见性

- `GameMode` 只在 Standalone 和 Server World 创建；Client World 不再创建本地 GameMode。
- `GameState` 在服务器创建并复制，客户端收到 Spawn 后绑定到自己的 World。
- `PlayerController` 标记为 `OnlyRelevantToOwner`，只出现在服务器和所属客户端。
- `PlayerState` 与 Pawn 对所有客户端复制。
- 所属客户端上的 PlayerController/Pawn 为 `AutonomousProxy`，其他客户端看到的 Pawn 为 `SimulatedProxy`。
- 独立服务器不创建 `LocalPlayer`；客户端收到所属 PlayerController 后才把本地输入入口绑定给它。

`Pawn.Controller` 使用 `OwnerOnly` 条件，避免非拥有者客户端收到一个不可见 PlayerController 的悬空对象引用。

## RPC 路由

PHT 支持 `Server`、`Client`、`NetMulticast` 和 `Reliable` 标记，并拒绝互斥方向组合、无网络方向的 Reliable 以及
网络 Pure 函数。运行时 RPC 使用函数反射元数据和 `ProcessEvent`，参数按明确线格式序列化，不发送 C++ 地址。

```cpp
PFUNCTION(Server, Reliable)
void ServerTryInteract(PActor* Target);

PFUNCTION(Client, Reliable)
void ClientInteractionResult(bool bAccepted);

PFUNCTION(NetMulticast)
void MulticastDoorPulse(int32 Revision);
```

可靠 RPC 进入 Connection 的确认、重发和有序队列；不可靠 RPC 只发送一次，丢失后不会重发。首版支持现有
`PFunction` 标量、Name、String、Vector、Rotator、Transform、AssetPath 和 Actor 参数，每次最多 8 个参数。

服务器在调用前检查：目标 NetId、函数存在性、参数数量与类型、可靠性标记、RPC 方向、调用者是否拥有目标 Actor，
以及每连接每帧最多 32 次 RPC。任一检查失败都不会调用 `ProcessEvent`，并增加 rejected 统计。

## 开门闭环

`PSandboxReplicationLabActor` 现在也是测试门。客户端按 `F` 后执行：

```text
Local PlayerController
 -> ServerTryInteract(Door) [可靠 Server RPC]
 -> 服务器校验 Ownership 与距离
 -> ToggleDoor
 -> bDoorOpen + Transform 通过属性复制同步
 -> ClientInteractionResult [可靠 Client RPC]
 -> MulticastDoorPulse [不可靠 Multicast RPC]
```

按 `F1` 打开 Gameplay Debug，可以观察对象 Role、NetId、RPC sent/received/rejected、可靠与不可靠消息计数，
以及 Door open/closed、authority uses、multicast pulses 和 Client RPC replies。

## 自动化验收

- `PicoNetCoreTests` 验证不可靠消息丢失后不重发，重复包最多交付一次。
- `PicoReplicationTests` 验证 Autonomous/Simulated Role、合法 Server RPC、非拥有者拒绝、可靠性不匹配拒绝、
  Client RPC、Multicast RPC 和每帧调用上限。
- 完整 Debug 测试集 19/19 通过，并已重新构建 `PicoEditor.exe` 与 `PicoSandboxGame.exe`。

## 当前边界

- 本周 RPC 用于离散 Gameplay 事件，不用于每帧移动输入或连续 Transform。
- Multicast 是瞬时通知；门的最终状态以可靠属性复制为准，因此丢失 Multicast 不会造成永久状态错误。
- 尚未实现 RPC 参数分片、网络 Component/Subobject、Dormancy、重连或公网安全协议。
- Character SavedMove、服务器重演、Correction、模拟代理快照插值和网络延迟模拟属于第 4 周。

