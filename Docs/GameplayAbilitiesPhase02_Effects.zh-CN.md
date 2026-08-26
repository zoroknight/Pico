# 第 8 月第 2 周：GameplayEffect 与组合规则

## 本周完成内容

本周在第 1 周的 ASC、Attribute、Ability 和 GameplayTag 基础上，加入了 Pico 的第一版
GameplayEffect 管线：

- `PGameplayEffect` CDO 保存可复用的默认规则；
- `FGameplayEffectSpec` 保存一次应用所需的来源、等级、时间、Tag 和 Modifier；
- `FActiveGameplayEffect` 保存目标 ASC 上的剩余时间、周期累计、堆叠数和稳定 Handle；
- DurationPolicy 支持 Instant、Duration 和 Infinite；
- Modifier 支持 Add、Multiply 和 Override；
- 支持周期执行、同 Key 堆叠上限、持续时间刷新、Tag 前置/阻止条件与 Granted Tag；
- Cost 和 Cooldown 通过 GameplayEffect 提交，不建立独立的扣费与计时系统；
- Attribute、Tag、Effect Applied/Removed 均有可监听的多播委托。

## 与 UE GAS 的关系

| Pico | UE5 | 学习重点 |
| --- | --- | --- |
| `PGameplayEffect` CDO | `UGameplayEffect` CDO | Effect 是共享规则，不保存某个角色的运行状态 |
| `FGameplayEffectSpec` | `FGameplayEffectSpec` | 应用时从 CDO 产生，可携带来源、等级和本次参数 |
| `FActiveGameplayEffect` | `FActiveGameplayEffect` | 目标上的运行实例，拥有 Handle、剩余时间和 Stack |
| ASC ActiveEffect 容器 | `FActiveGameplayEffectsContainer` | 统一负责应用、Tick、到期与移除 |
| Attribute/Tag/Effect 委托 | GAS 对应变更委托 | UI、任务、动画和网络只观察结果，不侵入 Effect 内核 |

Pico 保留了 UE 的 CDO -> Spec -> ActiveEffect 三层职责和统一提交思路，但没有复制 Aggregator、Attribute
Capture、MMC、Execution Calculation、GameplayCue 或 Fast Array。这些不属于当前小引擎的最低学习闭环。

## 三类 Effect 的运行方式

```text
Instant
  CDO -> Spec -> 立即修改 BaseValue -> 广播 Applied -> 结束

Duration / Infinite（非周期）
  CDO -> Spec -> ActiveEffect -> 修改 CurrentValue/授予 Tag
  -> 到期或显式移除 -> 回滚本 Effect 实际施加的临时增量/移除 Tag

Duration / Infinite（周期）
  CDO -> Spec -> ActiveEffect -> 累计时间
  -> 每到 Period 修改 BaseValue -> 到期或显式移除
```

Damage 和 Cost 属于 Instant；Regen 属于 Duration + Periodic；Stun 属于 Duration + Granted Tag；Cooldown
也是 Duration + Granted Tag。Ability 只询问“Mana 是否足够、Cooldown Tag 是否存在”，真正的扣费和计时仍由
Effect 管线完成。

## 堆叠与生命周期规则

- 相同 `StackingKey` 的持续 Effect 复用同一个 ActiveEffect Handle；
- StackCount 最多增长到 `StackLimitCount`，再次应用仍刷新 Duration；
- 周期 Modifier 每次按当前 StackCount 执行；
- 非周期持续 Modifier 每增加一层立即应用一层，并记录实际增量以便移除时逆向恢复；
- Granted Tag 对一个 ActiveEffect 增加一次计数，在该 ActiveEffect 移除时减少一次；
- Effect 到期、显式移除或 ASC 销毁都经过同一清理入口；
- 周期属性广播中请求移除 Effect 时先进入待移除队列，Tick 遍历结束后执行，避免委托重入令容器失效；
- Tick 中不允许从回调直接应用新的 Effect，首版用明确边界换取可预测生命周期。

## PicoInspector 可视化验收

启动：

```powershell
.\BuildCodex\Debug\PicoInspector.exe
```

进入 `Experiments -> GAS Lab`，按以下顺序操作：

1. 点击 `Apply Damage`。
   Health 立即下降，Active Effects 列表不新增条目；验证 Instant Effect 不保留运行实例。
2. 连续点击三次 `Apply Regeneration`。
   列表始终只有一个 `Inspector.Regeneration`，Stacks 变为 3；验证相同 Key 合并和堆叠上限。
3. 保持 `Advance Seconds=1`，点击 `Advance Effects`。
   Health 增加 15，Remaining 减少 1；验证每层每周期恢复 5 点。
4. 再推进 4 秒。
   Regen 到期并从列表移除，日志出现 Effect removed；验证持续时间和统一清理。
5. 点击 `Apply Stun`。
   Active Effects 出现 `Inspector.Stun`，Owned Tags 出现 `State.Stunned`；验证 Effect 授予 Tag。
6. 推进 3 秒。
   Stun 与 `State.Stunned` 同时消失；验证 Tag 生命周期属于 ActiveEffect。
7. 点击 `Grant Ability` 后点击 `Activate`。
   Mana 从 100 降到 90，列表出现 Cooldown Effect；验证 Cost/Cooldown 复用 Effect 管线。
8. 点击 `Cancel` 后立刻再次 `Activate`。
   激活被拒绝；推进 3 秒后再次激活成功且 Mana 再减少 10，验证取消 Ability 不等于取消 Cooldown。
9. 选中任一 Active Effect 后点击 `Remove Selected`。
   条目消失并产生 Removed 日志；验证稳定 Handle 和显式清理路径。

Event Log 中的 Attribute、Ability、Effect Applied 和 Effect Removed 记录用于确认广播发生的先后关系，
Active Effects 表格用于确认当前真实运行状态，两者应彼此对应。

## 自动化验收

```powershell
.\BuildCodex\Debug\PicoGameplayAbilitiesTests.exe
.\BuildCodex\Release\PicoGameplayAbilitiesTests.exe
```

测试覆盖 Instant、Duration、Infinite、Periodic、Stack、Required/Blocked/Granted Tag、持久 Modifier 回滚、
委托重入移除、非法枚举拒绝、Cost、Cooldown，以及第 1 周全部 Tag/Attribute/Ability/GC 行为。

