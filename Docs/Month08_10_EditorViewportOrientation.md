# 编辑器视口方向与独立资产窗口

本阶段改善 PicoEditor 中“看清世界坐标”和“有足够空间装配 Actor”两类问题，不改变 World、Actor、
Component 或运行时渲染的数据模型。

## 主编辑器视口

- `Axes` 控制世界原点处的长轴线：红色为 `+/-X`，绿色为 `+/-Y`，蓝色为 `+/-Z`。三条线的交点是
  世界原点，关闭后渲染器不提交这组辅助几何。
- `Orientation` 控制右上角方向控件。控件根据当前编辑器相机投影 X/Y/Z 方向；按住鼠标左键拖动时，
  只修改编辑器相机 Yaw/Pitch，不修改 World、Actor Transform 或场景相机。
- 使用场景 Camera 预览时，方向控件保持可见但不可拖动，避免编辑器操作偷偷修改关卡相机。
- 方向控件和显示开关会优先消费鼠标，不会同时触发场景拾取、Transform Gizmo 或右键飞行相机。

世界轴属于 `FSceneViewportRenderOptions`。主编辑器显式启用它；Actor Blueprint、Skeletal Preview 与
Standalone Runtime 使用各自的显示选项，所以游戏窗口不会出现编辑器世界轴或组件辅助线。

## Actor Blueprint 独立窗口

Actor Blueprint 仍属于 PicoEditor 进程，但通过 Dear ImGui Multi-Viewport 获得独立的 GLFW/系统窗口。
它可以移出主编辑器、独立缩放和关闭，同时继续共享同一个 `FEngineLoop`、AssetRegistry 和 OpenGL
资源上下文。

Standalone Play 则不同：它由编辑器启动独立的游戏进程，拥有自己的 EngineLoop、World 和生命周期。
因此关闭 Actor Blueprint 只关闭资产编辑窗口；点击红色 Stop 才会终止正在运行的游戏进程。

Actor Blueprint Preview World 中的红、绿、蓝局部轴分别表示 Actor `+X Forward`、`+Y Right` 和
`+Z Up`。每条轴现在带有 V 形箭头，帮助判断正方向；这些轴 Actor 使用 Transient 标记且关闭碰撞，
不会写进 `.pblueprint` 或当前 `.pworld`。

箭头线段先以 Cube 的局部 `+X` 为长度方向，再旋转到目标向量。Pico 与 UE 的 Rotator 约定下，
`Pitch=-90` 会把局部 `+X Forward` 转到世界 `+Z Up`；因此由方向向量计算 Pitch 时必须对几何仰角
取反。该约定由 `PicoCoreTests` 锁定，避免蓝色箭头或其他“沿向量摆放几何体”的工具再次上下翻转。

## 构建注意事项

编辑器 Play 前会检查项目 Runtime 是否比当前 `PicoEditor.exe` 更旧。修改共享 Engine、Render 或项目
Gameplay 代码后，应同时构建编辑器和项目 Runtime：

```powershell
cmake --build Build --config Release --target PicoEditor PicoSandboxGame --parallel 8
```

如果只重建 `PicoEditor`，Play 会提示 `Project Runtime is older than PicoEditor` 并拒绝启动。这是为了避免
新编辑器使用旧 Runtime 产生难以定位的行为差异，不代表 `.pworld` 或 `.pblueprint` 已损坏。

## 可视化验收

1. 启动 PicoEditor，确认主 Viewport 世界原点处出现红、绿、蓝三条长轴。
2. 分别关闭、打开 `Axes`，确认只影响长轴，网格、场景对象和 Transform Gizmo 保持不变。
3. 关闭、打开 `Orientation`，确认右上角控件独立隐藏和恢复。
4. 按住方向控件左键左右、上下拖动，确认视角旋转，而场景 Actor 的 Transform 数值不变化。
5. 在 Content Browser 双击 `BP_Knight.pblueprint`，确认出现可移出主窗口的独立系统窗口。
6. 在蓝图预览中确认三根局部轴具有清晰箭头，并继续用右键拖动、滚轮缩放预览。
7. 保持蓝图窗口打开后点击绿色 Play，确认游戏仍打开为另一个独立进程窗口；Stop 游戏不会关闭蓝图窗口。

自动验收包括 Release 全量构建、`FSceneViewportRenderOptions` 默认契约以及现有 16 组 CTest。平台窗口
位置、拖拽手感和箭头可读性属于可视化验收项目。
