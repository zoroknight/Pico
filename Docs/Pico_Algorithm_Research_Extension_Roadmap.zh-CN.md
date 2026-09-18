# Pico 算法研究扩展落地路线

本文档定义 Pico 后续算法研究的唯一主线、工程边界、实验方法与阶段验收。目标不是为引擎继续堆叠零散 AI
功能，而是形成一项可复现、可比较、能进入真实游戏 Demo，并能作为独立扩展长期维护的研究成果。

本文是专项研究路线，不替代
[`Pico_AI_First_Development_Roadmap.zh-CN.md`](Pico_AI_First_Development_Roadmap.zh-CN.md)。当前 AI 游戏搭建、
Render Architecture、视觉资产、受控 Code Harness 和第二玩法验收仍按主路线推进；本路线在这些基础上分阶段接入，
不得反向阻塞 Pico 原生功能。

## 1. 最终研究成果

研究项目暂定名：

> **Pico Semantic World Agent：基于引擎原生对象语义、可执行技能和世界模型规划的协作型游戏角色。**

最终演示采用三个小型双人合作关卡。一个角色由玩家控制，另一个角色由学习策略控制。AI 需要识别门、机关、
金币、平台、能力、角色和任务阶段，选择并执行语义技能；玩家打断既定流程后，AI 能根据新状态重新规划。

该成果必须同时包含：

1. 独立维护的 `PicoResearchKit` 扩展。
2. 可复现的 Pico Learning Environment 与 Benchmark。
3. Primitive、Semantic Skill、Flat World Model、Object World Model 和 Planner 的公平对照实验。
4. 可打包运行的 AI Companion 游戏演示。
5. 训练配置、数据版本、模型、指标、失败案例和环境版本的审计记录。

本阶段不追求“万能游戏 AI”、从像素理解任意游戏、LLM 逐帧控制角色、神经渲染或完整物理人形。

## 2. 研究问题与可证伪假设

### H1：语义技能抽象

在相同底层仿真步数、奖励信息和训练预算下，`Semantic Skill PPO` 是否比逐帧 `Primitive PPO` 获得更高的
长时序任务成功率、更低的无效动作率和更好的训练稳定性？

### H2：引擎原生对象表示

在输入信息等价、模型参数量近似的条件下，对象集合世界模型是否比固定长度 Flat State World Model 更能泛化到：

- 未见过的关卡布局；
- 不同数量或排列的交互对象；
- 同类对象的替换组合；
- 玩家中途改变机关和任务状态。

### H3：模型规划与恢复

在玩家干扰、技能失败或环境状态变化后，`Learned Model Planner` 是否比纯 Reactive Skill Policy 更快恢复任务，
并减少需要人工编写的分支规则？

即使假设不成立，只要实验公平、可复现并解释了失败原因，研究阶段仍可验收；不得通过更换指标隐藏负结果。

## 3. 与现有 Agent 的边界

必须区分两种 AI，不共享逐帧决策职责：

```text
创作侧 LLM Agent
  MCP / Harness / Approval / Transaction
  创建场景、配置组件、生成 ExperimentSpec、启动实验、汇总结果
                         |
                         v
PicoResearchKit
  Environment / Skill Runtime / Recorder / Policy Runtime
                         |
                         v
训练侧 Python
  Gymnasium Adapter / PPO / World Model / Planner / Evaluation
                         |
                         v
运行侧模型
  ONNX Policy -> Pico 游戏中的 AI Companion
```

- LLM Agent 不进入游戏逐帧循环，不直接访问训练进程内存。
- LLM 可以提出实验配置和工具调用，但所有场景副作用仍经过 Editor 权限、审批、事务和验证。
- 一次 Run 开始后，`ExperimentSpec` 冻结；LLM 不得静默改写奖励、种子、数据或验收条件。
- 运行时 NPC 只加载已批准的策略资产；断网和关闭 Provider 不影响游戏运行。

## 4. 扩展形式与源码隔离

Pico 当前已有 `IGameModule` 和多个 Runtime/Developer 模块，但尚无成熟的通用动态插件加载器。因此第一阶段采用
**源码级可选扩展**，而不是先开发完整插件市场或把研究代码放入 `Source/Runtime`。

建议建立独立仓库或 Pico 同级目录：

```text
PicoResearchKit/
  Native/
    PicoLearningRuntime/       # 游戏运行时接口与模型推理
    PicoLearningDeveloper/     # Headless、Recorder、Checkpoint、Benchmark
    PicoLearningBridge/        # 本机训练协议
    PicoResearchGame/          # 研究地图、任务和技能适配
  Python/
    pico_learning/             # Gymnasium 风格 API
    algorithms/                # PPO、World Model、Planner
    experiments/               # 冻结配置与启动入口
  Assets/                      # 研究专用资产，不进入 Pico 默认内容
  Benchmarks/
  Results/
```

