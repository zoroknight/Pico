# Agent 游戏搭建第 1～4 周：结构化规划与可恢复执行底座

## 状态

已完成。本文记录阶段 A 前四周的实际实现、验证证据和后续边界。总体目标与后四周安排见
[Agent 游戏制作链路规划](AgentGameCreationPipeline.zh-CN.md)。

本阶段完成的是“理解项目 -> 判断支持度 -> 生成可审批计划 -> 在暂存区可恢复执行”的工程底座。双角色、机关、
胜利条件等真实 Gameplay Producer 与三进程验收仍属于第 5～8 周，当前不宣称已经完成端到端游戏生成。

## 第 1 周：AssetDescriptor 与 Capability Catalog

- 新增版本化 `FAgentAssetDescriptor`，统一记录资产类型、稳定路径、修订、大小、依赖、Tag、证据句柄、结构摘要、
  来源和验证器。
- 新增 `FAgentCapabilityDescriptor` 与 `FAgentCapabilityCatalog`，能力带稳定 ID、版本、Producer、前置资产类型、
  Authority、副作用、审批和 Verifier。
- 编辑器新增只读工具 `editor.asset.describe_catalog`。它从实时 `AssetRegistry`、资产依赖服务和当前 World 生成描述器，
  不让模型直接猜测二进制资产内容。
- 描述器通过正式资产加载器生成类型化技术摘要：Material 返回颜色来源、PBR 参数与当前不支持的法线/高度/顶点位移能力；
  Texture 返回尺寸、RGBA8 通道和透明度使用；Mesh、Skeleton、Animation、CharacterProfile、ControlProfile、
  Actor Blueprint、PicoGraph 与 World 分别返回各自可验证的结构计数和关键配置。读取失败显式返回
  `inspection_status=unreadable`，不会用文件名或模型推断补值。
- Actor Blueprint 依赖通过正式蓝图解析器从 Actor/Component 默认属性中提取，覆盖 Mesh、AnimationSet、
  CharacterProfile、PicoGraph 等合法资产路径，而不是扫描文本猜引用。
- Knowledge Store 的资产事实改为 `asset-descriptor` 来源，保留来源与依赖信息。

## 第 2 周：Recipe、PicoGameSpec 与支持度诊断

- 新增版本化 `FAgentGameplayRecipe`、`FAgentGameSpec` 和 Requirement 数据契约。
- Requirement Parser 对同一输入确定性生成相同 Spec ID；首版覆盖玩家、双人联网、差异化能力、收集、门、机关、
  共享胜利和 Windows Package 等阶段 A 需求。
- 支持度诊断输出 `Supported`、`MissingDependency` 或 `Unsupported`，并列出候选 Capability、Recipe 和缺失资产类型。
- 多个候选能力按“任一完整候选即可满足”处理；Recipe 会展开到真实 Capability，避免可执行候选被另一个缺资产候选误判。

## 第 3 周：Build Plan、Dry Run 与 PlanHash

- 新增 Build Plan DAG、拓扑排序、未知依赖/自依赖/循环/重复幂等键校验。
- Dry Run 汇总确定性执行顺序、Producer、副作用和预期 Artifact 类型。
- 计划采用确定性 `PlanHash`；执行时批准 Hash 必须与当前计划一致，计划变更后旧批准失效。
- 每个步骤具有独立 `IdempotencyKey`。Producer 只能写入分配的暂存目录，项目发布统一由
  `IAgentBuildPlanTransaction::Commit` 完成；失败执行 `Rollback`。

## 第 4 周：Checkpoint、Artifact Handle 与恢复

- 新增内容寻址 `FAgentArtifactStore`。大结果以 `artifact:<hash>` 句柄引用，内容原子写入，聊天上下文无需承载完整数据。
- 新增原子保存的 `FAgentAssemblyCheckpointStore`，记录 PlanHash、已完成步骤、幂等键、Artifact 和 Agent Counter。
- 恢复只接受相同 PlanHash；已完成幂等步骤不会重复产生副作用，失败后保留最后一个已验证边界。
- 复用现有 Harness 的失败分类、上下文预算、重复调用停止、Journal 和结构化 Tool Result，不另建第二套 Agent Runtime。

## 关键代码

- `Source/Developer/Agent/Public/Pico/Agent/AgentGameAssembly.h`
- `Source/Developer/Agent/Private/AgentGameAssembly.cpp`
- `Source/Editor/PicoEditor/Private/EditorAgentTools.cpp`
- `Tests/Agent/Private/Main.cpp`
- `Tests/Editor/Private/Main.cpp`

## 自动化验收

- `PicoAgentTests`：169 项通过，覆盖 Descriptor、Catalog、Recipe、确定性 Spec、支持度诊断、DAG、Dry Run、
  PlanHash、循环拒绝、Artifact、Checkpoint、断点恢复、幂等和错误 Hash 拒绝。
- `PicoEditorTests`：161 项通过，覆盖实时资产目录工具、World 来源、依赖和既有编辑器事务链路。
- `PicoEditorAssetTests`：33 项通过，覆盖 Actor Blueprint 默认属性中的结构化资产依赖提取。
- `PicoMcpAdapterTests`：25 项通过，确认外部 Agent 仍经稳定 Toolset、审批、事务和结构化结果访问编辑器能力。

## 后续入口

第 5 周开始把真实 Gameplay 积木注册为 Capability 和 Recipe，并实现
`ExistingAssetProducer`、`ActorBlueprintProducer`、`PicoGraphProducer` 与 `WorldProducer`。本阶段接口已经为后续
`TextureProducer`、`External3DProducer` 和受控 `CodeModuleProducer` 保留扩展点，但这些 Producer 当前均未启用。
