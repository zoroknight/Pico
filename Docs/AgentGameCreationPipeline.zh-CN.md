# Pico Agent 游戏制作链路规划

## 文档定位

本文档是 Pico 在**暂不引入代码生成 Harness**的前提下，让 Agent 组装简单 3D 游戏的权威实施方案。
总体月份优先级仍以 [AI-First Development Roadmap](Pico_AI_First_Development_Roadmap.zh-CN.md) 为准；
涉及第 10 月 Agent 游戏搭建的范围、架构、周计划和验收标准时，以本文档为准。

当前目标不是让模型自由创造任意游戏，也不是让模型直接修改引擎源码。目标是在 Pico 已有反射、Data-Only
Actor Blueprint、PicoGraph、GAS、Replication、编辑器事务、Play 和 Package 链路上，让 Agent 可靠组装以下
受控类型的游戏：

- 第三人称移动、跳跃和交互；
- 收集、机关、门、胜利条件和简单 HUD；
- 使用既有 GAS Ability/Effect 的简单战斗；
- 两人联机目标和服务器权威结算；
- 保存、Play 验证和 Windows 打包。

## 核心原则

1. **模型负责理解和规划，系统负责权限与正确性。** Assistant 不能以文字宣布完成，必须由确定性验证器判定。
2. **能力来自可发现契约，不来自 Prompt 猜测。** 反射元数据、Graph Schema、资产和 Recipe 共同描述可用积木。
3. **先组合，后生成代码。** 当前只允许修改项目数据、Blueprint、Graph 和 World；未来代码 Harness 作为新的产物生产器接入。
4. **服务器权威不能被 Graph 绕过。** 收集、开门、伤害和胜利结算必须声明并验证 Authority/Ownership。
5. **每次副作用都可预览、审批、撤销和审计。** 批量操作应幂等，并拥有后置条件和事务边界。
6. **可运行不等于已验收。** 静态检查、三进程场景测试、打包检查和证据报告缺一不可。

## 完整链路

```text
用户自然语言需求
 -> Requirement Parser
 -> PicoGameSpec
 -> Capability Catalog
 -> Recipe Resolver
 -> Build Plan
 -> 用户审批（绑定 Plan Hash）
 -> Authoring Tools / Artifact Producers
 -> Actor Blueprint + PicoGraph + World
 -> Static Validator
 -> Runtime Scenario Runner
 -> 最多两轮定向修复
 -> Package Validator
 -> Evidence Report
```

### 1. PicoGameSpec

`PicoGameSpec` 是版本化、确定性的 JSON 需求，不直接包含 Tool Call。至少描述：

- 玩家数量、视角、移动方式和输入；
- 网络模式与权威要求；
- 场景区域和所需资产；
- 目标、交互、失败/胜利条件；
- HUD 需要显示的状态；
- Play、联网和 Package 验收条件。

Requirement Parser 最多提出一至两个真正阻塞的问题。能力范围以外的需求必须在执行前列为
`UnsupportedRequirements`，不能用相似但错误的功能代替。

### 2. Capability Catalog

Capability Catalog 是 Agent 能使用什么的统一目录，由以下来源构建：

- `PClass/PProperty/PFunction` 反射注册表；
- Component、Actor Blueprint 和默认子对象模板；
- PicoGraph 节点 Schema；
- GAS Ability、Effect、Tag 和 Attribute Schema；
- AssetRegistry、输入动作、网络能力、Tool 和 Validator；
- 人工维护的 Gameplay Recipe。

每项能力至少包含稳定 ID、版本、Tag、输入/输出、前置条件、冲突项、Authority/Ownership、可能副作用、
所需审批以及 Verifier ID。反射元数据解决“能调用什么”，Recipe 解决“这些积木应该怎样组合”。

### 3. Gameplay Recipe

Recipe 是机器可读的玩法组合契约，不是只给模型看的自然语言教程。首批 Recipe：

```text
network-third-person-player
collectible.pickup
owned-door
all-objectives-victory
replicated-runtime-hud
```

