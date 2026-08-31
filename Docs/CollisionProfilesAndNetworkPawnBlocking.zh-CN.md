# Collision Profile 与联机玩家阻挡

本文记录 Pico 第一版 UE 风格碰撞语义、Agent 配置方式和联机玩家胶囊阻挡边界。目标不是复制 UE 的完整
Collision Preset 系统，而是让碰撞规则、Jolt 后端、CharacterMovement 和服务器权威网络移动使用同一份语义。
Collision Profile、Collision Enabled、Body Type 与常用配置的基础说明见
[`CollisionSystemGuide.zh-CN.md`](CollisionSystemGuide.zh-CN.md)。

## 分层

```text
编辑器 / Agent
  -> Collision Profile、Physics Body Type、Replicates、Replicate Movement
  -> .pworld 持久化
  -> PPrimitiveComponent 生成 FCollisionFilterData
  -> Jolt Moving / NonMoving BroadPhase
  -> Pico 查询与接触过滤解析 Ignore / Overlap / Block
  -> CharacterMovement Sweep
  -> SavedMove、服务器重演、Correction 与 SimulatedProxy
```

Jolt BroadPhase 仍只区分 Moving 与 NonMoving。`WorldStatic`、`Pawn`、`PhysicsBody` 等是 Pico Gameplay
Channel，不会让后端 BroadPhase Layer 随玩法类型膨胀。双方 Response 的最终结果遵循：任一方 Ignore 则 Ignore；
否则任一方 Overlap 则 Overlap；只有双方都 Block 才阻挡。

`Collision Enabled` 现在同时保留 Query 与 Physics 两个维度。Jolt Body 即使因为 `QueryOnly` 使用 Sensor 形态，
查询过滤也只把显式 `Trigger` 当作 Sensor 忽略；QueryOnly Blocking Body 仍会被 Raycast/Sweep 命中，但不会建立
物理解算 Contact。`PhysicsOnly` Body 则参与解算而不会进入场景查询。

## 首批 Profile

| Profile | 主要用途 | 默认行为 |
|---|---|---|
| Custom | 兼容旧场景和低层设置 | 按 Body Type 推导 Object Type，默认 Block |
| No Collision | 关闭碰撞 | 不创建物理体 |
| Block All | 地面、墙壁 | WorldStatic，Block |
| Pawn | Character 胶囊 | Pawn，阻挡其他 Pawn |
| Pawn (Ignore Pawns) | 不需要玩家互挡的 Character | 阻挡世界，忽略 Pawn |
| Character Mesh | 角色视觉 Mesh | 不参与碰撞 |
| Physics Actor | 动态箱子 | PhysicsBody，Block |
| Trigger | 拾取区、触发器 | QueryOnly，Overlap |
| Projectile | 投射物查询 | 阻挡世界、Pawn 与 PhysicsBody |

`PCharacter` 和 `PSandboxPawn` 的 `CollisionCapsule` 默认使用 Pawn Profile、Kinematic Body。Profile 对编辑器仍呈现
`QueryAndPhysics`，但 Character 运行时关闭胶囊的 Jolt Contact Response：角色、地面和箱子仍通过 Sweep 相互阻挡，
不会因为网络快照直接移动胶囊而让客户端物理解算器推动本地刚体。两个客户端各自拥有一个 AutonomousProxy 和一个
SimulatedProxy；本地预测 Sweep 会被远端胶囊阻挡，服务器使用同一规则重演并保留最终权威。

`PCharacterMovementComponent` 额外暴露 `Enable Physics Interaction` 与 `PushImpulse`。Standalone 中由本地
CharacterMovement 显式施加冲量；网络会话中 Authority 决定最终玩法结果，AutonomousProxy 在两次服务器快照之间
执行本地预测推动。SimulatedProxy 保留查询阻挡，但绝不能改变观察端本地的箱子。复制 Dynamic Body 在客户端仍标记
为 Network Physics Proxy，但保留 Dynamic 本地模拟，服务器快照会覆盖预测误差；这接近 UE 默认 Physics
Replication 的基本方向，但 Pico 尚未实现 Chaos Resimulation。

## Agent 配置

Agent 继续使用通用 `editor.object.describe` 与 `editor.object.set_properties`，没有新增一次性碰撞工具。描述结果会
暴露 Collision Profile 枚举及语义，并对以下组合给出 `configuration_warnings`：

- Replicate Movement 已开但 Replicates 未开；
- 已复制 Actor 的 Dynamic 组件没有开启 Replicate Movement；
- Skeletal Mesh 错用 Pawn Profile；
- Physics Actor Profile 没有配 Dynamic Body。

配置服务器权威同步箱子时，可输入：

```text
检查场景中的 PhysicsCrate。把 Actor 的 Replicates 和 Replicate Movement 打开；
把根组件设置为 Physics Actor Profile、Dynamic Physics Body，并保存当前世界。
```

对照不同步效果时，可输入：

```text
关闭 PhysicsCrate 的 Replicates 和 Replicate Movement，保留根组件的 Physics Actor 与 Dynamic，
保存当前世界。不要运行或打包项目。
```

这是修改场景中同一个 Actor 的项目配置，不是单独修改 Client 2。关闭复制后，各进程加载并模拟自己的地图箱子；
开启两个复制开关后，服务器 Jolt 模拟权威箱子，客户端使用 Dynamic Network Physics Proxy 做短时本地预测并接收
服务器位置、旋转和速度。地图副本、Actor 复制和整个网络会话的区别见
[`ReplicationSwitchesAndWorldIsolation.zh-CN.md`](ReplicationSwitchesAndWorldIsolation.zh-CN.md)。

## 自动化验收

- `PicoPhysicsTests` 验证 Pawn 对 Pawn 阻挡、Pawn (Ignore Pawns) 放行，以及 QueryOnly Pawn 不建立 Physics
  Contact 但仍是 Blocking Sweep Target。
- `PicoCharacterMovementTests` 验证 CharacterMovement 胶囊阻挡另一名 Character，并验证关闭
  `Enable Physics Interaction` 后不会推动 Dynamic Body。
- `PicoSandboxTests` 建立一个服务器 World 和两个客户端 World；Player A/B 分别归属两条连接，两个客户端获得相反的
  AutonomousProxy/SimulatedProxy 角色，并从各自视角验证所属玩家被远端胶囊阻挡；测试还把 SimulatedProxy 快照
  直接放入客户端本地 Dynamic Body，确认该非复制刚体不再被代理胶囊推动。
- `PicoEditorTests` 验证 Agent 可以通过通用属性事务配置复制、Collision Profile 和 Dynamic Body，并能读取错误
  组合警告；Undo 会恢复整次修改。

## 当前边界

- Profile 是固定首批预设，尚未提供 UE Project Settings 式任意 Channel/Response 表编辑器。
- 运行时碰撞变化不会因为 Actor 开启复制而自动同步；应复制有业务含义的状态并在 OnRep 中应用，`NetworkDoor`
  已采用该方式。
- 非复制 Dynamic Body 在每个网络进程中仍是彼此独立的本地实例；本地 AutonomousProxy 可以推动自己的副本，
  远端 SimulatedProxy 不能推动观察端副本。这不会把三份独立模拟变成共享状态，影响玩法的动态刚体仍必须开启
  Replicates 与 Replicate Movement。
- 玩家互挡会受远端代理位置延迟影响，服务器纠错仍可能在高 RTT 下出现；当前未实现玩家推挤、踩头、移动基座、
  Spawn Overlap 策略或物理回滚。
- 普通动态刚体采用服务器权威快照与客户端短时 Dynamic 预测，暂不引入可重演的 Physics Resimulation。
