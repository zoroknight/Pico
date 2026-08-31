# Replicates、Replicate Movement 与 World 隔离

本文解释 Pico 联机测试中最容易混淆的三个概念：地图加载、Actor 复制和整个网络会话。重点回答关闭
`PhysicsCrate` 的两个复制开关后，为什么角色仍会同步和接受服务器纠错，以及非复制箱子为什么会在不同窗口中
产生不同结果。

## 先区分三个 World

`Separate Server + Clients` 启动一个服务器和两个客户端时，实际存在三个独立 World：

```text
Server World
Client 1 World
Client 2 World
```

每个进程都会独立加载同一份 `.pworld`。因此，放在地图里的 `PhysicsCrate` 即使关闭网络复制，也会在三个 World
中各出现一次。这是地图反序列化创建了三个本地实例，不是网络系统复制出了一只共享箱子。

三个实例可以从完全相同的位置开始，但它们分别拥有自己的 Transform、Jolt Body 和速度。相似的初始状态或短时间
相似的运动不能证明它们正在同步。

## 两个复制开关

### Replicates

`Replicates` 决定 Actor 是否进入网络复制系统。开启后，服务器可以为 Actor 建立 ActorChannel，并同步：

- 网络生成和销毁；
- 带 `Replicated` 标记的反射属性；
- RepNotify；
- 网络对象引用和所有权关系；
- 以该 Actor 为目标的网络 RPC 基础关系。

关闭后，服务器不会为该 Actor 建立正常复制通道。地图中仍能看到它，不代表它正在复制。

### Replicate Movement

`Replicate Movement` 决定通用 Actor 移动快照是否发送。当前 Pico 快照可以包含：

- Transform；
- 线速度；
- 角速度；
- 动态刚体激活状态。

它依赖 `Replicates`。四种组合的含义如下：

| Replicates | Replicate Movement | 含义 |
|---|---|---|
| Off | Off | Actor 不进入网络复制；地图实例各自运行 |
| Off | On | 无效配置；没有 ActorChannel 可以发送移动快照 |
| On | Off | Actor、属性和 RPC 关系可复制，但不自动复制通用移动 |
| On | On | Actor、属性和通用移动都由服务器复制 |

`Replicates On + Replicate Movement Off` 不等于 Actor 永远不能在客户端改变位置。玩法可以复制一个更有意义的
状态，再通过 RepNotify 应用结果。例如门复制 `bDoorOpen`，客户端收到后自行改变门的位置和碰撞。

## Replicates 开、Replicate Movement 关

这个组合最容易被误解。它的准确含义是：

- Actor 仍有网络身份，服务器可以为它建立 ActorChannel；
- Actor 的生成、销毁、复制属性、RepNotify、网络引用和 RPC 仍可工作；
- 只有通用 Transform、线速度和角速度快照被关闭；
- `Replicate Movement = Off` 不会禁止 Actor 移动，只是不再由通用移动复制链告诉其他 World“它移动到了哪里”。

对于关卡中预先放置的 Actor，服务器与各客户端仍会从同一张地图各自创建一个本地实例。`Replicates = On` 会让
网络系统把这些启动 Actor 绑定到同一个网络身份，而不是让三个进程共用同一块内存。

### 用在 PhysicsCrate 上

`PhysicsCrate` 的主要可见变化就是刚体位置与速度。因此当它采用 `On + Off` 时，各 World 可以继续模拟自己的动态
刚体，但服务器不会下发通用移动快照。客户端 1 推动本地箱子后，客户端 2 的箱子通常不会跟随，三个 World 的位置
可能逐渐分离。

这在视觉上可能接近 `Off + Off`，但底层并不相同：`On + Off` 的箱子仍有 ActorChannel，也仍能同步以后加入的
玩法属性、网络销毁或 RPC；`Off + Off` 则完全不进入该 Actor 的复制链。

### 用在 NetworkDoor 上