Pico 原仓库只允许出现以下通用改动：

1. 一个可关闭的 `PICO_EXTERNAL_EXTENSIONS` CMake 接入点。
2. 少量不依赖算法框架的接口，如 `ILearningEnvironment`、`ISkillExecutor`、`IPolicyRuntime`。
3. Fixed Step、Headless 和物理状态适配所需的通用生命周期钩子。
4. 扩展版本、能力和 Schema 的查询接口。

默认构建不启用扩展；删除 `PicoResearchKit` 后 Pico Editor、Sandbox、测试和打包仍须通过。禁止让 Pico Runtime 依赖
PyTorch、Gymnasium、训练脚本或研究模型格式。

后续只有在至少两个独立扩展验证相同需求后，才评估 Manifest、动态加载、版本解析和依赖管理等通用插件系统。

## 5. PicoLearning 工程合同

### 5.1 环境 API

```cpp
class ILearningEnvironment
{
public:
    virtual FEnvironmentSpec GetSpec() const = 0;
    virtual FResetResult Reset(const FResetRequest& Request) = 0;
    virtual FStepResult Step(const FActionBatch& Actions, int32 FixedSteps) = 0;
    virtual FLearningCheckpoint SaveCheckpoint() const = 0;
    virtual bool RestoreCheckpoint(const FLearningCheckpoint& Checkpoint) = 0;
};
```

每条请求和结果至少包含：

- `ProtocolVersion`、`SchemaHash` 和 `EngineBuildId`；
- `EnvId`、`AgentId`、`EpisodeId` 和单调递增的 `StepId`；
- `Seed`、`FixedDeltaSeconds`、`ActionRepeat`；
- Observation、Action Mask、Reward、`Terminated`、`Truncated` 和诊断 Metrics；
- 明确的错误码，不用日志字符串承担协议语义。

通信第一版使用仅监听 `127.0.0.1` 的 TCP，并设置消息大小、超时、并发数和协议版本上限。完成基准后，只有通信
确实成为瓶颈才增加共享内存；不得过早把优化复杂度带入研究正确性阶段。

### 5.2 四种运行模式

| 模式 | 用途 | 约束 |
| --- | --- | --- |
| `LogicOnly` | 算法、奖励和 Reset 快速调试 | 可简化物理，不作为最终游戏成绩 |
| `PhysicsAuthoritative` | Jolt 中的正式训练与评估 | 固定步进、单进程、无网络复制 |
| `VisualCapture` | RGB/Depth/ObjectId 数据与可视化 | 可降低采样频率，不阻塞主训练 |
| `NetworkValidation` | 最终双人联机行为验收 | 仅评估，不作为第一版训练环境 |

训练初期不得让多进程网络同步、渲染帧率和 RL 时间步耦合在一起。

### 5.3 确定性等级

Pico 不承诺跨平台逐位一致，按以下等级验收：

| 等级 | 含义 | 首次要求 |
| --- | --- | --- |
| D0 | 同 Seed Reset 得到一致逻辑状态 | PicoLearning 第一版必须通过 |
| D1 | 同动作序列产生一致事件和容差内数值结果 | Jolt 正式环境必须通过 |
| D2 | 同构建内 Checkpoint/Restore 后可重放 | Oracle Planner 前必须通过 |
| D3 | 跨机器逐位确定 | 不作为当前目标 |

当前引擎循环使用真实帧间隔驱动 World 和 Physics，因此 Fixed Step 是首要前置。渲染可以继续使用真实时间，研究
世界必须由独立仿真时钟驱动。

### 5.4 Reset 与 Checkpoint

现有 `FWorldAssetData` 是创作态世界资产，不能直接冒充运行时快照。单独定义 `FLearningCheckpoint`：

```text
AuthoredBaselineRef
+ Stable runtime object IDs and reflected deltas
+ RNG streams
+ gameplay timers and task state
+ GAS attributes, tags, effects and cooldowns
+ skill executor state
+ physics adapter state
+ episode and recorder cursors
```

第一版限制 Checkpoint 期间的任意动态生成；必须生成的对象使用稳定 ID 与可重放 Spawn/Destroy Event Log。物理层
通过 `IPhysicsCheckpointAdapter` 封装 Jolt StateRecorder，同时由 Pico 保存 Jolt 不负责的对象配置、生命周期和游戏状态。

