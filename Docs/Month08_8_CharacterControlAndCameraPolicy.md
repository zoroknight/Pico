# Pico Character Control And Camera Policy

## 实现状态

本轮加固已经完成 A-D，以及 E 的首版能力：Character Profile 视觉 Transform、三种移动参考系、
UE 风格旋转策略、FreeLook/Strafe 切换、`PCameraActor` 和显式 `ViewTarget`。编辑器顶部
`Add > Camera` 会直接创建带默认 `CameraComponent` 的 `PCameraActor`。

第三人称回归中进一步按 UE 模板重构了 SpringArm：ControlRotation 不再写入组件 RelativeRotation，
而是由 `GetTargetRotation()` 独立计算 Camera Socket 的世界朝向。按 S 导致 Character 转身时，摄像机
只跟随角色位置，不再叠加 Actor Yaw；只有鼠标输入改变 ControlRotation 时才改变自由视角。

`SetViewTargetWithBlend`、Camera Lag、自动激活固定镜头和完整的策略冲突提示仍是后续增强，
不进入当前网络移动基线。首版固定镜头由 Gameplay 调用 `SetViewTarget`，目标销毁后会安全回退到
Possessed Pawn。

## 目标

本文记录 Pico 角色输入、朝向和摄像机的固定设计，作为进入网络移动前的实现依据。目标不是把某一种
WASD 规则写死进引擎，而是像 UE5 一样，由引擎提供通用能力、Gameplay 组合策略、模板给出开箱即用的
第三人称效果。

## UE5 对照结论

UE5 的 `APawn::AddMovementInput` 接收世界空间方向，只负责累计输入，不知道 W 应该表示摄像机前方、
角色前方还是世界固定方向。第三人称模板在 Character 蓝图或 C++ 中读取 `ControlRotation.Yaw`，计算
Forward/Right，再调用 `AddMovementInput`。

`UCharacterMovementComponent` 负责消费输入、加速度、碰撞、重力和角色旋转，并通过以下配置组合行为：

- `bOrientRotationToMovement`：朝移动加速度方向旋转；
- `bUseControllerDesiredRotation`：由 MovementComponent 平滑朝 Controller 期望方向旋转；
- Pawn 的 `bUseControllerRotationYaw`：角色直接响应 Controller Yaw；
- `RotationRate`：限制每秒旋转速度。

因此 UE5 提供的是能力和开关，最终控制策略通常由项目蓝图或 C++ 编写。Camera、SpringArm、CameraActor
与移动参考系彼此独立，切换 ViewTarget 不应暗中改变 WASD 规则。

UE `USpringArmComponent::GetTargetRotation()` 会在启用 `bUsePawnControlRotation` 时取 Pawn 的 View/Control
Rotation，再应用 `bInheritPitch/Yaw/Roll`。ControlRotation 是摇臂末端的目标世界朝向，不是简单写进
挂在 Pawn 根组件下的 RelativeRotation；否则 Actor 转身会再次叠加到 Camera，造成按 S 时镜头跟转。

## Pico 固定职责

```text
Input Mapping
 -> PlayerController 更新 ControlRotation 和读取二维 Move 输入
 -> Gameplay Movement Policy 将二维输入转换为世界方向
 -> Pawn::AddMovementInput 缓存世界空间移动意图
 -> CharacterMovement 执行加速、碰撞、旋转、Root Motion 和未来网络重演

PlayerController / ViewTarget
 -> SpringArm / CameraComponent / CameraActor

CharacterProfile
 -> Mesh、动画、材质和仅视觉使用的 Mesh Transform Offset
```

- 引擎层不得规定 W 永远等于某一种方向。
- Gameplay 层决定输入参考系和模式切换。
- `MovementComponent` 仍是 Transform、碰撞和网络重演的唯一执行边界。
- Character Profile 只描述资产装配和视觉坐标修正，不保存玩法控制策略。
- 网络记录最终世界移动意图或可确定性重建该意图的输入与 ControlRotation，不复制摄像机组件 Transform。

## 默认第三人称模板

PicoSandbox 默认采用与 UE5 Third Person Template 相同的自由探索模式：

