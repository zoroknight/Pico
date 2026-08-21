# 角色网络移动低延迟可视化验收

本文用于验证协议 v2 时间诊断、自适应平滑、SimulatedProxy 限时外推和 AutonomousProxy 校正表现。测试必须使用
同一次构建生成的服务器与客户端；旧协议进程不能混入。

## 准备

1. 启动 `BuildCodex/Release/PicoEditor.exe`，打开 PicoSandbox 与 `StarterWorld.pworld`。
2. 在 Play 下拉菜单选择 `Separate Server + Clients (Visible)`，Players 设为 2。
3. 初始设置 `Target RTT=0`、`Jitter=0`、`Packet Loss=0`，启动一个服务器和两个客户端。
4. 三个窗口都按 `F1`。确认连接保持 Open，Invalid Packets、Rejected 和 Unresolved Refs 不持续增加。

这一步建立本机基线。若 0 ms 下已经有明显停顿，应先查看服务器与客户端 FPS，不能把本机帧率问题归因于网络。

## F1 指标

- `Move RTT`：所属客户端生成 Move 到收到服务器确认的平滑往返时间。它主要用于判断输入链路，不代表另一个客户端
  最终看到角色所需的全部时间。
- `Remote smoothing adaptive`：当前实际 Mesh 平滑窗口；稳定 60 Hz 快照通常接近 25 ms，抖动或漏包后允许向
  100 ms 增大。
- `recv`：本客户端相邻 Snapshot 的接收间隔。
- `server`：服务器生成相邻 Snapshot 的间隔。稳定 60 Hz 时约为 16.7 ms。
- `jitter`：接收间隔与服务器间隔差异的平滑值。它高时增大平滑窗口是预期行为。
- `transit`：根据服务器时间映射和连接 RTT 估算的单程 Snapshot 传输时间。
- `clock`：客户端 World 时间相对服务器 World 时间的平滑偏移。不同进程启动时刻不同，因此它不需要接近 0；应当
  稳定而不是持续漂移。
- `extrapolation`：当前快照后已经外推的时间。断包时最多到配置上限，随后停住；`clamps` 增加表示保护触发。
- `Remote visual root Z / Mesh Z / delta`：分别表示远端权威 Actor 高度、最终渲染 Mesh 高度和有符号视觉偏差；
  `anim/time` 用于判断动画状态是否切换或重新开始。它们用于区分真实位置变化和单纯 Mesh 平滑表现。

## 四组对比

每组都在客户端 1 连续执行直行、突然反向、左右急转和跳跃，并同时观察服务器与客户端 2。每次修改 Play 参数后
停止整组进程再重新启动，记录客户端 2 的 F1 数值。

| 组别 | Target RTT | Jitter | Loss | 验收目的 |
|---|---:|---:|---:|---|
| A | 0 ms | 0 ms | 0% | 测量本机帧调度与渲染基线 |
| B | 40 ms | 0 ms | 0% | 验证正常近距离网络下低延迟窗口 |
| C | 80 ms | 10 ms | 1% | 验证动态窗口随抖动扩大后仍连续 |
| D | 150 ms | 10 ms | 5% | 验证高延迟与丢包下有界外推和最终收敛 |

## 通过标准

- 客户端 1 的 WASD、镜头和跳跃保持立即响应；网络 RTT 不应被直接加到本地输入手感上。
- 客户端 2 观察稳定移动时应连续，不能每个 Snapshot 停顿一次。
- A/B 组的有效平滑时间应明显低于旧固定 100 ms；稳定样本通常落在 25～50 ms。
- C/D 组允许平滑时间增大，但角色停止操作后必须最终与服务器位置收敛。
- Pending Move、Server Move Queue、Snapshot Buffer、Delayed Packets 和 Reliable Queue 不能持续单调增长。
- D 组出现连续丢包时，外推到上限必须停止，不能无限沿旧速度漂移；恢复收包后应重新收敛。

起步、急停和急转在客户端 2 上仍会受到真实传输时间限制。平滑只能消除快照跳变，不能在数据到达前知道客户端 1
的新输入；因此这类延迟应与 `transit`、目标 RTT 一起判断，不能以画面与本地所属客户端完全同时作为通过标准。

## 配置对照

在 Actor Blueprint 中选中 `CharacterMovement` 可调整：

- `Use Adaptive Network Smoothing`：默认开启；关闭后回到固定窗口。
- `Network Min Adaptive Smooth Time`：默认 0.025 秒，不建议在完成多组抖动测试前继续降低。
- `Network Max Adaptive Smooth Time`：默认 0.1 秒，限制坏网络下的额外视觉拖尾。
- `Network Simulated Smooth Location Time`：关闭自适应时使用的固定窗口。
- `Enable Simulated Proxy Extrapolation`：控制远端快照间外推。
- `Network Max Simulated Proxy Extrapolation Time`：默认 0.2 秒，防止断包后无限漂移。

对比算法时一次只改变一个配置，并保存每组 F1 截图。若出现异常，优先记录 `Move RTT、server、recv、jitter、
transit、effective smoothing、extrapolation`，再修改代码或参数。

四组人工对比用于手感调优，不要求每次开发都等待 10 分钟。CI 中的 36,000 帧快速压力回归负责持续检查
150 ms RTT、约 5% 快照丢失时的有界资源和最终收敛；真实长稳运行留到公网或发布候选版本。
