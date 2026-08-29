# 工程深度第 4 周收尾：Frame Pacing Correctness

## 问题

Pico 原先同时使用 `EngineLoop::WaitForMaxFPS(60)` 和 GLFW VSync。EngineLoop 在 Render/Present 前睡眠，
随后 `glfwSwapBuffers` 可能再次等待显示刷新。Release Game 在 StarterWorld 中约为 `28 FPS`；保留 VSync 并
通过 `-maxfps=0` 关闭软件等待后达到 `59.2 FPS / 16.89 ms`，角色移动明显更平滑。

## 最终边界

```text
EngineLoop::Tick
  -> 只推进 FrameTimer、World、GC 和帧回调

Game/Editor Host
  -> Input -> Tick -> Render -> UI -> Present
  -> WaitForFrameLimit

无窗口 Host
  -> Tick -> WaitForFrameLimit(false)
```

`FFramePacingSettings::ResolveMode` 是唯一策略入口：有可用 Present 且 VSync 开启时返回 `VSync`；否则
`MaxFPS > 0` 返回 `Software`，`MaxFPS == 0` 返回 `Unlimited`。窗口最小化、Present 无法可靠阻塞时会回退到
Software，避免后台无限空转。

配置使用：

```ini
[Display]
VSync=true
MaxFPS=60
```

命令行 `-vsync=0/1` 和 `-maxfps=N` 优先于配置。旧项目中的 `[Engine] MaxFPS` 继续作为兼容回退。

## Windows 软件节拍

直接使用 `std::this_thread::sleep_for` 时，固定30帧实测每帧稳定多睡约8ms。Pico 改为线程本地高精度
Waitable Timer，并保留0.5ms短尾段校准。Release `PicoLaunch` 固定30帧、每档3样本的中位结果：

| 模式 | 实测 | 理论29个间隔 |
|---|---:|---:|
| Software 30 | 979.10 ms | 966.67 ms |
| Software 60 | 495.05 ms | 483.33 ms |
| Software 120 | 253.59 ms | 241.67 ms |
| Unlimited | 9.67 ms | 无等待 |

## 真实游戏验收

Release PicoSandboxGame、StarterWorld、640x360、固定60帧：

| 模式 | 进程总耗时 |
|---|---:|
| Default VSync | 1607.46 ms |
| Software 30 | 2561.16 ms |
| Software 60 | 1514.87 ms |
| Software 120 | 1004.96 ms |
| Unlimited | 555.44 ms |

约0.55秒是进程启动、资产注册、蓝图编译和场景加载固定成本。该表用于验证档位顺序和等待量，不代表逐帧
P95。Runtime `Mini GAS Status` 同时显示 FPS、平均帧时间和最终 Pacing Mode。

## 正确性

- Core 108/108；Engine 699/699；Editor 142/142；Game 53/53；Packaging 13/13；Sandbox 58/58；
- Debug 和 Release 均通过；
- Movement、Physics、Animation 与 Network 仍使用实际 Delta/各自时间步，没有绑定到渲染 FPS；
- 当前 `-server` 仍是带窗口的 Listen/Authority 进程，不冒充 Dedicated Server；真正无渲染服务器的独立
  TickRate 留给后续服务器生命周期任务。

## 后续

第 7 周继续增加 Frame P50/P95/P99、长帧计数、Game/Render/Present/Idle 分项、Editor 后台降频和不可见
Viewport/Preview 暂停。120 FPS 当前是性能余量模式，不是强制质量门槛。
