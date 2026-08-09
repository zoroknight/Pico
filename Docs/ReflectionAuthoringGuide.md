# Pico 反射类型编写指南

这份指南只回答一个问题：如何新增一个能够被 Pico 创建、观察、编辑、调用和保存的类型。

完整示例位于：

```text
Source/Samples/Reflection/Public/Pico/Samples/DemoCharacter.h
Source/Samples/Reflection/Private/DemoCharacter.cpp
Source/Programs/ReflectionDemo/Private/Main.cpp
```

## 1. 声明反射类

包含 `ReflectionMacros.h`，再把同名 `.generated.h` 放在所有普通 include 的最后。使用
`PCLASS()` 标记类，并在类体第一处写 `GENERATED_BODY()`：

```cpp
#include "Pico/Object/ReflectionMacros.h"
#include "Pico/Samples/DemoCharacter.generated.h"

PCLASS()
class PDemoCharacter final : public PObject
{
    GENERATED_BODY()

public:
    int32 GetHealth() const;

    PFUNCTION(Callable)
    int32 ApplyDamage(int32 Damage);

private:
    PPROPERTY()
    int32 Health = 100;
    PPROPERTY()
    float MoveSpeed = 600.0f;
    PPROPERTY()
    bool bAlive = true;
};
```

`PCLASS/PPROPERTY/PFUNCTION` 在 C++ 编译器看来是空注解；构建前，`PicoHeaderTool` 会读取
Token，生成 `Build/Generated` 下的 `.generated.h` 与 `.gen.cpp`。`GENERATED_BODY()` 最终展开为
`ThisClass`、`Super`、`StaticClass`、`RegisterClass`、动态构造入口和元数据注册入口。它会将访问级别
切换为 `private`，所以后续成员应显式写出 `public`、`protected` 或 `private`。

没有 `PPROPERTY` 或 `PFUNCTION` 的普通成员仍然只有 C++ 含义，不会进入反射、序列化或 Inspector。

## 2. 定义类元数据和构造入口

不再需要在 CPP 中手写 `PICO_DEFINE_CLASS` 或 `RegisterProperties`。生成的 `.gen.cpp` 会连接：

```text
PDemoCharacter
→ "PDemoCharacter"
→ PObject::StaticClass()
→ sizeof(PDemoCharacter)
→ PDemoCharacter::ConstructInstance
→ PDemoCharacter 原生类型令牌
```

因此重命名或复制类时，不需要在多个手写位置同步类型名、父类、大小和构造入口。

没有属性或函数的类仍然使用同一套 `PCLASS + GENERATED_BODY` 写法。PHT 只生成 C++ 注册模板，
没有复制第二套运行时反射系统，也没有全局静态自动注册或额外的对象实例开销。

## 3. 注册属性元数据

属性与函数元数据直接写在声明旁：

```cpp
PPROPERTY(Replicated, ReadOnly)
int32 Health = 100;

PFUNCTION(Pure)
int32 GetHealth() const;
```

第一版支持的属性标记为：`Transient`、`ReadOnly`、`Replicated`、`NotEditable`、
`NotSerializable`。空 `PPROPERTY()` 默认为 `Editable | Serializable`。

对象引用使用代次安全包装器。当前稳定对象引用序列化尚未接入，因此必须标记为临时且不可序列化：

```cpp
PPROPERTY(Transient, NotSerializable)
TObjectPtr<PTarget> StrongTarget;

PPROPERTY(Transient, NotSerializable)
TWeakObjectPtr<PTarget> WeakTarget;
```

强引用属性会被 GC 遍历，弱引用不会阻止目标回收。仅声明 `TObjectPtr` 而不使用 `PPROPERTY`，
也不通过 `AddReferencedObjects` 上报时，GC 无法发现该引用。

