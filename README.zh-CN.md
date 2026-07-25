# Pico

[English](README.md) | [简体中文](README.zh-CN.md)

Pico 是一个以学习为目的、参考 Unreal Engine 架构设计的小型 C++ 引擎。

## Visual Studio 工作流程

在 Visual Studio 2022 中，可以通过以下两种方式打开 Pico。

### 文件夹视图

日常浏览和编辑源码时使用：

```text
Scripts\OpenFolder.bat
```

该脚本会直接打开 Pico 根目录，因此解决方案资源管理器会显示项目的实际目录结构：

```text
Pico
  Build
  Docs
  Scripts
  Source
  CMakeLists.txt
```

### 解决方案视图

需要使用传统的 Visual Studio 解决方案和项目视图时运行：

```text
Scripts\GenerateProjectFiles.bat
Scripts\OpenSolution.bat
```

脚本将打开：

```text
Build\Pico.sln
```

在这种模式下，Visual Studio 显示的是生成后的 `PicoCore`、`PicoLaunch` 等项目，而不是磁盘上的实际文件夹结构。

## 构建

在 Pico 根目录运行：

```powershell
cmake -S . -B Build -G "Visual Studio 17 2022" -A x64
cmake --build Build --config Debug --target PicoLaunch
.\Build\Debug\PicoLaunch.exe -frames=5
```

## 反射系统演示

运行控制台演示程序：

```powershell
cmake --build Build --config Debug --target PicoReflectionDemo
.\Build\Debug\PicoReflectionDemo.exe
```

运行图形化反射与序列化检查器：

```powershell
cmake --build Build --config Debug --target PicoInspector
.\Build\Debug\PicoInspector.exe
```

类型定义与反射注册的完整步骤参见 [`Docs/ReflectionAuthoringGuide.md`](Docs/ReflectionAuthoringGuide.md)。
