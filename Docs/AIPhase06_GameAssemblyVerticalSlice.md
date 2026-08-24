# AI 游戏搭建竖切：项目、场景、玩法与打包

本阶段把 Pico Agent 从“修改单个对象属性”扩展到一条受控的游戏搭建链路。它不是让模型执行任意 C++、Shell
或文件操作，而是让模型编排编辑器已经验证过的服务。

## 新增工具

```text
editor.asset.search
editor.scene.create_room
editor.gameplay.create_third_person_character
editor.actor.spawn_blueprint
editor.actor.delete
editor.play.validate
editor.play.start
editor.play.stop
editor.world.save
editor.project.create_from_third_person_template
editor.project.package
```

- `asset.search` 从活动项目的 AssetRegistry 查询真实资产，避免模型编造 `/Game/...` 路径。
- `scene.create_room` 一次创建带静态碰撞的地板和四面墙，整个操作只产生一个 Undo 记录。
- `gameplay.create_third_person_character` 只接受已注册的 `.pblueprint` 和 `.pcharprofile`，验证生成类是可构造
  `PPawn`，然后创建 Auto Possess Player 0 的角色与 PlayerStart。动画集、Mesh、多材质、相机和移动组件继续由
  Character Profile 与 Data-Only Actor Blueprint 组合，不在 Agent 中复制一套配置系统。
- `actor.spawn_blueprint` 使用 AssetRegistry 中真实存在的 `.pblueprint` 创建实例，并允许设置名称和 Transform；
  `actor.delete` 只删除可解析的当前 World Actor，两者都进入普通编辑器 Undo 事务。
- `play.validate` 复用正式编辑器的 Gameplay 校验；`world.save` 复用 World Document 安全保存。
- `play.start/stop` 复用编辑器 `FPlaySession`，真实启动或停止独立 `PicoGame`，不会把运行伪装为打包。
- `project.package` 复用 PicoPackager，只启动受控参数的打包进程，不向模型开放命令行。

## Play 与 Package 的严格分离

`Play` 是开发期快速运行当前项目，`Package` 是生成可分发 Stage。它们具有不同工具、参数、审批说明和返回值：

```text
“运行、启动、Play、测试项目” -> editor.play.start
“停止运行”                  -> editor.play.stop
“打包、导出 EXE、Stage”     -> editor.project.package
```

Provider System Prompt 会解释这一区别，但提示词本身不是安全边界。Game Thread Tool Executor 还会依据本轮原始
用户意图执行硬检查：运行意图调用 Package、或打包意图调用 Play 时，在进入审批和副作用之前直接拒绝，并返回
应使用的正确工具。因此旧会话、模型误判或历史中的打包示例都不能让“运行项目”再次启动 Packager。

## 新项目策略

`project.create_from_third_person_template` 在 `Engine/Projects/<Name>` 下创建内容型项目，复制当前已验证项目的
`Content` 与 `Config`，生成独立 `.pico` 描述符，但不会复制 `Saved`、API Key、临时文件和用户布局，也不会
覆盖同名项目。这类似于从受控项目模板创建工程，而不是让模型临时拼写 CMake 与游戏模块。

当前 Editor 进程只能编辑启动时加载的一个项目。创建结果会明确返回 `open_required=true`；用户需要用
`File -> Open Project` 打开新项目，之后 Agent 才能继续修改它的活动 World。项目切换后的自动恢复计划属于后续
Harness 工作，不在本阶段伪装成已经支持。

## 安全与事务边界

- `ModifyWorld`：必须批准，使用完整 World 快照事务，可 Undo/Redo，失败整体回滚。
- `WriteProject`：必须批准，使用文件操作自身的原子/清理边界，不创建无意义的 World 事务。
- `LaunchProcess`：必须批准，只调用编辑器持有的 `FPlaySession` 或 PicoPackager 服务，不开放任意可执行文件与参数。
- 新项目名只允许字母、数字和下划线，目标固定在 Engine 的 `Projects` 目录，并拒绝覆盖。
- 通用属性修改仍只允许反射标记为 `Editable` 的支持类型。

## 推荐提示词

在已有项目中搭建并打包：

```text
先检查当前世界和可用角色资产。创建一个 1200x900、高 350 的碰撞房间；如果没有玩家角色，使用 Knight
的 Actor Blueprint 和 Character Profile 在 (0,0,120) 创建第三人称角色。校验玩法，保存世界，然后把
Development 包输出到 E:/PicoPackages，包名为 AgentDemo，并运行 smoke test。
```

创建独立项目：

```text
从第三人称模板创建一个名为 AgentDemo 的新 Pico 项目。不要覆盖已有项目，并告诉我生成的项目文件路径。
```

## 自动化验收

- `PicoAgentTests` 验证项目写入获得批准但不会错误开启 World 事务。
- `PicoEditorTests` 验证 18 个工具完成注册，房间包含五个碰撞部件，并可由一次普通 Undo 完整移除。
- 自动化覆盖 Blueprint Actor 创建/删除、Play 启停服务和 Play/Package 意图冲突的副作用前拒绝。
- Debug 下 Agent 26 项、Editor 126 项测试通过。