每个 Recipe 记录所需组件、Blueprint 默认值、Graph 事件与连接、输入映射、网络 Role、可配置参数、静态
验证器和运行时断言。Recipe 可被 Skill 检索和解释，但 Skill 不能扩大 Recipe 所允许的工具与权限。

### 4. Build Plan

Build Plan 是由 GameSpec 解析出的操作 DAG。每个步骤记录依赖、Artifact Producer、预期修改、副作用、
后置条件、回滚边界和产物句柄。Dry Run 必须先展示新增/修改的 World、Blueprint、Graph、资产引用、输入和
打包配置；用户批准的是具体 `PlanHash`，计划改变后必须重新批准。

大批量创建使用幂等的批处理工具：重复执行不得产生重复 Coin、门、输入绑定或 Graph 节点。项目文件、
World、Blueprint 和 Graph 的联合修改需要项目级事务；失败时回滚本次 Plan，而不是清理用户已有内容。

### 5. Artifact Producer 边界

当前统一通过生产器接口生成或更新产物：

```text
IArtifactProducer
 |- ExistingAssetProducer
 |- ActorBlueprintProducer
 |- PicoGraphProducer
 |- WorldProducer
 `- CodeModuleProducer        // 未来 Code Harness，当前禁用
```

Build Plan 只依赖生产器契约和 Artifact Handle，不依赖某个 Tool 的内部实现。这样未来加入 C++ Code Harness
时不需要重写 Requirement Parser、Recipe、审批、Validator、Scenario Runner 或 Evidence Report。

### 6. 两层验证

静态验证在 Play 前执行，至少检查：

- 资产引用、GeneratedClass、CDO、默认子对象和场景实例覆盖；
- Graph 编译、Pin 类型、节点预算、事件参数和 Callable 权限；
- 输入映射、PlayerStart、GameMode/GameState/PlayerState/Controller 配置；
- Replication、RPC、Authority 和 Ownership；
- Recipe 后置条件及 Package 依赖。

运行时验证由仅在 Development/Test 启用的 Runtime Probe 和 Scenario Runner 执行。Probe 暴露稳定的测试
状态与命令，不暴露任意内存；Runner 可启动 Server、Client 1、Client 2，注入 Input Action，并读取结构化
断言、日志和必要截图。

双人收集开门 Demo 至少验证：

1. 两个玩家均可移动和跳跃；
2. Coin 只由服务器结算，`CoinCount` 正确同步；
3. Client 1 不能打开 Client 2 的门，反向同理；
4. 数量不足时开门请求被服务器拒绝；
5. 单门开启不能触发胜利；
6. 两门开启后 GameState 进入 `Won`；
7. 两个客户端均显示 Victory。

验证失败后最多进行两轮定向修复。每轮必须引用失败断言，只允许修改相关 Artifact，并保留前后 Diff、
Tool Trace、日志、Checkpoint 和验证结果。超过预算后停止并报告，不继续盲试。

### 7. Package 与证据

Package 成功必须同时满足退出码、`PackageReport.ini`、`PicoPackage.complete`、Stage Manifest、可执行文件
启动 Smoke 和最小联网 Smoke。最终 Evidence Report 包含 GameSpec、PlanHash、审批、修改清单、静态检查、
场景断言、修复轮次、Package Receipt 和可复现启动方式。

## 首批可复用 Gameplay 积木

先在 `Agent Game Starter Template` 中提供以下项目级积木：

- `CollectibleComponent`：服务器权威拾取、价值和拾取者；
- `InteractableComponent`：统一交互入口、距离和拒绝原因；
- `OwnedDoorComponent`：Owner、所需数量、开门状态和复制；
- `ObjectiveComponent`：目标进度、完成和依赖；
- `MatchStateComponent`：汇总目标并驱动 GameState；
- `RuntimeHUDComponent`：只显示稳定的玩家、目标和胜负状态；
- 示例 GameMode、GameState、PlayerState 和第三人称 Pawn Blueprint。

这些能力先属于模板而不是 Engine Runtime。至少被第二个不同玩法复用且职责稳定后，才考虑提升到通用模块，
避免为了 Agent 把项目特例固化进引擎。

## PicoGraph 必要增量

第 10 月只补齐组装 Demo 必需的表达能力：

- Input、Overlap、RepNotify 和 Custom Event；
- 事件参数、对象引用和目标对象；
- Int、比较、布尔组合与受限转换；
- 参数化 `PFunction`；
- Spawn/Destroy；
- `ServerOnly/OwnerOnly/All` 执行策略。

所有节点继续由 Schema、反射元数据和权限规则生成或注册。禁止为某个 Demo 编写名称匹配的隐藏节点，禁止
Graph 直接发送任意网络包或调用未标记 Callable 的函数。

## 六周实施计划

| 周次 | 任务 | 周末验收 |
| --- | --- | --- |
| 第 1 周 | Capability Descriptor/Catalog、Recipe 格式、`PicoGameSpec` Schema 和支持度诊断 | 同一需求稳定生成 Spec；清楚列出已支持、缺失和不支持项；Catalog 可追溯到来源与 Verifier |
| 第 2 周 | Build Plan、Dry Run、PlanHash 审批、项目级事务、幂等批处理工具和 Artifact Handle | 执行前可预览完整修改；批准后原子执行；故障可回滚且不污染已有项目 |
| 第 3 周 | PicoGraph 事件/参数、Int/比较/布尔、参数化 PFunction 和 Authority Policy | 不新增项目专用 C++ 也能表达收集、条件判断、交互和服务器权威调用 |
| 第 4 周 | Starter Template Gameplay Components、GameMode/GameState/PlayerState、正式 HUD 和首批 Recipes | 人工只用现有积木即可组装并运行双人收集开门 Demo |
| 第 5 周 | Runtime Probe、Input Action 注入、三进程 Scenario Runner 和网络断言 | 自动完成拾取、错误门拒绝、正确开门和双端胜利验收，并输出结构化证据 |
| 第 6 周 | 端到端 Game Assembly Skill、最多两轮修复、Package 验证和真实 Golden Tasks | 从中文需求到可审计、可运行、可联网的 Windows Stage 形成闭环 |

六周完成后必须再用一个不同玩法验证复用性，例如“钥匙 + 双人压力板”，不能只重复更换双人收集 Demo 的
名称和颜色。第二个用例未通过前，不宣称 Agent 已具备通用游戏制作能力，也不提前进入 ECS 主线。

## Skill、RAG 与模型路由

- Skill 负责告诉 Agent 何时使用 Recipe、步骤顺序、常见失败和验收方式；实际能力和权限仍来自 Catalog/Policy。
- RAG Lite 索引 Catalog、Recipe、Graph/GAS Schema、验证结果和项目事实，并返回来源；不把聊天猜测写成事实。
- 明确意图、否定词和安全规则继续使用确定性路由。仅当出现真实候选歧义时，才让模型从已筛选 Skill ID 中选择。
- MCP Adapter 只作为未来外部客户端的协议适配层，不参与核心正确性，也不列入六周完成门槛。

## 未来 Code Harness 接入条件

当前阶段不允许 Agent 任意写 C++、执行 Shell、静默下载依赖或修改 Engine Source。只有组合能力无法覆盖、
且至少两个真实需求证明需要新代码时，才单独规划 `CodeModuleProducer`：

1. 在隔离 Worktree/临时项目中生成，只允许写项目 `Source` 白名单；
2. 依赖必须来自 Allowlist，新增依赖单独审批；
3. 生成后执行格式化、静态检查、构建和测试；
4. 用户按 Diff 审批后才合并；
5. 加载新模块并刷新 Capability Catalog；
6. 回到同一个 Build Plan，继续静态、运行时和 Package 验证。

Code Harness 是新增 Artifact Producer，不是第二套 Agent Runtime，也不能绕过现有 Tool Pipeline、审批、事务、
Checkpoint 和验证器。

## 明确不做

- 不承诺从一句话生成任意类型的完整商业游戏；
- 不实现任意 C++/Shell 执行；
- 不把 LangChain/LangGraph 作为运行时依赖；
- 不在本阶段引入多 Agent、在线 Marketplace 或公网部署；
- 不为了 Demo 把项目专用类名写入通用 Editor/Engine；
- 不用截图或 Assistant 文本代替结构化运行时断言。
