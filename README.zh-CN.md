# Pico

[English](README.md) | [简体中文](README.zh-CN.md)

Pico 是一个以学习为目的、参考 Unreal Engine 架构设计的小型 C++ 3D 引擎。项目重点不是堆叠功能数量，
而是理解并亲手连接引擎启动、对象、反射、序列化、World、Actor、Component、Transform、编辑器和渲染等系统。

Pico 不以替代成熟商业引擎为目标。每个系统都会尽量保持小而清晰，同时保留可以完整运行和继续扩展的架构边界。

![支持场景层级编辑和视口选择的 Pico 编辑器](Docs/Images/PicoEditorMonth04.png)

## 当前状态

前三个月的开发任务已经完成。目前 Pico 可以：

- 运行类似 UE 的 `PreInit -> Init -> Tick -> Exit` 引擎循环。
- 通过 `PClass` 和 `NewObject` 创建具有反射信息的原生 C++ 对象。
- 使用薄反射宏注册类和属性。
- 通过通用的元数据驱动界面查看和修改属性。
- 将反射对象序列化为 `.pobj`，并通过 `PostLoad` 完成加载后的处理。
- 将经过校验的 World 场景图原子保存为确定性的 `.pworld` 文件，并在不持久化运行时 Handle 的前提下事务式重建运行时 World。
- 事务式替换 `FEngineLoop` 的当前 World，并在文件加载或 `PostLoad` 失败时完整保留旧 World。
- 使用 `FObjectRegistry`、Outer 和带代数的 Handle 集中管理对象。
- 创建具有明确生命周期的 `PWorld`、`PLevel`、`PActor` 和 Component。
- 使用 RootComponent 为 Actor 提供 Transform。
- 建立 SceneComponent 父子挂接树并计算 Relative/World Transform。
- 通过 `.pico` 打开引擎目录之外的项目边界。
- 在 Outliner 和 Details 中查看、创建、修改和销毁运行时对象。
- 在 OpenGL 3.3 编辑器视口中渲染 `PCubeComponent`。
- 通过编辑器事务撤销和重做场景层级、Actor Transform 与反射属性修改。
- 使用重新映射的场景 ID 复制和粘贴完整 Actor 或 SceneComponent 挂接子树。
- 自由停靠编辑器面板，并将每个项目的布局保存到 `Saved/Editor`。
- 通过 `PicoRender` 私有的 GLAD 目标加载现代 OpenGL 函数。

Debug 和 Release 均可完整构建，四个自动化测试程序全部通过。

## 架构

主要模块依赖方向为：

```text
PicoEditor
  -> PicoRender
  -> PicoEngine
  -> PicoObject
  -> PicoCore
```

开发工具和示例只依赖运行时公开接口：

```text
PicoReflectionTools -> PicoObject
PicoSandbox         -> PicoEngine / PicoObject
PicoInspector       -> PicoReflectionTools
```

| 模块 | 职责 |
| --- | --- |
| `PicoCore` | App状态、命令行、配置、日志、FName、路径、时间、数学和项目描述 |
| `PicoObject` | `PObject`、`PClass`、`PProperty`、反射、注册表、Handle、Outer和序列化 |
| `PicoEngine` | EngineLoop、World、Level、Actor、Component、挂接和可渲染场景数据 |
| `PicoRender` | 基于GLAD的OpenGL、Shader、几何体、Framebuffer、场景遍历和绘制提交 |
| `PicoEditor` | Outliner、Details、运行时场景操作、编辑器相机和3D视口 |
| `PicoReflectionTools` | 通用元数据检查和反射属性工具 |
| `PicoSandbox` | 项目侧反射、序列化、资产和自动化测试示例 |

运行时模块不依赖 ImGui。`PicoCore`、`PicoObject` 和 `PicoEngine` 也不依赖 GLFW 或 OpenGL。

## 运行时对象模型

Pico 明确区分四种关系：

```text
PClass       = 对象是什么类型
Outer        = 对象的命名和生命周期归谁管理
Handle       = 如何从注册表安全地重新找到对象
AttachParent = SceneComponent的Transform相对于谁
```

当前场景继承和组织主干为：

```text
PWorld
  -> PLevel
    -> PActor
      -> PActorComponent
        -> PSceneComponent
          -> PPrimitiveComponent
            -> PCubeComponent
```

