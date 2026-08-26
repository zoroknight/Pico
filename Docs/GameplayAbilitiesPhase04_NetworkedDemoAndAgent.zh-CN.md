# Mini GAS 第 4 阶段：联网投射物 Demo、状态面板与 Agent

## 目标与边界

这一阶段把 Tag、Attribute、Effect、Ability 和 AbilityTask 接入真实 Gameplay、网络和 Agent 链路。为了让效果可直接观察，
联网 Demo 使用三种颜色固定、命中状态不同的服务器权威投射物。`FGameplayPredictionLedger` 与确认/拒绝回滚仍由
PicoInspector 的 Dash Prediction 面板独立验证，不把预测伤害或预测投射物混入本轮 Demo。

## Runtime 对象关系

```text
PSandboxPawn
  -> PGameplayAbilitySystemComponent (DefaultSubobject)
       -> PAttributeSet
       -> Gravity / Burn / Freeze AbilitySpec
       -> Cost / Cooldown / Duration / Periodic ActiveEffect
  -> PCharacterMovementComponent

PSandboxPlayerController
  -> ServerActivateGravityShot / BurnShot / FreezeShot RPC

PSandboxFireballActor
  -> replicated EffectTypeValue
  -> purple / red / blue Cube visual
  -> authority-only homing, impact and Effect application
```

角色现在使用可反射、可保存的 `Mini GAS Profile`。`bGravityShotEnabled`、`bBurnShotEnabled`、
`bFreezeShotEnabled` 决定能力集合；Details 还可编辑 Mana Cost、Cooldown、Range、Projectile Speed/Color、
Effect Duration，以及 Gravity Launch Velocity、Burn Tick Interval/Damage。运行时将 Cost 和 Cooldown 复制到
每个角色自己的 `FGameplayAbilitySpec`，不会修改全局 Ability CDO。`AbilityLoadoutBits` 仅保留为旧场景兼容字段。
`InitialHealth` 和 `InitialMana` 是可保存的初始 Attribute 输入；`ReplicatedHealth`、`ReplicatedMana`、Tag bit、
Effect/Cooldown Remaining 和 Revision 都是运行时复制摘要，只读且不可作为配置来源。

内部 C++ 类名仍为 `PSandboxDashAbility`、`PSandboxFireballAbility`、`PSandboxStunAbility`，它们是历史兼容标识，
不是当前玩法名称；输入、Tag、UI、Agent Schema 和 `semantic_name` 均按 GravityShot、BurnShot、FreezeShot 工作。

## 三种能力

- `1 - Gravity`：发射紫色投射物；命中后赋予 `State.GravityLifted` 2.5 秒，并把目标竖直速度设为 260，产生一次不太高的上抛。
- `2 - Burn`：发射红色投射物；命中后赋予 `State.Burning` 8 秒，每 1 秒由服务器扣除 5 Health。
- `3 - Freeze`：发射蓝色投射物；命中后赋予 `State.Frozen` 6 秒，期间停止目标移动、跳跃和 Ability 激活。

三种请求都由服务器检查目标、距离、Mana、Cooldown 和阻止 Tag；客户端不能生成权威投射物，也不能自行结算命中。

## GAS 状态同步

当前 ActorChannel 复制 Actor 的反射属性，不直接复制 Component 的内部容器。因此 ASC 是服务器玩法真源，Pawn 提供紧凑摘要：

- Health、Mana；
- Burning、GravityLifted、Frozen 与三个 Cooldown 的 Tag bitmask；
- 三种状态和三个 Cooldown 的剩余时间；
- GameplayStateRevision。

客户端 `OnRep_GameplayState` 将摘要写回本地 AttributeSet 和已知 Tag。它验证了权威、RepNotify 和 ASC 边界，但不是 UE GAS
Fast Array、Replication Mode、Aggregator 或 GameplayCue 的替代品。

## 固定状态面板

运行窗口右上角始终显示 `Mini GAS Status`，不需要打开 F1：

- 顶部固定显示 `1 PURPLE Gravity | 2 RED Burn | 3 BLUE Freeze`；
- 每个 Pawn 显示 NetId、网络角色，客户端自身带 `[LOCAL]`；
- 只显示 HP、Mana、三种 Effect 剩余时间和三个技能冷却；
- 独立服务器列出两个权威 Pawn，两个客户端分别列出本地与远端副本，便于直接比较三端结果。