第一版支持的函数标记为：`Callable`、`Pure`、`Server`、`Client`、`NetMulticast`、`Reliable`。
空 `PFUNCTION()` 默认为 `Callable`，`Pure` 自动包含 `Callable`。网络标记目前只保存为未来 RPC 使用的
元数据，本地 `ProcessEvent` 不会发送网络消息。

生成代码仍然使用成员指针，例如 `Health` 最终等价于：

```cpp
Properties.push_back(
    PProperty::Create<&PDemoCharacter::Health>(FName("Health")));
```

PHT 从函数声明提取参数名；参数类型、返回值、`const` 状态和调用 Thunk 仍由现有 `PFunction::Create`
在 C++ 编译期校验。属性和函数继续以事务批次写入 `PClass`，因此生成工具没有绕过原有安全检查。

若标记拼错、缺少 `GENERATED_BODY()` 或类没有父类，PHT 会用
`文件(行,列): error PHTxxxx: 原因` 的格式令构建失败。

## 4. 注册类型

对象系统初始化后，将 `PClass`放入 Class Registry：

```cpp
PObjectSystem::Init();
PDemoCharacter::RegisterClass();
```

注册的内容不是实例，而是“如何识别和创建 `PDemoCharacter`”的类型元数据。

当前宏不会自动注册类。Engine 等模块仍应使用显式、父类优先的集中注册函数。未来的 PicoHeaderTool 可以生成这些函数，但底层仍调用同一个 `FClassRegistry`。

## 5. 创建对象

模板形式：

```cpp
PDemoCharacter* Player =
    NewObject<PDemoCharacter>(nullptr, "Player");
```

动态形式：

```cpp
PObject* Player =
    NewObject(PDemoCharacter::StaticClass(), nullptr, "Player");
```

两种形式最终都会经过：

```text
PClass::ConstructObject
→ PObject 构造
→ Object Registry
→ PostInitProperties
```

## 6. 通过反射读写属性

下面的代码没有直接访问 `Player->Health`：

```cpp
const PProperty* HealthProperty =
    Player->GetClass()->FindProperty(FName("Health"));

HealthProperty->SetValue(Player, int32 { 75 });
```

Inspector 的属性控件也走同一条链路。它不知道当前对象是 `PDemoCharacter`，只读取 `PClass`和 `PProperty`。

## 7. 通过 PFunction 调用函数

注册阶段的 `PICO_ADD_FUNCTION` 会生成函数名、参数、返回值、Flags 和类型化调用 Thunk。运行时先从
实际对象的类查找函数，因此也能找到父类声明的函数：

```cpp
const PFunction* Function =
    Player->GetClass()->FindFunction(FName("ApplyDamage"));

const std::array<FFunctionValue, 1> Arguments { int32 { 25 } };
FFunctionValue ReturnValue;
const EFunctionInvokeResult Result =
    Player->ProcessEvent(Function, Arguments, &ReturnValue);

if (Result == EFunctionInvokeResult::Success)
{
    const int32 NewHealth = std::get<int32>(ReturnValue);
}
```

调用链为：

```text
PClass::FindFunction("ApplyDamage")
→ PObject::ProcessEvent
→ 校验目标对象、参数数量、参数类型和对象生命周期
→ PFunction 的类型化 Native Thunk
→ PDemoCharacter::ApplyDamage
```

类型不会隐式转换。例如把 `float { 25.0f }` 传给 `int32 Damage` 会返回
`EFunctionInvokeResult::ArgumentTypeMismatch`，函数不会执行。非 `void` 函数必须提供返回值存储。

`Server`、`Client`、`NetMulticast` 和 `Reliable` 当前只是经过合法性校验的元数据；本地
`ProcessEvent` 不会发送 RPC。

## 8. 保存并重新加载

```cpp
SaveObjectToFile("Player.pobj", Player);
DestroyObject(Player);

PObject* LoadedPlayer =
    LoadObjectFromFile("Player.pobj", nullptr);
```

加载流程为：

