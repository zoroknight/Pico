# PicoInspector 可视化验收指南

这份文档用于手动检验 Pico 当前的 `PFunction`、`ProcessEvent`、Native Delegate、弱对象绑定和
反射属性序列化。它不仅说明按钮怎么操作，也解释每一步在验证哪条引擎契约。

PicoInspector 的定位是 Developer Sandbox：不创建 `.pico` 项目、不等待完整编辑器，通过最小
Fixture 快速观察 Runtime 系统。可视化验收不能替代自动化测试，但能帮助理解调用链和生命周期。

## 1. 启动

在 Pico 根目录运行：

```powershell
cmake --build Build --config Debug --target PicoInspector
.\Build\Debug\PicoInspector.exe
```

默认 UI Scale 为 `1.4`，直接启动 Debug 或 Release `.exe` 都会使用较大的字体和控件，也可以覆盖：

```powershell
.\Build\Debug\PicoInspector.exe -uiscale=1.4
```

程序默认进入 `Experiments -> Native Delegates`，并创建：

```text
PDemoCharacter       __InspectorDelegateCharacter
PDemoHealthObserver  __InspectorHealthObserver
```

## 2. 被验证的完整链路

```text
Inspector 参数控件
 -> PClass::FindFunction("ApplyDamage")
 -> PObject::ProcessEvent
 -> 校验目标、参数和返回存储
 -> PFunction Native Thunk
 -> PDemoCharacter::ApplyDamage
 -> 先修改 Health
 -> OnHealthChanged.Broadcast
 -> Lambda Listener + Weak PObject Listener
```

PFunction 与 Delegate 是独立系统：PFunction 负责运行时发现、校验和调用函数；Delegate 负责让状态
发布者通知零个或多个监听者。`ApplyDamage` 把两者串成一条可观察链路。

## 3. 初始 Fixture

启动后应看到：

```text
Health: 100
Observer: Alive
Live delegate bindings: 2
Lambda: Active, Calls 0
Weak PObject: Active, Calls 0
```

这一步验证 `SetUp` 能确定性创建 Character、Observer 和两种监听。已知初始状态让后续失败可以定位
到调用、广播或生命周期，而不是被上一次操作残留干扰。`Reset` 应恢复对象、Health、监听、计数和日志。

## 4. PFunction 调用并触发广播

保持 `Damage = 20`，点击 `Invoke ApplyDamage`。

预期：

```text
Health: 80
Lambda Calls: 1
Weak PObject Calls: 1
ProcessEvent ApplyDamage(20): Success, Health=80
```

这一步同时验证：函数能通过名字和元数据找到；`int32` 参数能由 `FFunctionValue` 进入类型化 Thunk；
返回 Health 能写回通用返回容器；函数修改真实状态后，两个监听者按加入顺序收到广播。

如果直接调用 `Character->ApplyDamage(20)`，只能证明普通 C++ 调用有效，不能证明反射调用链有效。

## 5. 错误参数必须零副作用

点击 `Invoke Wrong Float Argument`。

预期日志：

```text
Wrong Float argument rejected: ArgumentTypeMismatch
```

Health 和两个 Calls 都不能变化。函数要求 `Int32 Damage`，调用帧却提供 Float；运行时反射、RPC 和
AI Tool 没有编译器替它们保证参数正确，因此必须在进入 Native 函数前拒绝错误类型。失败调用不能修改
Gameplay 状态，也不能广播事件。

## 6. 手动 Broadcast 隔离验证 Delegate

设置 Old Health 和 New Health，点击 `Broadcast OnHealthChanged`。

两个监听者的 Calls 应增加，但 Character Health 不应变化。手动广播绕过 PFunction 和 ApplyDamage，
用于单独证明 Delegate 本身能够调用监听者。

Delegate 是“通知某件事已经发生”，不是替 Character 管理状态。正确顺序是：

```text
Character 提交新 Health
 -> 对象状态已经一致
 -> Broadcast(OldHealth, NewHealth)
```

核心状态不能依赖是否存在监听者或监听者执行顺序。

## 7. 三种解除方式

### Remove Lambda

按 Lambda 的 `FDelegateHandle` 精确移除一个绑定，Weak PObject 仍应继续收到广播。它验证每次 Add
产生稳定、独立的绑定身份。

### Remove Object Listeners

调用语义相当于：