门更适合展示这个组合的价值。服务器不必每帧复制门的 Transform，而只需复制 `bDoorOpen` 这样的玩法状态：

```text
客户端按 F
  -> RPC 请求服务器开门
  -> 服务器修改 bDoorOpen
  -> 属性复制到客户端
  -> RepNotify 在各客户端应用门的位置和碰撞
```

因此，即使 `Replicate Movement = Off`，两个客户端仍能看到门一致地打开。这里同步的是“门已打开”这个语义，
Transform 只是每个 World 根据该状态计算出的表现。

可以把两种方案理解为：

- `On + On`：服务器持续告诉客户端“这个 Actor 现在在哪里”；
- `On + Off` 加复制属性：服务器告诉客户端“这个 Actor 现在是什么状态”，客户端据此更新表现。

门、宝箱和开关通常适合后者；自由移动的动态箱子、移动平台通常需要前者。Character 则使用专用网络移动链，不能
仅用这两个通用开关概括。

## 为什么箱子关闭复制后角色仍会纠错

这两个开关只属于 `PhysicsCrate`，不会关闭 PlayerController、Character、PlayerState、RPC、NetDriver 或连接。
Character 使用的是独立网络移动链：

```text
客户端输入
  -> AutonomousProxy 本地预测
  -> SavedMove 发送服务器
  -> Authority 重演移动
  -> Correction 返回所属客户端
  -> 客户端校正并重演尚未确认的 Move
```

所以关闭箱子复制后，仍会看到：

- 两名玩家的位置在三个窗口中更新；
- 本地玩家接受服务器 Correction；
- 远端玩家以 SimulatedProxy 插值或外推；
- RPC 和其他复制 Actor 继续工作。

这不是箱子复制仍在工作，而是角色自己的网络链路从未关闭。

## 关闭复制后推箱子的表现

当前 Pico 对网络角色采用以下边界：

- Authority 决定服务器上的最终玩法结果；
- AutonomousProxy 可以预测推动自己 World 中的 Dynamic Body；
- SimulatedProxy 只展示服务器角色状态，不能推动观察端本地刚体。

因此关闭箱子两个复制开关后：

1. 客户端 1 可以推动 Client 1 World 中的箱子；
2. Client 2 World 中的箱子不会跟随移动；
3. 客户端 2 看到的客户端 1 角色是 SimulatedProxy，不能推动客户端 2 的箱子；
4. 服务端快照仍会让这个远端角色继续到达权威位置，因此它可能在客户端 2 的画面中穿过客户端 2 独有的箱子。

第 4 点是非复制本地障碍与服务器权威角色移动之间的必然冲突。客户端 2 不能用一个服务器不知道的本地箱子，擅自
修改远端玩家的位置。会影响联网玩法路径的箱子、门、移动平台和障碍物必须复制，或者由服务器拥有等价的权威状态。

如果角色在自己的窗口中穿过自己正在推动的箱子，则不属于上述正常边界，应视为本地预测、Sweep 或物理校正问题。

## 关闭两个开关不等于关闭网络

可以把网络能力粗略分成五层：

```text
1. Transport / Connection / Session
2. Actor Spawn、Destroy、Property Replication
3. Actor Movement Replication
4. Character SavedMove、Correction、SimulatedProxy
5. RPC 与 Gameplay 权威状态
```

关闭 `PhysicsCrate` 的 `Replicates` 和 `Replicate Movement`，只关闭这个 Actor 使用第 2、3 层的能力。其他 Actor
和整个会话仍在使用其余层。

如果多个进程收到近似输入并独立推动各自箱子，短时间内可能看起来接近。这只是平行模拟，不具备网络保证，随着帧率、
碰撞顺序或延迟变化仍会分叉。

## 怎样真正关闭网络同步

编辑器 Play Settings 选择 `Standalone`，才是不创建客户端网络会话的正常单机路径：