Oracle Planner 最初可使用轻量 `FPlanningState` 或隔离 World Clone，不允许反复回滚正在显示或联网的 Live World。

## 6. 语义对象与技能模型

### 6.1 Observation

主线使用引擎原生结构化状态，不从像素重新识别 Pico 已经知道的信息：

```text
ObjectState
  TypeId / StableObjectId / RelativeTransform / Velocity
  GameplayTags / Ownership / InteractionState
  Affordances / Reachability / TaskRelevantProperties

GlobalState
  Goal / TaskPhase / PlayerState / TimeRemaining / TeamState
```

所有特征必须有单位、范围、缺失值、归一化和版本定义。训练数据只保存稳定 ID，不泄漏进程指针或容器顺序。

像素观察以后作为单独实验变量加入，不能与对象模型第一轮实验同时改变。

### 6.2 Skill Contract

```cpp
class ISemanticSkill
{
public:
    virtual FSkillDescriptor Describe() const = 0;
    virtual bool CanExecute(const FSkillContext& Context) const = 0;
    virtual FSkillHandle Start(const FSkillRequest& Request) = 0;
    virtual FSkillStatus Update(FSkillHandle Handle, float FixedDelta) = 0;
    virtual void Cancel(FSkillHandle Handle) = 0;
};
```

第一组技能固定为：

- `MoveTo(Target)`；
- `Face(Target)`；
- `JumpTo(Target)`；
- `Interact(Target)`；
- `HoldSwitch(Target)`；
- `ActivateAbility(AbilityId, Target)`；
- `Wait(Duration)`。

每个 Skill Descriptor 记录前置条件、参数 Schema、互斥资源、可能结果、超时和失败原因。Action Mask 由引擎的
Affordance 与 `CanExecute` 生成，而不是让模型通过大量失败尝试猜测非法动作。

Skill 负责“做什么”，Character Movement、GAS、Animation 和 Physics 负责“如何执行”。研究层不得复制第二套移动、
碰撞或能力系统。

## 7. 算法实施顺序

### A0：Primitive PPO

连续移动/朝向加离散跳跃、交互动作。目的只是证明完整训练链，禁止在这一阶段发明新算法。

### A1：Semantic Skill PPO

策略选择 Skill 与目标对象；底层控制器执行并返回 Running/Succeeded/Failed。比较时同时报告 Agent Decision 数和
Underlying Physics Steps，避免 Skill 方案通过减少决策次数获得不公平优势。

### A2：Flat World Model

使用 MLP/GRU 预测下一摘要状态、Reward、Termination 与 Skill Outcome。先验证数据、Loss、误差累积和多步 rollout。

### A3：Object World Model

第一版采用共享 Object Encoder 与 DeepSets mean/max pooling；对象关系先使用显式相对位置、距离、所有权和任务关系。
只有它在对象数量或组合泛化上出现明确瓶颈，才增加 Attention 或 GNN。

### A4：Oracle Planner

用真实环境 Transition 或可靠 Checkpoint 建立 Beam Search 上限，深度从 4～6 个 Skill 开始。若 Oracle 都无法改善
任务成功率，说明任务、Skill 或搜索空间有问题，禁止直接训练 Learned Planner。

### A5：Learned Model Planner

基于世界模型搜索 Skill 序列；使用小型模型 Ensemble 或多次预测估计不确定性，对高不确定路径施加惩罚。执行时只
提交第一个 Skill，失败、状态变化或玩家干扰后重新规划；超预算时回退到 Reactive Skill Policy。

### A6：AI Companion

高层决策采用事件触发或 2～5 Hz，低层 Movement/Animation 继续逐帧运行。最终验证固定合作、状态选择和玩家干扰
三类房间，不扩大到开放世界。

## 8. 动画、物理与渲染框架取舍

### 8.1 物理

- Pico 运行时与正式游戏验收继续使用 Jolt，不为研究主线替换主物理引擎。
- `IPhysicsCheckpointAdapter` 隔离 Jolt 特性，避免世界模型依赖其内部类型。
- 只有 Learned Physical Motion 阶段才使用 MimicKit/ProtoMotions 支持的外部训练后端。
- MuJoCo 适合低成本单环境调试；Isaac Lab/Newton 适合 GPU 批量训练。导出策略后必须在 Pico/Jolt 做独立验证。
- 使用质量、摩擦、碰撞、控制延迟和关节参数随机化降低 Sim-to-Sim 差异；迁移失败则停止物理控制路线。

### 8.2 动画

在 Learned Motion 前先建立可度量的传统基线：动画混合、根运动、转向、落脚接触、脚底滑动、穿插和响应延迟。
第一项学习内容应是“根据轨迹与状态选择/混合动画片段”，而不是完整物理人形 RL。策略通过 ONNX 运行，骨骼播放与
蒙太奇仍由 Pico Animation 管理。

