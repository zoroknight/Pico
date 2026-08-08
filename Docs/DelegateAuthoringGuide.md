# Pico Native 委托编写指南

这份指南说明如何在 Pico 游戏代码中声明、监听和广播 Native Delegate。完整可运行示例位于：

```text
Source/Samples/Reflection/Public/Pico/Samples/DemoCharacter.h
Source/Samples/Reflection/Private/DemoCharacter.cpp
Source/Programs/ReflectionDemo/Private/Main.cpp
```

## 1. 委托解决什么问题

没有委托时，角色通常需要直接持有 UI、任务或音效对象并调用它们：

```cpp
HealthWidget->Refresh(Health);
Quest->OnPlayerDamaged();
```

这会让角色依赖所有接收者。委托把关系反过来：角色只声明“Health 已变化”，感兴趣的对象自行监听。

```text
Character changes Health
  -> OnHealthChanged.Broadcast
    -> UI listener
    -> Quest listener
    -> Animation listener
```

广播者不需要知道有多少监听者，也不依赖它们的具体类型。

## 2. 单播与多播

单播委托只能保存一个回调，可以有返回值：

```cpp
TDelegate<int32(int32)> ComputeDamage;
ComputeDamage.BindLambda([](int32 BaseDamage) { return BaseDamage * 2; });

const std::optional<int32> Result = ComputeDamage.ExecuteIfBound(10);
```

多播委托可以保存多个监听者，只允许 `void` 返回值：

```cpp
TMulticastDelegate<void(int32, int32)> OnHealthChanged;
OnHealthChanged.AddLambda([](int32 OldHealth, int32 NewHealth) { /* ... */ });
OnHealthChanged.Broadcast(100, 75);
```

多播没有统一的返回值合并规则，因此每个监听函数都必须返回 `void`。

## 3. 在 PObject 类中声明事件

需要监听 `PObject` 成员函数时，使用支持弱对象绑定的 `TObjectMulticastDelegate`：

```cpp
#include "Pico/Object/ObjectDelegate.h"

class PDemoCharacter final : public PObject
{
public:
    int32 ApplyDamage(int32 Damage);
    TObjectMulticastDelegate<void(int32, int32)> OnHealthChanged;

private:
    int32 Health = 100;
};
```

模板签名 `void(int32, int32)` 表示所有监听函数都必须接收 OldHealth 和 NewHealth。

## 4. 修改状态后广播

```cpp
int32 PDemoCharacter::ApplyDamage(int32 Damage)
{
    const int32 OldHealth = Health;
    Health = std::max(0, Health - std::max(0, Damage));

    if (Health != OldHealth)
    {
        OnHealthChanged.Broadcast(OldHealth, Health);
    }
    return Health;
}
```

应先把对象状态修改到一致状态，再广播事件。监听者执行时看到的 `GetHealth()` 应当已经是 NewHealth，
核心状态也不能依赖监听者的执行顺序或返回结果。

## 5. 绑定 Lambda

```cpp
const FDelegateHandle Handle = Character->OnHealthChanged.AddLambda(
    [](int32 OldHealth, int32 NewHealth)
    {
        std::cout << "Health " << OldHealth << " -> " << NewHealth << '\n';
    });
```

`AddLambda` 返回唯一的 `FDelegateHandle`。保存它，之后可以精确解绑：

```cpp
Character->OnHealthChanged.Remove(Handle);
```

捕获引用的 Lambda 不会自动感知被捕获对象是否已销毁。它适合生命周期明确的局部代码；不要用它捕获寿命不确定的裸 `PObject*`。

## 6. 弱绑定 PObject 监听者

监听函数签名必须完全匹配：

```cpp
class PDemoHealthObserver final : public PObject
{
public:
    void HandleHealthChanged(int32 OldHealth, int32 NewHealth);
};

const FDelegateHandle Handle = Character->OnHealthChanged.AddObject(
    Observer,
    &PDemoHealthObserver::HandleHealthChanged);
```

`AddObject` 保存对象的带代数 Handle，而不是长期捕获裸指针。Observer 销毁后：

```text
Broadcast
  -> ResolveObject(saved handle)
  -> object no longer exists
  -> skip and remove stale binding
```

对象槽位以后被复用，也不会让旧绑定错误地调用新对象。

可以按 Handle 删除一个绑定，或删除某个对象的全部绑定：

```cpp
Character->OnHealthChanged.Remove(Handle);
Character->OnHealthChanged.RemoveAll(Observer);
Character->OnHealthChanged.Clear();
```

## 7. 广播期间修改监听列表

Pico 当前的确定性规则是：

- 按加入顺序调用。
- 广播开始时记录监听 Handle 快照。
- 在轮到某监听者前将其移除，本次广播会跳过它。
- 广播过程中新增的监听者从下一次广播开始生效。
- 允许监听者移除自己、Clear 或触发嵌套广播。
- 委托默认只用于游戏线程，不提供跨线程同步。

## 8. Native Delegate 与 PFunction

Native Delegate 直接保存类型安全的 C++ Thunk，不依赖 `PFunction`。示例中的链路是：

```text
ProcessEvent finds reflected ApplyDamage
  -> PFunction native thunk calls ApplyDamage
    -> ApplyDamage broadcasts Native OnHealthChanged
```

两套系统在这里协作，但 Native Delegate 自己不通过函数名调用。未来 Dynamic Multicast Delegate
才会保存“对象稳定引用 + 函数名”，通过 `FindFunction` 和 `ProcessEvent` 广播。

## 9. 运行示例

```powershell
cmake --build Build --config Debug --target PicoReflectionDemo
.\Build\Debug\PicoReflectionDemo.exe
```

Demo 第一部分单独验证反射属性的保存、销毁、加载与 `PostLoad`；第二部分验证 PFunction 调用、
Lambda 监听、弱对象监听、参数错误拒绝，以及监听对象销毁后的自动失效。

看到以下最终输出即表示两部分均通过：

```text
[Serialization 7] Round trip succeeded
[Events 6] PFunction + Delegate demo succeeded
[Result] All reflection demonstrations succeeded
```

## 10. 当前限制

当前实现是 Native Delegate，不会序列化绑定，也不能在编辑器中按函数名配置监听者。不要把
`FObjectHandle` 写入 `.pworld` 作为持久引用。动态多播、稳定引用修复和编辑器配置将在
PicoHeaderTool 与 GC 之后实现。
