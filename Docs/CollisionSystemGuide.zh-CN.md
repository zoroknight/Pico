# Pico 碰撞系统指南

本文从使用者视角整理 Pico 当前碰撞系统，重点说明 Collision Profile、Object Channel、Collision Response、
Collision Enabled、Physics Body Type、角色移动和网络复制之间的关系。联机玩家互挡与复制动态刚体的实现细节另见
[`CollisionProfilesAndNetworkPawnBlocking.zh-CN.md`](CollisionProfilesAndNetworkPawnBlocking.zh-CN.md)。

## 一句话理解

Collision Profile 是一套碰撞预设，用来集中回答：

1. 这个组件属于哪类碰撞对象；
2. 它遇到其他类型时是 Ignore、Overlap 还是 Block；
3. 它默认是否参与查询和物理解算。

Profile 不决定 Actor 是否联网复制，也不单独决定物体能否被推动。网络复制、刚体类型和碰撞 Profile 是三组相互
配合、但职责不同的配置。

## 完整碰撞链

```text
PrimitiveComponent 的碰撞形状
  -> Collision Profile 选择对象类型和默认响应
  -> Collision Enabled 决定参与查询、物理或两者
  -> Physics Body Type 决定 Static、Kinematic 或 Dynamic
  -> Pico Collision Filter 解析双方响应
  -> Jolt 接触或 CharacterMovement Sweep
  -> Block、Overlap、Ignore 结果
```

### 碰撞形状

Box、Sphere、Capsule 等 Shape 决定物理世界中实际参与检测的几何范围。可见 Mesh 和碰撞 Shape 是两件事：模型
看起来很复杂，并不代表碰撞体也必须同样复杂。

### Object Channel

Object Channel 表示“我是什么类型”。Pico 当前包括：

- `WorldStatic`：地面、墙壁等静态世界；
- `WorldDynamic`：由玩法移动但不自由模拟的世界对象；
- `Pawn`：玩家或可控制角色；
- `PhysicsBody`：动态刚体；
- `Trigger`：触发区域；
- `Projectile`：投射物；
- `Camera`：摄像机查询。

### Collision Response

每个对象都可以针对对方的 Object Channel 返回一种响应：

| Response | 结果 |
|---|---|
| `Ignore` | 不阻挡，也不产生重叠结果 |
| `Overlap` | 不阻挡，但可以产生重叠事件 |
| `Block` | 产生阻挡、Sweep Hit 或物理接触 |

最终结果由双方共同决定：任意一方 Ignore 就 Ignore；否则任意一方 Overlap 就 Overlap；只有双方都 Block 才 Block。

## Collision Enabled

Collision Enabled 决定组件进入哪条碰撞链：

| 设置 | 场景查询 Raycast/Sweep | 物理解算 Contact |
|---|---:|---:|
| `No Collision` | 否 | 否 |
| `Query Only` | 是 | 否 |
| `Physics Only` | 否 | 是 |
| `Query And Physics` | 是 | 是 |

`Query Only` 不等于 Trigger。一个 Query Only 对象仍可以是 Sweep 的 Blocking Target；只有 Trigger Profile 会被标记
为 Sensor，并默认使用 Overlap。

## Physics Body Type

Body Type 决定物理体如何移动：

| Body Type | 含义 | 常见用途 |
|---|---|---|
| `Static` | 运行时不移动，由静态世界持有 | 地面、墙壁 |
| `Kinematic` | 由代码或移动组件驱动，不由冲量自由推动 | Character 胶囊、门、移动平台 |
| `Dynamic` | 由物理解算、重力和冲量驱动 | 箱子、木桶 |

选择 `Physics Actor` 并不会自动让组件变成 Dynamic。一个可推动箱子仍需同时配置 Dynamic Body 或开启 Simulate
Physics。

## 当前 Collision Profile

| Profile | Object Channel | 默认行为 | 主要用途 |
|---|---|---|---|
| `Custom` | 根据 Body Type 推导 | 保留低层设置，响应默认 Block | 特殊配置、旧场景兼容 |
| `No Collision` | 不重要 | 不创建有效碰撞 | 纯视觉对象 |
| `Block All` | `WorldStatic` | Query And Physics，阻挡全部 | 地面、墙壁 |
| `Pawn` | `Pawn` | Query And Physics，阻挡其他 Pawn | Character 根胶囊 |
| `Pawn (Ignore Pawns)` | `Pawn` | 阻挡世界，忽略 Pawn | 不需要玩家互挡的角色 |
| `Character Mesh` | `Pawn` | No Collision | 角色视觉 Mesh |
| `Physics Actor` | `PhysicsBody` | Query And Physics，阻挡全部 | 动态箱子、木桶 |
| `Trigger` | `Trigger` | Query Only，Overlap 全部 | 拾取区、触发区域 |
| `Projectile` | `Projectile` | Query Only，阻挡世界、Pawn 和 PhysicsBody | 子弹、能力投射物 |

## Pawn、Physics Actor 与 Custom

### Pawn

`Pawn` 把组件标识为玩家或可控制角色。它默认阻挡世界、物理物体和其他 Pawn，适合作为 Character 的根胶囊。

Pico Character 的实际移动由 CharacterMovement Sweep 负责。运行时会抑制胶囊直接推动本地 Jolt 刚体的 Contact
Response，避免网络快照直接移动代理胶囊时把客户端箱子错误推走；角色与世界、箱子及其他玩家仍通过 Sweep 阻挡。