物理模仿控制只作为主研究成果完成后的独立纵向切片，预计至少 12 周，不进入当前世界模型验收。

### 8.3 渲染

Render Graph 不是结构化对象研究的前置条件。阶段 C 完成后，可由扩展注册 `ResearchCapturePass`，按需输出：

- RGB；
- Linear Depth；
- World Normal；
- Object/Instance ID；
- Motion Vector。

Headless 模式必须绕过全部 Capture Pass。OpenGL/Vulkan 差异保持在 RHI 与 RenderGraph 后方，训练协议和 Observation
Schema 不得包含图形 API Handle。像素世界模型、神经渲染与 Vulkan Backend 均不与第一篇研究结果捆绑。

## 9. Benchmark 与实验公平性

### 9.1 三类关卡

| 关卡 | 主要能力 | 干扰变量 |
| --- | --- | --- |
| Fixed Cooperation | 双角色分别站机关、开门、汇合 | 目标位置与顺序固定 |
| State Choice | 根据门、平台、金币和能力选择路线 | 布局、对象数量和角色能力变化 |
| Player Intervention | 玩家中途离开机关、抢先交互或改变目标 | 触发计划失效与重新规划 |

### 9.2 基线

固定保留：Scripted/GOAP、Primitive PPO、Skill PPO、Flat WM Planner、Object WM Planner 和 Oracle Planner。行为树或
GOAP 作为工程基线，不要求与学习算法共用内部实现，但必须使用相同任务、可观测信息和成功判定。

### 9.3 指标

算法指标：成功率、回报、环境步数、决策数、样本效率、无效动作率、规划耗时、模型多步误差。

游戏指标：完成时间、玩家等待时间、干扰后恢复时间、碰撞/卡死次数、技能失败率、联机最终一致性。

工程指标：环境吞吐、Reset P50/P95、内存增长、模型推理 P95、包体增量、崩溃率和重放一致率。

创作指标：新增任务所需代码、配置、Skill 数量、人工分支数和调试时间。Skill 的一次性实现成本必须摊入统计，不能
只比较最终 Graph 节点数。

正式实验至少使用 5 个训练 Seed；独立 Evaluation Seed 与训练 Seed 分离。报告均值、标准差和 Bootstrap 置信区间，
保存每次运行的原始数据。模型参数量原则上控制在同量级，并分别报告墙钟时间与环境交互量。

## 10. 可审计性与安全

每次运行生成不可变 `run_manifest.json`：

```text
Pico commit / ResearchKit commit / EngineBuildId
Environment, observation, action, reward and skill schema hashes
Map and asset hashes / Physics backend and version
Algorithm config / Seed / Dataset IDs / Model artifact hash
Start and finish time / Exit reason / Metric artifact paths
```

- API Key、访问令牌和用户私有路径不得进入数据集、模型或 Manifest。
- 训练进程默认无编辑器写权限，只能写入当前 Run 的 Staging/Results 目录。
- LLM 发起训练、覆盖模型、发布策略和写回场景时必须分别审批。
- 策略导入先做 Schema、尺寸、数值范围和模型 Hash 验证，再进入 Sandbox Play。
- 研究扩展崩溃不得导致 Editor 工程资产半写入；结果目录采用临时目录加原子提交。

## 11. 实施阶段与验收门

| 阶段 | 预计时间 | 关键交付 | 准入条件 |
| --- | ---: | --- | --- |
| R0 扩展骨架 | 2 周 | 独立构建、版本查询、ResearchGame | 无扩展时 Pico 全量回归通过 |
| R1 Learning Runtime | 4 周 | Fixed Step、Headless、Reset、Schema、Recorder | D0/D1、1000 次 Soft Reset 无状态泄漏 |
| R2 PPO 基线 | 4 周 | Python API、Primitive PPO、指标面板 | 固定任务可重复收敛 |
| R3 Semantic Skills | 5～6 周 | Skill Contract、Action Mask、公平对照 | H1 数据与失败案例完整 |
| R4 World Model | 6～8 周 | Flat/Object 模型与组合泛化 | 单步及多步预测达到预设门槛 |
| R5 Planning Companion | 6～8 周 | Oracle/Learned Planner、干扰恢复 | H3 与实时推理预算验收 |
| R6 研究收尾 | 4 周 | 消融、统计、打包 Demo、复现说明 | 新机器按文档可复现实验和运行 |
| R7 Learned Motion | 12 周以上 | 外部训练与 Pico 推理适配 | 不阻塞 R0～R6，单独立项 |