对象内存由 `FObjectRegistry` 实际持有。Handle 不延长生命周期，失效 Handle 会解析为 `nullptr`。
Outer 负责命名关系和确定性的销毁顺序。这是未来追踪式垃圾回收的基础，但当前并不是追踪式 GC。

## 项目边界

Pico 将引擎安装内容与用户项目内容分开：

```text
EngineRoot/
  Source/
  ThirdParty/
  Config/

ProjectRoot/
  MyGame.pico
  Source/
  Content/
  Config/
  Intermediate/
  Saved/
```

编辑器和工具自动写入时，只允许写入项目的 `Content`、`Intermediate` 和 `Saved`。引擎源码和项目源码
不会成为自动写入目标。

`Projects/PicoSandbox/PicoSandbox.pico` 是仓库维护的示例项目。同样的项目结构也可以放在 Pico 仓库之外。

## 环境要求

- Windows 10 或 Windows 11（x64）
- Visual Studio 2022，并安装 **使用 C++ 的桌面开发** 工作负载
- MSVC v143 和 Windows 10/11 SDK
- CMake 3.22 或更高版本
- Git for Windows
- 支持 OpenGL 3.3 的显卡和驱动

GLFW 和 Dear ImGui 已包含在 `ThirdParty` 中。

## 快速开始

```powershell
git clone https://github.com/zoroknight/Pico.git
Set-Location Pico
powershell -ExecutionPolicy Bypass -File .\Scripts\SetupWindows.ps1
```

初始化脚本会检查开发环境、生成 Visual Studio 2022 x64 工程、编译全部目标并运行测试。构建产物只写入 `Build`。

启动编辑器：

```powershell
.\Build\Debug\PicoEditor.exe .\Projects\PicoSandbox\PicoSandbox.pico
```

编辑器默认最大化，默认 UI 缩放为 `1.4`。需要更大的界面时可以指定：

```powershell
.\Build\Debug\PicoEditor.exe .\Projects\PicoSandbox\PicoSandbox.pico -uiscale=1.6
```

## 编辑器操作

编辑器启动后只有空场景：

```text
GameWorld
  -> PersistentLevel
```

- 使用 `Add > Empty Actor` 创建带有 `DefaultSceneRoot` 的编辑器 Actor。
- 使用 `Add > Cube` 创建以可渲染 `PCubeComponent` 直接作为根组件的 Actor。
- 可以向选中的 Actor 添加 Scene 或 Cube Component，也可以向选中的 SceneComponent 添加子组件。
- 按 `F2` 重命名 Actor 或 Component，按 `Delete` 删除；删除 SceneComponent 会删除完整的附着子树。
- 右键 World、Level、Actor 或 Component 节点可执行对应的创建、重命名、设置根组件和删除操作。
- 左键点击 Viewport 中的可见几何体会选中它所属的 Actor；选中 Actor 时会给它的全部可见
  CubeComponent 绘制白框，在 Outliner 中选择组件时只给该组件绘制白框。
- 使用 `Ctrl+Z` 撤销，使用 `Ctrl+Y` 或 `Ctrl+Shift+Z` 重做场景层级、Actor Transform 和
  反射属性修改；一次连续拖动只会生成一条事务记录。
- 使用 `Ctrl+C` 和 `Ctrl+V` 复制粘贴 Actor 及其全部组件，或选中的 SceneComponent
  及其完整挂接子树；一次粘贴对应一条可撤销事务。
- 在 Details 中修改 Actor Transform，可以移动、旋转和缩放 Cube。
- 修改 `CubeComponent` 的反射属性，可以调整相对 Transform、Extent、Color 和可见性。
- 在 Viewport 中按住鼠标右键会捕获光标，移动鼠标可以稳定地转动视角。
- 按住鼠标右键时使用 `W/A/S/D` 飞行，使用 `Q/E` 下降或上升。
- 按住 `Shift` 可获得四倍速度，飞行时滚动鼠标滚轮可调整相机速度。
- 使用工具栏，可以创建 Actor、增加场景子组件、设置 Root 或销毁运行时对象。
- 拖动面板标签可以重新停靠或合并面板，使用 `Reset Layout` 恢复默认布局。

使用 `Ctrl+S` 可以将场景保存到当前项目的 `Content/Maps/EditorWorld.pworld`，使用 `Ctrl+O`
可以重新加载。保存场景数据不会重写 C++ 源码。