```cpp
Character->OnHealthChanged.RemoveAll(Observer);
```

Observer 仍然存活，只是不再关注这个事件；Lambda 不受影响。它验证按监听对象所有权批量解绑。

### Clear All

清除这个 Delegate 的所有 Lambda 和 Object 绑定，但不销毁 Character 或 Observer。之后
`ApplyDamage` 仍必须修改 Health，证明业务状态不依赖观察者存在。

| 操作 | Character | Observer | Lambda | Object Binding |
| --- | --- | --- | --- | --- |
| Remove Lambda | 存活 | 存活 | 移除 | 保留 |
| Remove Object Listeners | 存活 | 存活 | 保留 | 移除 |
| Clear All | 存活 | 存活 | 移除 | 移除 |

## 8. Destroy Observer 验证弱绑定

Reset 后点击 `Destroy Observer`，然后再次 Invoke 或 Broadcast。

Character 和 Lambda 应继续工作，Weak PObject Calls 不再增加，也不能崩溃。`AddObject` 保存的是带
代数的 `FObjectHandle` 和成员函数，而不是长期使用裸对象地址：

```text
Broadcast
 -> ResolveObject(saved handle)
 -> Observer 已销毁
 -> 跳过并清理失效绑定
```

即使 Registry Slot 被新对象复用，Serial 已改变，旧绑定也不会错误调用新对象。这是忘记主动解绑时
的生命周期安全兜底；主动 `RemoveAll` 与对象销毁导致弱绑定失效是两种不同情况。

## 9. Lambda 与 Weak PObject 的区别

Lambda 保存匿名 C++ 回调及其捕获；Weak PObject 保存对象 Handle 与成员函数。

```cpp
Delegate.AddLambda(
    [Widget](int32 OldHealth, int32 NewHealth)
    {
        Widget->Refresh(NewHealth);
    });
```

这里的 Widget 可以理解为血条等 UI 控件。若 `Widget` 是 `PWidget*`，`[Widget]` 只把地址复制进
Lambda，并没有复制 Widget 对象或跟踪其生命周期：

```text
Lambda 保存地址 0x1234
 -> Widget 对象被销毁
 -> 地址副本仍是 0x1234
 -> 下次 Broadcast 访问已释放对象，产生悬空指针风险
```

即使外部执行 `Widget = nullptr`，也不会改变 Lambda 内保存的地址副本。Delegate 只看到一个通用
回调，不知道 Lambda 捕获的是整数、字符串还是对象指针，因此不能自动检查捕获对象是否仍然存活。

PObject 监听应优先写成：

```cpp
Delegate.AddObject(
    Widget,
    &PWidget::HandleHealthChanged);
```

广播前会通过 Object Registry 解析 Handle。无捕获 Lambda、捕获普通值的短期逻辑和日志仍很适合
`AddLambda`；生命周期不确定的 PObject 更适合 `AddObject`。

| 特性 | Lambda | Weak PObject |
| --- | --- | --- |
| 保存内容 | 匿名回调及捕获 | `FObjectHandle` + 成员函数 |
| 必须是 PObject | 否 | 是 |
| 自动检测对象销毁 | 否 | 是 |
| 支持 `RemoveAll(Object)` | 否 | 是 |
| 支持 Handle 精确移除 | 是 | 是 |
| 适合场景 | 临时计算、日志、生命周期明确的捕获 | Gameplay、组件、UI 等 PObject 监听 |
| 依赖 PFunction | 否 | 否 |
| 当前可序列化 | 否 | 否 |

Demo 的 Lambda 只写 Event Log，并由 Experiment 在退出前 Clear；Weak PObject 则专门演示监听对象
比广播者更早销毁时仍然安全。

## 10. Runtime Browser 验证通用性

Reset 后切到 `Runtime Browser`，选择 `__InspectorDelegateCharacter`，打开 `Functions`，选择
`ApplyDamage`，输入 Damage 并 Invoke。

应显示 `Success` 和 Int32 返回值；切回 Experiments 后，Health、Calls 和 Event Log 应同步变化。
这证明 Functions 面板只依赖通用 `PObject/PClass/PFunction`，没有硬编码调用 Character；两个页面
操作的也是 Object Registry 中同一个真实实例，而不是两份演示状态。

