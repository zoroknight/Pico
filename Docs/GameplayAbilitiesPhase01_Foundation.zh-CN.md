# 第 8 月第 1 周：Mini GAS 基础

## 目标与范围

本周建立的是 UE GAS 核心对象关系的 Pico 简化版，而不是一次复制完整 GAS。实现范围包括：

- 层级 `FGameplayTag`、确定性 `FGameplayTagContainer` 和注册管理器；
- 区分 BaseValue 与 CurrentValue 的 `FGameplayAttributeData`；
- 提供 Health、MaxHealth、Mana、MoveSpeed 的 `PAttributeSet`；
- `PGameplayAbility` CDO、`FGameplayAbilitySpec` 和稳定 SpecHandle；
- 明确 OwnerActor、AvatarActor、AttributeSet 和 AbilitySpec 所有权的 ASC；
- 授予、激活、提交、取消、结束和移除的最小生命周期；
- 反射、序列化、GC、委托、自动化测试和 PicoInspector GAS Lab。

GameplayEffect、Cost/Cooldown 的 Effect 化、持续时间、叠加和周期执行属于第 2 周；AbilityTask 属于第 3 周；
网络预测属于第 4 周。

## 模块边界

新增独立 Runtime 模块：

```text
PicoGameplayAbilities
  -> PicoEngine
  -> PicoObject
  -> PicoCore
```

该模块不依赖 Editor、Inspector、Agent 或模型 Provider。PicoInspector 只是它的一个外层调用者，后续项目代码、
PicoGraph 和 AI Tool 也必须调用同一套 Runtime API。

## 与 UE GAS 的对应关系

| Pico | UE5 | 保留的学习重点 |
| --- | --- | --- |
| `FGameplayTag` / Container / Manager | 同名类型与 `UGameplayTagsManager` | 层级匹配、精确匹配、确定性注册 |
| `PAttributeSet` | `UAttributeSet` | Base/Current 分离和统一变更入口 |
| `PGameplayAbilitySystemComponent` | `UAbilitySystemComponent` | Owner/Avatar、能力容器、Tag 与属性入口 |
| `PGameplayAbility` CDO | `UGameplayAbility` CDO | 类默认配置不保存角色运行状态 |
| `FGameplayAbilitySpec` / Handle | UE 同名类型 | 每个 ASC 的等级、输入和激活状态 |

Pico 没有照搬 UE 的 Fast Array、Aggregator、InstancingPolicy 或完整 GameplayTask 框架。

## 关键数据流

### Ability CDO 与 Spec

`PGameplayAbility` 的 CDO 保存 DefaultCost、DefaultCooldown、可取消标记和 Tag 配置。授予 Ability 时，ASC 不会
修改 CDO，而是创建一份 Spec：

```text
Ability Class/CDO: 所有角色共享的规则与默认值
Ability Spec:      某个 ASC 的 Handle、Level、InputId、Active 状态
```

因此同一个 Ability Class 可以授予多个角色，也可以在同一个角色上产生多个稳定 Handle，互不覆盖运行状态。

### OwnerActor、AvatarActor 与 GC

- OwnerActor 表示拥有能力和属性的逻辑对象；
- AvatarActor 表示当前在世界里实际执行动作的对象；
- ASC 使用弱反射引用保存 Owner/Avatar，避免反向引用延长 Actor 生命周期；
- ASC 使用强反射引用保存 AttributeSet，使已存活 ASC 的属性不会被 GC；
- Actor 通过组件列表持有 ASC；Owner 失去 Root 后，Actor、ASC 和 AttributeSet 会作为不可达图一起回收；
- ASC 销毁时结束 Ability、清理 Tag 和委托，不把运行态写回 CDO。

由于 Pico 当前的显式销毁规则不允许直接销毁仍有子对象的对象，AttributeSet 首版使用独立 Outer 加 ASC 强引用，
而不是机械复制 UE 的 ASC Outer。这样既保留 GC 所有权语义，也能正确支持组件单独移除。

### Attribute 修改

属性只能通过 `SetBaseValue`、`SetCurrentValue` 或 `ModifyCurrentValue` 修改。广播数据包含：

- Attribute；
- 修改前 Base/Current；
- 修改后 Base/Current；
- Source 名称。

修改 BaseValue 时会保留已有 CurrentValue 偏移。例如 Health Base=100、Current=75 时，把 Base 改为 120，
Current 会变为 95，而不是自动回满。这对应 UE 中基础值与临时修饰结果分离的核心思想。

## PicoInspector 可视化验收

启动：

```powershell
.\BuildCodex\Debug\PicoInspector.exe
```

进入 `Experiments -> GAS Lab`：

1. 确认 OwnerActor 与 AvatarActor 都存在，Health=100/100、Mana=100、MoveSpeed=600。
2. 点击 `Grant Ability`。列表新增稳定 Handle，状态为 Inactive；这验证 CDO 被转换成角色自己的 Spec。
3. 点击 `Activate`。状态变为 Active，Owned Tags 出现 `Ability.Active.Inspector`；这验证激活和 Tag 计数。
4. 点击 `Cancel`。日志依次出现 Cancelled 和 Ended，状态回到 Inactive，Owned Tags 清空。
5. 再次激活后直接点击 `Remove`。Ability 被强制结束并移除，不留下激活 Tag。
6. 点击 `Apply Damage`。Health 下降，日志显示旧值、新值和 `Inspector.Damage` 来源。
7. 点击 `Reset Lab`。旧 Owner 对象树被销毁并创建新夹具，属性、Spec 和日志恢复初始状态。

## 自动化验收

```powershell
.\BuildCodex\Debug\PicoGameplayAbilitiesTests.exe
.\BuildCodex\Release\PicoGameplayAbilitiesTests.exe
```

测试覆盖 Tag 父级关系与确定性文本、属性变更与序列化、反射元数据、CDO/Spec 分离、稳定 Handle、Ability
生命周期、MaxHealth 联动通知、激活 Tag 清理，以及 Owner/ASC/AttributeSet 的 GC 图。Debug 和 Release
均通过 36/36 项断言。
