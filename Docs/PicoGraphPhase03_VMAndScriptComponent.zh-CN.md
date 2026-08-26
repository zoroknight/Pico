# PicoGraph 第 3 周：VM、执行预算与 ScriptComponent

## 本周完成的纵向链路

```text
.pgraph
 -> Graph Schema / Validate
 -> FPicoGraphIR
 -> PGRB v2 Bytecode
 -> DecodeGraphBytecode
 -> FPicoScriptVM
 -> FScriptExecutionContext(Self + Entry + Budgets)
 -> PFunction / PProperty / Dynamic Delegate
```

第 2 周只证明“图能被安全地检查和编译”。本周补上真正的运行边界：VM 不理解 Actor 的 C++
具体类型，只持有一个 `Self` 对象，通过 Pico 既有反射系统寻找函数、属性和动态委托。因此 Graph 没有
建立第二套对象系统，也没有绕过 `ProcessEvent`、属性变更通知或动态委托的弱对象绑定规则。

## PGRB v2 为什么升级

第 2 周字节码只能保存执行目标的数组下标，无法区分 Branch 的 `True` 与 `False`；数据输出也没有保存
自身 Pin ID，运行时无法可靠地解析 Link。v2 为每条控制边保存输出 Pin 名，为每个 Operand 保存稳定 Pin ID。
编辑器布局仍不参与字节码，语义相同的图仍会得到确定性产物。

## FPicoScriptVM 如何执行

`FScriptExecutionContext` 为一次运行提供：

- `Self`：当前脚本所属对象；`PScriptComponent` 使用自己的 Actor Owner；
- `EntryEvent`：本次从哪个 Entry 开始，当前默认是 `BeginPlay`；
- `MaxInstructions`：一次执行最多解释多少条指令；
- `MaxLoopIterations`：同一条指令最多被访问多少次；
- `MaxCallDepth`：数据依赖递归求值的最大深度。

VM 先完整校验并解码字节码，再执行控制流。Bool/Float Literal、Add Float 和 Get Property 是按需求值的
数据节点；Entry、Sequence、Branch 和反射副作用节点进入控制流。每次求值和执行都会消耗预算。损坏字节码、
越界跳转、控制环和递归数据依赖都会返回 `FScriptExecutionReport`，不会无限占用 Game Thread。

## 反射节点的当前边界

- `Call Function`：在 `Self` 上查找并通过 `ProcessEvent` 调用无参数 `Callable` PFunction；
- `Get Property`：读取 `Self` 的 Int32、Float、Bool 或 Vector3 属性并输出字符串；
- `Set Property`：把字符串转换为属性真实类型，再走 `PProperty::SetValue`；
- `Broadcast Delegate`：查找 `Self` 上的零参数动态多播委托并广播。

这些节点是最小可运行切片，不是最终节点生成方案。参数 Pin、对象 Target Pin、更多属性类型和按反射元数据
自动生成的专用节点，继续由第 4 周的反射驱动迁移完成。

## PScriptComponent 与 Actor 生命周期

`PScriptComponent` 是正常的 `PActorComponent`，拥有可序列化的 `GraphAsset`、自动 BeginPlay 开关和三项预算。
Actor 的 `DispatchBeginPlay` 会先标记 BeginPlay，再注册组件，所以组件在 `OnRegister` 中可以安全地加载
`/Game/...pgraph`、编译并执行 `BeginPlay` Entry。运行失败只记录在 `LastExecutionReport`，不会阻止 World Tick。

`ExecuteBytecode` 是运行时和自动化测试共用的入口；`ExecuteEvent` 则负责从项目 Content 解析资产并编译。
第 4 周 Cook 后，后者将优先读取派生的 Cooked Script，而不是在最终包中现场编译 JSON。