R0～R1 可以在 AI 游戏搭建主线中提前准备稳定 ID、Skill Descriptor 和 Headless 边界；正式训练应在阶段 A.2 的第二
玩法验收后集中推进，避免同时维护游戏搭建、渲染迁移和训练环境三条高风险主线。

## 12. 止损与降级规则

1. Fixed Step 下 Jolt 仍无法满足 D1：首轮训练退到 `LogicOnly`，Jolt 只做正式评估。
2. Semantic Skill 不优于 Primitive：保留负结果，重点检查公平性、任务跨度与一次性创作成本，不追加模型规模。
3. Object Model 不优于 Flat Model：保留对象数量/布局消融，不直接切换 Transformer 或大型视觉模型。
4. Oracle Planner 无收益：停止 Learned Planner，优先修任务、Skill 粒度和搜索空间。
5. Learned Planner 误差累积严重：缩短 Horizon、启用不确定性回退，不允许不受限想象轨迹控制 Live World。
6. 物理动画迁移失败：保留学习式 Clip Selector，继续使用 Pico 的运动与动画系统。
7. 任一阶段超过预设周期仍无可测结果：冻结功能开发，先修数据质量、复现性和 Benchmark。

## 13. 明确不做的内容

- 不先开发完整动态插件管理器。
- 不在性能数据前使用共享内存或大规模并行环境。
- 不用联网双客户端作为第一版训练环境。
- 不用 LLM 逐帧控制 NPC。
- 不先做 GNN、Transformer、像素 World Model 或多智能体社会模拟。
- 不替换 Pico 的 Jolt 主物理，也不在 Pico 内重写大规模 GPU 物理训练器。
- 不将 Python、PyTorch 或实验代码加入 Pico Runtime 默认依赖。
- 不因研究方便绕过 Harness 审批、资产事务、回滚或模型校验。

## 14. 外部参考与采用范围

| 参考 | Pico 采用内容 | 不照搬内容 |
| --- | --- | --- |
| [UE Learning Agents](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/LearningAgents) | Manager、Interactor、Observation/Action Schema、Training/Inference 分层 | 实验性 API 的具体类层级 |
| [Unity ML-Agents](https://github.com/Unity-Technologies/ml-agents/blob/develop/docs/ML-Agents-Overview.md) | 引擎 SDK、通信层、Python Trainer 分离 | 完整 Trainer 与 Unity 生命周期 |
| [Craftium](https://github.com/mikelma/craftium) | Gymnasium/PettingZoo 风格环境与可复现实验 | 对引擎的深度 Fork 和体素任务假设 |
| [OC-STORM](https://arxiv.org/abs/2501.16443) | 对象信息对决策相关细节的价值、World Model 实验方法 | 视觉对象提取和与 Pico 特权语义的不公平直接比较 |
| [Infernux](https://github.com/ChenlizheMe/Infernux) | 原生核心、类型化 Python 边界、Headless 模式 | Python-first 引擎结构与整体 Vulkan 迁移 |
| [MimicKit](https://github.com/xbpeng/MimicKit) | 可替换训练模拟器与运动模仿基线 | 把训练框架嵌入 Pico Runtime |
| [ProtoMotions](https://github.com/NVLabs/ProtoMotions) | 后期物理运动训练与多后端验证 | 将物理人形变成当前主线 |
| [Jolt StateRecorder](https://jrouwe.github.io/JoltPhysics/class_state_recorder.html) | 物理状态恢复与确定性诊断 | 把物理快照当作完整游戏快照 |

## 15. 第一阶段开工清单

在正式进入 R0 前只做以下设计验证，不立即引入训练依赖：

1. 写出 `FEnvironmentSpec`、`FObservationSchema`、`FActionSchema`、`FSkillDescriptor` 的最小 RFC。
2. 为当前合作 Demo 中的角色、门、机关、金币和平台建立稳定对象 ID 与 Affordance 清单。
3. 画出 Engine Loop 的 Simulation Clock 注入点，确认 Editor、Game、Render 和 Network 各自时间来源。
4. 制作一张 Reset 状态所有权表，覆盖 World、Actor、GAS、Physics、Timer、RNG、Skill 和任务状态。
5. 用固定 Seed 和固定动作序列记录当前非确定性基线；没有基线数据前不修改 Jolt 或 Tick。
6. 验证独立 `PicoResearchGame` 可以只通过现有 `IGameModule` 启动，确定 R0 所需的最小 CMake Hook。

完成这六项后再锁定 R0 接口；接口 RFC 通过前，不创建训练 Bridge、PPO 封装或 World Model 类。
