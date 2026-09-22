# Agent 资产理解与安全创作纵向切片

## 文档定位

本文档规划 Pico Agent 对 Mesh、Texture、Material 和场景 Actor 的理解、澄清、分步创作与安全修改链路。
它位于 [ReAct 轻量化加固门](AgentReActLightweightHardeningRoadmap.zh-CN.md) 之后、
[Agent 游戏制作链路](AgentGameCreationPipeline.zh-CN.md) 第 5～8 周之前，是后续玩法组装与 AI 视觉资产生产的前置门。

> 状态：**S0～S3 已完成，S4～S6 待实施**。安全写入、Material 工具、反射驱动的通用组件装配和人工语义元数据已经进入主线；标准预览和外部视觉 API 尚未开放。

本切片解决的不是“为奶牛写一个专用命令”，而是建立一条可复用链路，使 Agent 能够：

- 从项目中发现真实资产，并用确定性数据说明候选项的结构与适用范围；
- 面对“创建一只奶牛”“让材质更像金属”这类不完整需求时，先给出项目内可选项和推荐参数；
- 等用户确认 Mesh、材质方案、颜色、位置等语义选择后，再逐步执行；
- 创建或复制 Material，调整 PBR 参数，为 Actor 添加组件并绑定 Mesh/Material；
- 每一步均可预览、审批、读回验证、撤销和审计，失败时不破坏用户已有资产与场景；
- 在没有视觉模型时继续依靠结构化描述、人工元数据和预览完成基础工作。

## 当前基线与缺口

### 已有基础

- `editor.asset.search` 与 Catalog Descriptor 可发现项目资产；
- Mesh Descriptor 可提供几何规模、Bounds、材质槽等确定性事实；
- Texture Descriptor 可提供尺寸、通道、颜色空间等确定性事实；
- Material Descriptor 可提供颜色来源与 PBR 参数；
- 反射属性工具可读写 `StaticMeshAsset`、`MaterialAsset` 等已有组件属性；
- Build Plan、PlanHash、Tool Policy、审批、事务、Checkpoint、Artifact Handle 和 Unified Trace 已存在；
- World 与 Blueprint 已有保存、重开和静态验证基础。

### S0～S3 后仍需补齐

- 单资产详细描述、引用影响分析和人工语义元数据已完成；预览生成仍待 S4；
- 通用 Material 创建、复制和字段级更新已完成；Material Graph、更多贴图槽不属于本阶段；
- 通用组件发现、添加和删除已完成，可通过 Empty Actor 分步装配；Blueprint 默认组件持久化仍沿用现有 Blueprint 链路；
- Descriptor 中视觉语义仍可能为 `unknown`，Agent 不能只凭文件名可靠判断“奶牛”“陶瓷”“金属”；
- 缺少跨轮澄清状态，模型可能在用户尚未选定候选项时直接执行；
- 单步项目资产写入使用原子文件替换，World 写入使用 Undo 事务，Agent Run 使用 ChangeSet 留痕；跨资产与 World 的多步创作继续以分步 Checkpoint 恢复，不伪装成一个不可观察的大事务。

## S0～S3 交付记录

### S0 安全写入

- `editor.object.describe`、`editor.asset.describe` 和 `editor.material.describe` 返回基于当前内容计算的 Revision；新 Material 与组件写工具强制携带 `expected_revision`，旧版本写入在产生副作用前失败。
- `editor.material.update` 强制使用 `update_mask`，只更新明确列出的 Base Color、Metallic、Roughness 或 Base Color Texture；颜色与 PBR 标量限制在 `[0, 1]`，贴图必须是已注册 Texture。
- 创建和复制拒绝覆盖同名资产；共享 Material 默认拒绝原地更新，只有调用方显式传入 `allow_shared_update` 才能继续。
- 写结果包含 `operation_id`、Before/After、Changed Fields 和前后 Revision；Material 保存后重新加载验证，失败时恢复旧文件或删除本次半成品；组件写入由现有 World 事务和 Verifier 回滚。
- PlanHash 继续绑定完整工具参数，稳定 ToolCall ID 继续作为幂等键，审批、Operation Journal、Run ChangeSet 与 Unified Trace 复用现有 Harness，不新增第二套协议。

### S1 资产与 Material

