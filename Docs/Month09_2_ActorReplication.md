# 第 6 月第 2 周：Actor 与属性复制

本阶段把第 1 周的 UDP Connection 与 Engine 的 World、Actor、反射和 GC 串联起来。服务器现在可以为明确启用
复制的 Actor 分配网络身份，并为每条连接建立独立 ActorChannel；客户端能够生成或绑定对应 Actor，应用 Transform
和反射属性 Delta，修复延迟对象引用，并处理权威销毁。

## UE 思路与 Pico 对应关系

```text
UE UNetDriver / UNetConnection / UActorChannel / NetGUID / RepLayout
                 |
Pico FNetDriver / FNetConnection / ActorChannel / FNetObjectId / FReplicationSchema
```

Pico 没有复制 UE 的 Iris、ReplicationGraph、Dormancy 和 Subobject Replication，但保留了后续 RPC 与预测需要的
关键边界：Transport 不认识 Actor；网络身份不复用本地 Handle；属性布局按类生成；基线归每条连接所有。

## 网络对象身份

`FNetObjectRegistry` 保存 `FNetObjectId <-> FObjectHandle` 映射。服务器单调分配会话内 NetId，客户端只能绑定
服务器给出的 NetId。映射内部保存可失效 Handle，不保存跨 GC 裸指针；网络消息从不发送 Handle、SceneId 或地址。

## Replication Schema 与 PHT

`FReplicationSchema::Build(PClass)` 按基类到派生类的稳定顺序收集 `Replicated` 属性，生成 FieldId、类型、复制条件、
RepNotify 和 Schema Hash。首版支持现有反射标量、Vector、Rotator、Transform、AssetPath 和 Actor 引用，不支持动态
委托、容器、任意结构体和 Component/Subobject 复制。

PHT 支持以下写法：

```cpp
PPROPERTY(Replicated, Transient, NotSerializable,
    InitialOnly, RepNotify=OnRep_Health)
int32 Health = 100;

PFUNCTION()
void OnRep_Health();
```

`InitialOnly` 已生效；`OwnerOnly` 和 `SkipOwner` 元数据已建立，其中 `OwnerOnly` 在第 3 周完成 Ownership 前不会发送。
RepNotify 必须指向已反射的零参数函数，否则 Schema 构建失败。

## ActorChannel 与确认基线

```text
PendingOpen -> Open -> PendingClose -> Closed
```

每个 Connection 与每个 Actor 对应一个 Channel。Spawn、Delta 和 Destroy 使用有序可靠消息。Channel 排队消息时只
保存 Pending Snapshot；只有底层可靠消息收到 Packet ACK 后，才提交为该连接的确认基线。一个客户端延迟或丢包
不会推进另一个客户端的基线。存在尚未确认消息时不会继续堆叠同一 Actor 的 Delta，确认后重新捕获最新状态。

当前 Delta 发送变化字段的最终值，而不是依赖旧值的二进制补丁。这样可靠重发后可以直接收敛，并为未来高频
Transform 改成不可靠快照保留边界。

## Spawn、引用与 Destroy

Spawn 包含 NetId、类名、Actor 名、Schema Hash、初始 Transform 和属性。客户端先验证完整消息、类、Schema、字段
类型和长度，再创建 Actor；地图中已有的同名同类启动 Actor 可以被绑定，避免重复创建 GameState 一类对象。

对象属性发送目标 Actor 的 NetId。若引用方先到、目标后到，客户端先写空引用并保存 Owner Handle、属性和目标
NetId；目标 Spawn 到达后再类型检查并修复。反射属性记录引用目标 `PClass`，错误类型不会写入 `TObjectPtr`。

```text
服务器 Actor 离开 World
 -> Channel 可靠发送 Destroy
 -> 客户端 DestroyActor 并删除 NetId/Channel
 -> 服务器收到 ACK 后关闭 Channel
 -> 无剩余连接时删除失效映射
 -> GC 安全点回收
```

断线按 ConnectionId 清理自己的 Channel，不影响其他客户端的基线。

## 帧接入与调试

```text
TickDispatch
 -> 应用 Spawn/Delta/Destroy
 -> 处理可靠 ACK 并提交服务器基线
 -> GameInstance / World Tick
TickFlush
 -> 捕获服务器 World 最终状态并生成复制消息
 -> Connection 发包
 -> GC Safe Point
```

运行时按 `F1` 可查看 NetObject、ActorChannel、未解析引用、Spawn/Delta/Destroy、接收/拒绝和 OnRep 计数，并列出
每个 Channel 的 ConnectionId、NetId、状态、基线字段数和 Pending ReliableId。

## 自动化验收

`PicoReplicationTests` 创建两个独立 World，模拟可靠消息交付与确认，覆盖 Spawn、Transform、RepNotify、乱序 Spawn
引用修复、ACK 后基线提交、未变化零 Delta、`InitialOnly`、双连接独立基线、断线、Destroy，以及截断 Spawn 在创建
Actor 前被拒绝。PHT 测试同时验证 RepNotify 和复制条件代码生成。

## 当前边界

- `PGameStateBase` 默认启用复制，其他 Actor 必须显式调用 `SetReplicates(true)`。
- Actor Transform 当前通过可靠 Channel 直接应用，适合证明权威同步，不是 Character 的最终网络移动方案。
- PlayerController Ownership、PlayerState/Pawn 可见性、RPC 方向与权限在第 3 周完成。
- 模拟代理插值、自主代理 SavedMove/Correction/Replay 在第 4 周完成。
- 暂不支持 Component/Subobject、数组、Dormancy、分片、热更 Schema、动态刚体预测或 Root Motion 预测。