取消 `Provide return storage` 后调用非 Void 函数，应得到 `MissingReturnStorage`，Health 和 Calls
不变。调用契约不完整时必须在执行前失败，不能先产生副作用再丢失返回值。

## 11. 属性保存与加载回归

在 Runtime Browser 选择普通 Player，在 Properties 修改 Health，然后依次 Save、Destroy、Load。
加载对象应恢复 Health，默认文件为：

```text
Saved/Inspector/SelectedObject.pobj
```

这一步证明增加 `PFunction` 和 Native Delegate 没有破坏 `PProperty` 序列化。当前 Delegate 是运行时
状态，不会把 Lambda、成员函数地址、DelegateHandle 或本次运行的 ObjectHandle 写入 `.pobj`。

## 12. Garbage Collection与调度实验

打开 `Experiments -> Garbage Collection`。初始表格包含 Root、Strong target、Weak target、Cycle A
和 Cycle B，只有 Root 显示 Rooted。`Scheduler State` 初始应为 `Idle`，事件时间线记录 Fixture 创建。

### 12.1 请求不会立即回收

点击 `Request Explicit`，预期：

- Scheduler State 变为 `Pending`。
- Pending reasons 显示 `Explicit`。
- 五个对象仍全部为 `Alive`。
- Event Timeline 只记录请求，没有 `Collected` 事件。

继续点击 `Request Time Limit` 和 `Request World Transition`。Pending reasons 应合并显示三个原因，
而不是由后一个覆盖前一个。这一步验证 `RequestGarbageCollection` 只调度工作，不在任意调用位置开始
Stop-the-world。

### 12.2 安全点消费请求

点击 `Run Safe Point`，它模拟 `EngineLoop` 完成 World Tick 后调用
`CollectGarbageIfRequested`。预期：

- Root 与 Strong target 保持存活。
- Weak target 变为 `Collected`。
- 互相强引用但没有 Root 的 Cycle A/B 都变为 `Collected`。
- 统计中 Collected 应为 3；Inspector 的其他实验对象由临时 Root 隔离，不会被这次实验误回收。
- Scheduler State 恢复 `Idle`，Consumed reasons 保留本次合并原因用于检查。
- Event Timeline 先记录安全点执行，再分别记录两个 Survived 和三个 Collected。

点击 `Remove Root`，再执行 `Request Explicit -> Run Safe Point`，Root 与 Strong target 都应被回收。
点击 `Reset` 会清除Pending请求、统计与日志，并重新创建确定性的五对象图。切到 Runtime Browser
选择GC节点时，对象引用属性会显示目标路径及 `Strong/Weak` 类型。

### 12.3 对比立即收集

Reset后不提交任何请求，直接点击 `Collect Now`。它调用 `CollectGarbage`，不经过请求检查：对象结果
与第一次安全点收集相同，但 Trigger 显示 `Collect Now`、Consumed reasons 显示 `None`。这用于区分：

```text
Request GC + Run Safe Point = 调度层决定何时执行
Collect Now                 = 执行层立即开始Mark-Sweep
```

这组操作验证：Root 是遍历起点；反射 `TObjectPtr` 形成强边；`TWeakObjectPtr` 不保持目标存活；
Mark-Sweep 可以回收引用计数无法处理的无 Root 循环；GC请求与真正收集是两个独立阶段。

## 13. 完整通过标准

- 正确的 PFunction 调用返回结果并触发两个监听者。
- Float 参数和缺失返回存储在执行前被拒绝且零副作用。
- 手动 Broadcast 调用监听者但不修改 Character Health。
- Remove、RemoveAll、Clear 的作用范围互不混淆。
- Observer 销毁后弱绑定安全失效，Lambda 继续工作。
- Runtime Browser 和 Experiment 操作同一个对象。
- Reset 恢复确定性初始状态。
- 原有属性保存、销毁和加载仍然工作。
- GC 实验按 Root、强引用、弱引用和无 Root 循环规则回收对象。
- GC请求保持对象Alive，多个请求原因正确合并，并只在Run Safe Point后被消费。
- Collect Now可以和延迟调度形成明确对照，事件时间线与对象Handle状态一致。

当前不测试动态委托配置。下一阶段先实现运行时 Dynamic Multicast Delegate，用“弱对象引用 +
PFunction 名称”广播；稳定身份序列化与加载后的引用修复在随后阶段接入。
