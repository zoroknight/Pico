# 第 4 月第 3 周：Standalone Login、Possess 与 Gameplay Debug

本周目标是把第 2 周只有“类型和所有权”的 Gameplay Framework 接成一条可以实际运行、重生和验证的本地玩家链路。实现有意对应 UE 的 `ULocalPlayer`、`APlayerController`、`APlayerState`、`APawn` 和 `AGameModeBase`，但暂不引入网络连接、MatchState、移动组件和物理。

## 最终对象链

```text
PGameInstance (跨地图)
  -> PLocalPlayer : PPlayer (跨地图，代表本机玩家入口)
       -> PPlayerController (属于当前 World)
            -> PPlayerState (属于当前 World，由 GameState 登记)
            -> PPawn (属于当前 World，通过 Possess 控制)

PWorld
  -> PGameModeBase (当前 Standalone 的权威规则入口)
  -> PGameStateBase (当前 World 的共享游戏状态)
  -> PPlayerStart (场景保存的出生点)
```

`PPlayer` 是玩家来源与 Controller 之间的抽象层。现在的具体实现是 `PLocalPlayer`；以后接入网络时，可以让服务器连接拥有另一种 Player，而不必把 Controller 写死为“只能来自本地输入”。

## 登录和出生流程

World 初始化后，`PGameInstance` 为每个 LocalPlayer 调用 GameMode：

```text
Login(Player)
  -> 创建 PlayerController
  -> 创建 PlayerState
  -> Player <-> PlayerController
  -> PlayerController <-> PlayerState
  -> GameState.AddPlayerState

PostLogin(PlayerController)

HandleStartingNewPlayer(PlayerController)
  -> RestartPlayer
       -> ChoosePlayerStart
       -> SpawnDefaultPawnFor
       -> PlayerController.Possess(Pawn)
```

GameMode CDO 决定默认的 PlayerController、PlayerState 和 Pawn 类。Sandbox 将 Controller 配置为 `PSandboxPlayerController`，将 Pawn 配置为 `PSandboxPawn`，因此引擎代码不需要认识项目类型。

`ChoosePlayerStart` 第一版按 World 中稳定的 Actor 顺序选择第一个有效 `PPlayerStart`。场景没有 PlayerStart 时会输出警告并在原点出生，便于旧地图继续运行。

## Possess、重生和退出

- `Possess` 先处理旧 Pawn 和旧 Controller，再建立双向关系；Pawn 被其他 Controller 控制时会先让原 Controller `UnPossess`。
- `UnPossess` 只解除控制关系，不销毁 Pawn。PlayerState 进入旁观状态。
- `RestartPlayer` 销毁旧 Pawn，在 PlayerStart 重新创建默认 Pawn，并由原 Controller 占有。Controller 和 PlayerState 不会随一次死亡被替换。
- `Logout` 解除 Player、Controller、PlayerState 和 Pawn 的关系，从 GameState 移除 PlayerState，再销毁当前 World 的运行时对象。
- 地图替换时保留 `PGameInstance` 和 `PLocalPlayer`；旧 World 的 Controller、PlayerState、Pawn、GameMode 和 GameState 被清理，新 World 重新执行登录与出生流程。

这些关系都使用反射对象引用，并由 GC 引用扫描维持。跨帧调试记录使用 `FObjectHandle`，避免保存对象销毁后的裸指针。

## 输入职责迁移

Sandbox 的 WASD 输入现由 `PSandboxPlayerController` Tick 读取，再调用当前占有 Pawn 的 `MoveFromInput`。`PSandboxPawn` 不再主动寻找 InputSystem，也不再承担玩家身份判断。

这验证了 UE 风格职责划分：Controller 表达“谁在控制和给出意图”，Pawn 表达“被控制的场景实体”。后续 MovementComponent、物理和网络预测可以接在 Controller 输入与 Pawn 移动之间。

## 可视化验收

先启动编辑器：

