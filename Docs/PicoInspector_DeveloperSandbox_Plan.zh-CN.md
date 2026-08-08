# PicoInspector Developer Sandbox 计划

## 定位

PicoInspector 是 Pico 的轻量开发验证工具，而不只是属性查看器：

> 不创建完整游戏项目、不等待 PicoEditor 完成，通过最小测试对象快速操作、观察和验证正在开发的引擎系统。

三类验证工具职责保持分离：

```text
自动化测试       验证结果、边界和回归
PicoInspector    手动操作并观察系统行为
PicoEditor       编辑真实 World、资产和游戏项目
```

PicoInspector 不承担场景生产、资产工作流、Undo/Redo、完整项目运行和游戏编辑器职责。

## 总体结构

```text
PicoInspector
├─ Runtime Browser
│  ├─ Classes
│  ├─ Objects
│  ├─ Properties
│  └─ Functions
├─ Experiments
│  ├─ Reflection
│  ├─ Native Delegates
│  └─ Future subsystem fixtures
└─ Diagnostics
   ├─ Operation Result
   ├─ Event Log
   ├─ Object Count
   └─ Errors
```

Runtime Browser 读取真实 `PClass/PProperty/PFunction/FObjectRegistry` 数据；Experiments 使用最小
Fixture 可视化尚未具备通用反射入口的系统。

## 轻量实验接口

首期引入 Developer-only 接口，避免把每个实验继续堆进 `FInspectorApp`：

```cpp
class IInspectorExperiment
{
public:
    virtual ~IInspectorExperiment() = default;
    virtual const char* GetName() const = 0;
    virtual bool SetUp() = 0;
    virtual void Draw() = 0;
    virtual void Reset() = 0;
    virtual void TearDown() = 0;
};
```

实验负责自身 Fixture、临时对象和日志；Inspector 负责注册、选择、绘制、Reset 和退出清理。
接口只存在于 Developer/Inspector 层，Runtime 模块不能反向依赖它。

## 第一阶段：PFunction 与 Native Delegate

状态：已完成。

手动操作步骤和每一步对应的架构目的见
[`PicoInspector_VisualVerificationGuide.zh-CN.md`](PicoInspector_VisualVerificationGuide.zh-CN.md)。

### 通用 Functions 面板

- 显示选中对象的本类和继承 `PFunction`。
- 显示函数名、声明类、参数、返回值和 Flags。
- 根据 `EFunctionValueType` 生成参数控件。
- Object 参数只能选择活动且类型兼容的对象，也允许显式传入 null。
- Invoke 必须调用真实 `PObject::ProcessEvent`。
- 显示返回值和完整 `EFunctionInvokeResult`。
- 提供 Reset Parameters，不缓存失效对象裸指针。

参数控件覆盖 Int32、Float、Bool、Name、String、Vector3、Rotator、Transform、AssetPath 和 Object；
Void 只用于返回值。

### Native Delegate 实验

第一版明确使用 `PDemoCharacter::OnHealthChanged` Fixture，不声称 Native Delegate 已能被通用反射发现。

- 创建或重置 Character 与 HealthObserver。
- Add Lambda Listener。
- Add Object Listener，走带代数 Handle 的弱对象绑定。
- 显示本实验管理的监听项、类型、活动状态和调用次数。
- 按 `FDelegateHandle` Remove，按对象 RemoveAll，并支持 Clear。
- Destroy Observer 后再次广播，显示弱监听自动失效。
- 提供 OldHealth/NewHealth 参数和手动 Broadcast。
- 通过 Functions 面板 Invoke `ApplyDamage` 时，同一事件日志必须收到广播。
- Event Log 按顺序显示函数调用、状态变化、监听调用、解绑和失效跳过。

监听列表只展示该实验自己创建和持有的绑定记录，不暴露 Core Delegate 内部回调对象，也不伪装成
全局 Delegate 反射系统。

### 第一阶段验收

```text
启动 PicoInspector（无需 .pico 项目）
 -> 自动创建最小 Fixture
 -> 在 Functions 中输入 Damage=20
 -> Invoke ApplyDamage
 -> 显示 Success 和返回 Health
 -> Lambda/Object 两个监听者写入 Event Log
 -> 销毁 Object Listener
 -> 再次 Invoke 或 Broadcast
 -> Lambda 继续执行，弱对象监听安全失效
 -> Reset 后对象、参数、监听和日志恢复初始状态
```

首期可视化验证 Wrong Float 参数产生 `ArgumentTypeMismatch`，并可关闭返回存储观察
`MissingReturnStorage`。Object 参数控件只列出活动且类型兼容的对象；对象参数不兼容、失效目标和
函数异常将在增加对应 Fixture 时继续补入实验。核心行为仍需自动化测试覆盖，Inspector 的成功显示
不能替代测试。

当前窗口默认使用 `1.25` UI Scale，并加载 Segoe UI 作为可读性更高的界面字体。可通过命令行覆盖：

```powershell
.\Build\Debug\PicoInspector.exe -uiscale=1.4
```

## 后续实验

后续系统达到可独立验证状态时，可以按需增加实验，但不作为 Runtime 功能完成的硬依赖：

| 系统 | 可视化实验 |
| --- | --- |
| CDO/默认子对象 | 浏览 CDO、修改默认值、生成实例并比较对象图 |
| GC | 查看 Root、强弱引用和引用图，手动 Collect 并观察回收结果 |
| Gameplay Framework | 最小 World 中运行 Login、RestartPlayer、Possess 和 MatchState |
| Movement/Physics | 最小 Preview World 中输入移动、碰撞查询和刚体状态 |
| Animation | 切换参数并观察状态机、Clip 时间和 Notify |
| Replication/RPC | 本机连接中查看 Role、Dirty 属性、RPC 路由和修正日志 |
| Mini GAS | 授予、激活、取消 Ability，观察 Effect、Tag 和 AbilityTask |
| AI Tools | 显示 Tool Schema、参数、执行结果和受控编辑器命令 |

只有确实需要画面时才创建最小 Preview World；普通对象、反射、GC、委托和协议状态不引入 3D 模型。

## 动态多播边界

第一阶段不实现 Inspector 私有的字符串回调表。Dynamic Multicast Delegate、PicoHeaderTool、GC
对象引用和稳定引用修复完成后，再增加通用动态委托面板：

```text
Delegate Signature
 -> compatible target object
 -> compatible PFunction
 -> Bind / Unbind
 -> Broadcast through ProcessEvent
 -> save stable object identity + function name
 -> reload and repair binding
```

持久化不能保存本次运行的 `FObjectHandle`。动态绑定应使用 SceneId/ObjectPath 等稳定身份，并在对象图
重建后修复。