编辑器面板布局与场景数据相互独立，布局保存在
`Projects/<ProjectName>/Saved/Editor/PicoEditorLayout.ini`。

## 程序和示例

| 目标 | 用途 |
| --- | --- |
| `PicoLaunch` | 无窗口的 EngineLoop 和 World 生命周期程序 |
| `PicoEditor` | 带有 OpenGL 场景视口的运行时编辑器 |
| `PicoInspector` | 通用反射对象检查器 |
| `PicoReflectionDemo` | 控制台反射流程演示 |
| `PicoSandboxDemo` | 项目侧创建、修改、保存、销毁和加载完整流程 |

运行示例：

```powershell
.\Build\Debug\PicoLaunch.exe -frames=5
.\Build\Debug\PicoReflectionDemo.exe
.\Build\Debug\PicoInspector.exe
.\Build\Projects\PicoSandbox\Debug\PicoSandboxDemo.exe
.\Build\Debug\PicoEditor.exe .\Projects\PicoSandbox\PicoSandbox.pico
```

## 构建与测试

手动生成、构建和测试：

```powershell
cmake -S . -B Build -G "Visual Studio 17 2022" -A x64
cmake --build Build --config Debug --parallel
ctest --test-dir Build -C Debug --output-on-failure
```

构建和测试 Release：

```powershell
powershell -ExecutionPolicy Bypass -File .\Scripts\SetupWindows.ps1 -Configuration Release
```

在 Visual Studio 中打开 Pico：

```text
Scripts\OpenFolder.bat
Scripts\GenerateProjectFiles.bat
Scripts\OpenSolution.bat
```

生成的解决方案位于 `Build\Pico.sln`。

## 仓库结构

```text
Pico/
  Config/                  引擎配置
  Docs/                    里程碑与开发说明
  Projects/PicoSandbox/    仓库维护的外部项目式示例
  Scripts/                 Windows初始化和构建脚本
  Source/
    Developer/             仅开发期使用的反射工具
    Editor/                PicoEditor和PicoInspector
    Runtime/               Core、Object、Engine、Render和Launch
    Samples/               可复用的引擎侧示例
  Tests/                   Core、Object、Engine和Sandbox测试
  ThirdParty/              GLAD、GLFW和Dear ImGui
```

## 编写反射类

Pico 当前使用薄原生 C++ 宏：

```cpp
class PExample final : public Pico::PObject
{
    PICO_DECLARE_CLASS(PExample, Pico::PObject)

private:
    Pico::int32 Health = 100;
};
```

在 `.cpp` 中显式定义类和反射属性：

```cpp
PICO_DEFINE_CLASS(PExample)

bool PExample::RegisterProperties(Pico::PClass& Class)
{
    std::vector<Pico::PProperty> Properties;
    PICO_ADD_PROPERTY(Properties, Health);
    return Class.AddProperties(std::move(Properties));
}
```

Pico 目前还没有类似 UHT 的头文件工具。未来的 PicoHeaderTool 可以生成这些样板代码，同时继续使用同一套运行时元数据系统。

相关文档：

- [反射类编写指南](Docs/ReflectionAuthoringGuide.md)
- [PicoSandbox指南](Projects/PicoSandbox/README.md)
- [第三个月编辑器视口](Docs/Month03_10_Editor3DViewport.md)
- [第三个月编辑器停靠布局](Docs/Month03_11_EditorDocking.md)
- [第三个月GLAD集成](Docs/Month03_12_GLADIntegration.md)
- [第四个月编辑器事务](Docs/Month04_9_EditorTransactions.md)
- [第四个月属性事务](Docs/Month04_10_EditorPropertyTransactions.md)
- [第四个月编辑器剪贴板](Docs/Month04_11_EditorClipboard.md)

## 路线图

下一阶段的重点是将运行时编辑器发展为可以持久化的内容工作流：

- 事务式 World 重建与 `.pworld` 文件保存和加载
- AssetRegistry和项目Content Browser
- StaticMesh资产和模型导入
- Transform Gizmo
- RenderScene缓存、材质、贴图和PBR

更后面的阶段计划探索：

- 追踪式垃圾回收
- 委托和事件
- Replication和RPC
- 物理与动画集成
- 打包和项目代码生成
- 光线追踪
- 核心引擎成熟后实现精简版Gameplay Ability System

详细里程碑记录位于 [`Docs`](Docs)。
