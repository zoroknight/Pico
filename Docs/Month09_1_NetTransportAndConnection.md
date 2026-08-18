# 第 6 月第 1 周：网络传输、连接与帧阶段

本阶段建立 Pico 的第一条网络基础链。目标不是同步 Actor，而是让一个服务器和两个客户端通过有界、可测试、
可观察的 UDP 协议稳定建立连接，并为后续 ActorChannel、Replication、RPC 和预测提供正确阶段。

## 模块边界

```text
PicoNetCore
 -> FNetAddress / FNetConnectionId / FNetObjectId
 -> FNetPacketHeader / FNetByteReader / FNetByteWriter
 -> INetTransport
      -> FLoopbackTransport
      -> FUdpTransport
 -> FNetConnection
      -> Handshake / Sequence / Ack / Reliable / Heartbeat / Timeout

PicoEngine
 -> FNetDriver
      -> NetMode / NetRole
      -> Connection 管理
      -> BeforeWorldTick TickDispatch
      -> AfterWorldTick TickFlush
```

`PicoNetCore` 不依赖 `PObject`、World、Actor、Jolt、渲染器或 ImGui。Windows UDP 使用非阻塞 Winsock，并由
Game Thread 每帧轮询；首版没有网络线程。`FNetDriver` 位于 Engine，因为它需要接入 GameEngine 和 World 帧阶段，
第二周的 ActorChannel 也会建立在这一层。

Windows 会把发送到尚未监听端口的 UDP 数据触发的 ICMP Port Unreachable，转换为后续 `recvfrom` 的
`WSAECONNRESET (10054)`。编辑器多进程启动时，客户端窗口可能早于服务器完成端口绑定，因此 Transport 会关闭
`SIO_UDP_CONNRESET`，并把残留的 `10054` 视为可重试的“当前无数据”；Connection 的握手重发和超时仍负责判断
服务器最终是否可达，调试面板不再把启动竞态显示为永久 Last Error。

## Wire Format

单个 UDP Datagram 上限为 1200 字节，首版不分片。Packet Header 使用显式大端字节序：

```text
Magic             uint32
ProtocolVersion   uint16
Flags             uint16
ConnectionId      uint32
Sequence          uint32
Ack               uint32
AckBits            uint32
PayloadBytes      uint16
Payload           0..1174 bytes
```

解码会拒绝错误 Magic、未知版本、未知 Flag、截断、Payload 长度不符和超过上限的数据。Sequence 比较使用回绕安全
算法，`AckBits` 表示 Ack 之前 32 个 Packet 的接收情况。

## 连接与握手

```text
ClientHello(Protocol, Nonce)
 -> ServerWelcome(ConnectionId, Nonce)
 -> ClientAck(ConnectionId, Nonce)
 -> Open
```

服务器只在收到合法、无 ConnectionId 的 ClientHello 后创建 Connection。客户端第一次收到 Welcome 后采用服务器
返回的数字地址，后续 Packet 必须来自完全相同的 Endpoint。连接具有 Handshaking、Open、Closing、Closed 状态，
并通过周期 Heartbeat 和五秒空闲超时清理失联端点。

## 可靠消息

Packet Sequence 与 ReliableId 分离。一个可靠消息重发时可以进入新的 Packet；接收端按 ReliableId 去重、缓冲乱序
消息并严格有序交付。发送队列、接收缓冲、重发次数、单帧发送数量和已发送 Packet 历史都有硬上限。

当前可靠层适合小型控制消息和后续可靠 RPC，不支持任意大消息、分片或商业级拥塞控制。移动输入和高频状态将在
后续使用可覆盖旧值的不可靠消息。

## 帧顺序

```text
Input
 -> BeginFrame
 -> FNetDriver::TickDispatch
 -> PGameInstance::Tick
 -> PWorld::Tick
 -> FNetDriver::TickFlush
 -> GC Safe Point
 -> Frame Pacing
```

GameEngine 测试通过可控 Transport 记录 World TickCount，直接证明 Dispatch 位于 World 前，Flush 位于 World 后。

## 启动方式

```powershell
.\Build\Debug\PicoSandboxGame.exe -server -port=7777
.\Build\Debug\PicoSandboxGame.exe -client=127.0.0.1 -port=7777
```

`-server` 与 `-client` 互斥，端口必须位于 1～65535。没有网络参数时保持 Standalone，不打开 Socket。

编辑器现已提供 UE 风格的最小 Play Session 下拉菜单：可选择 Standalone，或一个可视化独立服务器加 1～4 个
客户端，并配置端口及客户端窗口尺寸。设置持久化到 `Saved/Editor/PlaySettings.ini`；每次 Session 将各实例日志
分别写入 `Saved/Logs/PlaySession/Session_*/Server.log`、`Client_1.log` 等文件。绿色 Play 启动整组进程，红色
Stop 统一关闭仍在运行的实例；某个客户端单独退出不会误关其他客户端。

当前 `-server` 仍创建渲染窗口，因此界面明确称为“可视化独立服务器”。Listen Server 和不初始化窗口、输入、
渲染的 Headless Dedicated Server 尚未冒充为已完成能力，待 Actor Replication 与服务器 Gameplay 路径稳定后接入。

运行时按 `F1` 打开 Gameplay Debug，其中 Network 区域显示 NetMode、本地/远端地址、Connection 状态、RTT、包统计、
重复/乱序数量、可靠队列、非法包和最近错误。

## 自动化与真实进程验收

- `PicoNetCoreTests` 验证 Wire Format、非法包、Sequence 回绕、Loopback 丢包/重复/延迟、三步握手、可靠消息
  丢包重发、恰好一次与有序交付、队列上限、握手超时和 localhost UDP。
- `PicoGameTests` 验证一个 NetDriver 服务器接受两个 Loopback 客户端、超时隔离、真实 UDP 双客户端握手，以及
  `TickDispatch -> World -> TickFlush` 顺序。
- 三个独立 `PicoSandboxGame` 进程已完成真实 UDP 冒烟：服务器 `127.0.0.1:17877` 接受两个临时端口客户端，
  三方均进入 Open，客户端退出后服务器超时清理，三个进程退出码均为 0。
- `PicoEditorTests` 验证 Play 设置保存加载、Standalone 单进程计划、独立服务器加 N 客户端角色分配、命令行与
  独立日志路径；Runtime 接受实例标题、窗口尺寸和级联位置参数。

## 本阶段边界

本周没有实现 ActorChannel、Actor Spawn/Destroy、属性 Replication、RPC、CharacterMovement 预测、Dedicated
Server Target、公网、重连、分片或加密。`ENetRole` 和 `FNetObjectId` 已建立强类型，但 Actor Role、网络对象映射
和 Ownership 语义由第二周落地。
