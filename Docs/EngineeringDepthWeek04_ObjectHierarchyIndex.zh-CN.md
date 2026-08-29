# 工程深度第 4 周后置门：Object Hierarchy Index

## 问题与证据

第 3 周 Object Name Index 将 100K Create、Find 和 Rename 的 P95 分别提升约 195.2x、181.6x 和 153.6x，
但 Destroy P95 仍为 `7.573 s`。Profiler 与代码路径共同指向 `HasChildObjects`：每销毁一个对象都会扫描完整
Live Slot，以确认它是否拥有直接子对象；100K 个平铺对象逐个销毁时，即使全部没有子节点，也形成近似二次工作量。

稳定 Slot Registry 必须继续负责所有权、Serial 和 Handle 安全，因此优化不能用另一份对象列表替代 Registry。

## 实现边界

新增可校验的加速层：

```text
OuterHandle -> unordered_set<ChildHandle>
```

生命周期顺序为：

```text
Add
 -> 分配稳定 Handle 并放入 Slot
 -> 发布 Name Index
 -> 发布 Hierarchy Relation
 -> PostInitProperties

Destroy / GC Sweep
 -> 确认 Child Set 为空或按深度处理
 -> BeginDestroy（对象和 Handle 仍可解析）
 -> 进入 Destroying
 -> 删除 Name Index
 -> 删除 Child Relation 和空 Parent Entry
 -> 清空 Handle 并释放 Slot
```

`DestroyObjectTree` 从 Child Set 复制 Handle 快照，再按 Serial 排序递归。这样 BeginDestroy 内销毁兄弟对象时，
旧 Handle 会安全失效，不会解引用悬空指针。`DestroyAllObjects` 从一次初始叶节点扫描开始；每销毁一个 Child 后，
若其 Outer 变成叶节点，就将 Parent Handle 加入队列，避免深链每层重新扫描全部 Slot。

Pico 当前没有运行时 Reparent/SetOuter API，Outer 只在构造时写入。未来若增加 Reparent，必须先设计 Name Key 与
Hierarchy Relation 的原子迁移和失败回滚，本次没有为了满足测试清单增加无实际调用方的接口。

## 一致性与统计

`ValidateHierarchyIndex` 进行双向检查：

- 每个有 Outer 的 Live Object 必须存在于对应 Parent 的 Child Set；
- 每个 Parent Handle 必须仍能解析，并且集合不能为空；
- 每个 Child Handle 必须仍能解析，且 `Child->GetOuter()` 与 Parent 一致；
- 索引关系总数必须等于 Live Registry 中有 Outer 的对象数。

`GetHierarchyIndexStats` 返回 Parent Entry、Child Relation 和结构存储估算。存储估算包含 bucket、容器对象和
`FObjectHandle`，不包含标准库节点分配器的隐藏头部，不能当作进程 RSS。

## Release Full 数据

配置：Release、5 个连续样本、100K Object，与第 3 周相同平铺 Destroy Case。

| Case | 第 3 周 P50 | 第 3 周 P95 | 本次 P50 | 本次 P95 | P95 约提升 |
|---|---:|---:|---:|---:|---:|
| Destroy | 7100.452 ms | 7573.463 ms | 33.865 ms | 107.531 ms | 70.4x |

新增的层级维护 Case：

| 100K Case | P50 | P95 | Max |
|---|---:|---:|---:|
| HierarchyCreate | 128.412 ms | 180.959 ms | 180.959 ms |
| HierarchyDestroy | 48.375 ms | 172.380 ms | 172.380 ms |

100K 直接子关系形成一个 Parent Entry，估算结构存储 `1,848,648 bytes`，约 `18.5 bytes/relation`。同次
基准的平铺 Create P95 为 `143.098 ms`；HierarchyCreate P95 高约 26.5%，其中同时包含 Child Set 哈希维护和
所有对象共享同一 Outer 的 Name Key 工作，不把差值全部归因于 Hierarchy Index。

原始报告：

- `Docs/Baselines/EngineeringDepthWeek03/ReleaseBuild/RuntimeBaseline.json`
- `Docs/Baselines/EngineeringDepthWeek04HierarchyIndex/ReleaseBuild/RuntimeBaseline.json`

本次基准增加 `--no-trace`，只禁止生成数百 MB 的逐事件 Chrome Trace；Profiler 聚合、P50/P95/Max、规模、样本数
和 JSON/CSV 报告保持不变。

## 正确性验收

- Object `185/185`、GarbageCollection `42/42`；
- 测试覆盖父对象销毁拒绝、递归树销毁、Slot Serial 复用、PostInit 回滚、BeginDestroy 重入、GC Sweep、
  Shutdown、Registry Reset 和 Object System 重启；
- Debug/Release 完整 22 目标测试矩阵全部通过；
- Engine、Replication、Movement、Physics、GAS、Graph、Agent、Editor、Packaging 和 Sandbox 均无回归。

## 结论

该索引以约 18.5 bytes/relation 的可见结构成本，将当前 100K Destroy P95 降低约 70.4x，收益足以覆盖局部
复杂度，因此保留实现。Slot Registry 仍是唯一生命周期事实来源，Hierarchy Index 只负责回答直接父子查询，
不继续扩张为 Class、Tag 或 Path Index。