```text
MovementReference             = ControlRotation
bOrientRotationToMovement     = true
bUseControllerDesiredRotation = false
bUseControllerRotationYaw     = false
SpringArm.UseControlRotation  = true
```

预期效果：

- 鼠标只改变 `ControlRotation`，SpringArm 带动摄像机围绕角色旋转，角色不会因空转镜头立即旋转；
- W/S 沿摄像机的水平前后方向移动，A/D 沿摄像机的水平左右方向移动；
- 发生移动时，CharacterMovement 按最短 Yaw 路径和 `RotationRate` 平滑把 Actor 转向移动方向；
- 停止输入时保留最后朝向；Pitch 不进入地面移动方向。
- 固定鼠标按 S 时 Camera Yaw 保持不变，角色沿 Camera 后方移动并平滑转身；摄像机只随角色位置平移。
- Pico 的 Camera View 以 `Cross(Forward, Up)` 作为屏幕右方向，因此 D 必须使用同一方向，不能直接假设
  数学常量 `RightVector` 与屏幕右侧具有相同符号。

这就是 Pico 在没有完整 Blueprint 系统时的“第三人称蓝图效果”。第一版由
`PSandboxPlayerController/PSandboxPawn` C++ 模板实现；未来 PicoGraph 只负责可视化组合同一组接口，不能
创建第二套移动链。

## 输入参考系

Gameplay 层增加：

```cpp
enum class EMovementReference
{
    ControlRotation,
    ActorRotation,
    World
};
```

| 参考系 | Forward/Right 来源 | 典型用途 |
| --- | --- | --- |
| `ControlRotation` | Controller 的水平 Yaw | 默认第三人称、越肩、瞄准 |
| `ActorRotation` | Pawn 当前水平 Yaw | 角色相对移动、坦克控制、独立横移 |
| `World` | 固定世界轴 | 棋盘、固定方向、特殊俯视玩法 |

无论参考系如何，最终都转换为世界方向后调用 `Pawn::AddMovementInput`。不要在
`CharacterMovementComponent` 内反查按键或摄像机。

`ActorRotation + bOrientRotationToMovement` 不能作为默认组合：角色侧向输入会改变 Actor 朝向，下一帧
Actor 的局部侧向也随之改变，容易形成反馈旋转。角色相对横移应保持朝向，或提供独立 Turn/Target 策略。

## 旋转策略

Pico 补齐 `bUseControllerDesiredRotation`，并采用与 UE 相同的清晰优先级：

```text
bOrientRotationToMovement
 -> 朝有效移动输入或加速度方向平滑旋转
else bUseControllerDesiredRotation
 -> 朝 Controller DesiredRotation 平滑旋转
else bUseControllerRotationYaw
 -> Pawn 直接响应 Controller Yaw
else
 -> Movement 不主动修改朝向
```

编辑器应对互斥开关给出警告，并显示实际生效的策略。所有平滑旋转使用规范化角度差和最短路径，不能
通过逐帧线性插值未经处理地跨越 `-180/180`。

## 控制模式

首版只实现两个模式：

| 模式 | 输入参考 | 角色旋转 | 用途 |
| --- | --- | --- | --- |
| `FreeLook` | ControlRotation | Orient to Movement | 默认探索 |
| `Strafe` | ControlRotation | Controller Desired Rotation | 越肩/瞄准 |

按下 Aim 时切换到 `Strafe`，松开后回到 `FreeLook`。切换可以同时调整 SpringArm 长度、SocketOffset、FOV
和灵敏度，但首版以输入与旋转正确为验收重点。控制模式属于可重建的 Gameplay 状态；网络阶段至少同步
影响权威朝向和命中判定的模式，不同步纯本地相机过渡。

## 模型 Forward 与视觉偏移

Pico 的统一 Gameplay 前向为 Actor 本地 `+X`。必须区分：

```text
Actor Forward       移动、碰撞、物理、Root Motion 和网络 Transform
Mesh Visual Forward 导入模型的视觉轴向
Camera Forward      当前视角方向
```

当前 Sandbox Pawn 中硬编码的 Mesh `180` 度旋转应迁移到 `.pcharprofile`：

```ini
[Visual]
MeshLocation=0,0,-96
MeshRotation=0,180,0
MeshScale=1,1,1
```