Actor Blueprint 编辑器可以通过 `Add Script Component` 增加脚本组件，并在 Details 的 `GraphAsset` 下拉框中
选择 `.pgraph`。`Compile & Save` 不只保存属性差异，还会把新增组件的反射类名写为
`ComponentClass=PScriptComponent`。重新打开蓝图时，编译器通过统一的 CDO/default-subobject 构造链重建组件，
再应用 `GraphAsset` 等覆盖值；它不再是只存在于预览 Actor 的临时组件。

蓝图预览 World 只注册组件以建立渲染代理，不执行 World Tick，也不派发 Actor `BeginPlay`。这是必须保持的
生命周期边界：预览 Actor 表示正在编辑的默认数据，Graph 在预览期间产生的运行时状态不能被 `Compile & Save`
写入 GeneratedClass CDO。只有 Play/Runtime World 会根据 `bExecuteOnBeginPlay` 执行 Graph。此前预览 World 的
一次微小 Tick 会触发 `BeginPlay`，导致测试 Graph 把 `InitialHealth=55` 反写为蓝图默认值；该路径已经移除，
并增加“组件已注册、Actor 未 BeginPlay、属性未变化、VM 未执行”的回归测试。

`FActorBlueprintReinstancer` 会在保存前快照旧 CDO 与组件模板，保存后刷新当前 Editor World 中属于同一
GeneratedClass 的已放置 Actor。新增默认组件直接补到原 Actor 上，因此 Actor 指针、Handle、名称、Transform、
选择和外部引用保持不变；实例值仍等于旧默认值时才传播新默认值，用户已经修改过的实例属性会被保留。
编辑器随后重建蓝图预览并把地图标记为 Dirty。

组件树右键菜单允许删除蓝图自己新增的组件。删除先作用于预览实例，`Compile & Save` 再从 `.pblueprint`、
GeneratedClass 默认子对象模板和当前地图实例同步移除。继承组件、Root Component 和仍拥有挂接子节点的场景
组件不能删除，避免破坏父类对象布局或产生悬空附件关系。

Play 使用独立的项目 Runtime 可执行文件。引擎或反射组件更新后，如果只重建 `PicoEditor` 而没有重建项目
Game Target，旧 Runtime 会因为无法解析地图中的新类（例如 `PScriptComponent`）而在加载默认地图后立即退出。
开发布局下 Play 现在会比较 Game 可执行文件与核心 Runtime libraries 的更新时间，发现旧 Runtime 时阻止启动并
提示构建项目 Game Target；World 加载日志也会输出无法解析的具体 Class 和 Object 名称。

当前是有意收敛的第一版：组件删除/重命名/换类、显式实例覆盖标记、Construction Script 重跑和完整对象替换
仍未实现。现阶段使用“当前值等于旧 CDO”作为未覆盖判断；后续场景格式应保存显式 override 数据，再扩展为
更接近 UE `FBlueprintCompileReinstancer` 的结构迁移。

## 自动化验收

`PicoGraphTests` 当前 21 项，新增验证：

- PGRB v2 编译、解码和执行；
- Property 写入、PFunction 调用、动态委托广播共享同一条反射路径；
- 控制环分别被循环预算和总指令预算截停；
- 递归数据依赖被调用深度预算截停；
- 损坏 PGRB 在执行前被拒绝。

`PicoEngineTests` 新增原生 Actor + `PScriptComponent` 集成测试、动态组件文件往返、动态蓝图类 CDO 实例化、
编辑器预览不触发 BeginPlay，以及已放置实例刷新测试。Release Engine 完整套件为 669 项并全部通过；刷新测试
验证 Actor 身份不变、默认值传播、实例覆盖保留，以及新增和删除 ScriptComponent 的双向同步。

## 第 4 周入口

下一阶段增加 Delay、WaitGameplayEvent、PlayMontageAndWait、ActivateAbility 等异步节点，并让挂起状态受到
World/Actor 销毁和 AbilityTask 生命周期约束；随后生成 Cooked Script、接入打包、AI Graph Tools、可视化
运行状态，以及清偿项目专用 GAS/蓝图硬编码。