- 没有服务器 Connection 和 ActorChannel；
- 没有 SavedMove 或 Correction；
- 没有 AutonomousProxy 和 SimulatedProxy；
- 没有网络 RPC；
- 本地 World 直接拥有玩法权威。

当前编辑器一次启动一个 Standalone 玩家。分别启动两个 Standalone 进程时，它们相当于两个独立单机游戏：

- 从相同地图和初始数据开始；
- 一边移动、推箱、开门，另一边完全看不到；
- 两边分别拥有自己的 GameMode、GameState、角色和物理世界；
- 不存在共同胜负状态，也不存在服务器纠错。

让联网 Client 直接断开服务器不等于切换到 Standalone。Client World 通常没有本地服务器 GameMode 和完整权威上下文，
断线后应进入连接失败、超时或重连流程，而不是自动变成单机游戏。

## 配置建议

| 对象用途 | 建议配置 |
|---|---|
| 纯客户端装饰、不会影响玩法 | 可以关闭复制，并使用 No Collision 或不阻挡 Pawn |
| 共享静态关卡障碍 | 放入同一地图；所有端保持一致配置，不在运行时单独改变 |
| 可推动箱子、移动平台 | 开启 Replicates 与 Replicate Movement，由服务器权威模拟 |
| 门、机关、宝箱 | 开启 Replicates；优先复制 Open/Active 等玩法状态并在 RepNotify 中应用 |
| Character | 使用专用 SavedMove、Correction 和 SimulatedProxy 链路，不用通用 Actor Movement 替代 |

判断原则很简单：如果一个对象会影响玩家是否能通过、站立、命中、拾取或获胜，它就不能只是某个客户端独有的本地
阻挡物。

## 可视化对照

### 共享箱子

```text
Replicates = On
Replicate Movement = On
Collision Profile = Physics Actor
Physics Body Type = Dynamic
```

客户端本地做短时 Dynamic 预测，服务器快照提供最终权威；三个窗口最终应看到同一位置。

### 独立箱子

```text
Replicates = Off
Replicate Movement = Off
Collision Profile = Physics Actor
Physics Body Type = Dynamic
```

每个 World 独立模拟。所属客户端可以推动自己的副本，远端 SimulatedProxy 不得推动观察端副本，三个箱子不保证一致。

### 只关闭通用移动复制

先将 `PhysicsCrate` 配置为：

```text
Replicates = On
Replicate Movement = Off
Collision Profile = Physics Actor
Physics Body Type = Dynamic
```

使用 `Separate Server`、两个可见客户端和 `RTT = 0 ms` 运行，然后只在客户端 1 推动箱子。

预期结果：

1. 客户端 1 的本地箱子可以移动；
2. 客户端 2 的箱子不应依靠通用移动快照跟随；
3. 服务器箱子可能按自己的物理接触独立移动，三个位置不保证一致；
4. 玩家角色仍正常同步和接受服务器纠错；
5. 网络调试信息中该箱子仍可拥有 ActorChannel，因为 `Replicates` 仍然开启。

这组测试验证的是“关闭箱子的通用移动快照”，不是“关闭整个网络”或“禁止箱子移动”。

随后对 `NetworkDoor` 使用同一组复制开关，并在客户端按 `F`。如果门的打开状态仍同步到另一客户端，说明复制属性、
RPC 和 RepNotify 链仍然工作，也证明 `Replicate Movement` 与玩法状态复制是两条不同的链路。

### 完全单机

```text
Play Mode = Standalone
```

此时没有网络角色、服务器纠错或其他客户端，所有行为只存在于当前进程。

## 相关文档

- [`CollisionProfilesAndNetworkPawnBlocking.zh-CN.md`](CollisionProfilesAndNetworkPawnBlocking.zh-CN.md)
- [`Month09_2_ActorReplication.md`](Month09_2_ActorReplication.md)
- [`Month09_4_CharacterNetworkMovement.md`](Month09_4_CharacterNetworkMovement.md)
