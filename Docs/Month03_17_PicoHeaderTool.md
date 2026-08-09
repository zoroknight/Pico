# Month 03.17: PicoHeaderTool

## 目标

这一阶段实现 UE UHT 的受约束学习版：把反射声明放回 C++ 类旁边，并在编译前生成重复的注册代码。
PHT 不替代 `PClass/PProperty/PFunction`，它只是这些运行时类型的代码生成前端。

```text
PCLASS / PPROPERTY / PFUNCTION
  -> PicoHeaderTool tokenizer + parser
  -> .generated.h + .gen.cpp
  -> PICO_DECLARE_CLASS / PICO_DEFINE_CLASS
  -> PClass / PProperty / PFunction
```

## 已实现范围

- 独立 `PicoHeaderTool` 命令行程序，不依赖引擎运行时。
- 基于 Token 的解析，跳过注释和预处理行，并记录文件、行、列。
- 解析单继承 `PCLASS`、`GENERATED_BODY`、字段 `PPROPERTY` 和成员函数 `PFUNCTION`。
- 生成类构造入口、属性元数据、函数元数据和类型安全调用 Thunk 的连接代码。
- CMake `add_custom_command` 增量依赖：输入头或 PHT 变化时重新生成。
- 输出统一位于 `Build/Generated`；内容未变化时不重写文件。
- 迁移 `PDemoCharacter`、`PDemoHealthObserver` 和项目侧 `PSandboxPawn`。
- 合法输入、非法 Specifier、文件/行号诊断和不重复写入测试。

## 与 UE5 的对应关系

UE 的 UHT 扫描 `UCLASS/USTRUCT/UPROPERTY/UFUNCTION`，生成 `.generated.h` 与 `.gen.cpp`，再把
生成的注册信息接到 `UClass/FProperty/UFunction`。Pico 保留了同一条学习链路，但第一版只支持普通类、
属性与函数，不实现 Blueprint、热重载、结构体、枚举、接口和完整 Specifier 语法。

`GENERATED_BODY()` 使用“文件 ID + 源码行号”选择生成宏。移动它后重新构建即可，PHT 会生成新的宏名；
不要手工编辑 `Build/Generated` 下的文件。

## CMake 接入

项目目标通过 `pico_generate_reflection` 声明输入头、公开 include 路径、生成头路径与文件 ID。
生成 `.gen.cpp` 作为目标源码参与编译，生成目录作为 include 目录加入目标。项目模块因此只依赖运行时
引擎库，运行游戏时不需要启动 PHT。

## 验收

```powershell
cmake --build Build --config Debug --target PicoReflectionDemo PicoSandboxModule
ctest --test-dir Build -C Debug -R PicoHeaderToolTests --output-on-failure
.\Build\Debug\PicoReflectionDemo.exe
```

验收重点不是只看生成文件存在，而是确认原有序列化、`ProcessEvent`、委托和项目 Pawn 注册行为不变。