已新增 `editor.asset.describe`、`editor.asset.find_references`、`editor.material.describe/create/duplicate/update`。资产描述同时返回确定性技术摘要、依赖、项目资产引用、活动 World 引用和内容 Revision；Material 修改可以先判断共享影响，再选择复制或显式修改共享资产。

### S2 组件与 Actor 装配

已新增 `editor.component.list_types/add/remove`。类型列表来自实时反射注册表，不维护 PointLight、StaticMesh 等硬编码白名单；添加组件使用显式 Actor 路径、类名、名称和可选父组件/Socket，删除组件禁止直接删除 Root，并要求对附着子树显式授权。每次增删都是独立 Undo Checkpoint，因此 Agent 可以按“创建 Empty Actor -> 添加组件 -> 设置反射属性 -> 保存 World”逐步执行和验收。

### S3 人工语义元数据与知识来源

- 每个正式资产可拥有同目录 `<asset>.pmeta.json` Sidecar，Schema v1 保存 Display Name、Description、Semantic Tags、Intended Use、Surface Tags、固定的 `user-confirmed` 来源以及对应资产内容 Revision；元数据自身另有独立 Revision。
- Content Browser 的 `Metadata` 入口提供字段编辑。保存前同时比较资产 Revision 与元数据 Revision，避免编辑窗口打开后被其他操作覆盖；保存采用临时文件发布并重新加载验证。
- `editor.asset.semantic_metadata.get/set` 向内置 AI Chat 和 MCP 外部 Agent 暴露同一份数据。写工具要求显式资产路径、双 Revision、字段更新掩码和正常的 `WriteProject` 审批，不允许把模型推断伪装成正式事实。
- `editor.asset.describe`、AssetDescriptor Catalog 与 Knowledge Store 会纳入人工元数据、来源、元数据 Revision 和过期状态，人工标签参与检索；视觉模型的推断缓存尚未实现，因此当前不存在推断自动进入正式知识视图的旁路。
- AssetService 在重命名、暂存删除、删除回滚和最终删除时同步处理 Sidecar；Material 复制会复制元数据并重新绑定目标资产 Revision。任一环节失败时恢复本次文件变化，避免孤立或串错资产的元数据。

### 自动化证据

- Editor Agent 工具总数由 34 增至 45；
- 覆盖 PointLight 反射发现/装配、陈旧 Revision 零副作用拒写、组件删除与 Undo；
- 在隔离项目中覆盖 Material 创建、读回、字段掩码更新、陈旧 Revision、复制、资产描述与引用查询；
- 覆盖语义元数据双 Revision、字段掩码、正式来源、Knowledge Store 摄取，以及复制、重命名、暂存删除回滚和最终删除；
- `PicoEditorTests`：`172 passed, 0 failed`；`Release PicoEditor` 完整构建通过。

## 固定架构决策

### 1. 只保留一套 Agent Harness

视觉模型不成为第二个 Agent，也不拥有规划、审批或写项目的权限。它只是一个可替换的只读分析 Provider：

```text
AI Chat / External Agent
          |
          v
现有 Agent Harness / Build Plan / Tool Policy
          |
          +--> editor.asset.describe
          +--> editor.asset.preview
                    |
                    v
             FAssetVisionService
                    |
                    v
             IAssetVisionProvider
              |- DeepSeek Vision
              |- OpenAI-compatible Vision
              `- Disabled / Manual fallback
```

Provider 只能返回带来源、模型版本、置信度和预览句柄的候选语义。它不能调用写工具，不能把推断直接提升为项目事实，
也不能绕过用户确认、PlanHash、审批、事务和验证器。

### 2. 运行时不依赖视觉服务

预览生成、视觉分析、编辑器语义元数据和外部 HTTP 调用位于 Editor/Developer 模块。打包 Runtime 只消费已经发布的
Mesh、Material、Texture 和 World 数据，不链接视觉 Provider，不携带 API Key，也不依赖网络服务。

### 3. 确定性事实、人工事实和模型推断分层存储

```text
正式资产文件
  -> Loader/Reflection 生成确定性 Descriptor

<asset>.pmeta.json
  -> 用户确认的名称、描述、标签和用途，可进入版本控制

Saved/DerivedData/AssetUnderstanding/<asset-hash>.json
  -> 视觉模型推断、置信度、证据句柄和模型版本，可失效、可重建
