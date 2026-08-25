# Agent 持久操作与崩溃恢复

本阶段补齐 Agent 在“工具已经产生副作用，但 Tool Result 尚未写入 Session”窗口内的跨进程恢复。目标不是把
编辑器变成数据库，而是让 World 保存、项目创建、Play 和打包这些高价值边界具备可判断、可重试、少残留的行为。

## 三层完成语义

有副作用的 Tool Call 现在经过：

```text
Prepared -> Executing -> Applied -> Committed
```

- `Prepared`：工具名、稳定 Call ID 和规范化参数已原子写入磁盘，副作用尚未开始。
- `Executing`：工具允许进入真实 Handler；此时崩溃的结果可能未知。
- `Applied`：Handler 的成功或失败结果已经持久化，但 Session 可能还没有 Tool Result。
- `Committed`：Tool Result 已成功追加到 JSONL Session，Runtime 随后提交 Operation Journal。

记录位于：

```text
<Project>/Saved/Agent/Operations/<CallId稳定哈希>.json
```

文件名只使用稳定哈希，Provider 不能通过 Call ID 构造路径穿越。相同 Call ID 若更换工具名或参数会失败关闭。
`Applied` 记录在会话恢复时直接返回原结果，真实 Handler 不会再次执行；聊天事件补写成功后转为 `Committed`。
AI Chat 的 `Agent Recovery` 区域会列出尚未提交的记录。

## 各工具边界

### World 和配置保存

现有 `.pworld` 和 `FConfigFile` 保存继续使用临时文件、Flush 和替换，不会让半写文件直接成为正式资产。Journal
解决“保存已经完成但 Session 未记录”的重复判断，文件层负责写入完整性。

### 创建项目

项目不再直接写入 `Projects/<Name>`。完整流程为：

```text
Projects/.AgentStaging/<Name>-<OperationHash>
 -> 复制 Content/Config
 -> 写 Descriptor 和项目 Config
 -> 写 .PicoProject.complete
 -> 同卷目录 Rename 到 Projects/<Name>
```

因此崩溃前只会留下隐藏 Staging，不会让 Project Browser 把半成品当成项目；正式目录仍坚持不覆盖已有项目。

### 打包

PicoPackager 原有 `.PicoStaging-*`、验证、旧 Stage 备份和最终目录替换保持不变。成功 Stage 新增
`PicoPackage.complete`，只有报告和完成标记都写成功才提交正式目录。Agent 的 Package Handler 在 Game Thread
启动进程后返回 `running`，Agent Worker 等待编辑器轮询到进程退出；只有退出码为 0、`PackageReport.ini`
报告成功且 `PicoPackage.complete` 状态为 Complete，最终 Tool Result 才写入 `completed/succeeded` 并进入
Operation Journal 的 Applied/Committed。失败或取消同样回写原 Tool Result，不再把“成功启动”当成“打包完成”。

### Play 与打包进程

Windows 下编辑器为每次 Play Session 和打包进程建立独立 Job Object，并启用
`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`。子进程先以挂起状态创建，加入 Job 后才恢复执行。编辑器正常 Stop、退出或
意外崩溃关闭 Job Handle 时，只终止该次 Pico 所属子进程，不影响其他 Windows 程序。项目切换时需要存活的新
编辑器进程不加入此 Job。

## 恢复判断

| 崩溃位置 | 重启后的处理 |
| --- | --- |
| `Prepared` 前 | 没有副作用，也没有恢复记录 |
| `Prepared/Executing` | 恢复面板显示未完成；恢复会话后由工具自身的原子/暂存边界安全重试 |
| `Applied` 后、Session 前 | 复用已保存结果，不重复调用 Handler，然后补写 Session |
| `Committed` 后 | Session 自带 ToolCall 幂等缓存；Journal 仅保留审计记录 |
| Play/Package 运行中编辑器崩溃 | Windows 关闭 Job 并终止所属进程；正式打包 Stage 保持上一次成功版本 |

## 自动化验收

- `PicoAgentTests`：71/71；验证 Journal 状态、重启复用、参数冲突关闭和 Call ID 路径安全。
- `PicoCoreTests`：98/98；验证挂起创建、Job 归属与关闭 Job 后终止子进程。
- `PicoPackagingTests`：12/12；验证完成标记、原子替换和失败保留上一份 Stage。
- `PicoEditorTests`：129/129；验证现有编辑器、Agent、事务、Play 设置和资产流程无回归。

## 当前边界

- 这不是跨机器分布式事务，也不尝试回滚已经发送到外部服务的请求。
- Package Tool 已回写异步最终状态；用户取消会请求 Game Thread 终止本次受 Job Object 管理的 Packager，并在
  真实退出后记录取消；编辑器关闭时 Journal 保留 Executing 记录供恢复面板判断，不会伪造成功结果。
- `Prepared/Executing` 只能依赖对应工具的幂等、原子写或暂存提交边界；以后新增项目写工具必须先声明自己的
  Commit Marker、Verifier 和清理策略，不能仅靠 Journal 获得安全性。
- Committed 记录作为审计保留，后续可增加按会话或保留期归档，而不是在当前阶段自动删除。
