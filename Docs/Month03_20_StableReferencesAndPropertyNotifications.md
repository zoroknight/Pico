# 第三个月：稳定引用、动态委托持久化与属性通知

## 目标

阶段 H 把反射、CDO、动态多播委托、`.pworld` 和编辑器事务连接成闭环：场景重新加载后动态绑定仍然有效；
反射属性写入经过统一的前后通知；Undo/Redo 重建 World 时能标明变化来源。

## 三种对象身份

```text
FObjectHandle                当前进程的 Slot + Generation，不得写入磁盘
FSceneObjectId               当前场景快照中的稳定引用，负责同一对象图内的精确修复
ObjectPath                   Outer 层级和对象名，作为可读诊断与 SceneId 失效后的后备查找
```

`.pworld` v4 使用 `FSerializedObjectReference { SceneId, ObjectPath }`。保存动态绑定时记录委托属性名、
目标引用和 `PFunction` 名称，不保存运行时地址或 Handle。

## 两阶段加载

```text
验证纯数据
 -> 创建 World、Level、Actor、Component，建立 SceneId -> PObject*
 -> 应用普通反射属性
 -> 恢复 RootComponent 和 Attachment
 -> 修复动态委托的目标引用并 AddDynamic
 -> PostLoad
```

目标对象不存在、函数被删除或签名已经改变时，该条绑定会被跳过，不会让整个 World 加载失败。委托属性本身
不存在或类型改变属于资产结构错误，加载会失败并销毁已经创建的临时对象图。

## 反射动态委托属性

动态委托现在是 `EPropertyType::DynamicMulticastDelegate`。PHT 可以直接处理：

```cpp
PPROPERTY(NotEditable)
TDynamicMulticastDelegate<void(int32)> OnHealthChanged;
```

`PProperty` 提供类型擦除后的 `GetDynamicMulticastDelegate`，序列化器不需要知道模板参数。构造链不会复制
CDO 中的运行时监听列表；动态绑定由场景加载后的引用修复阶段恢复。

## 属性通知

所有 `PProperty::SetValue` 调用统一执行：

```text
PreEditChange
 -> OnPropertyChanging
 -> 写入成员值
 -> PostEditChangeProperty
 -> OnPropertyChanged
```

`FPropertyChangedEvent` 包含 Object、Property 和来源。当前来源包括 `ValueSet`、`Interactive`、`Load` 和
`UndoRedo`。模板/CDO 构造复制使用 `SetValueSilently`，因此对象初始化不会被误报为用户编辑。

修改 CDO 会产生通知，并改变后续实例复制的默认值；已经存在的实例不会被强制覆盖。这与 Pico 当前简化的
CDO 语义一致。

## 编辑器与剪贴板

编辑器 Details、资产引用替换和 Inspector 都通过 `PProperty::SetValue`，不再手动重复调用
`PostEditChangeProperty`。编辑器恢复事务快照时向 World Loader 传入 `UndoRedo` 来源。

复制多个彼此绑定的 Actor 时，Clipboard 会把绑定目标的 SceneId 重映射到复制后的对象；指向复制范围外
对象的引用继续指向原场景对象。组件 Socket 名也在同一重映射链中保留。

## 可视化验收

打开 `PicoInspector -> Experiments -> Property Notifications`：

1. 选择变化来源并点击 `Set Instance Through PProperty`，日志先出现 Pre、再出现 Post。
2. Pre 行观察旧 Health，Post 行观察新 Health。
3. 点击 `Set CDO Default`，确认事件目标为 CDO。
4. 点击 `Spawn From CDO`，新实例继承 CDO Health，原实例保持自己的值。
5. Reset 恢复原始 CDO，避免实验污染其他 Fixture。

动态委托落盘由 Engine 测试使用真实 World 对象图验证，包括新 Handle 修复、失效目标、缺失函数和 v1
旧场景兼容。

## 自动化验收

```powershell
cmake --build Build --config Debug
ctest --test-dir Build -C Debug --output-on-failure
cmake --build Build --config Release
ctest --test-dir Build -C Release --output-on-failure
```
