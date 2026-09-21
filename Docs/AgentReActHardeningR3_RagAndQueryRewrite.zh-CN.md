# ReAct 加固 R3：分块检索、BM25 与低置信 Query Rewrite

## 目标

R3 在现有 Project Knowledge Store 上补齐轻量检索纵向切片，让 Agent 能以稳定 ID、类型化字段、实体和
Revision 找到真实项目证据，并在普通关键词召回不足时进行确定性的 Query Rewrite。该实现不引入向量数据库、
Embedding 服务或额外模型 Runtime，也不替换用户原始指令。

## 已完成

- 长文档按段落和 UTF-8 边界切分，每块最多 `4096` 字节；子块保留 ParentId、ChunkIndex 和 ChunkCount。
- Knowledge Record 新增 EntityIds、RevisionDomain 与类型化 Fields；旧版 `index.json` 缺失这些字段时仍可读取。
- Store 加载或更新后建立 Exact、Entity、Revision 和文档频率派生索引，不复制第二套事实数据库。
- 检索融合精确标识符、标题、路径、Tag、类型化字段、Entity 和 BM25，并在 Hit 中保留各项分数与 MatchedFields。
- Query 可携带当前 Revision；同域旧 Revision 在进入上下文前被过滤，避免历史事实覆盖当前 World、Asset、
  Gameplay、Graph、Reflection 或 Message Log 状态。
- 低置信检索才执行确定性 Query Rewrite。目前仅扩展 Pico 常用领域别名与资源路径 basename，不调用模型。
- Rewrite 只生成第二条检索查询；OriginalQuery、授权范围和用户目标保持不变。
- `bEnableBm25` 与 `bEnableQueryRewrite` 可独立关闭，关闭后回退到 RAG Lite 基线。
- Grounding JSON 升级为格式版本 2，包含原始/有效查询、置信度、Rewrite 状态、Chunk、Revision、Entity 和评分依据。
- AI Chat 的 `Grounding & Skills` 面板显示检索置信度、Rewrite 状态，以及每条证据的 Exact/BM25/Entity 分数。

## 运行链路

~~~text
Knowledge Source Snapshot
 -> UTF-8 Document Chunks
 -> Exact / Entity / Revision / Document Frequency Indices
 -> Exact Fields + BM25 + Entity Fusion
 -> Low Confidence?
      no  -> bounded cited evidence
      yes -> deterministic alias rewrite -> merge by stable record id
 -> Context Assembler -> ReAct
~~~

- Knowledge Store 仍是事实所有者；所有索引都能由 Snapshot 重建。
- Context Assembler 只消费检索出的有界候选，不拥有或执行检索。
- Harness、审批、Tool Provider 和 MCP 协议均未改变。
- 默认最多返回 8 条结果、Grounding 上下文最多 `12000` 字节；调用方可按任务缩小预算。

## 基准结果

固定 `Tests/Agent/Fixtures/RagBenchmark.json` 当前包含 7 条记录和 7 个查询，覆盖关键词、中文领域别名、精确实体、
来源过滤和安全来源排除。Debug 实测结果：

| 指标 | RAG Lite 基线 | R3 | 提升 |
| --- | ---: | ---: | ---: |
| Recall@3 | 0.8571 | 1.0000 | +0.1429 |
| MRR | 0.8571 | 1.0000 | +0.1429 |
| Forbidden Source Rate | 0 | 0 | 持平 |

- Rewrite Rate：`0.1429`，只命中一个低置信中文领域查询。
- 平均 Grounding Context：约 `949 B`。
- 本机 Debug 平均检索耗时：约 `0.188 ms`；该值用于回归观察，不作为跨机器性能承诺。

## 自动化验收

测试覆盖：

1. Knowledge Snapshot 兼容加载与审计幂等；
2. 机密 `Saved` 内容不进入索引；
3. 9 KB 文档被切块，末段稀有词仍能召回；
4. Grounding 严格受字节预算约束并提供 Citation；
5. Entity 精确命中且错误 Revision 被排除；
6. 高置信普通查询不触发 Rewrite；
7. 低置信中文查询经本地别名扩展命中目标，OriginalQuery 不变；
8. BM25/Rewrite Feature Flag 关闭后保留基线路径；
9. Benchmark 改善 Recall@3/MRR，且禁止来源泄漏率为 0。

Debug 与 Release 验收命令：

~~~powershell
cmake --build BuildCodex --config Debug --target PicoEditor PicoAgentTests PicoEditorTests -j 2
ctest --test-dir BuildCodex -C Debug --output-on-failure -R "^(PicoAgentTests|PicoEditorTests)$"
cmake --build BuildCodex --config Release --target PicoEditor PicoAgentTests PicoEditorTests -j 2
ctest --test-dir BuildCodex -C Release --output-on-failure -R "^(PicoAgentTests|PicoEditorTests)$"
~~~

## 可视化验收

1. 启动 `BuildCodex/Debug/PicoEditor.exe` 并打开 `Projects/PicoSandbox`。
2. 在 AI Chat 展开 `Grounding & Skills`，发送一个项目事实查询，例如“描述角色技能系统”。
3. 确认面板显示 `Retrieval confidence` 和 `rewrite yes/no`。
4. 发送“根据项目 README，说明 PicoSandbox 的 Development Stage 如何定位 Engine 和项目内容”。该文件位于
   ProjectRoot 内，应命中 `README.md` 的局部 Chunk，并显示 Exact、BM25、Entity 分数。
5. 再发送普通精确资源查询，确认显示 `rewrite no`，证明正常路径没有额外 Rewrite。

仓库根目录的 `Docs/` 属于 Engine 开发文档，不在打开项目的 Project Knowledge 默认采集范围内。若要查询 R3
本文档，应使用源码/文档工具，而不能把它作为 AI Chat 项目 RAG 的可视化验收用例。

## 后续

R4 将在同一 Store 中增加 Knowledge Kind、Episode/Entity 轻量视图、达到阈值才运行的摘要压缩和更完整的
Revision 失效策略。不会建立四套 Memory 数据库，也不会让 Memory 或 RAG 反向侵入 Harness。
