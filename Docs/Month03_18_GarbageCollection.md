# Month 03.18: Stop-the-world Mark-Sweep GC

## 目标

本阶段实现 UE GC 主链的可读单线程版本：全局对象表、Root Set、反射引用描述、
`AddReferencedObjects`、可达性分析、不可达对象收集、`BeginDestroy` 与立即 Purge。

```text
Root Set
  -> reflected strong TObjectPtr properties
  -> AddReferencedObjects native references
  -> Outer
  -> mark reachable
  -> gather unreachable
  -> child-first BeginDestroy
  -> registry slot purge
```

## 引用语义

`TObjectPtr<T>` 与 `TWeakObjectPtr<T>` 都保存代次安全的 `FObjectHandle`，不拥有对象内存。
Registry 仍是运行时对象的唯一内存所有者。

```cpp
PPROPERTY(Transient, NotSerializable)
TObjectPtr<PTarget> StrongTarget;

PPROPERTY(Transient, NotSerializable)
TWeakObjectPtr<PTarget> WeakTarget;
```

`TObjectPtr` 只有进入 `PPROPERTY` 或由 `AddReferencedObjects` 上报时才对 GC 可见；普通未上报的
包装器不会被扫描。`TWeakObjectPtr` 永不参与 Mark，目标回收后通过 Serial 校验解析为 `nullptr`。

稳定对象引用序列化和加载后的引用修复安排在阶段 H，所以第一版对象引用属性必须同时使用
`Transient` 与 `NotSerializable`。

## 原生引用

无法用单个反射属性描述的容器通过虚函数上报：

```cpp
void PLevel::AddReferencedObjects(FReferenceCollector& Collector) const
{
    PObject::AddReferencedObjects(Collector);
    Collector.AddReferencedHandles(ActorHandles);
}
```

当前引擎强引用链为：

```text
Active World Root
  -> Levels
  -> Actors
  -> Components
```

`PObject` 默认上报 `Outer`，方向为 `Child -> Outer`。Outer 不会自动反向保持所有 Inner 存活；
拥有集合仍必须显式上报。

## 收集过程

`CollectGarbage` 在一次 Stop-the-world 临界区中完成：

1. 拒绝对象创建、显式销毁、重命名和 Root 修改。
2. 扫描 Registry 中的 `RootSet` 对象。
3. 使用工作栈遍历反射强引用、原生引用和 Outer。
4. 按 Outer 深度对子对象优先排列不可达对象。
5. 调用一次 `BeginDestroy`，释放 Registry Slot 并使旧 Handle 失效。

第一版不实现并行 Mark、增量 Reachability、GC Barrier、Cluster 和分帧 FinishDestroy/Purge。
Gameplay 的 `Actor::Destroy` 与 `DestroyObjectTree` 继续保留，不等待 GC 才体现游戏语义上的死亡。

## 调度与安全点

`RequestGarbageCollection` 只记录请求，不会在调用位置立即销毁对象。多个来源会以位标记合并：

- `Explicit`：工具、测试或运行时系统主动请求。
- `TimeLimit`：达到配置的周期。
- `WorldTransition`：活动 World 替换完成。
- `EngineExit`：对象系统关闭前的最后清理。

`FEngineLoop` 在 World Tick 完成且 `bTickingWorld == false` 后执行
`CollectGarbageIfRequested`。这样 Actor 和 Component 不会在遍历过程中被 Sweep。项目配置中的
`[Engine] GarbageCollectionIntervalSeconds=60` 控制定时请求；设置为 `0` 会关闭定时请求，但不影响
显式、World 切换和退出请求。

World 切换和 Engine Exit 本身也是安全位置，因此会立即消费刚提交的请求。请求成功执行后会清空
所有已合并原因并重置时间累计。当前调度器与 GC 一样限定在游戏线程，不提供跨线程请求协议。

## CDO边界

Pico CDO 和默认子对象模板仍由 `PClass` 的 `unique_ptr` 独立拥有，不在运行时 Registry 中，
因此不参与普通 Sweep。这是与 UE 统一 UObject Array 的明确简化，不影响普通实例的 Mark-Sweep 原理。

## 验收

```powershell
cmake --build Build --config Debug --target PicoGarbageCollectionTests PicoInspector
.\Build\Debug\PicoGarbageCollectionTests.exe
.\Build\Debug\PicoInspector.exe
```

专项测试覆盖 Root、强引用、弱引用、原生引用、循环、Outer 方向、Handle 重用、CDO 边界、请求合并、
安全点消费和 World/Level/Actor/Component 对象图。Inspector 的 `Garbage Collection` 实验可观察
同一 Mark-Sweep 行为。
