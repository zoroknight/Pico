# 第 4 项目月第 1 周：Game Thread、Tick 与 PGameInstance

本周为 Gameplay Framework 建立时序和生命周期地基。实现保留 UE 的核心思想，但第一版仍是可读的
单线程模型，不引入 TaskGraph、并行 Tick 或完整 `FWorldContext`。

## 1. Game Thread 边界

`PObjectSystem::Init` 把调用线程登记为 Game Thread，Shutdown 时释放身份。运行期可以使用：

```cpp
Pico::IsInGameThread();
Pico::CheckGameThread("OperationName");
```

`NewObject`、对象销毁、句柄解析、GC 请求与执行、反射属性写入和 TickFunction 注册/执行都检查该
身份。Worker 可以处理文件、网络包和其他纯数据，但不能直接改 `PObject` 对象图；未来 Dispatcher
会把结果投递回 Game Thread。

## 2. Tick 调度

`PWorld` 不再直接遍历 Actor 调用 Tick，而是拥有一个 `FTickTaskManager`：

```text
World::BeginPlay
  -> Actor PrimaryActorTick 注册
  -> Component PrimaryComponentTick 注册
World::Tick
  -> PrePhysics
  -> DuringPhysics
  -> PostPhysics
  -> PostUpdateWork
```

Actor 默认拥有可执行的 `PrimaryActorTick`。Component 默认不 Tick；需要在构造函数中启用：

```cpp
PrimaryComponentTick.SetCanEverTick(true);
PrimaryComponentTick.SetStartWithTickEnabled(true);
PrimaryComponentTick.SetTickGroup(Pico::ETickGroup::PrePhysics);
PrimaryComponentTick.SetTickInterval(0.1f);
```

两个已经注册到同一 World 的 TickFunction 可以建立依赖：

```cpp
MovementTick.AddPrerequisite(ControllerTick);
```

同组依赖使用稳定拓扑排序；跨组顺序由 TickGroup 保证。循环依赖会被诊断，剩余节点仍只执行一次，
避免卡死。Tick 中注册的新函数从下一帧开始执行；对象在 Tick 中销毁时按注册 ID 重新查找，避免继续
使用已经失效的 TickFunction 地址。

这是 UE `FTickFunction/FTickTaskManager` 的学习版。它解决“谁先 Tick”和“对象销毁是否安全”，并不
代表多线程；未来物理可放在 DuringPhysics，Movement 可通过 prerequisite 明确依赖 Controller 输入。

## 3. PGameInstance 生命周期

旧 `FGameInstance` 是 `unique_ptr` 管理的普通 C++ 类。现在 `PGameInstance` 继承 `PObject`，Game Module
返回 GameInstance 的 `PClass`，`FGameEngine` 通过统一对象构造链创建并以 RootSet 保活：

```text
EngineLoop::Init
  -> GameModule::StartupModule
  -> NewObject(GameInstanceClass)
  -> PGameInstance::Init
  -> LoadMap
  -> PGameInstance::OnWorldInitialized
  -> Tick...
  -> PGameInstance::OnWorldCleanup
  -> PGameInstance::Shutdown
  -> DestroyObject(GameInstance)
  -> EngineLoop::Exit
  -> GameModule::ShutdownModule
```

GameInstance 跨地图存活，但只保存当前 World 的代数句柄，不拥有 World。地图文件先解析成纯数据；只有
数据有效并准备替换时才发送 cleanup。替换失败会把旧 World 重新通知为 initialized，成功后则绑定新
World。Sandbox 的 `PSandboxGameInstance` 已迁移到这条链路。

## 4. 验收覆盖

`PicoEngineTests` 覆盖 Game Thread 拒绝后台对象创建、TickGroup 顺序、prerequisite、interval、启停、
Component Tick 以及 Actor 销毁后的注销。`PicoGameTests` 覆盖反射类构造、RootSet 经完整 GC 后存活、
地图替换通知和关闭顺序。完整 Debug/Release CTest 仍是阶段最终门槛。

## 5. 暂不实现

- UE TaskGraph、并行 Tick、Tick dependency 跨线程调度。
- 完整 `FWorldContext`、PIE 多 World Context 和 GameInstance Subsystem。
- 动态修改 `bCanEverTick` 后的自动注册；第一版应在构造或 BeginPlay 前配置。
- Controller、GameMode、PlayerState 与 Possession；这些属于第 2～3 周。