```

项目元数据建议包含：

```json
{
  "schema_version": 1,
  "display_name": "Ceramic Cow",
  "description": "A decorative cow prop using a glazed ceramic surface.",
  "semantic_tags": ["animal.cow", "prop.decorative"],
  "intended_use": ["environment", "display"],
  "surface_tags": ["ceramic", "glazed"],
  "provenance": "user-confirmed",
  "source_asset_revision": "<content-hash>"
}
```

AI 推断只写派生缓存。用户在 UI 中接受后，才把选中的字段提升到 `.pmeta.json`；拒绝或过期的结果不得进入 RAG 的
已验证事实视图。资产内容、导入设置或依赖 Revision 改变后，相关推断缓存必须失效。

## 安全第 0 阶段

任何新的写工具和外部视觉调用开始前，先完成以下公共保护：

1. **稳定目标。** 写操作必须携带显式资产路径或 Actor Stable ID；当前选择只用于提出候选，不能在执行时隐式决定目标。
2. **Revision 校验。** 参数包含 `expected_revision`；对象在规划后被用户或其他 Agent 修改时拒绝写入并重新规划。
3. **字段级更新。** Material 和元数据工具只修改 `update_mask` 指定字段，不允许用不完整对象覆盖整个资产。
4. **禁止静默覆盖。** 创建目标已存在时失败；覆盖、重命名、删除和替换引用必须单独审批。
5. **影响分析。** 修改共享 Material 前调用引用查询，向用户展示受影响的资产和 Actor，并提供“修改共享资产”或“复制后修改”。
6. **计划绑定。** PlanHash 绑定目标路径、Revision、字段更新、候选选择和预期副作用；参数变化后重新确认。
7. **幂等与预算。** 每个操作带 Idempotency Key，并限制单次创建/修改/删除数量，重试不得生成重复资产或组件。
8. **前后差异。** 写工具返回字段级 Before/After Diff、读写 Revision、Artifact Handle 和受影响引用摘要。
9. **读回验证。** Tool 返回成功不等于完成；重新加载资产或对象，检查路径、类型、参数、组件和引用是否符合后置条件。
10. **复合事务。** 项目资产、Blueprint 和 World 修改共享一次 Operation/Checkpoint；失败只回滚本次新增和本次字段修改，
    绝不删除执行前已经存在的用户内容。

目标域必须显式区分：

```text
Asset                  // Material、Mesh、Texture 等项目资产
ActorBlueprintDefaults // Blueprint 默认组件与默认值
WorldInstance          // 当前 World 中的实例覆盖
```

同名属性位于不同目标域时不能互相替代，也不能为了让 Play 看起来生效而偷偷修改更底层或更上层的数据。

## 工具契约

### 只读与语义工具

```text
editor.asset.describe
editor.asset.find_references
editor.asset.preview
editor.asset.semantic_metadata.get
editor.asset.semantic_metadata.set
```

- `describe` 汇总确定性 Descriptor、人工元数据、派生分析和 Revision，但明确标注每条事实的来源；
- `find_references` 返回直接引用、目标域和受影响实例，用于共享资产风险判断；
- `preview` 为 Mesh 生成前后左右和等距视图，为 Material 生成球体与平面预览，为 Texture 生成原图、通道和颜色统计；
- Preview 作为 Artifact 保存并返回 Handle，避免把大图塞进聊天上下文；
- `semantic_metadata.set` 只写用户明确接受的字段，使用 Revision 与字段更新掩码。

### Material 工具

```text
editor.material.describe
editor.material.create
editor.material.duplicate
editor.material.update
```

第一版只开放 Pico 已正式支持的 PBR 字段，如 Base Color、Metallic、Roughness、贴图槽与必要的颜色空间设置。
参数范围由 Schema 校验；工具不根据“陶瓷”“更金属”之类自然语言自行猜值，Agent 应先把语义转换为候选参数方案并请求确认。

### Actor 与组件工具

```text
editor.actor.spawn
editor.component.list_types
editor.component.add
editor.component.remove
editor.object.get_properties
editor.object.set_properties
editor.world.save
```

组件工具基于反射和 Capability Catalog 发现可用类型，不提供 `create_cow` 等项目专用 API。删除组件属于破坏性操作，
需要引用检查和单独审批；设置 Mesh/Material 继续复用通用反射属性工具。

## 澄清与分步执行

复杂创作任务采用显式任务状态：

```text
Discovering
 -> AwaitingUserChoice
 -> Proposed
 -> Confirmed
 -> ExecutingStep
 -> VerifyingStep
 -> Completed / Failed
