# Pico 反射类型编写指南

这份指南只回答一个问题：如何新增一个能够被 Pico 创建、观察、编辑和保存的类型。

完整示例位于：

```text
Source/Samples/Reflection/Public/Pico/Samples/DemoCharacter.h
Source/Samples/Reflection/Private/DemoCharacter.cpp
Source/Programs/ReflectionDemo/Private/Main.cpp
```

## 1. 定义普通 C++ 类

`PDemoCharacter`首先是一个普通的 `PObject`派生类：

```cpp
class PDemoCharacter final : public PObject
{
public:
    static const PClass* StaticClass();
    static bool RegisterClass();

private:
    int32 Health = 100;
    float MoveSpeed = 600.0f;
    bool bAlive = true;
};
```

这三个成员变量此时只有 C++ 含义。反射系统还不知道它们存在。

## 2. 创建 PClass 元数据

`StaticClass`返回描述 `PDemoCharacter`的唯一 `PClass`：

```cpp
const PClass* PDemoCharacter::StaticClass()
{
    static PClass Class = PClass::Create<PDemoCharacter>(
        FName("PDemoCharacter"),
        PObject::StaticClass(),
        sizeof(PDemoCharacter),
        &PDemoCharacter::ConstructInstance);
    return &Class;
}
```

这里依次连接：

```text
类型名称
父类
C++ 对象大小
构造函数入口
原生 C++ 类型令牌
```

## 3. 连接对象构造

`PClass`不直接知道具体 C++ 类型，因此通过函数指针创建实例：

```cpp
FObjectPtr PDemoCharacter::ConstructInstance(const FObjectConstructionParams& Params)
{
    return FObjectPtr(new PDemoCharacter(Params));
}
```

随后 `NewObject`可以只拿着 `PClass*`动态创建 `PDemoCharacter`。

## 4. 注册属性元数据

每个 `PProperty`保存属性名称、类型、大小和由成员指针生成的类型安全访问函数：

```cpp
PProperty::Create<&PDemoCharacter::Health>(FName("Health"));
```

同一个类的属性应当组成批次后一次提交：

```cpp
std::vector<PProperty> Properties;
Properties.push_back(
    PProperty::Create<&PDemoCharacter::Health>(FName("Health")));
Properties.push_back(
    PProperty::Create<&PDemoCharacter::MoveSpeed>(FName("MoveSpeed")));
Class.AddProperties(std::move(Properties));
```

如果批次中任一属性无效，整个批次都不会写入 `PClass`。类注册成功后元数据会被封存，不能在运行过程中继续增加属性。

成员指针方式不依赖多态 C++ 对象的内存偏移。访问函数仍然向序列化和 Inspector 提供通用地址，但会先检查对象类型和属性类型。

当前阶段必须显式注册属性。它对应 UE 生成代码最终建立元数据的结果，但 Pico 暂时没有 UHT 和 `UPROPERTY`宏。

## 5. 注册类型

对象系统初始化后，将 `PClass`放入 Class Registry：

```cpp
PObjectSystem::Init();
PDemoCharacter::RegisterClass();
```

注册的内容不是实例，而是“如何识别和创建 `PDemoCharacter`”的类型元数据。

## 6. 创建对象

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

## 7. 通过反射读写属性

下面的代码没有直接访问 `Player->Health`：

```cpp
const PProperty* HealthProperty =
    Player->GetClass()->FindProperty(FName("Health"));

HealthProperty->SetValue(Player, int32 { 75 });
```

Inspector 的属性控件也走同一条链路。它不知道当前对象是 `PDemoCharacter`，只读取 `PClass`和 `PProperty`。

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

## 使用 ReflectionDemo

在 Pico 根目录执行：

```powershell
cmake --build Build --config Debug --target PicoReflectionDemo
.\Build\Debug\PicoReflectionDemo.exe
```

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
2. 在 `AddProperties`中注册 `Mana`。
3. 编译并运行 `PicoReflectionDemo`。
4. 确认 `DumpClass`和 `DumpObject`中出现 `Mana`。
5. 打开 Inspector，将 `Mana`修改为其他值。
6. 保存对象。
7. 销毁对象后重新加载。
8. 确认 `Mana`保持修改后的值。

完成这个练习，就亲手走过了“C++ 成员变量变成运行时可观察、可编辑、可持久化属性”的完整链路。
