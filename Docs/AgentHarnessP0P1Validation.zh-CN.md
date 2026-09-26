# Agent Harness P0/P1 验收流程

## 离线基线

在 `E:\UnrealSourceCode\Pico` 运行：

```powershell
.\Build\Release\PicoAgentTests.exe
.\Build\Release\PicoEditorTests.exe
.\Scripts\TestAgentRealTaskEval.ps1 -Action Summary
```

Agent 测试覆盖相同 Session 种子与外部快照的逐字节请求重建、外部知识漂移、工具顺序和重试，以及压力投影的只读权限、工具配对、待变更读回、旧 PlanHash、省略后 Session 原文回读。Editor 测试覆盖 Actor 移动但数量不变时 World 快照内容变化。测试通过只证明离线机制，不证明真实缓存收益。

## Release Editor 观察

1. 打开 `Build/Release/PicoEditor.exe` 和 `PicoSandbox`。AI Chat 的 `Compact prior task history` 保持勾选。在 `StarterWorld` 发送：`只读分析当前世界：列出主要 Actor，检查 PhysicsCrate 的组件配置及相关资产，最后总结；不要修改场景。` 记录 Run ID、回答事实与来源、工具数、Provider 命中/未命中 Token。界面可能显示 `History projection`；仅当同一任务内较早的成功只读结果确实达到压力门时，才显示 `Tool result projection`。显示 0 不是故障。
2. 新建**未保存的临时 World**，用 UI 放入一个可移动 Actor。在这个 World 先发送：`只读描述当前 World 的主要 Actor 及其位置；不要修改场景。` 记录基线 Run ID。然后只移动该 Actor 的 X 坐标，不增删 Actor，再发送：`重新只读描述当前 World 的主要 Actor 及其位置；不要修改场景。` 记录变化后 Run ID。两轮必须读到各自的当前状态，不能沿用旧位置。
3. 对基线和变化后两个 Run ID 分别在项目根目录运行下面命令。若会话文件不是 `editor-chat-deepseek.jsonl`，改为 AI Chat 当前 Session 对应文件。

```powershell
.\Scripts\InspectAgentRequestProfiles.ps1 `
  -SessionPath .\Projects\PicoSandbox\Saved\Agent\Sessions\editor-chat-deepseek.jsonl `
  -RunId run_xxx | Format-List
```

核对 `SourceEventFirstSequence`/`SourceEventSequence`、`BodyBytes`/`BodyFingerprint`、Provider/模型、工具顺序和各 `*Bytes`。同一个临时 World 的移动前后 Actor 数应相同，`WorldContentFingerprint` 应不同；`WorldRevision` 可为空，不把数量冒称修订号。`ExternalSnapshotPersisted=False` 表示仅有指纹，不能用当前快照重建旧请求。真实 Provider 的 `BodyBytes` 应大于 0，Fake Provider 可以为 0。旧计划若涉及写入，仍须通过当前工具核验，不得从摘要中取旧 PlanHash 直接执行。

## 在线放行

同 World、Provider、模型与任务目标下，分别记录 Compact On/Off 的最终事实、证据、工具集合、未命中/总输入 Token、Provider 延迟、World 状态和静止 FPS。只有回答深度与任务必需事实相当、审批和读回均正确时，才比较成本；工具顺序可不同，额外查询造成的深度差异不硬凑成严格配对。投影触发后若出现漏答、无证据断言、旧计划执行或明显延迟/FPS 退化，关闭 Compact 或回退 P1 投影并保留 Run ID。当前尚无此版本的真实 Provider 对照结果，不能宣称缓存优化已在线通过。

## P1 独立受控 A/B（Debug Editor）

该夹具只在 Debug `PicoEditor.exe` 编译：只提供 `editor.harness.read_fixture`，按固定顺序本地执行五次只读查询，产生相同的大结果；不访问或修改 World。第一步 ToolCalls 是本地脚本生成，不计 Provider Token；第二步才调用选定的真实 Provider。两轮保持 Compact 开启，只切 P1，因而不会把 CE1 差异误认为 P1 收益。使用单独的新 Chat，避免已有长会话干扰。夹具使用 96 KiB 应用级上下文预算，专用于触发压力阈值，不代表正式版预算。

