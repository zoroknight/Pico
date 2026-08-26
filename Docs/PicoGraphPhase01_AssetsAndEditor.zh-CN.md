# PicoGraph 第 1 周：图资产与基础编辑器

## 本周目标

本周只建立 PicoGraph 的持久化编辑数据层：用户可以创建图、摆放节点、连接 Pin、定义变量、撤销修改并保存。
图还不会执行，也不会生成字节码；Schema、编译器和 VM 分别属于后续阶段。

```text
Content Browser .pgraph
 -> FPicoGraphAsset
 -> Node / Pin / Link / Variable stable IDs
 -> deterministic JSON
 -> PicoGraph Editor
 -> graph-local Undo / Redo
```

## 数据模型

`PicoGraph` 是独立 Runtime 模块，目前只依赖 `PicoCore`。主要类型包括：

- `FPicoGraphAsset`：保存版本、GraphId、变量、节点和连接。
- `FGraphNode`：保存稳定 NodeId、类型名、显示名、画布位置和 Pins。
- `FGraphPin`：保存稳定 PinId、方向、值类型和默认值。
- `FGraphLink`：通过 OutputPinId/InputPinId 连接 Pin，不保存内存地址。
- `FGraphVariable`：保存稳定 ID、名称、类型和默认文本。

首版值类型包括 `Exec/Bool/Int/Float/String/Vector/Object`。第 1 周只验证相同类型和输入/输出方向，完整类型
转换、控制流规则和节点 Schema 在第 2 周完成。

稳定 ID 在创建对象时生成并写入 `.pgraph`。移动节点、重命名资源、关闭编辑器或重新扫描 AssetRegistry 不会
改变这些 ID，后续编译诊断、Agent ToolCall 和运行时调试才能可靠引用同一个节点或 Pin。

## 文件与校验

`.pgraph` 使用带 `format=PicoGraph` 和显式 `version=1` 的确定性 JSON。保存采用固定字段和数组顺序；同一份
图完成 Save -> Load -> Save 后字节完全一致。加载阶段拒绝：

- 不支持的版本；
- 空或重复的 Graph/Variable/Node/Pin/Link ID；
- 指向不存在 Pin 的连接；
- Input-to-Input、Output-to-Output 或类型不匹配；
- 同一个输入 Pin 同时接受多个连接。

删除节点会同时删除引用其 Pin 的 Link，资产不会留下悬空关系。

## 编辑器能力

Content Browser 增加 `Create Graph`，`.pgraph` 会显示为 `PicoGraph` 类型，双击即可打开独立窗口。当前窗口支持：

- 新建时自动生成 `BeginPlay` Entry Event；
- 添加 Custom Entry Event 和 Sequence 结构节点；
- 创建 Bool、Int、Float、String、Vector、Object 变量；
- 左键拖动节点标题或中央区域，中键拖动画布；节点两侧 Pin 保留独立点击区域；
- 独立窗口只能从标题栏移动，在节点或画布上按住左键不会误拖整个窗口；
- 依次点击两个兼容 Pin 建立连接；
- 右键 Pin 删除与它关联的连接；
- 删除非 Entry 节点并级联清理连接；
- `Ctrl+Z/Ctrl+Y` 使用 64 步 Graph 专用事务历史；
- `Ctrl+S` 保存节点位置、变量、Pin 和 Link。

Graph 事务不复用 World Snapshot。World 和 `.pgraph` 是两个独立文档，把 Graph 修改塞入 World 事务会导致
撤销场景 Transform 时意外回滚图资产，也无法在没有打开 World 时编辑 Graph。
PicoGraph 窗口获得键盘焦点时，主编辑器会暂停处理全局 World Undo/Redo，因此快捷键只作用于当前 Graph。

## 可视化验收

1. 启动 `BuildCodex/Debug/PicoEditor.exe`，打开 PicoSandbox。
2. 在 Content Browser 点击 `Create Graph`，保留 `/Game/Graphs`，输入 `DoorLogic` 并创建。
3. 确认独立 PicoGraph 窗口中已经存在 `BeginPlay`。
4. 点击 `Add Sequence Node`，分别从标题和节点中央区域开始拖动；节点应立即跟随鼠标，独立窗口不能移动。
5. 在节点两侧 Pin 附近点击，确认不会误拖节点；点击 BeginPlay 的 `Then`，再点击 Sequence 的 `In`。
6. 在画布空白处按住左键，窗口不应移动；按住中键拖动，画布应正常平移。窗口本身仍可从标题栏移动。
7. 点击 `+ Variable`，创建 Float 类型 `OpenSpeed`，默认值设为 `120.0`。
8. 按 `Ctrl+Z`，变量或最近连接应撤销；按 `Ctrl+Y` 应恢复。
9. 右键已连接的 Pin，连接线应消失；再次连接。
10. 按 `Ctrl+S`，关闭 Graph 窗口，在 Content Browser 双击 `DoorLogic.pgraph`。
11. 节点位置、变量、稳定 ID 和连接应完整恢复。

这套流程验证的是“编辑数据链和资产身份”，不是运行结果。第 2 周加入编译按钮和诊断后，图才开始具有确定的
静态语义；第 3 周接入 `FPicoScriptVM` 后才会驱动 Actor。

## 自动化验收

`PicoGraphTests` 覆盖稳定身份、确定性 round-trip、重复 ID、缺失 Pin、节点删除和 Undo/Redo。
`PicoAssetTests` 覆盖 `.pgraph` 的 AssetRegistry 扫描、类型和确定性虚拟路径排序。
