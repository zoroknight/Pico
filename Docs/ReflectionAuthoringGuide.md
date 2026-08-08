# Pico 反射类型编写指南

这份指南只回答一个问题：如何新增一个能够被 Pico 创建、观察、编辑、调用和保存的类型。

完整示例位于：

```text
Source/Samples/Reflection/Public/Pico/Samples/DemoCharacter.h
Source/Samples/Reflection/Private/DemoCharacter.cpp
Source/Programs/ReflectionDemo/Private/Main.cpp
```

## 1. 声明反射类

包含 `ReflectionMacros.h`，并在 `PObject`派生类中使用 `PICO_DECLARE_CLASS`：

```cpp
#include "Pico/Object/ReflectionMacros.h"

class PDemoCharacter final : public PObject
{
    PICO_DECLARE_CLASS(PDemoCharacter, PObject)

public:
    int32 GetHealth() const;
    int32 ApplyDamage(int32 Damage);

private:
    int32 Health = 100;
    float MoveSpeed = 600.0f;
    bool bAlive = true;
};
```

该宏声明 `ThisClass`、`Super`、`StaticClass`、`RegisterClass`、动态构造入口和属性注册入口。宏结尾会将访问级别切换为 `private`，所以后续成员应显式写出 `public`、`protected`或 `private`。

这些成员此时仍然只有 C++ 含义，只有加入属性或函数批次的成员才会反射。

## 2. 定义类元数据和构造入口

在 CPP 中使用：

```cpp
PICO_DEFINE_CLASS(PDemoCharacter)
```

宏会从 C++ 类型本身推导并连接：

```text
PDemoCharacter
→ "PDemoCharacter"
→ PObject::StaticClass()
→ sizeof(PDemoCharacter)
→ PDemoCharacter::ConstructInstance
→ PDemoCharacter 原生类型令牌
```

因此重命名或复制类时，不需要在多个手写位置同步类型名、父类、大小和构造入口。

没有属性的类型使用：

```cpp
PICO_DEFINE_CLASS_NO_PROPERTIES(PEmptyObject)
```

宏只生成当前手写反射流程的 C++，没有全局静态自动注册，也没有额外运行时开销。

## 3. 注册属性元数据

实现宏声明的 `RegisterProperties`：

```cpp
bool PDemoCharacter::RegisterProperties(PClass& Class)
{
    std::vector<PProperty> Properties;
    PICO_ADD_PROPERTY(Properties, Health);
    PICO_ADD_PROPERTY(Properties, MoveSpeed);
    PICO_ADD_PROPERTY(Properties, bAlive);
    if (!Class.AddProperties(std::move(Properties)))
    {
        return false;
    }

    std::vector<PFunction> Functions;
    PICO_ADD_FUNCTION(
        Functions,
        ApplyDamage,
        EFunctionFlags::Callable,
        FName("Damage"));
    return Class.AddFunctions(std::move(Functions));
}
```

`PICO_ADD_PROPERTY`把成员名同时用于 C++ 成员指针和反射名称，等价于：

```cpp
Properties.push_back(
    PProperty::Create<&PDemoCharacter::Health>(FName("Health")));
```

属性仍然组成一个事务批次。任一属性无效时，整个批次不会写入 `PClass`；类注册成功后元数据会被封存。

宏没有改变成员指针访问、原生类型令牌验证、序列化或 Inspector。它只减少重复代码。

`PICO_ADD_FUNCTION` 同样从成员函数指针生成参数、返回值、const 状态和类型安全调用 Thunk。
参数名仍需显式提供；调用方可通过 `PClass::FindFunction` 查找，并交给
`PObject::ProcessEvent` 调用。网络相关 Flags 当前只作为未来 RPC 的声明元数据，本地
`ProcessEvent` 不会发送网络消息。

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
