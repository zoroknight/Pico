# 第 8 月第 3 周：AbilityTask 异步生命周期

## 为什么需要 AbilityTask

Ability 的 `ActivateAbility` 是一次同步函数调用，但实际玩法经常需要“先开始，等某件事发生后再继续”：

- 等待 0.5 秒后冲刺结束；
- 等待命中事件后结算伤害；
- 播放攻击 Montage，等 Notify 生成判定，等动画完成后结束 Ability。

如果每个 Ability 都自行保存计时器、绑定委托和清理动画监听，取消、Actor 销毁和地图切换时很容易留下回调。
AbilityTask 将这些等待过程统一成由 ASC 管理的短生命周期对象。

## Pico 与 UE5 的对应关系

| Pico | UE5 | 保留的学习重点 |
| --- | --- | --- |
| `PAbilityTask` | `UAbilityTask` | 异步节点有明确状态和所属 Ability |
| `PAbilityTaskWaitDelay` | `UAbilityTask_WaitDelay` | 使用 World/ASC Tick，而不是创建线程 |
| `PAbilityTaskWaitGameplayEvent` | `UAbilityTask_WaitGameplayEvent` | GameplayTag 过滤、事件载荷与弱对象委托 |
| `PAbilityTaskPlayAnimationAndWait` | `UAbilityTask_PlayMontageAndWait` | 复用动画系统的 Completed/Interrupted/Cancelled/Notify |
| ASC Task Handle 容器 | Ability Task/GameplayTask 所有权 | 强引用、稳定身份、统一取消和销毁 |

Pico 没有复制完整 GameplayTasks 调度器、资源声明、网络模拟 Task 或蓝图潜在节点。由于 Pico 当前没有
`PStruct/UStruct` 反射，`FGameplayEventData` 是明确类型的 Runtime DTO；反射、PicoGraph 和 Agent 可通过 ASC 的
`SendGameplayEventByTagName(string, float)` PFunction 进入同一事件管线，避免维护另一套事件实现。

## 生命周期与所有权

```text
Created
  -> ReadyForActivation
  -> Active
  -> Finished / Cancelled
  -> Destroyed
```

- Task 只允许由处于 Active 状态的 Ability Spec 创建；
- Task 保存 ASC 弱引用与 Owning Ability SpecHandle，不引用共享 Ability CDO；
- ASC 用稳定 TaskHandle 和 GC 强引用保存活动 Task；
- Task 完成时先进入终态，再广播结果，防止回调中结束 Ability 导致执行中的 Task 被提前销毁；
- 广播结束后进入 ASC 待清理队列，遍历或委托栈退出后统一销毁；
- Ability `Cancel/End/Clear` 会以 `OwnerEnded` 结束所属全部 Task；
- ASC/Actor 销毁、World 替换或引擎退出会销毁 Task，并解除 GameplayEvent 与 Montage 弱对象监听；
- `EndTask`、外部取消、Ability 结束和动画结束均为幂等路径，不会重复广播。

## 三种 Task

### WaitDelay

ASC 的组件 Tick 传入 `DeltaSeconds`，Task 累计世界推进时间。它不读取系统时钟、不创建线程，因此暂停、固定步长
和未来的服务器权威时间都可以沿用同一入口。

### WaitGameplayEvent

事件携带 EventTag、InstigatorHandle、TargetHandle、Magnitude 和 PayloadName。非 Exact 模式下，等待
`Event.Combat.Hit` 可以接收 `Event.Combat.Hit.Critical`；Exact 模式只接受完全相同的 Tag。监听使用
`TObjectMulticastDelegate::AddObject`，Task 销毁后弱绑定自动失效。

### PlayAnimationAndWait

Task 调用现有 `PAnimInstance::PlayMontage`，监听 `OnMontageNotify` 和 `OnMontageEnded`：

- 正常播放到底：`Completed`；
- 另一个 Montage 替换当前 Montage：`Interrupted`；
- Task 或 Ability 被取消：`Cancelled`；
- Montage Notify 原样转发名称和 Trigger/Begin/End 类型。

## C++ 使用方式

```cpp
PAbilityTaskWaitDelay* Task = AbilitySystem.CreateWaitDelayTask(AbilityHandle, 1.0f);
if (Task != nullptr)
{
    Task->OnFinish().AddObject(this, &PMyObject::HandleDelayFinished);
    Task->ReadyForActivation();
}
```

创建、绑定输出、调用 `ReadyForActivation` 的顺序与 UE AbilityTask 的使用习惯一致。不要长期保存 Task 裸指针；
跨帧识别使用 TaskHandle，生命周期由 ASC 管理。

## PicoInspector 可视化验收

启动：

```powershell
.\BuildCodex\Debug\PicoInspector.exe
```

进入 `Experiments -> GAS Lab`：

1. 点击 `Grant Ability`，选中该 Ability，再点击 `Activate`。
   Task 只能属于活动 Ability，这一步建立合法 Owner SpecHandle。
2. 保持 `Delay Seconds=2`，点击 `Create Delay`。
   Active Tasks 出现 WaitDelay，状态为 Active，Waiting For 显示约 2 秒。
3. 设置 `Task Advance Seconds=1` 并点击 `Advance Tasks`。
   Remaining 降为约 1 秒；再次推进后 Task 消失，日志只出现一次 Completed。
4. 点击 `Wait Event`。
   表格出现等待 `Event.Inspector.Action` 的 Task。
5. 设置 Event Magnitude 后点击 `Send Event`。
   实际发送子 Tag `Event.Inspector.Action.Triggered`；Task 因父级匹配完成，日志显示 Tag 和 Magnitude。
6. 点击 `Play Animation Task`，设置推进时间为 0.3 秒并推进。
   日志出现 `Animation Notify: ActionPoint`，Task 仍然活动。
7. 再推进 0.8 秒。
   Montage 正常结束，日志出现 `Animation Task ended: Completed`，Task 被清理。
8. 再创建一个 Animation Task，点击 `Interrupt Animation`。
   新 Montage 替换旧 Montage，日志出现 `Interrupted`。
9. 创建 Delay 或 Animation Task，选中后点击 `Cancel Selected Task`。
   Task 进入 Cancelled 并从表格移除。
10. 创建多个 Task 后点击 Ability 的 `Cancel`。
    所有属于该 Ability Handle 的 Task 同时以 OwnerEnded/Cancelled 清理，Active Tasks 归零。

## 自动化验收

`PicoGameplayAbilitiesTests` 覆盖 Delay 单次完成、GC 强引用、GameplayEvent 父级/精确匹配、事件载荷、事件回调
重入结束 Ability、Montage Notify、Completed、Interrupted、Ability 结束取消和 Task 销毁。

