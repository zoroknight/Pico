# Month 03.19: Dynamic Multicast Delegates

## 目标

阶段 G 将弱对象引用、`PFunction` 和 `ProcessEvent` 组合成反射驱动的动态多播委托：

```text
Dynamic binding
  = generation-safe weak object handle
  + reflected function name

Broadcast
  -> resolve target
  -> FindFunction
  -> validate event signature
  -> ProcessEvent
  -> compact expired bindings
```

第一版只实现 Dynamic Multicast。Dynamic Single-cast、稳定对象身份序列化和 `.pworld` 加载后的绑定
修复不属于阶段 G，安排在阶段 H。

## 类型化声明

```cpp
TDynamicMulticastDelegate<void(int32, int32)> OnHealthChanged;
```

多播事件必须返回 `void`。模板参数生成运行时 `FFunctionValueDescriptor`，因此声明仍有C++类型约束，
绑定和广播同时拥有反射元数据。

对象参数的Class通过延迟解析器获得。注册 `PFUNCTION void OnTarget(ThisClass*)` 时不会在当前
`StaticClass -> RegisterProperties` 调用栈中再次进入同一个 `StaticClass`；绑定或调用阶段才把原生类型
解析为 `PClass`。这使自引用对象参数不会触发静态初始化递归。

目标函数必须是 `PFUNCTION(Callable)`，而且参数数量、参数类型、对象参数类和 `void` 返回必须完全匹配：

```cpp
PFUNCTION(Callable)
void HandleHealthChanged(int32 OldHealth, int32 NewHealth);

const FDynamicDelegateBindingResult Result = OnHealthChanged.AddDynamic(
    Observer,
    FName("HandleHealthChanged"));
```

绑定不保存成员函数地址。广播时重新从目标的 `PClass` 查找函数，并通过 `PObject::ProcessEvent` 调用。

## 绑定操作

- `AddDynamic`：允许同一“对象 + 函数名”重复绑定，与Native Multicast的普通Add一致。
- `AddUniqueDynamic`：若相同绑定已存在，返回 `AlreadyBound`。
- `Remove(FDelegateHandle)`：精确移除一次绑定。
- `RemoveAll(PObject*)`：移除目标对象的全部动态绑定。
- `Clear`：清空委托实例中的所有绑定。
- `CompactInvalidBindings`：主动压缩目标已经失效的弱绑定；普通Broadcast也会自动完成。

绑定失败使用 `EDynamicDelegateBindResult` 区分空目标、函数不存在、缺少Callable、签名不匹配和重复绑定，
调用者不需要从一个模糊的 `false` 推断原因。

## 广播语义

```cpp
const FDynamicDelegateBroadcastReport Report =
    OnHealthChanged.Broadcast(OldHealth, NewHealth);
```

广播先复制当前 `FDelegateHandle` 快照：

- 广播期间新增的绑定从下一次广播开始生效。
- 广播期间移除且尚未执行的绑定会被当前广播跳过。
- 目标已显式销毁或被GC回收时，弱Handle解析失败，绑定被自动移除。
- 单个 `ProcessEvent` 返回失败只计入Report，不阻断后续监听者。
- 错误的运行时参数帧在调用任何监听者前整体拒绝，保证零副作用。

`FDynamicDelegateBroadcastReport` 记录快照数量、成功调用数、失败数、清理的失效绑定数和最后一次
`ProcessEvent` 失败原因。

## 与GC和序列化的边界

动态绑定中的 `TWeakObjectPtr<PObject>` 不参与GC Mark，Delegate不会仅因为监听关系而保活目标。
当前 `FObjectHandle` 只在本次进程中代数安全，不能写入磁盘。阶段 H 持久化时必须保存SceneId或
ObjectPath与函数名，并在对象图重建后执行引用Fixup。

## PHT修复

阶段G首次使用“只有 `PFUNCTION`、没有 `PPROPERTY`”的监听类，发现旧生成器会调用
`AddProperties(empty)` 并提前返回。PicoHeaderTool现在只为非空集合生成Add调用；新增Function-only
Fixture防止该问题回归。

## 验收

```powershell
cmake --build Build --config Debug --target PicoDynamicDelegateTests PicoInspector
ctest --test-dir Build -C Debug -R "PicoHeaderToolTests|PicoDynamicDelegateTests" --output-on-failure
.\Build\Debug\PicoInspector.exe
```

`PicoDynamicDelegateTests` 覆盖数值与对象参数签名校验、Callable校验、重复策略、Handle移除、
RemoveAll、错误广播参数、广播期间增删、异常隔离、显式销毁、Slot复用和GC回收。Inspector的
`Dynamic Multicast` 实验用于观察动态配置、`ProcessEvent` 调用次数和弱绑定失效清理。
