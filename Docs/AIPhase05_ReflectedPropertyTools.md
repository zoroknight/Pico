# AI 场景 Agent：通用反射属性工具

本阶段解决“每增加一个属性就要手写一个 Agent Tool”的扩展性问题。PicoEditor 不向模型开放裸内存或任意
`PFunction`，而是把已有 `PClass`、`PProperty`、属性通知和 Editor Transaction 组合成三个稳定工具：

```text
editor.object.describe
editor.object.get_property
editor.object.set_properties
```

## 工作流程

模型必须先用 `editor.object.describe` 查询对象。Actor 的结果包含自身可编辑属性、Root Component，以及所有
Component 的对象路径、实际 Class、继承属性、当前值和元数据。颜色、尺寸、Transform 等值均使用结构化 JSON，
不依赖文本解析或模型猜测字段布局。

`editor.object.set_properties` 在一个对象上一次修改最多 32 个属性。整批修改只请求一次用户审批，只创建一个
Undo 事务；任一属性类型错误、越界、只读、资产类型不匹配或后置验证失败，整批回滚。例如：

```json
{
  "object_path": "StarterWorld.PersistentLevel.MyCube.CubeComponent",
  "properties": {
    "Extent": {"x": 50, "y": 50, "z": 50},
    "Color": {"x": 1.0, "y": 0.15, "z": 0.05}
  }
}
```

`Extent` 是半尺寸，因此上述值产生 `100 x 100 x 100 cm` 的 Cube。`Color` 是 0 到 1 的 Linear RGB。

## 新增游戏属性

新属性只要进入 Pico 反射并带有 `Editable`，就会自动出现在 Details 和 Agent 的属性描述中，不需要注册新的
Agent Tool。当前通用编解码支持：

- `int32`、`float`、`bool`
- `FVector3`、`FRotator`、`FTransform`
- `FAssetPath`

新增一种反射数据类型时，只需要为该类型增加一次 JSON 编解码，此后所有同类型属性都可复用。对象引用和动态
多播委托当前只读显示，不允许 Agent 通用写入；有行为副作用的函数仍必须使用 `AgentCallable` 白名单或专用的
高层工具。

## 元数据与副作用

`FPropertyMetadata` 增加 Description、Semantic、Units、Minimum 和 Maximum。模型由元数据理解同为
`FVector3` 的值究竟是位置、颜色还是 Box Extent，并在调用前知道单位和合法范围。

反射写入通过 `PProperty::SetValue` 触发 Pre/Post Property Change。依赖属性变化重建缓存的组件，应把统一刷新
逻辑放在 `PostEditChangeProperty`，而不是只放在手写 Setter。`PCubeComponent::Extent` 已按此规则重建物理
Shape；`PPrimitiveComponent::Color` 会统一限制到 0..1。

## 安全边界

- 只处理当前 Editor World 中可解析的稳定对象路径。
- 只允许 `Editable` 且非 `ReadOnly` 的支持类型。
- 所有调用仍经过 Validate、Permission、Approval、Transaction、Execute 和 Verify。
- 写入在 Game Thread 执行，成功后读回实际值验证。
- 资产引用继续经过 AssetRegistry 类型检查。
- 当前不修改 CDO、Actor Blueprint 默认值或运行中网络对象；这些目标需要独立作用域和权限策略。
- 修改共享 Material 资产会影响所有引用者，后续应通过 Material Instance/参数覆盖工具处理，而不是把资产修改
  混入 World 对象属性工具。

## 自动化验收

`PicoEditorTests` 实际创建 Cube，并验证：

1. Actor 描述能发现 Root Component、继承的 `Color` 和自身的 `Extent`。
2. 一次批准可同时修改 Color 和 Extent。
3. 一次普通 Editor Undo 恢复整个批次。
4. 批次后半段出现非法负 Extent 时，前面已经写入的 Color 也会回滚。
5. 通用写入继续触发属性通知和物理状态刷新。