```text
读取类名文本
→ Class Registry 找到 PClass
→ NewObject
→ 根据属性名找到 PProperty
→ SetValue 恢复属性
→ PostLoad
```

## 9. 使用 ReflectionDemo

在 Pico 根目录执行：

```powershell
cmake --build Build --config Debug --target PicoReflectionDemo
.\Build\Debug\PicoReflectionDemo.exe
```

程序包含两个彼此独立的验收部分：

1. 反射属性写入 `.pobj`，销毁原对象，再加载并验证 `PostLoad`。
2. 通过 `PFunction` 调用 `ApplyDamage`，触发 Native Delegate，并验证弱对象监听自动失效。

委托的完整编写方式见 [`DelegateAuthoringGuide.md`](DelegateAuthoringGuide.md)。

程序会打印每一步的类元数据、对象身份和属性值。

## 使用 PicoInspector

```powershell
cmake --build Build --config Debug --target PicoInspector
.\Build\Debug\PicoInspector.exe
```

界面中的三个区域分别观察：

```text
Classes：Class Registry 中的 PClass
Objects：Object Registry 中的 PObject
Details：通过 PProperty 读取和修改的实例属性
```

`Create`、`Destroy`、`Save`和 `Load`分别调用 Pico 已有的对象与序列化接口。

## 练习：新增 Mana

按照下面的顺序自行完成一次：

1. 在 `PDemoCharacter`中加入 `int32 Mana = 50`。
2. 在 `RegisterProperties`中使用 `PICO_ADD_PROPERTY`注册 `Mana`。
3. 编译并运行 `PicoReflectionDemo`。
4. 确认 `DumpClass`和 `DumpObject`中出现 `Mana`。
5. 打开 Inspector，将 `Mana`修改为其他值。
6. 保存对象。
7. 销毁对象后重新加载。
8. 确认 `Mana`保持修改后的值。

完成这个练习，就亲手走过了“C++ 成员变量变成运行时可观察、可编辑、可持久化属性”的完整链路。

## 动态多播委托

动态多播监听函数必须进入反射系统，并使用 `Callable`：

```cpp
PFUNCTION(Callable)
void HandleHealthChanged(int32 OldHealth, int32 NewHealth);
```

声明、绑定和广播：

```cpp
TDynamicMulticastDelegate<void(int32, int32)> OnHealthChanged;

FDynamicDelegateBindingResult Binding = OnHealthChanged.AddDynamic(
    Observer,
    FName("HandleHealthChanged"));

FDynamicDelegateBroadcastReport Report =
    OnHealthChanged.Broadcast(100, 75);
```

绑定时会校验函数存在、`Callable`、参数数量、参数类型和 `void` 返回。绑定保存弱对象Handle与函数名，
广播时经 `FindFunction` 和 `ProcessEvent` 调用。监听对象被显式销毁或GC回收后，下一次广播会跳过并
清理失效绑定。详细契约见
[`Month03_19_DynamicMulticastDelegates.md`](Month03_19_DynamicMulticastDelegates.md)。

需要随 `.pworld` 保存的动态委托必须声明为反射属性：

```cpp
PPROPERTY(NotEditable)
TDynamicMulticastDelegate<void(int32, int32)> OnHealthChanged;
```

PHT会将它注册为 `DynamicMulticastDelegate` 属性。磁盘保存目标SceneId/ObjectPath和函数名，不保存
`FObjectHandle`。目标函数仍必须是 `PFUNCTION(Callable)`。

通过 `PProperty::SetValue` 修改普通反射属性时，会自动执行Pre/Post虚函数并广播
`OnPropertyChanging/OnPropertyChanged`。不要再手动调用 `PostEditChangeProperty`，否则监听者会收到两次。
需要复制构造模板且不产生编辑事件时使用 `SetValueSilently`；它只供构造和恢复基础设施使用，不应作为
普通玩法代码的默认写入方式。
