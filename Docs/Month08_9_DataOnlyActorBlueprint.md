# Data-Only Actor Blueprint 与角色装配编辑器

## 为什么现在实现这一层

角色的碰撞、移动、镜头、骨骼网格和动画已经分别存在，但过去只能在主场景中微调组件，或者修改
原生 Pawn 的 CDO。前者会把“角色类型默认值”和“某个关卡实例的修改”混在一起；后者每增加一个人物
都可能要求改 C++ 或项目配置。

Pico 现在增加 UE 风格的 **Data-Only Actor Blueprint**：它不包含 Event Graph，而是从一个原生 Actor
类派生出新的可放置类型，保存 Actor 和既有组件的默认属性。它解决角色装配问题，同时为后续
PicoGraph 保留生成类、CDO 和场景类身份基础。

## 从资产到运行实例

```text
原生 PSandboxPawn
  -> .pblueprint 记录父类和反射属性覆盖
  -> 编译为运行时 PClass（GeneratedClass）
  -> 创建该类自己的 CDO 和默认子对象模板
  -> World::SpawnActor(GeneratedClass)
  -> 生成互相独立的 Actor 与 Component 实例
```

`.pblueprint` 保存的是可读、确定性的配置数据，不保存 C++ 指针，也不保存整个对象的原始内存。
运行时在加载地图前扫描并编译项目蓝图，所以 `.pworld` 可以只记录稳定的生成类名。编辑器保存蓝图后会
立即更新生成类 CDO 和组件模板；之后创建的实例使用新默认值，已存在的场景实例不会被悄悄覆盖。

PicoSandbox 提供了 `/Game/Characters/BP_Knight.pblueprint`。项目 GameMode 的 DefaultPawnClass 也可以
直接指向它的生成类，因此找不到场景内 Auto Possess Pawn 时，运行时仍能通过正常 Gameplay 链创建该类型。

## 三层 Transform

```text
Actor World Transform
  * Component Relative Transform
  * Character Profile Visual Transform
  = 最终骨骼网格渲染 Transform
```

- **Actor World Transform**：角色在关卡中的真实位置和朝向；移动、物理和未来网络同步关注这一层。
- **Component Relative Transform**：组件相对 Actor 的装配偏移，可用于调整网格高度或局部朝向。
- **Profile Visual Transform**：修正某个源模型的坐标轴、单位和参考姿势，只影响视觉模型。

角色 Gameplay Forward 固定为局部 `+X`。模型若原始面朝 `+Y` 或 `-Z`，应在 Profile Visual Transform
中修正；不要旋转 Actor 或碰撞胶囊来迁就模型。组件 Relative Transform 仍可用于某个蓝图类型的额外装配，
且不会再被 Profile 覆盖。

## 编辑器使用流程

1. 启动 PicoEditor，在 Content Browser 点击 `Create Actor Blueprint`。
2. 选择一个原生 Actor 父类，例如 `PSandboxPawn`，输入资产目录和名称后创建。
3. 双击 `.pblueprint`，或右键选择 `Open Actor Blueprint`。
4. 在左侧 Components 树选择 Actor、Capsule、SkeletalMesh、SpringArm 或 Camera。
5. 在右侧 Details 修改反射属性；中间 Preview World 会显示当前装配。
6. 选择 SkeletalMeshComponent，查看 `Forward Relationship` 中组件偏航、Profile 修正和最终视觉偏航。
   选择 CameraBoom 时，查看 Actor Yaw、Control Yaw 与 Camera Target Yaw，并编辑 Use Pawn Control Rotation
   以及 Inherit Pitch/Yaw/Roll。预览 World 没有 Controller，因此 Control Yaw 会明确显示为不可用。
7. 右键拖动预览镜头，滚轮缩放；需要放弃临时改动时选择 `Reset Preview`。
8. 使用 `Compile & Save` 保存默认值，再用 `Spawn In Level` 把生成类实例放进当前关卡。
9. 保存 `.pworld` 并 Standalone Play，确认运行时创建/加载的是生成类，而不是原生 Pawn CDO。

每一步分别验证资产创建、组件模板选择、反射编辑、坐标关系、预览隔离、编译保存、场景实例化和运行时
重建。预览 World 是临时对象图，不会污染当前编辑关卡。

## 与 UE Blueprint 的关系

这一版学习并实现了 UE Blueprint 的一条重要纵向链路：

```text
编辑器资产 -> GeneratedClass -> CDO/组件模板 -> SpawnActor -> 场景序列化
```

当前有意限制为：

- 只能选择原生 Actor 父类，暂不支持蓝图继承蓝图。
- 编辑既有原生默认组件，暂不支持新增、删除、重命名或重新挂接组件。
- 没有 Construction Script、Event Graph、节点 VM、热重载和完整 Blueprint 调试器。
- 保存的是受 Pico 反射支持的属性类型；复杂容器和任意对象图覆盖尚未加入。

后续 PicoGraph 只需补充行为图、编译器和 VM，不应重新发明生成类、CDO 或默认子对象链。网络接入时，
生成类名用于 Actor 类型身份，Replication 同步运行实例的权威状态，而不是同步蓝图编辑器预览数据。

## 验收标准

- 创建和重开 `.pblueprint` 后，Actor 与组件默认值保持一致。
- 修改 Character Profile 或组件 Relative Transform 时，两层修正可以同时生效。
- `Spawn In Level` 产生生成类实例，保存/重开 `.pworld` 后类身份不退化为原生父类。
- 项目默认 Pawn 可选择生成类，并通过 Login、RestartPlayer、Possess 正常进入游戏。
- CameraBoom 的控制旋转配置可保存在蓝图默认组件模板中，角色转身不会反向驱动 Camera Target Yaw。
- 蓝图预览对象不进入主 World，也不会被保存进当前地图。