F1 仍保留完整网络、对象链、物理和动画诊断，不承担日常 GAS 验收。

## Inspector 预测验收

打开 `PicoInspector.exe`，选择 `Experiments -> GAS Lab`：

1. 点击 `Predict Dash`，位置立即增加 260、Mana 减少 20、状态变为 Pending。
2. 点击 `Server Confirm`，预测结果保留且不重复位移或扣费。
3. 再次预测后点击 `Server Reject + Rollback`，位置和 Mana 恢复到快照。
4. Pending 时重复预测会被阻止，完成或未知 Key 不能重复结算。

## 联网验收

在 PicoEditor 的 Play 下拉菜单启动 `Separate Server + 2 Clients`，把角色移动到 900 以内：

1. 按 `1`，三端看到紫色投射物；命中后目标轻微起飞，Gravity 约 2.5 秒归零。
2. 按 `2`，三端看到红色投射物；Burn 显示约 8 秒，HP 每秒减少 5。
3. 按 `3`，三端看到蓝色投射物；Freeze 显示约 6 秒，目标期间不能移动、跳跃或施放能力。
4. 比较三端右上角面板，最终 HP、Mana、Effect 和 Cooldown 应一致；冷却期重复按键应被拒绝。

## Agent 安全接入

- `editor.gameplay.asc.describe` 只读 ASC、Attribute、Tag、Spec 和 ActiveEffect。
- `editor.gameplay.configure_ability_loadout` 只能选择 Gravity、Burn、Freeze 三个已注册能力，并通过反射属性、审批、World 事务和 Verifier 修改。
- 其他 Profile 字段复用 `editor.object.set_properties` 批量修改，包括颜色向量；无需为每个新反射属性增加专用 Tool。
- `editor.actor_blueprint.describe_defaults/set_defaults` 通用读写 Actor Blueprint 生成类默认值。GameMode 生成的
  玩家必须修改 Default Pawn Blueprint；只修改地图中的预览 Pawn 不会影响独立服务器和客户端新生成的角色。
- `configure-character-abilities.pskill` 限制可用工具；Knowledge Source 提供实时 ASC 与固定 GAS Schema。
- ASC 描述带 `schema_revision=2`：`semantic_name` 是当前能力名，`internal_class` 仅表示兼容类名；结果还包含
  所选角色的 `mini_gas_profile` 与 AbilitySpec 实际 Cost/Cooldown。
- 路由评测为 39 条，Golden Tasks 为 11 条，并检查禁止调用 Package。
- Agent 不能生成任意 Ability C++，也不能绕过服务端权威。

旧版 ASC 描述直接暴露 `Dash/Fireball/Stun` 内部类名，模型可能把它误判成更新前玩法，这不是运行时能力回退。
新实现已修正实时事实源。旧聊天仍可能保留历史文本，验收时以新的 ASC Tool Result 为准，必要时新建聊天。

另一个已修正的失败模式是把 `ReplicatedMana` 当作初始 Mana：该字段即使在编辑器内短暂写成 200，ASC 真值仍
可能是 100，并会在下一次状态刷新时覆盖。Agent 现在必须修改 `InitialMana`，并从运行时 AttributeSet/ASC 验证。

## 编辑器配置流程

1. 在 Scene Outliner 选中 `PSandboxPawn`，或打开对应 Actor Blueprint。
2. 在 Details 中修改 Initial Health/Mana、三个 Enabled 开关及相邻的消耗、冷却、射程、弹速、颜色、持续时间和效果强度。
3. 仅修改固定摆放角色时保存 World；希望正常 Play、独立服务器和客户端生成的新玩家都继承时，保存 Default Pawn Actor Blueprint。
4. 按 `1/2/3` 验证弹速、颜色、持续时间、伤害与冷却，并用右上角状态面板观察结果。
5. 让 Agent 调用 `editor.gameplay.asc.describe`，确认语义名、Profile 与运行时 Spec 数值一致。

当前未引入 `.pability/.peffect/.pabilityset` 三套资产。固定三能力时，反射 Profile 更直接，也避免过早固化
资产 Schema；能力数量和跨项目复用需求增长后，再把同一字段迁移到 AbilitySet 数据资产。

## 暂不实现

预测投射物、预测伤害、GameplayCue、TargetData、Attribute Capture/Aggregator、MMC、Execution Calculation、Fast Array
和 UE GAS 全部复制模式继续留在后续扩展项。