普通箱子即使设成 Pawn 后看起来也能阻挡角色，语义仍是错误的。后续角色过滤、网络代理、AI 查询或伤害逻辑可能把
它误认为玩家对象，因此不应靠“目前也能挡住”选择 Profile。

### Physics Actor

`Physics Actor` 把组件标识为 `PhysicsBody`，默认参与 Query 和 Physics 并阻挡其他 Channel，适合动态刚体。

标准可推动箱子配置为：

```text
Collision Profile = Physics Actor
Collision Enabled = Query And Physics
Physics Body Type = Dynamic
Simulate Physics = On
```

如果 Body Type 仍是 Static 或 Kinematic，箱子可以阻挡玩家，但不会像自由动态刚体一样响应重力和推动冲量。

### Custom

`Custom` 不套用固定 Profile，而是保留组件当前的 Collision Enabled 和 Sensor 等低层配置。当前 Object Channel 根据
Body Type 推导：

| Body Type | Custom 推导出的 Object Channel |
|---|---|
| `Static` | `WorldStatic` |
| `Kinematic` | `WorldDynamic` |
| `Dynamic` | `PhysicsBody` |

当前 Custom Filter 的各 Channel 响应默认是 Block，但编辑器还没有 UE5 那种逐 Channel 的 Ignore、Overlap、Block
响应表。因此 Custom 目前主要用于低层组合和兼容入口，不代表已经拥有完整的自定义 Collision Preset 能力。

还要注意配置来源：新组件的 Custom 默认可能保留 `No Collision`；从 Pawn 等 Profile 切换到 Custom，则会保留切换前
的 Collision Enabled。选择 Custom 后必须同时检查 Collision Enabled，不能只看 Profile 名称。

当编辑 Collision Enabled 或 Sensor 时，组件会自动切到 Custom，表示这些值已不再完全服从某个固定 Profile。

## 常用配置

### 地面和墙壁

```text
Collision Profile = Block All
Physics Body Type = Static
```

### Character 根胶囊

```text
Collision Profile = Pawn
Physics Body Type = Kinematic
```

不希望玩家互相阻挡时使用 `Pawn (Ignore Pawns)`，不要直接关闭整只角色的碰撞。

### 角色视觉 Mesh

```text
Collision Profile = Character Mesh
```

移动和阻挡由根胶囊负责，Mesh 只负责显示和动画，避免同一角色拥有两套互相干扰的主碰撞体。

### 动态箱子

```text
Collision Profile = Physics Actor
Physics Body Type = Dynamic
Simulate Physics = On
```

### 触发区域

```text
Collision Profile = Trigger
Physics Body Type = Static 或 Kinematic
```

Trigger 产生 Overlap，不应用来承担墙壁或地面的阻挡职责。

## 碰撞与网络复制

Collision Profile 只决定当前 World 中如何碰撞，不决定是否跨网络同步。`Replicates` 和 `Replicate Movement` 属于 Actor
网络配置。

```text
Physics Actor + Dynamic
Replicates = Off
Replicate Movement = Off
```

表示服务器和每个客户端都可以加载并模拟自己的本地箱子，但这些箱子不共享位置。一个客户端推箱子，不保证其他窗口
中的箱子跟随。

服务器权威共享动态箱子通常需要：

```text
Collision Profile = Physics Actor
Physics Body Type = Dynamic
Replicates = On
Replicate Movement = On
```

门、宝箱等对象可以采用 `Replicates = On`、`Replicate Movement = Off`，只复制 Open、Active 等玩法状态，再由
RepNotify 在每个 World 应用位置和碰撞。详细组合见
[`ReplicationSwitchesAndWorldIsolation.zh-CN.md`](ReplicationSwitchesAndWorldIsolation.zh-CN.md)。

## 快速判断

- 角色胶囊：选择 `Pawn`；
- 动态箱子：选择 `Physics Actor`，再启用 Dynamic；
- 纯视觉 Mesh：选择 `Character Mesh` 或 `No Collision`；
- 拾取区：选择 `Trigger`；
- 特殊低层组合：选择 `Custom`，并检查 Collision Enabled；
- 是否联网：检查 Actor 的 Replicates；
- 是否同步位移：检查 Replicate Movement；
- 是否能被冲量推动：检查 Dynamic 和 Simulate Physics。

## 当前边界

- Profile 仍是代码定义的固定预设，尚未实现 UE5 Project Settings 式自定义 Profile 资产；
- Custom 尚未在编辑器暴露逐 Object Channel 响应矩阵；
- Character 使用专用 Sweep 和网络移动链，不能只用普通刚体接触解释；
- 普通动态刚体采用服务器权威快照和客户端短时预测，尚未实现 Chaos 式 Physics Resimulation；
- 运行时修改碰撞不会因为 Actor 开启 Replicates 就自动同步，应复制有业务含义的状态并在 RepNotify 中应用。

## 相关实现与文档

- `Source/Runtime/PhysicsCore/Public/Pico/PhysicsCore/CollisionTypes.h`
- `Source/Runtime/PhysicsCore/Private/CollisionTypes.cpp`
- `Source/Runtime/Engine/Private/PrimitiveComponent.cpp`
- [`Month08_2_JoltPhysics.md`](Month08_2_JoltPhysics.md)
- [`Month08_3_CharacterMovement.md`](Month08_3_CharacterMovement.md)
- [`CollisionProfilesAndNetworkPawnBlocking.zh-CN.md`](CollisionProfilesAndNetworkPawnBlocking.zh-CN.md)
- [`ReplicationSwitchesAndWorldIsolation.zh-CN.md`](ReplicationSwitchesAndWorldIsolation.zh-CN.md)
