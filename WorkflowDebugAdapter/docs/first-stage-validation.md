# 第一阶段验证清单

## VSCode 入口

1. 打开仓库根目录 `D:\repos\Workflow-master`。
2. 在 VSCode 的“运行和调试”面板里可以直接选下面的配置：
   - `WorkflowDebugAdapter: 第一阶段烟雾测试`
   - `Workflow RuntimeTest x64`
   - `Workflow RuntimeTest Win32`
3. 在 VSCode 的“任务”面板里可以直接运行：
   - `Workflow: 第一阶段验证`
4. 这套配置用于验证第一阶段成果，不是最终的 Workflow 调试扩展入口。
5. 真正的调试链路是“VSCode 启动调试适配器，调试适配器监听端口，宿主程序连接进来”，不是 VSCode 自己直接监听 Workflow 进程。

## 自动化验证

1. 进入 `WorkflowDebugAdapter` 目录。
2. 执行 `npm test`。
3. 确认以下检查全部通过：
   - 协议与会话状态机
   - 传输层
   - 源码映射与断点同步
   - 暂停、继续、单步
   - 调用栈
   - 异常暂停
   - 第一阶段烟雾测试
   - 变量展开

## 运行时回归

1. `Test/Resources/Debugger/RaiseException.txt` 已经是当前阶段的最小异常样例。
2. `Test/UnitTest/RuntimeTest/TestDebugger.cpp` 里已经包含异常场景回归，重点覆盖：
   - `WfRuntimeExceptionInfo::GetMessage()`
   - `WfRuntimeExceptionInfo::GetFatal()`
   - `WfRuntimeExceptionInfo::GetCallStack()`
   - 暂停后读取帧信息和变量值
3. 如果后续改动触及 `Source/Runtime/WfRuntime.cpp`、`Source/Runtime/WfRuntimeDebugger.h` 或 `Source/Runtime/WfRuntimeDebugger.cpp`，再按下面顺序跑 `RuntimeTest`：
   - `copilotExecute.ps1 -Mode UnitTest -Executable RuntimeTest -Configuration Debug -Platform Win32`
   - `copilotExecute.ps1 -Mode UnitTest -Executable RuntimeTest -Configuration Debug -Platform x64`

## 手工确认

1. 以 `Debugger/RaiseException` 样例启动调试。
2. 验证断点能命中。
3. 验证继续后能恢复运行。
4. 验证单步后停在预期行号。
5. 验证调用栈能看到完整帧列表。
6. 验证局部、参数、捕获、全局变量可以按需展开。
7. 验证异常上报后会进入 `exception` 暂停状态。