```powershell
.\Build\Debug\PicoEditor.exe .\Projects\PicoSandbox\PicoSandbox.pico
```

1. 从顶部 `Add -> Player Start` 创建并选中 `PPlayerStart`，观察绿色胶囊和朝向箭头。也可以在 Scene Outliner 的 World/Level 上右键，通过 `Add Actor -> Player Start` 创建。
   作用：验证出生点是可保存的场景 Actor，位置和朝向来自编辑器场景，而不是运行时代码常量。
2. 点击绿色 Play。`Message Log` 使用绿色、黄色和红色分别记录信息、警告和错误；缺少 PlayerStart 时会打开黄色 `Play Validation`，可选择 `Play Anyway`，缺少 RootComponent 时会以红色错误阻止运行。
   作用：验证编辑场景在进入运行时之前能诊断出生配置。
3. 修改过场景后点击 Play，应出现 `Save & Play`，而不是静默覆盖文件。`Save` 写回当前文档，`Save As` 才选择新文件；未命名 World 第一次 Save 也会要求选择文件。
   作用：验证编辑器文档保存语义清晰，同时满足 Standalone 子进程只能从磁盘加载场景的限制。
4. 游戏窗口默认显示 `Gameplay Debug`，也可按 `F1` 隐藏或显示。确认 GameInstance、LocalPlayer、World、GameMode、GameState、PlayerController、PlayerState 和 Pawn 均有有效名称与 Handle。
   作用：验证完整登录链已经建立，并区分跨地图对象和 World 对象。
5. 按 WASD，Pawn 应相对当前游戏相机前后/左右移动；按 `R`，Pawn 应回到 PlayerStart。
   作用：验证输入经过 PlayerController，并由相机 Forward/Right 转为世界方向；重生经过 GameMode，而不是 Sandbox GameInstance 直接操纵 Pawn。
6. 点击 `UnPossess`。Pawn 仍存在，但 Controller 的 Pawn 为空，PlayerState 显示 Spectator。
   作用：验证“解除控制”和“销毁实体”是两个不同操作。
7. 点击 `Possess Last Pawn`。原 Pawn 再次被控制，Spectator 恢复为否。
   作用：验证双向 Possess 关系可以安全重建。
8. 点击 `Destroy Pawn`。Pawn Handle 失效，Controller 和 PlayerState 仍存在；再点击 `Restart Player`，应出现新的 Pawn Handle。
   作用：验证死亡只替换 Pawn，玩家身份和控制器保持稳定。
9. 点击 `Reload Map`。GameInstance 和 LocalPlayer Handle 保持不变；World、GameMode、GameState、PlayerController、PlayerState 和 Pawn Handle 更新。
   作用：验证编辑器/运行时 World 分离，以及跨地图对象与 World 生命周期对象的边界。
10. 查看面板事件区并点击 `Clear Events`。
   作用：验证对象链变化由 Handle 变化检测得出，并能观察销毁、重生和地图替换的先后顺序。

## 自动化验收

- `PicoEngineTests`：Possess、UnPossess、转移控制和销毁解绑。
- `PicoGameTests`：登录、PlayerState 登记、GC 可达性和地图替换重建。
- `PicoSandboxTests`：项目 Controller 输入、Pawn 重生及 Controller 保持。
- `PicoEditorTests`：PlayerStart 缺失、RootComponent 缺失和重复 ID 的 Play 前校验。

## 当前边界

- 仅实现 Standalone，本周没有网络 `PreLogin`、连接认证、Role 或 Replication。
- 没有 MatchState；Waiting、InProgress 和 PostMatch 属于第 4 周。
- 旁观仅表现为未占有 Pawn 和 PlayerState 标记，没有 SpectatorPawn。
- Pawn 仍使用简单 Transform 位移；统一 `MoveComponent`、Jolt 和 CharacterMovement 属于第 5 月。
- Gameplay Debug 是 Development 运行时观察面板，不是项目最终 HUD，也不负责修改持久化场景。
