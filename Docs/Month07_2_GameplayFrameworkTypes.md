# 第 4 项目月第 2 周：Gameplay Framework 类型与所有权

本周建立 UE Gameplay Framework 的第一层对象模型。目标是先回答“对象代表什么、由谁持有、随谁销毁”，
暂不实现完整登录、占有、重生和比赛状态机。

## 1. 对象关系

```text
PGameInstance (跨地图)
  -> PLocalPlayer
       -> weak PlayerController

PWorld (当前地图)
  -> PGameModeBase (运行时、Transient)
       -> strong PGameStateBase
       -> CDO default Gameplay classes
  -> PGameStateBase (运行时、Transient)
       -> strong PlayerState list
  -> PPlayerController
       -> weak PPawn
       -> strong PPlayerState
  -> PPawn
       -> weak PController
  -> PPlayerStart (场景对象、可持久化)
```

`PLocalPlayer` 是 `PGameInstance` 的子对象，所以地图替换后仍保持同一实例。其 PlayerController 只是当前
World 的关联，World cleanup 时由 GameInstance 的 Dispatch 层强制清空，不依赖项目子类调用 Super。

GameMode、GameState、Controller、PlayerState 和 Pawn 都是 Level 中的 Actor。World/Level 才是它们的
生命周期所有者；Controller 与 Pawn 的双向关系使用弱对象指针，任一对象销毁后都不会保活或返回旧地址。

## 2. GameMode CDO 默认类

`PGameModeBase` 的 CDO 保存四个原生 `PClass` 默认值：

- Default Pawn Class
- PlayerController Class
- PlayerState Class
- GameState Class

只有 CDO 可以调用设置接口，运行时 GameMode 实例只读取其实际类的 CDO。项目模块注册
`PSandboxGameMode` 后，把 `PSandboxPawn::StaticClass()` 写入该 CDO，再通过
`IGameModule::GetGameModeClass` 交给 GameEngine。

Pico 当前反射属性尚未支持 Class Reference，因此这四个值暂时是 CDO 原生类元数据，不进入 Details
Panel 或 `.pworld`。后续增加 Class Property 时可以把配置开放到项目设置或 WorldSettings，不改变
GameMode 对外接口。

## 3. 运行时与场景对象分离

`FGameEngine::LoadMap` 在 World 数据成功替换后调用：

```text
World::InitializeGameplay(GameModeClass)
  -> spawn transient GameMode
  -> GameMode reads CDO
  -> spawn transient GameState
  -> GameInstance::OnWorldInitialized
```

编辑器只使用 `FEngineLoop`，不会自动生成运行时规则对象。World capture 也跳过 Transient Actor，避免
GameMode、GameState、Controller 和 PlayerState 被误写回场景。

`PPlayerStart` 则是关卡作者放置的 Actor：它拥有默认 SceneComponent 根节点、可编辑的
`PlayerStartId` 和 Transform。Scene Outliner 的 Add Actor 菜单可以创建它，Undo/Redo、保存和 World
重建沿用现有通用链路。

## 4. Tick 与网络预留

- PlayerController 和 Controller 使用 `PrePhysics`，为下一阶段输入命令预留时序。
- Pawn 保留 Actor Tick，未来 Movement Tick 可依赖 Controller Tick。
- GameMode、GameState、PlayerState 和 PlayerStart 默认不 Tick。
- Pawn/Controller 关联与 PlayerState 数据带 Transient/Replicated 元数据，当前只描述意图；真正的
  Replication Layout 和 ActorChannel 在网络阶段实现。
- GameMode 未来只在服务器创建；本周 Standalone 没有 NetMode 分支，因此先走唯一权威 World。

## 5. Sandbox 兼容路径

`PSandboxPawn` 已从 `PActor` 迁移为 `PPawn`。为了不在类型周同时引入登录和输入迁移，
`PSandboxGameInstance` 暂时仍在 World 初始化后直接生成 Pawn，Pawn 也暂时直接读取 InputSystem。
第 3 周会把生成责任交给 GameMode，把输入入口移到 PlayerController，并建立 LocalPlayer 到 Controller
的关联。

## 6. 验收覆盖

- GameMode CDO 默认类验证和非法父类拒绝。
- World GameMode/GameState 创建、Transient 标记和关系。
- Controller/Pawn 双向关联及 Pawn 销毁后的弱句柄失效。
- PlayerController/PlayerState 和 GameState 玩家列表。
- Gameplay 类型 Tick 默认策略。
- Runtime Gameplay Actor 不进入 World 资产。
- PlayerStart ID、默认根节点、Transform、编辑器命令和 World 重建。
- GameInstance/LocalPlayer 经 GC 和地图替换后保持同一实例。
- Sandbox GameMode CDO 选择项目 Pawn，旧 WASD Demo 不退化。

## 7. 下一阶段

第 3 周实现行为链：Standalone Login、PostLogin、PlayerController/PlayerState 创建、PlayerStart 选择、
RestartPlayer、Possess/UnPossess、Pawn 重生和基础旁观。当前受保护的 `SetPawn/SetPlayerState` 只是维持
内部引用一致性的原语，不是最终 Gameplay API。
