# PicoGraph 第 4 周：异步续点、Cook、Agent 与运行状态

## 本周目标

第 4 周把前三周的“可编辑、可编译、可同步执行”扩展成一个可交付纵向切片：Graph 可以暂停后跨帧恢复，
可以复用 Mini GAS 的事件与动画任务，可以在打包时 Cook 为 Runtime 字节码，也可以由 Agent 通过受控工具创建。

```text
.pgraph（编辑源数据）
 -> Schema / Validator / Typed IR
 -> PGRB v3（带 Continuation 的运行字节码）
 -> PScriptComponent
 -> Delay 或 GameplayAbilities 扩展处理器
 -> 完成/中断后从指定指令继续
```

这对应 UE 中“蓝图 VM + Latent Action/AbilityTask + Cooked Package”的核心思想，但 Pico 只实现学习所需的
小范围契约，不复制完整 Kismet、LatentActionManager 或 Blueprint Debugger。

## 异步节点怎样工作

`Delay`、`WaitGameplayEvent` 和 `PlayMontageAndWait` 执行时不阻塞 Game Thread，也不创建休眠线程。VM 返回
`Suspended` 报告，其中包含：

- 异步动作类型与参数；
- 成功 Continuation 指令；
- 可选的失败/中断 Continuation 指令；
- 本次执行的预算和诊断结果。

`PScriptComponent` 保存当前字节码、续点和执行代次。`Delay` 由组件 Tick 累计 World Delta；Gameplay Event
与 Montage 通过 `PicoGameplayAbilities` 注册的扩展处理器复用现有 AbilityTask。Actor/Component 注销、重新执行
Graph 或显式取消时，旧动作会被取消；代次不匹配的迟到回调会被忽略，避免旧 World 回调恢复新实例。

`ActivateAbility` 是同步分支节点：它通过现有 ASC 和 SpecHandle 激活 Ability，然后进入 `Succeeded` 或
`Failed` 控制输出。三个 Gameplay 节点都要求 Owner Actor 上存在 ASC；等待事件或动画还要求传入当前有效、
处于 Active 状态的 AbilityHandle。

## Cook 与 Runtime 边界

`.pgraph` 是供编辑器修改的确定性 JSON，包含节点坐标等编辑信息。打包时 `PicoPackaging` 会：

1. 扫描 Stage 中的 `.pgraph`；
2. 重新加载并执行完整 Graph 校验；
3. 编译为版本化的 `.pgraph.pgrb`；
4. 从 Stage 删除源 `.pgraph`；
5. 将 Cooked 文件记录为 `CookedPicoGraph`。

Runtime 的 `PScriptComponent` 优先加载同路径的 `.pgraph.pgrb`。开发目录仍可回退到源 Graph 编译，便于编辑器
Play；正式 Stage 不依赖源 JSON、Graph 布局或仓库源码。AI 的 Compile 工具只做临时验证，不允许直接写 PGRB，
因此不能绕开 Cook 和 Schema。

## Agent Graph 工具

第 4 周新增受控工具：

- `editor.graph.create`：创建 `/Game/*.pgraph`，并返回 Entry Node/Pin 稳定 ID；
- `editor.graph.describe`：读取已有 Graph 的 Node、Pin、Link、Variable 与稳定 ID；
- `editor.graph.add_node`：只能添加已注册 Schema 节点；
- `editor.graph.connect_pins`：通过稳定 Pin ID 连接兼容 Pins；
- `editor.graph.set_default`：修改非 Exec 输入默认值；
- `editor.graph.validate`：只读校验并返回定位到 Node/Pin 的诊断；
- `editor.graph.compile`：只读生成临时 Typed IR/PGRB 摘要，不写 Cooked 文件。

所有写工具限定在当前项目 Content 的 `/Game` 路径，并继续经过权限、审批、操作边界和审计 Trace。新的
`edit-picograph.pskill` 限制工具集合和执行顺序；路由 Eval 增至 42 条，Golden Task 增至 12 条。

## 反射驱动入口

