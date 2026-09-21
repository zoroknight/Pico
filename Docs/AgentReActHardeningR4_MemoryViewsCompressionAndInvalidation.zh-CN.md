# ReAct 加固 R4：Memory 视图、阈值压缩与 Revision 失效

## 目标

R4 在同一个 Project Knowledge Store 中加入逻辑 Memory 分类、可重建的 Episode/Entity 视图、默认 Revision
失效和阈值式摘要压缩。它用于减少重启后的上下文丢失与旧事实污染，同时保持普通查询不增加模型调用，且不建立
四套物理数据库。

## 已完成

- Knowledge Record 新增 `Kind`：`Semantic`、`Episode`、`Procedure`、`Entity`。
- 旧版 `index.json` 没有 Kind 时按 `Semantic` 兼容读取；保存后持久化明确 Kind。
- 项目文档属于 Semantic；工具、PicoGraph、反射和 Gameplay Schema 属于 Procedure。
- World、Asset、Selection 和运行时 GAS 状态属于 Entity，并保留稳定对象路径、EntityIds 和 RevisionDomain。
- Selection Entity 保存完整多选对象列表、Primary Object、SelectionCount 和单调 Selection Revision，不再只投影
  Primary Object。
- 编辑器警告/错误与最近 8 个持久化 Agent Session 的有界消息投影属于 Episode。
- Episode 原始所有者仍是 Session Event Log；Knowledge Store 只保存可重建投影，不复制第二套会话数据库。
- `GetKindView`、`GetViewStats` 和 `BuildMemoryViewsJson` 从同一 Record 集合派生视图。
- Entity View 默认只返回每个 Revision Domain 的最新记录；调用方也可传入精确 Revision 快照查看特定版本。
- 普通知识查询默认排除旧 Revision，并报告 `stale_records_excluded`；可通过 Query Flag 关闭。
- 证据内容总量超过 `8192 B` 时，从低排名证据开始生成确定性摘要，默认每条最多 `768 B`。
- 压缩不会调用模型，也不会修改 Store 原文；Grounding 保存 Full/Summary 模式、原始大小和压缩统计。
- 摘要压缩可独立关闭；低于阈值时完全不运行。
- AI Chat 的 `Grounding & Skills` 显示四类 Memory 数量、Entity 数量、Stale 数量及本次压缩前后字节。
- ToolCall 幂等结果按新用户回合隔离：同一回合的重复 ID 继续零副作用复用；Provider 在下一用户回合复用 ID 时会
  重新读取当前编辑器状态；空 Prompt 的中断恢复仍可读取先前持久化结果。

## 所有权与数据流

~~~text
Project files -------------------------------> Semantic
Session Event Log / Message Log --bounded--> Episode
Tool / Graph / Reflection / GAS schema ------> Procedure
World / Asset / Selection / Runtime GAS -----> Entity
                            |
                            v
                 one Knowledge Record store
                            |
             Kind View + Revision Active View
                            |
       exact / BM25 / entity retrieval from R3
                            |
       threshold exceeded? deterministic summary
                            |
                   bounded cited context
~~~

- Session、World、Asset Registry 和 Schema Registry 仍是权威来源。
- Knowledge Store 只拥有检索 Snapshot、审计记录与派生索引。
- Context Assembler 只装配检索结果，不拥有 Memory，也不执行摘要模型。
- Harness 不依赖 Kind、Episode、Entity、压缩或 RAG。

## Revision 失效

每个带 RevisionDomain 的 Record 都进入 Domain/Revision 派生索引。查询行为：

1. 调用方传入 Revision 快照时，只允许精确匹配该版本的记录；
2. 未传入时，默认使用 Store 中该 Domain 的最新 Revision；
3. 旧记录保留在审计与显式历史视图中，但不会进入默认模型上下文；
4. `bExcludeStaleRevisions=false` 可恢复 R3 兼容行为。

Revision 失效优先于纯时间衰减。有效但很久未修改的资产事实不会因为“年龄大”被删除；资源版本变化才使旧事实退出
默认上下文。

## 阈值式摘要压缩

- 默认阈值：所有候选证据正文合计 `8192 B`。
- 默认单条摘要上限：`768 B`。
- 压缩顺序：从低排名证据向高排名证据处理，尽量保留最相关证据的全文。
- 摘要方式：UTF-8 安全的确定性前缀提取并标记 `[summary truncated]`。
- Citation、来源、Kind、Entity、Revision、ContentHash 和评分元数据始终保留。
- `bEnableSummaryCompression=false` 时原文进入既有 `MaxContextBytes` 硬预算路径。

当前不使用模型摘要，因为它会增加延迟、成本与不可重放性。R5 只有在 Eval 证明确定性摘要不足时，才允许评估一次性
模型 Reflection/Compression，并且仍需独立开关。

## 自动化验收

测试覆盖：

1. Kind 保存、重载与旧格式默认值；
2. Entity Query 同时支持显式 Revision 和默认最新 Revision；
3. 旧 Entity 保留在 Stale 统计中但不进入默认 Entity View；
4. Episode 从持久化 Store 重载后仍可查询；
5. 四类 Memory View 来自同一 Store；
6. 超阈值时压缩并报告压缩前后字节；
7. 关闭压缩后正文大小不变；
8. R3 召回、Rewrite、Source Safety 和旧 `index.json` 路径不回归。
9. 单选切换为三对象多选后，Selection Entity 包含三个对象与新 Revision；
10. 两个用户回合复用相同 ToolCall ID 时，第二回合重新执行只读工具，而同一回合内仍保持幂等。

~~~powershell
cmake --build BuildCodex --config Debug --target PicoEditor PicoAgentTests PicoEditorTests -j 2
ctest --test-dir BuildCodex -C Debug --output-on-failure -R "^(PicoAgentTests|PicoEditorTests)$"
cmake --build BuildCodex --config Release --target PicoEditor PicoAgentTests PicoEditorTests -j 2
ctest --test-dir BuildCodex -C Release --output-on-failure -R "^(PicoAgentTests|PicoEditorTests)$"
~~~

## 可视化验收

1. 启动 `BuildCodex/Release/PicoEditor.exe` 并打开 PicoSandbox。
2. 在同一会话先询问当前 World，再发送第二个相关问题。
3. 展开 `Grounding & Skills`，确认 `Memory S / Ep / P / En` 都有清晰计数；第二轮后 Episode 应大于 0。
4. 查询当前选中 Actor，命中记录应标记 `entity`，且 Entity 数量反映稳定对象路径。
5. 使用宽泛工具查询召回多条 Tool Schema；正文超过阈值时应显示 `Compression yes`、压缩条数和下降后的字节数。
6. 修改并保存 World 后再次查询；旧 World Revision 不应进入 Evidence，`stale filtered` 在存在历史候选时增加。
7. 关闭再打开编辑器并继续原会话；Episode 视图和会话上下文应从本地 Event Log 恢复。

## 明确边界

- 没有四套 Memory 数据库或独立 Memory Service；
- 没有向量数据库、Embedding、多路召回或固定模型摘要；
- 没有按时间自动删除有效项目事实；
- Episode 不是完整聊天记录副本，只是最近会话的有界派生投影；
- Entity View 不替代 Asset Registry、World 或对象反射系统；
- Harness 的审批、事务、验证和回滚路径没有变化。

## 后续

R5 将增加条件式 Reflection、有限恢复阶梯、真实 Editor/Play/双客户端/Package Eval 和性能回归门。Reflection 只在
后置条件失败、振荡或最终证据不足时最多触发一次，不成为每轮固定步骤。
