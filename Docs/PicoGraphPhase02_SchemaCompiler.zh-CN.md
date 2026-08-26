# PicoGraph 第 2 周：Schema、Typed IR 与字节码编译

## 本周目标

第 1 周解决“图如何编辑和保存”，第 2 周解决“图表达的内容是否合法，以及如何变成稳定的运行输入”。固定管线为：

```text
.pgraph
 -> ValidateGraphAsset（文件结构）
 -> Graph Schema Registry（节点契约）
 -> ValidateGraphSemantics（静态语义）
 -> FPicoGraphIR（类型化中间表示）
 -> PGRB Bytecode（第 2 周为 v1；第 3 周运行格式已升级为 v2）
```

本阶段仍不执行字节码。`FPicoScriptVM`、执行预算和 Actor 运行上下文属于第 3 周。

## 为什么分两层校验

`ValidateGraphAsset` 只回答文件有没有损坏：ID 是否重复、Link 是否指向存在的 Pin、连接方向和底层类型是否一致。
它不认识 `Branch` 应该有哪些 Pin，也不知道 Bool 默认值能不能写成 `hello`。

`ValidateGraphSemantics` 读取 `FGraphSchemaRegistry`，负责脚本含义：

- 节点类型必须存在于 Schema Registry；
- 节点 Pin 的名称、方向、类型和数量必须与 Schema 完全一致；
- Bool、Int、Float、Vector 等默认值必须可解析；
- 变量名称不能重复，变量默认值必须符合声明类型；
- Graph 至少有一个 Entry Event；
- Exec 输出不能直接扇出，应该显式使用 Sequence；
- 当前 Lite 阶段拒绝控制流环；
- 不可从 Entry 到达的控制节点产生 Warning。

错误会阻止 IR 和字节码生成；Warning 会保留诊断但允许编译。诊断包含稳定 NodeId/PinId，编辑器点击诊断可定位节点。

## 内建 Schema

当前 Registry 提供最小但可验证的数据流与控制流节点：

| 节点 | 作用 | Pin |
| --- | --- | --- |
| Entry Event | 控制流入口 | `Then: Exec Output` |
| Sequence | 顺序控制流 | `In: Exec Input`、`Then: Exec Output` |
| Branch | Bool 分支 | `In`、`Condition: Bool`、`True`、`False` |
| Bool Literal | Bool 常量 | `Value: Bool Output` |
| Float Literal | Float 常量 | `Value: Float Output` |
| Add Float | 类型化 Float 运算 | `A/B: Float Input`、`Result: Float Output` |

编辑器的 Add Node 菜单也读取这份 Registry，不另外硬编码 Pin。后续增加节点时，编译器和编辑器共享同一份契约。

## Typed IR 与字节码

`FPicoGraphIR` 保存：

- 按稳定 ID 排序的变量表及其类型和默认值；
- Entry 指令索引；
- 每个节点对应的类型化 Opcode；
- Exec 目标指令索引；
- 数据 Operand 的类型、默认值或来源 Node/Pin。

字节码以 `PGRB` 魔数和显式版本开头，整数采用固定小端编码，字符串带长度。节点、变量和 Entry 使用稳定顺序，
因此同一语义 Graph 重复编译结果逐字节一致；修改变量或 Pin 默认值会改变字节码，移动节点等编辑器布局变化不会。

当前字节码只保存在内存中供验收。第 3 周由 VM 读取，第 4 周再进入 Cook/Package，不在 Content 中制造可手改的
派生资产，也不允许 Agent 直接写字节码。

## 编辑器验收

1. 启动 `BuildCodex/Release/PicoEditor.exe`，打开 PicoSandbox。
2. 在 Content Browser 双击 `/Game/Graphs/DoorLogic.pgraph`。
3. 点击 `Validate`，右侧应显示 `Validation passed`，且没有 Error。
4. 点击 `Compile`，右侧应显示 `Compile passed`、Typed IR 指令数和当前 `PGRB v2` 字节数。
5. 点击 `Add Node` -> `Branch`，选中 Branch，在 Details 将 `Condition` 从 `false` 改为 `maybe`。
6. 点击 `Compile`，应失败并显示 `InvalidDefaultValue`；点击诊断应重新选中对应 Branch。
7. 把 `Condition` 改回 `false`，连接 Sequence 的 `Then` 到 Branch 的 `In`，再次 Compile 应成功。
8. 添加 `Bool Literal`，把 Value 改为 `true`，连接到 Branch 的 `Condition`；Compile 仍应成功。
9. 添加第二个 Sequence，将两个 Sequence 首尾连接成环；Compile 应显示 `ControlFlowCycle`，且不产生字节码。
10. 使用 `Ctrl+Z` 撤销环连接并重新 Compile，应恢复成功。
11. 把两个普通节点拖到重叠位置；点击当前可见的上层节点应选中它，点击下层露出的区域会选中并将它提升到前景。
12. 点击画布空白处，右侧 Details 应回到 Graph 信息，不再保留之前的节点选择。
13. 重新选中一个节点，按键盘 `Delete` 或点击 Details 的 `Delete Node`，节点及其 Link 应删除；按 `Ctrl+Z` 应完整恢复。

这套流程分别验证 Schema 节点工厂、类型化数据 Link、默认值诊断、控制流诊断、稳定定位、编译结果失效机制，
以及重叠节点的 Z 顺序拾取、空白取消选择和可撤销删除。

## 自动化验收

`PicoGraphTests` 当前覆盖 16 项，包括：

- 第 1 周稳定 ID、确定性 `.pgraph`、结构拒绝和 Graph 事务；
- 合法 Graph 生成 Typed IR 与 `PGRB` 字节码；
- 变量默认值参与字节码，节点布局不参与字节码；
- Bool 数据依赖保留来源 Node/Pin；
- 未知节点、Schema Pin 漂移、非法默认值、重复变量名和控制流环拒绝。