这样不同来源的角色可独立修正视觉坐标，而不反转 W 输入或污染 Actor、胶囊体、物理和网络方向。导入与
验收必须确认 Actor Forward、速度方向、模型正面和 Root Motion 前向一致。

## 编辑器配置归属

### Project Settings

- 默认 Pawn/PlayerController 类；
- Move、Look、Jump、Aim 输入映射；
- 鼠标灵敏度和默认地图。

### Pawn / Character Details

- `MovementReference`；
- `bOrientRotationToMovement`；
- `bUseControllerDesiredRotation`；
- `bUseControllerRotationYaw`；
- `RotationRate`、默认控制模式和 Auto Possess。

### SpringArm / Camera Details

- Use Pawn Control Rotation；
- Inherit Pitch、Inherit Yaw、Inherit Roll；
- Arm Length、Target/Socket Offset；
- 后续 Camera Lag、碰撞检测和 FOV。

Pico SpringArm 第一版按需计算 Camera Socket，不修改自身 Relative Transform：

```text
SpringArm Origin       = Component World Location + TargetOffset
SpringArm Target Yaw   = ControlRotation.Yaw（Use Pawn Control Rotation）
Camera Socket Position = Origin + TargetRotation * (-ArmLength + SocketOffset)
```

ActorRotation 可以改变角色和组件原点的位置/装配，但不能改变控制驱动的 TargetRotation。Actor Blueprint
选择 CameraBoom 时会显示 Actor Yaw、Control Yaw 和 Camera Target Yaw，并可编辑三个 Inherit 开关。

### Character Profile

- Mesh、AnimationSet、Montage、Materials；
- Mesh Location/Rotation/Scale Offset。

同一个 Profile 可以被不同控制策略使用，因此 Profile 不保存 `MovementReference` 或旋转策略。

## 固定摄像机

后续增加最小 `PCameraActor` 和 PlayerController ViewTarget：

```cpp
void SetViewTarget(PActor* Target);
void SetViewTargetWithBlend(PActor* Target, float BlendTime);
PActor* GetViewTarget() const;
```

视图来源优先级为显式 ViewTarget、Possessed Pawn 的 Active Camera、Controller 默认视角。固定 CameraActor
只改变画面来源；Gameplay 仍明确选择 Actor、World 或视图方向作为移动参考。首版可先做无 Blend 切换，
平滑 Blend 在基础生命周期、切图和目标销毁清理稳定后加入。

## 实施顺序与验收

### A. Forward 加固

- Character Profile 增加 Mesh Transform Offset，移除 Sandbox Pawn 的模型旋转硬编码；
- 增加 Actor Forward、Camera Forward、输入方向和速度方向调试显示；
- 验证 W 时 Actor Forward、速度和模型正面一致，Root Motion 不倒退。

### B. UE 风格旋转能力

- 实现 `bUseControllerDesiredRotation` 和最短路径旋转；
- 固定三种旋转配置的优先级、冲突诊断和自动化测试；
- 保证旋转进入移动状态快照和未来客户端纠错边界。

### C. Gameplay Movement Policy

- 增加 `EMovementReference` 与统一 `ResolveMovementBasis`；
- 支持 ControlRotation、ActorRotation、World；
- PicoSandbox 默认使用 ControlRotation，避免把模板策略写入 Engine。

### D. 编辑器与模式切换

- Details 暴露并持久化配置，Gameplay Debug 显示参考系、控制模式和生效旋转策略；
- 增加 FreeLook/Strafe 与 Aim 切换；
- 验收鼠标空转、WASD、最短路径转身和模式切换。

### E. 固定摄像机

- 增加 CameraActor、ViewTarget、目标销毁/切图回退和最小编辑器放置；
- 验证固定镜头不会隐式改变移动参考系；
- 后续增加 `SetViewTargetWithBlend`。

默认最终验收：鼠标环绕时角色保持朝向；W 沿镜头水平前方移动；角色平滑朝移动方向转身；停止后保持
最后朝向；固定鼠标按 S 时角色转身而 Camera Yaw 不变；Aim 时角色平滑面向镜头并以越肩方式前后横移；
所有模式下模型正面、Actor Forward、速度和 Root Motion 语义一致。