1. 从 `E:\UnrealSourceCode\Pico` 启动 `Build\Debug\PicoEditor.exe`，打开 `PicoSandbox`。AI Chat 选同一个真实 Provider 和模型（DeepSeek 或 Kimi，不能选 Fake）。展开 `Request Settings`，勾选 `Compact prior task history` 和 `P1 controlled fixture (Debug only)`。
2. 点击 `New Chat`。勾选 `P1 projection (fixture only)`，发送：`P1受控评测：只读汇总五条固定测试记录。只报告记录总数、最后两条的编号与状态；不要猜测不可见的旧记录详情，不要修改场景。` 等待结束，记录 Run ID 和回答。预期五次只读工具调用，不弹审批、不改变 World；回答应为总数 5，记录 4 的状态 `checked`、记录 5 的状态 `ready`。
3. 再点击 `New Chat`，取消勾选 `P1 projection (fixture only)`，其他设置和提示词逐字不变，发送同一句。记录第二个 Run ID 和回答。测试结束后取消勾选 `P1 controlled fixture`，防止后续普通任务误用。
4. 在 `E:\UnrealSourceCode\Pico` 分别运行以下命令。`SessionPath` 用 AI Chat 的 `File:` 所示文件名拼到 `Projects\PicoSandbox\Saved\Agent\Sessions\` 下；不要拿两个 Run ID 查同一个新 Chat 文件。用 `BodyBytes -gt 0` 筛选真实 Provider 请求，排除首步本地种子。

```powershell
$onSession = '.\Projects\PicoSandbox\Saved\Agent\Sessions\替换为On文件名.jsonl'
$offSession = '.\Projects\PicoSandbox\Saved\Agent\Sessions\替换为Off文件名.jsonl'
$onRunId = '替换为OnRunID'
$offRunId = '替换为OffRunID'
.\Scripts\InspectAgentRequestProfiles.ps1 -SessionPath $onSession -RunId $onRunId | Where-Object BodyBytes -gt 0 | Format-List
.\Scripts\InspectAgentRequestProfiles.ps1 -SessionPath $offSession -RunId $offRunId | Where-Object BodyBytes -gt 0 | Format-List
```

合格的机制对照应当看到 On 的 `ProjectedReadEnabled=True`、`ProjectedReadResults` 大于 0、`ProjectedReadBytes` 大于 0；Off 的 `ProjectedReadEnabled=False`、投影结果与字节均为 0。两轮回答的必需事实应相同，且无额外工具重查和虚构事实。再比较真实请求的 `BodyBytes`、`ToolResultBytes`、`CacheMissTokens` 与 AI Chat 的 Provider 用量。`CacheHitTokens` 和命中率受服务端前缀缓存影响，不能单靠百分比判断 P1；若 Provider 响应步数或回答质量不同，标记为非严格配对。夹具证明机制和局部成本，不替代正式版真实任务、审批和视觉回归。

### 2026-09-26 受控结果

同一 `deepseek-v4-flash`、同一提示词与合成工具 schema，两个全新 Chat：On `run_1790430976135096_0`，Off `run_1790430998752804_14`。两轮 Compact 均开启，均 Completed、五次成功只读工具调用、无写入、一次真实 Provider 请求；回答均正确给出总数 5、记录 4=`checked`、记录 5=`ready`，On 对已省略的记录 1 细节明确保留未知。

| 指标（真实 Provider 请求） | P1 On | P1 Off |
| --- | ---: | ---: |
| 投影旧结果 / 省略字节 | 1 / 13,756 | 0 / 0 |
| 序列化请求字节 | 61,588 | 75,328 |
| 总输入 Token | 13,323 | 16,222 |
| 未命中 Token | 13,323 | 15,710 |
| 命中 Token | 0 | 512 |
| `Model.Generate` 耗时 | 1,440 ms | 1,307 ms |

On 比 Off 少 13,740 请求字节（18.2%）、2,899 总输入 Token（17.9%）、2,387 未命中 Token（15.2%）。这是单组夹具中的 P1 减重收益，不是命中率提升：On 命中 0，Off 命中 512。延迟单次测量反而高约 134 ms，不能据此声称加速或退化；正式版真实任务的质量、安全与成本仍需另行观察。

## P0 重点在线记录（2026-09-26）

- World 移动：`run_1790428760137842_10` 与 `run_1790428799999467_20` 均查询 `GameWorld.PersistentLevel.Cube_1`，Actor 数为 1；本轮 `editor.world.describe` 的位置从 `(0, 0, 0)` 变为 `(0, 0, 169.28)` cm，变化的是 Z 坐标。World 内容指纹由 `11573578803678679311` 变为 `6723610668482457173`，两轮回答均使用本轮位置。
- Selection 连续切换：`run_1790429334293994_0` 的 `editor.selection.describe` 返回 1 个对象 `Cow_1`；`run_1790429357727733_10` 返回 3 个对象 `Cow_1`、`Bunny_1`、`PointLight`。两轮 World 内容指纹相同，Selection Revision 由 2 变为 4，Selection 内容指纹由 `2065610397445500484` 变为 `5584168886631670797`。第二轮没有沿用第一轮的单对象结果；两轮均只读。
- 范围：上述两条 P0 重点可视化路径通过；不能据此宣称新会话、长会话、计划失效和重启等六类轨迹全部在线复测。Selection 提示同时要求类名，但两轮只调用了 `editor.selection.describe`；回答正确标注类名未验证，却未继续调用 `editor.object.describe` 完成该字段。这是任务完整性缺口，不是 Selection 缓存串值。两组的 P1 工具结果投影计数均为 0，不能从中判断 P1 在线收益。
