# 第 6 月第 3.5 阶段：第三人称控制基线

## 目的

本阶段位于 RPC 与客户端预测之间。它不增加新的移动玩法，而是把已经调好的第三人称手感从
`PSandboxPlayerController` 中提取为可保存、可复用、可测试的控制策略，避免第 4 周为网络移动复制代码时
再次改变 W/A/S/D 方向、角色转向或摄像机行为。

这与 UE 的职责划分相近：输入映射描述“哪个按键产生哪个输入”，PlayerController 把输入转换为移动意图，
CharacterMovement 执行移动，SpringArm/Camera 负责视角，而 Data Asset 或 Blueprint 默认值保存项目策略。

## 资产与职责

Pico 新增 `.pcontrolprofile` 原生资产。Sandbox 默认资产为：

```text
/Game/Controls/ThirdPersonDefault.pcontrolprofile
```

它保存以下稳定参数：

- 移动参考系：ControlRotation、ActorRotation 或 World。
- 最大步行速度、角色转向速度、是否朝移动方向旋转。
- 瞄准时是否使用 Controller 期望朝向。
- 普通/瞄准摄像机臂长度、瞄准偏移。
- 初始俯仰角、俯仰上下限、SpringArm 是否使用 ControlRotation。

相关资产的边界固定如下：

```text
Input Mapping             按键和轴映射
ThirdPersonControlProfile 移动、转向和摄像机策略
Actor Blueprint           组件树、组件默认值和 Control Profile 引用
Character Profile         SkeletalMesh、动画集、材质和源坐标修正
GameMode                   选择 PlayerController 与默认 Pawn Class
```

因此更换人物模型不应改变移动策略；复用控制手感也不要求复制模型和材质。

## 运行链

```text
GameMode 生成 BP_Knight
 -> PlayerController::Possess
 -> PSandboxPawn 加载 ThirdPersonControlProfile
 -> 参数写入 CharacterMovement、Pawn 和 SpringArm
 -> Controller 使用共享 BuildThirdPersonMovementBasis
 -> CharacterMovement 执行唯一的位移与碰撞流程
```

`BuildThirdPersonMovementBasis` 是纯函数。当前 FreeLook 使用 ControlRotation 的水平 Yaw：W/S 沿摄像机
水平前后，A/D 沿屏幕左右；鼠标只修改 ControlRotation，角色由 CharacterMovement 朝移动方向平滑旋转。
Strafe 模式仍使用 ControlRotation，但角色朝 Controller 期望方向。

第 4 周网络预测必须传递轴值、输入序号和必要视角数据，并在客户端预测与服务器重演中调用同一个方向函数；
禁止在网络代码中复制另一份三角函数或按键方向表。

## 在其他项目复用

1. 将 `ThirdPersonDefault.pcontrolprofile` 复制到目标项目的 `Content/Controls`，可以改名但保留扩展名。
2. 在目标 Pawn 的 Data-Only Actor Blueprint 中，将 `ThirdPersonControlProfileAsset` 选择为该资产。
3. 让 GameMode 的 Default Pawn Class 指向这个 Blueprint。
4. 保留或替换项目 Input Mapping；Profile 不绑定具体键位。
5. 运行后按 `F1`，确认 Pawn Class、Control Profile、Policy Hash 和 `loaded: yes`。

调整手感时只修改 Profile，不修改 CharacterProfile，也不要把 Profile 参数重新硬编码进 Controller。

## 可视化验收流程

使用本阶段构建后的 `PicoEditor` 打开 PicoSandbox 与 `StarterWorld.pworld`：

1. 在 Content Browser 将类型筛选设为 `Control Profile`，确认能看到
   `/Game/Controls/ThirdPersonDefault.pcontrolprofile`。这验证 Registry 已识别新资产类型。
2. 打开 `BP_Knight` Actor Blueprint，在 Actor Defaults 中检查
   `Third Person Control Profile Asset`。下拉列表只能选择 Control Profile；这验证 PHT 生成的类型元数据和
   编辑器资产选择器使用了同一类型约束。
3. 保持默认 Profile，保存 Blueprint，以 Standalone 运行并按 `F1`。确认调试面板显示正确的 Pawn Class、
   Profile 路径、非零 Policy Hash 和 `loaded: yes`；这验证运行实例经 Possess 加载资产，而不是只依赖 CDO
   或 Controller 硬编码。
4. 固定鼠标分别按 W/S/A/D。W/S 应沿镜头水平前后，A/D 应沿屏幕左右；按 S 时角色转向移动方向，但镜头
   Yaw 不跟随角色转动。转动镜头后重复测试，方向应使用新的 ControlRotation。
5. 按住 Aim，确认进入 Strafe 策略，相机臂缩短并使用侧向偏移；上下移动鼠标，确认视角限制在 Profile 的
   `-75` 到 `+55` 度范围。
6. 使用 Separate Server 与两个客户端运行，在两个客户端的 F1 面板中对比 Pawn Class、Profile 路径和
   Policy Hash。三项必须一致，两个 Pawn 的出生位置应不同。

第 4 周前的预期边界：客户端移动仍是本地效果，独立服务器没有本地输入，其他客户端尚不会平滑显示该角色
移动。服务器重演、Ack/Correction 和模拟代理插值属于下一阶段，不应被误判为本阶段失败。

## 自动化保护

`PicoSandboxTests` 现在验证：

- Profile 保存/加载后 Policy Hash 不变。
- ControlRotation Yaw 为 0 度和 90 度时，W 与 D 的世界方向固定。
- ActorRotation 参考系使用 Actor Yaw，不误用 Camera Yaw。
- PHT 为 Profile 引用生成带类型的资产元数据，编辑器只允许选择 `.pcontrolprofile`。
- Possess 后 Movement、SpringArm 与俯仰限制来自 Profile。
- 两个网络玩家使用相同 Pawn Class、Profile 路径和 Policy Hash，同时保持不同出生位置。

这些测试锁定的是控制语义，不锁死未来的网络实现。SavedMove、Ack/Correction 和模拟代理插值可以扩展，
但不能悄悄改变本地输入的含义。