```

例如用户提出“创建一只陶瓷材质的奶牛”时：

1. 搜索项目资产，读取多个 Cow Mesh 的 Descriptor、人工标签和预览；
2. 向用户展示真实候选项、外观差异、材质槽与可用性，不虚构项目中不存在的资源；
3. 询问使用已有 Material 还是新建 Material，并给出陶瓷 PBR 参数候选；
4. 确认 Actor 名称、位置、Mesh、材质方案和共享资产处理方式；
5. 生成绑定 Revision 和参数的 Build Plan，等待语义确认与工具审批；
6. 创建或复制 Material，读回验证；
7. 创建 Empty Actor，添加 StaticMeshComponent，绑定 Mesh，读回验证；
8. 绑定 Material，设置已确认的 Transform/Collision，读回验证；
9. 保存 World，重开或重新加载验证，输出最终差异和证据。

步骤之间保留 Checkpoint。用户可以在某一步停止、更换候选项或撤销本次操作，而不用重做已验证且仍满足 Revision 的步骤。
语义确认回答“是否是用户想要的东西”，Tool Approval 回答“是否允许产生这些副作用”，两者不能混为一次确认。

## 资产理解策略

资产理解按成本和可信度逐层升级：

1. **确定性结构。** 先读取几何、Bounds、材质槽、纹理尺寸、PBR 参数、组件和引用等正式数据；
2. **人工语义。** 使用 Description、Semantic Tags、Intended Use 和 Surface Tags；
3. **项目知识检索。** RAG 返回带来源与 Revision 的资产说明、Recipe 和历史确认事实；
4. **本地预览。** 生成统一视角、统一灯光、统一背景的可比较预览；
5. **可选视觉分析。** 用户同意上传预览后调用视觉 Provider，并展示 Provider、图片数量、用途和结果置信度；
6. **人工确认。** 低置信度、候选冲突或会触发写操作时由用户选择，模型不得替用户决定。

当前 DeepSeek Provider 可作为第一个实现，但接口不得含有 DeepSeek 专用字段。后续接入其他 OpenAI-compatible
视觉模型时只增加 Provider Adapter；现有 Provider 还需要支持多模态 `content` blocks，不能继续只发送字符串消息。
API Key 继续只保存在编辑器本地私有目录，并仅注入当前进程；不得写入项目资产、Trace、Prompt、配置导出或 Git。

## 编辑器交互

Content Browser 与 Details 增加轻量入口：

- Display Name、Description、Semantic Tags、Intended Use 与 Surface Tags；
- Generate Preview、Analyze with AI、Accept Analysis、Reject Analysis；
- Provider、模型版本、置信度、证据 Revision 和过期状态；
- Material 修改前的引用影响列表与“修改共享/复制后修改”选择。

AI Chat 使用结构化选择卡展示 Mesh 候选、Material 参数方案、目标域、影响范围、当前步骤和下一步。自由文本仍可用，
但实际执行只接受已经解析为稳定 ID、资产路径和版本化参数的选择结果。

## 权限与隐私

新增权限分层：

```text
ReadOnly
GeneratePreview
ExternalVision
WriteProject
ModifyWorld
Destructive
```

- 生成本地预览不等于允许上传图片；外部视觉分析单独授权；
- 上传前显示 Provider、图片数量和用途，默认不上传源工程文件；
- `.pmeta.json` 的复制、重命名和删除只能由 AssetService 随正式资产原子处理；
- Unified Trace 记录工具、目标、Revision、Diff 和证据句柄，不记录 API Key 或模型原始隐式推理；
- 外部 Agent、内置 AI Chat 和未来 Provider 使用同一 Tool Policy 与审批链。

## 七周实施计划

| 周次 | 任务 | 周末验收 |
| --- | --- | --- |
| S0 第 1 周（已完成） | 稳定目标、`expected_revision`、字段更新掩码、引用影响查询、禁止覆盖、PlanHash 参数绑定、幂等键与分步事务 | 陈旧版本、同名资产、共享 Material 和中途失败不会静默覆盖用户内容；写操作可读回验证并通过原子文件或 World Undo 恢复 |
| S1 第 2 周（已完成） | `asset.describe/find_references`、Material describe/create/duplicate/update 与统一字段 Diff | Agent 可解释材质当前状态；共享材质原地修改需要显式授权；创建和修改结果经过保存与重新加载验证 |
| S2 第 3 周（已完成） | 通用组件发现、添加/删除、Empty Actor 装配、目标域区分和分步 Checkpoint | 不增加项目专用工具即可发现并添加 PointLight、StaticMesh 等反射组件；每步是独立 Undo Checkpoint |
| S3 第 4 周（已完成） | `.pmeta.json` Schema、AssetService 生命周期、Content Browser 编辑、Knowledge Store 来源与 Revision | 用户可维护资产描述和标签；重命名/复制/删除资产时 Sidecar 不孤立；RAG 只把 `user-confirmed` Sidecar 作为正式人工事实，并保留来源、双 Revision 与过期状态 |
| S4 第 5 周 | Mesh/Material/Texture 标准化预览、Artifact Handle、缓存与失效 | 预览可在 UI 和 Agent 会话中查看；大图片不进入普通上下文；资产变化后旧预览与分析自动过期 |
| S5 第 6 周 | `IAssetVisionProvider`、DeepSeek 多模态 Adapter、隐私授权、置信度与人工接受/拒绝 | 视觉模型不可用时安全降级；未经授权零上传；推断不会自动写入正式元数据或修改项目 |
| S6 第 7 周 | 交互状态机、Asset Authoring Skill、真实 Editor Golden Tasks、回滚/迁移/性能验收 | 模糊需求必先澄清；“陶瓷奶牛”等端到端任务按步骤完成；错误分类、失败恢复和禁止副作用均有结构化证据 |

## Golden Tasks 与完成门槛

至少覆盖以下固定任务：

1. 项目中存在多个 Cow Mesh 时，Agent 必须展示候选并等待选择，不能按文件名擅自创建；
2. 修改被多个 Actor 引用的 Material 时，必须展示影响并询问修改共享资产还是复制；
3. “创建陶瓷奶牛”严格按 Material、组件、Mesh、Material、World Save 的依赖顺序执行并逐步验证；
4. 用户在确认前拒绝方案时，项目资产和 World 修改数均为零；
5. 视觉 Provider 不可用或未授权时，使用结构化 Descriptor、人工元数据和用户选择继续，不阻塞基础创作；
6. 视觉模型误判不能成为已验证事实，也不能直接触发写工具；
7. 任一步骤失败时只回滚本次操作，不影响执行前已有资产、组件或 Actor；
8. 重试相同 Idempotency Key 不产生重复 Material、组件或 Actor；
9. 外部 Agent 与内置 AI Chat 获得相同 Tool Result、审批、事务和验证行为；
10. 保存并重开项目后，资产元数据、Material 参数、组件绑定和 World 实例结果保持一致。

完成定义不是“模型说已完成”，而是：静态验证、重开验证、Golden Tasks、Trace 审计和禁止副作用断言全部通过。

## 与后续阶段的关系

- **阶段 A 第 5～8 周：** 复用本切片的资产发现、澄清、Actor 装配和安全写入能力，不再为每种玩法增加专用资产工具；
- **Render Architecture：** 标准预览先复用现有渲染路径，RenderGraph/RHI 完成后只替换 Preview Renderer 后端；
- **AI 视觉资产生产：** 本切片负责“理解和安全使用已有资产”，阶段 D 再增加 Texture/3D Producer；
- **Code Harness：** 若通用工具和 Graph 无法表达重复需求，未来以新的 Artifact Producer 接入同一 Plan、审批和验证链；
- **外部 Agent：** Codex、Claude Code 等只通过 MCP/Adapter 使用同一核心服务，不获得额外文件系统或项目写权限。

## 明确不做

- 不为奶牛、陶瓷或某个 Demo 增加名称匹配和隐藏硬编码；
- 不让视觉模型直接读取、改写项目二进制资产或执行工具；
- 不承诺仅凭截图准确理解任意 Mesh、Material 或复杂角色；
- 不在本切片生成高质量纹理、复杂 3D 模型、骨骼或动画；
- 不将模型推断、聊天内容或低置信度标签自动写入正式知识库；
- 不允许写工具隐式使用当前选择、静默覆盖同名资产或跨目标域修改。
