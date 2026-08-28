# 工程深度第 3 周：Object Index 与 Agent Capability Provider

## 本周目标

第 2 周基线证明，持续创建、查找、重命名和销毁对象时，Object Registry 的完整 Slot 扫描是当前最大热点。本周只增加一个有数据支撑的索引：

```text
(OuterHandle, FName) -> ObjectHandle
```

Agent 侧不增加新 Tool，而是把已有 Tool 从单体注册函数拆出明确的能力所有权，为后续独立测试、按 Revision 缓存和项目插件化建立边界。

## Object Name Index

稳定 Handle Slot Registry 仍负责对象所有权、Serial 与弱引用安全；Name Index 只是可重建的查询加速层，不成为第二份对象真相。

生命周期维护规则：

```text
Add       : 分配 Handle -> 放入 Slot -> 发布 Name Index -> PostInitProperties
Rename    : 先插入新 Key -> 校验旧 Key -> 删除旧 Key -> 修改对象名称
Destroy   : BeginDestroy 保持可查 -> 进入 Destroying -> 删除 Key -> 释放 Slot
GC Sweep  : BeginDestroy 全部不可达对象 -> 逐对象删除 Key -> 回收 Slot
Reset     : DestroyAllObjects 完成后同时清空 Slot、Free List 与 Name Index
```

`ValidateNameIndex()` 会双向检查：每个 Live Object 必须有唯一 Key，每个 Key 必须解析为 Serial 匹配的 Live Object。测试覆盖相同局部名的不同 Outer、Rename、Slot 复用、2000 次 churn、GC Sweep、序列化加载和 ObjectSystem 重启。

## Release Full 对比

第 2、3 周都使用 Release Full、5 个连续样本和相同规模契约：

| 100K Case | 第 2 周 P95 | 第 3 周 P95 | 提升 |
|---|---:|---:|---:|
| Create | 27.504 s | 140.873 ms | 195.2x |
| Find | 11.957 s | 65.831 ms | 181.6x |
| Rename | 28.335 s | 184.505 ms | 153.6x |
| Destroy | 9.056 s | 7.573 s | 1.2x |

完整 Full Benchmark 从约 371 秒降到 55 秒。原始结果位于：

- `Docs/Baselines/EngineeringDepthWeek02/ReleaseBuild/RuntimeBaseline.json`
- `Docs/Baselines/EngineeringDepthWeek03/ReleaseBuild/RuntimeBaseline.json`

Destroy 仍然昂贵，因为 `HasChildObjects` 为保证 Outer 生命周期规则仍会扫描 Live Slot，100K 逐个销毁形成近似二次增长。本周按范围约束不增加 Children/Class/Path 等次级索引；这项数据将作为未来 Object Hierarchy Index 的立项依据。

## Agent Capability Provider

新增 `IAgentCapabilityProvider`，Provider 对外提供：

- `FAgentToolDefinition`：名称、Schema、Handler、Preflight 和 Verifier；
- `CapabilityProvider`：明确 Tool 所属模块；
- `RevisionReadSet/RevisionWriteSet`：声明读取和修改的状态域；
- `FAgentKnowledgeRecord`：向 Project Knowledge Store 暴露可审计能力清单。

Editor 中现有 33 个 Tool 被完整分配给：

| Provider | 主要职责 |
|---|---|
| `WorldToolProvider` | World、Actor、Scene 创建与变更 |
| `ObjectToolProvider` | Selection、反射对象与属性 |
| `AssetToolProvider` | AssetRegistry 查询 |
| `BlueprintGraphToolProvider` | Graph 创建、连接、验证与编译 |
| `GameplayToolProvider` | ASC、Gameplay 配置与 Actor Blueprint Defaults |
| `ProjectProcessToolProvider` | Save、Play、Package、Project 与 ChangeSet |

Provider 以原子方式安装：任一 Tool 冲突或所有权错误都会回滚该 Provider 本次已注册的定义。Registry 继续统一负责 Schema、Permission、Approval、Transaction、Execute 和 Verify，不把安全逻辑复制到六个模块。

现有 Tool Name、参数 Schema、Session 事件和 Golden Task 协议保持兼容。通用 Editor Tool 实现已移除 `PSandbox*` 类名：Mini GAS Ability 通过 `Ability.Projectile.*` GameplayTag 查找，不再由 Editor 硬编码项目 C++ 类。

## 验收结果

- Release Object Tests：179/179；
- Release GC Tests：41/41；
- Release Engine/序列化 Tests：691/691；
- Release Agent Tests：106/106；
- Debug/Release Editor Tests：142/142；
- Debug/Release Sandbox Tests：58/58；
- Debug/Release PicoEditor 构建通过；
- Editor Tool Catalog 保持 33 个，12 个 Golden Tasks 继续通过；
- `Source/Editor/PicoEditor/Private/EditorAgentTools.cpp` 中不存在 `PSandbox` 类名。

## 后续输入

1. 第 4 周 Tick Cache 应使用 Generation，而不是每帧重新构建依赖顺序。
2. Revision Read/Write Set 目前是可审计元数据；后续可用于细粒度 Tool Cache 失效，不应立即替换现有全局 StateRevision。
3. Destroy 热点已排入总路线的“第 4 周后置门：Object Hierarchy Index”：单独评估 Children Index 的内存和维护
   成本，在进入第 5 周前依据 100K Destroy 基准完成实现或形成有数据支撑的否决结论。
4. 六个 Provider 已有所有权边界，但 Tool 构造代码仍集中在一个 `.cpp`；只有当独立变化频率和编译成本证明有价值时，再物理拆文件。
