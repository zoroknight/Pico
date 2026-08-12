# Pico Character Import And Project Settings

## 本阶段解决的问题

- Standalone 秒退：空的新增 `AssetPath` 现在反序列化为“未设置”，不再把旧地图误判为损坏；编辑器还会阻止
  启动早于 Editor 的旧 Game EXE，并把子进程输出写入 `Saved/Logs/StandaloneGame.log`。
- glTF 材质：Assimp 结果由 Developer AssetImport 层转换为 Pico `FMaterialData/FTextureData`，Runtime 不依赖
  Assimp。Knight 实测导入 17 个动画和 6 个材质。
- 多材质：Section 保留源材质槽名，`.pskeletalmesh` v2 保存每槽默认 `.pmat`，渲染时优先使用组件 Override。
- 项目配置：`Edit -> Project Settings` 编辑 `[Input]` 与 `[Game]`；下一次 Play 由新进程读取配置。
- 项目类：Sandbox 类随项目编辑器注册，`Add -> Actor Class...` 可以搜索并实例化可构造的 Actor 子类。
- 数据驱动角色：骨骼导入自动生成 `.pcharprofile`，默认 Pawn 通过项目设置选择 Profile，无需为每个模型改 CDO。

## 场景角色装配与第三人称控制

- `Add -> Playable Character...` 同时选择一个 `PPawn` 子类和一个 Character Profile，创建后的 Pawn 是地图中的普通持久对象，不是 CDO，也不是每次运行临时生成的默认 Pawn。
- 创建器将 `AutoPossessPlayerIndex` 设为 `0`。Standalone 加载运行时 World 后，`GameMode` 优先让本地 `PlayerController` 占有这个场景 Pawn；地图中没有该 Pawn 时，才回退到 Project Settings 的 Default Pawn Class/Profile 和 PlayerStart。
- Play Validation 会阻止同一地图存在多个 `Auto Possess Player 0` Pawn，避免运行结果依赖 Actor 遍历顺序。
- `Controller::ControlRotation` 保存视角朝向；右键捕获鼠标后，Mouse X/Y 修改 yaw/pitch。WASD 使用 ControlRotation 的水平前向和右向，因此 W 始终表示“朝当前镜头前方移动”。`Escape` 先释放鼠标，再次按下才关闭窗口。
- `SpringArmComponent::bUsePawnControlRotation` 让相机臂跟随 ControlRotation；`CharacterMovementComponent::bOrientRotationToMovement` 让 Actor 平滑朝移动方向旋转。视觉 Mesh 可以通过 Relative Rotation 修正源资产坐标，而不改变 Actor、碰撞、物理和未来网络 Transform 的统一 `+X` 前向。
- Sandbox Pawn 默认包含 Capsule、CharacterMovement、SkeletalMesh、SpringArm 和 Camera。角色 Profile 负责选择 Mesh、AnimationSet、Montage 与材质，场景实例负责位置、朝向、组件参数、物理和控制配置。
- Character Profile 与 SkeletalMeshComponent 现在支持 8 个独立材质覆写槽；空覆写会使用 `.pskeletalmesh` 中记录的 glTF 默认 PBR 材质。

## Project Settings 选择器

`Edit -> Project Settings -> Gameplay` 的 Default Map、Default Pawn Class、Player Controller Class 和 Default Pawn Profile 均来自 AssetRegistry 或 ClassRegistry 下拉列表。配置仍写入 `Config/Pico.ini`，但用户不再需要查找和手工输入类名或 `/Game` 虚拟路径。

## 材质覆写优先级

骨骼材质解析现在固定为：

```text
组件实例 MaterialOverride
 -> Character Profile 默认材质
 -> SkeletalMesh 导入时记录的 glTF 默认材质
```

加载 Profile 不再写回或覆盖组件实例属性。Knight 网格实际包含 6 个 Section，因此有效槽位是 `0..5`；组件保留 8 槽容量用于其他网格，Details 会把超出当前网格 Section 数量的槽位显示为 `Unused`，避免允许选择却没有几何可应用。

Asset 下拉弹窗使用与属性栏一致的固定宽度和受限高度，搜索或选择较长路径时不再改变右边界。

## Starter World

PicoSandbox 的 Editor Startup Map 和 Runtime Default Map 现在都是 `/Game/Maps/StarterWorld.pworld`。该地图不是 GameMode 临时搭建的测试场景，而是可编辑、可保存、可 Cook 的普通关卡资产，包含：

- Floor、WallNorth、WallEast、WallWest
- 可推动的 PhysicsCrate
- PlayerStart
- DirectionalLight 和 PointLight

基础几何与材质位于 `/Game/StarterContent`：`SM_Cube.pmesh`、`M_Ground.pmat`、`M_Wall.pmat` 和 `M_Accent.pmat`。每个环境块由隐藏的 `CubeComponent` 提供 Jolt 碰撞，由 `StaticMeshComponent` 提供资产渲染；二者跟随同一个 Actor Transform。原先 `SandboxGameMode::BeginPlay` 中临时生成环境的代码已移除，因此编辑器与运行时看到的是同一组场景对象。

## 依赖方向

```text
Source glTF/FBX
  -> PicoAssetImport (Assimp, editor/developer only)
  -> .pskeleton / .pskeletalmesh / .panimation
  -> .pmat / .ptex / .panimset / .pcharprofile
  -> AssetRegistry + AssetManager
  -> PSkeletalMeshComponent
  -> PicoRender
```

Gameplay 和 Runtime 只看到 Pico 原生数据。替换 Assimp、扩展 glTF 转换或以后增加 Cook，不需要修改 Pawn、
CharacterMovement 或渲染调用方的资产身份。

## 可视化验收

1. 打开 `Build/Debug/PicoEditor.exe`，确认旧地图正常加载；点击绿色 Play，游戏窗口应持续运行。
2. 打开 `Edit -> Project Settings -> Input`，修改映射并保存；重新 Play，确认新进程使用新输入。
3. 打开 `Gameplay` 页，从下拉列表选择地图、Pawn、Controller 和 Profile，保存后无需手工输入路径。
4. 在 `Add -> Actor Class...` 搜索 `Sandbox`，确认能看到项目 Actor 类。
5. 从 Content Browser 导入 Knight glTF；若覆盖旧导入，勾选 `Replace Existing Assets`。
6. 导入后确认 `Materials` 子目录、`.pskeletalmesh`、`.panimset` 和 `.pcharprofile` 出现在 Content Browser。
7. 使用 `Add -> Playable Character...`，选择 `PSandboxPawn` 与 Knight Profile；保存地图后重新 Play，运行时应直接占有这个场景实例。
8. 在 Details 中修改该实例的 Transform、SkeletalMesh 材质槽、动画引用和 Movement 参数；保存并重新 Play，确认运行窗口显示的是这些实例值。
9. 在游戏窗口按住右键捕获鼠标，移动鼠标控制视角；W/S 沿视角前后、A/D 横移、Space 跳跃，角色视觉朝移动方向旋转。

## 当前边界

- glTF 是正式材质验收路径；FBX 材质因文件导出器差异仍是实验性兼容路径。
- Profile 是视觉与动画引用集合，不是完整 Prefab/Blueprint，也不保存碰撞、Movement 或任意组件对象图。
- 覆盖重导入已经具备文件级备份/失败恢复，但 Source Metadata、骨架兼容性比较和用户 Override 合并仍属于
  后续正式 Reimport 阶段。