Actor Details 已经遍历 `PClass/PProperty`；PicoGraph 的 Add Node 菜单现在也从类注册表生成零参数 Callable、
基础类型 Get Property，以及仅针对 Editable、非 ReadOnly/Transient 属性的 Set Property；Project Knowledge
Store 会记录同一份实时类、属性、函数元数据。因此新反射类不需要新增专用 Details 控件、Graph 节点类或
Agent 属性工具即可被发现和修改。

早期 `configure-character-abilities` 仍作为 PicoSandbox 兼容适配器保留，便于旧会话和旧 Skill 继续工作；
新的 Gameplay 类型应优先使用通用反射属性工具和 Graph Schema，不再增加一项属性一个 Tool 的代码。

## 可视化验收

1. 打开 `BuildCodex/Release/PicoEditor.exe` 和 `Projects/PicoSandbox/PicoSandbox.pico`。
   目的：使用包含 PGRB v3、Agent Graph Tools 和运行状态面板的最新 Release。
2. 打开 `/Game/Graphs/Week4DelayTest.pgraph`，检查
   `BeginPlay -> Branch -> Delay -> Print String`：Branch 的 Condition 来自类型化的
   `Get Bool Property`；True 路径固定等待 10 秒，False 路径立即执行，两条路径输出同一句完成消息。
   目的：验证 Bool 反射属性驱动明确的两条控制流。
3. 在地图中选择 Knight 实例，设置 `bDelayBeginPlayAction`，保存地图后运行。
   目的：验证参数作为正常 PHT 反射属性保存在场景实例中；修改实例不污染 Actor Blueprint CDO。
4. `bDelayBeginPlayAction=true` 时，观察右上角先显示 `Suspended / Delay` 与递减剩余时间，完成后显示绿色
   `BeginPlay action completed`；设为 `false` 后重新运行，应立即显示同一句消息且不进入 Delay。消息持续时间
   只控制自动消失，不显示内部倒计时。
   目的：同时证明 Branch 两条控制流、跨帧 Continuation 和通用 Print String 消息。
5. 在 Agent Chat 输入“创建一个开始运行后等待一秒的 PicoGraph，验证并编译它”。批准写操作后，在 Content
   Browser 打开新资产检查节点和连线。
   目的：验证 Skill 路由、受控 Graph 工具、稳定 ID 回读、校验和编译摘要。
6. 从编辑器执行 Development Package，检查 Stage 中存在 `.pgraph.pgrb` 且不存在 `.pgraph`，再启动打包后的
   Runtime。
   目的：证明 Runtime 使用 Cooked 表示，不偷偷依赖编辑器源资产或仓库路径。

## 自动化验收

- `PicoGraphTests`：25/25，包含类型化 Bool/Float 反射输入、Branch 两条路径、Print String、四个 Gameplay
  节点协议、Delay suspend/resume 与 PGRB v3 Cook round-trip；
- `PicoEngineTests`：691/691，包含 `PScriptComponent` 跨 Tick 恢复 Continuation；
- `PicoGameplayAbilitiesTests`：73/73，覆盖事件等待、Montage 完成/中断和 Ability 生命周期；
- `PicoPackagingTests`：13/13，验证 Stage 只保留 Cooked Graph；
- `PicoAgentTests`：100/100，包含 42 条路由用例和 12 个 Golden Tasks；
- PicoSandbox Development 真实打包 Smoke Test 通过：65 个 Stage 文件，2 个源 Graph Cook 成 2 个 PGRB，
  Stage 中源 `.pgraph` 数量为 0。

## 暂不扩大的范围

当前 Knight 的 `bDelayBeginPlayAction` 是 PHT 生成的原生反射属性，作用等价于 UE 蓝图类上的实例可编辑
变量。待 PicoGraph Lite 支持在蓝图中定义变量并生成反射属性后，应由自动生成链替换这个项目演示字段；
类型化 Getter 和 VM 协议继续复用。

首版不包含任意参数 PFunction、完整蓝图调试器、断点/单步、任意循环、网络预测 Continuation 序列化或运行时
Graph 热替换。Gameplay 异步节点当前围绕本机有效 AbilityHandle 工作；网络权威仍由 ASC、RPC 和 Replication
决定，Graph 不能越过 Role/Ownership 边界。
