# 项目浏览器与编辑器会话恢复

PicoEditor 现在把“选择项目”和“打开项目内的 World/资产”分成两个层次，分别对应 UE 的 `.uproject`
入口和项目内资产编辑流程。

![恢复 StarterWorld 与 Skeletal Asset Preview 后的编辑器工作区](Images/PicoEditorSkeletalWorkspace.png)

上图来自实际会话恢复验收：`StarterWorld.pworld`、Content Browser 与
`Knight_Male.pskeletalmesh` 预览能够在同一个项目工作区中继续使用。截图展示的是恢复结果，Session
文件只记录资产逻辑路径，不会复制 World、模型、材质或动画数据。

## 启动方式

不带项目参数启动：

```powershell
.\Build\Release\PicoEditor.exe
```

编辑器先显示 Project Browser。可以双击 Recent Projects，也可以选择一个 `.pico` 文件，或选择一个
根目录下只包含一个 `.pico` 的项目文件夹。文件夹没有描述符时会报错；存在多个描述符时不会猜测，
需要明确选择其中一个文件。

命令行仍支持文件和文件夹：

```powershell
.\Build\Release\PicoEditor.exe .\Projects\PicoSandbox\PicoSandbox.pico
.\Build\Release\PicoEditor.exe .\Projects\PicoSandbox
```

最近项目属于用户级编辑器设置，不属于任何游戏项目：

```text
%LOCALAPPDATA%/PicoEditor/EditorSettings.ini
```

无效或已经移动的项目会在加载最近列表时自动剔除。列表最多保存 8 个项目，并按最近打开顺序排列。

## 会话恢复

每个项目拥有自己的会话文件：

```text
Projects/<ProjectName>/Saved/Editor/EditorSession.ini
```

当前保存：

```ini
[Session]
LastWorld=/Game/Maps/StarterWorld.pworld
OpenActorBlueprint=/Game/Characters/BP_Knight.pblueprint
OpenSkeletalAsset=/Game/Characters/Knight_Male/Knight_Male.pskeletalmesh
```

只有实际打开的资产窗口才写入对应字段。关闭资产窗口后，该字段会从下一次保存的 Session 中消失。
Session 在状态变化和正常退出时保存，不会把 World、蓝图或模型数据复制进 Saved 目录。

地图打开优先级为：

```text
EditorSession.LastWorld
 -> Config/Pico.ini [Editor] StartupMap
 -> 当前 EngineLoop 初始 World
```

如果 Session 中的地图已经删除或加载失败，编辑器回退到 StartupMap。Actor Blueprint 和 Skeletal
Preview 在项目 AssetRegistry 建立、项目蓝图编译和 World 恢复完成后才重新打开。

## 项目切换边界

`File -> Open Project...` 和 `Open Project Folder...` 不会在当前进程中热替换项目。编辑器会：

```text
检查当前 World Dirty 状态
 -> Save / Discard / Cancel
 -> 保存当前 EditorSession
 -> 停止 Standalone Game
 -> 启动新的 PicoEditor 进程并传入目标 .pico
 -> 关闭当前进程
```

这是必要的生命周期边界。项目原生类、GeneratedClass、CDO、默认子对象和项目 Game Module 已经注册到
当前进程，不能像普通 `.pworld` 一样卸载。未来支持任意原生项目时，应为项目生成匹配的 Editor Target，
或实现动态 Game Module；不应通过清空全局注册表伪装成安全热切换。

## 可视化验收

1. 直接启动 `PicoEditor.exe`，确认先出现 Project Browser。
2. 使用 Browse Folder 选择 `Projects/PicoSandbox`，确认进入 `StarterWorld.pworld`。
3. 退出并再次无参数启动，确认 PicoSandbox 出现在 Recent Projects。
4. 双击 `BP_Knight.pblueprint`，保持窗口打开后正常退出编辑器。
5. 再次打开 PicoSandbox，确认 `StarterWorld` 和 Actor Blueprint 独立窗口一同恢复。
6. 关闭 Actor Blueprint 窗口后退出、重开，确认地图仍恢复，但蓝图窗口不再自动出现。
7. 修改 World 后选择 `File -> Open Project...`，确认 Save、Discard、Cancel 保护仍然生效。

自动测试覆盖单描述符目录解析、多描述符拒绝、最近项目去重/排序，以及 World 和资产 Session 的配置
往返。项目浏览器、原生文件夹对话框和独立窗口恢复仍需要桌面可视化验收。

当前桌面验收已确认：无参数启动可以进入 Project Browser；从最近项目或项目文件夹打开 PicoSandbox
后能够恢复 `StarterWorld`、Actor Blueprint 和 Skeletal Preview；Release Runtime 也能继续从该项目
描述符加载默认地图。后续新增资产编辑器时，应继续沿用“逻辑路径写入 Session，资产数据留在 Content”
这一边界。
