# Workflow 远程调试方案

## 目标

1. 在 VSCode 中通过标准调试按钮调试 Workflow 脚本。
2. 支持远程 attach，宿主程序和 VSCode 不必在同一台机器。
3. 第一阶段已经完成断点、继续、单步、步出、调用栈、变量展开和异常暂停。
4. 后续再补条件断点、日志断点、`pause` 请求、异常断点筛选和 `launch` 入口。

## 当前状态

### 已完成

1. `attach` 会话链路、内嵌 `debugServer`、宿主连接与握手。
2. 断点同步、校验、回传，以及 `codeIndex + row` 映射。
3. `continue`、`next`、`stepIn`、`stepOut`。
4. `stackTrace`、`scopes`、`variables`。
5. 异常暂停上报和 `stopped` 事件。
6. 变量按需展开与句柄表。
7. `evaluate` / REPL / hover 求值，支持对象展开与基础表达式运算。
8. 第一阶段烟雾测试和运行时回归测试。

### 仍未支持

1. `pause` 请求。
2. 条件断点的真实执行语义。
3. 日志断点的真实执行语义。
4. `setExceptionBreakpoints` 的异常类型筛选。
5. `launch` 入口。

### 说明

1. `stepOut` 已落地，不再列为待做。
2. `evaluate` 已在 DAP 适配器侧实现，当前支持暂停态下的 REPL、hover 和基础对象展开。
3. 当前实现是 `attach` 模式，标准调试入口先通过 VSCode 连接到内嵌 `debugServer`，再由宿主程序连入。
4. 如果后续要继续补功能，优先看 `WorkflowDebugAdapter/src/dapServer.ts`、`WorkflowDebugAdapter/src/sessionMachine.ts`、`WorkflowDebugAdapter/src/breakpointRegistry.ts`。

## 方案取舍

| 方案 | 结论 | 原因 |
| --- | --- | --- |
| 目标端直接实现 DAP | 不选 | 目标端会被 VSCode 协议绑死，宿主程序集成成本高，移植性差。 |
| VSCode DAP + 主进程内 WorkflowDebugHost.dll | 选用 | VSCode 和运行时职责分离，桥接层随主进程加载，既保留远程能力又不把调试代理做成独立目标进程。 |

## 协议草图

### 总体链路

```text
VSCode
  -> DAP 调试适配器
      -> WorkflowDebugHost.dll
          -> WfDebugger
              -> Workflow Runtime
```

### 建议会话流程

```text
1. VSCode 启动调试会话。
2. 调试适配器开启监听或连接目标端。
3. 主进程内的 `WorkflowDebugHost.dll` 建立 TCP 连接。
4. 双方交换 hello、版本号、能力集、源码映射信息。
5. 调试适配器下发断点。
6. 运行时命中断点后，目标端上报 stopped。
7. VSCode 按需拉取 stackTrace、scopes、variables。
8. 用户点击继续或单步，适配器转发到主进程内库。
9. 主进程内库返回执行结果和新的暂停状态。
```

### 消息外壳

建议统一使用轻量 JSON 行协议，便于调试和抓包。

```json
{
  "type": "request",
  "seq": 12,
  "replyTo": 11,
  "cmd": "setBreakpoints",
  "sessionId": "wf-20260423-001",
  "body": {}
}
```

### 消息类型

| 消息 | 方向 | 作用 | 关键字段 |
| --- | --- | --- | --- |
| `hello` | 目标端 -> 适配器 | 完成握手，汇报版本和能力 | `runtimeVersion`、`capabilities`、`sourceMap` |
| `initialize` | 适配器 -> 目标端 | 初始化会话和路径规则 | `workspaceRoot`、`pathMapping`、`supports` |
| `setBreakpoints` | 适配器 -> 目标端 | 下发断点 | `sourcePath`、`codeIndex`、`rows`、`condition`、`logMessage` |
| `breakpointValidated` | 目标端 -> 适配器 | 回写断点校验结果 | `breakpointId`、`verified`、`reason` |
| `stopped` | 目标端 -> 适配器 | 通知暂停 | `reason`、`threadId`、`frameId`、`sourceId`、`row` |
| `stackTrace` | 适配器 -> 目标端 | 查询调用栈 | `threadId`、`startFrame`、`levels` |
| `scopes` | 适配器 -> 目标端 | 查询当前帧的作用域 | `frameId` |
| `variables` | 适配器 -> 目标端 | 查询变量展开内容 | `variablesReference`、`scopeKind`、`frameId` |
| `evaluate` | 适配器侧处理 | 断点态求值 | `expression`、`frameId`、`context` |
| `continue` | 适配器 -> 目标端 | 恢复运行 | `threadId` |
| `next` | 适配器 -> 目标端 | 单步越过 | `threadId` |
| `stepIn` | 适配器 -> 目标端 | 单步进入 | `threadId` |
| `stepOut` | 适配器 -> 目标端 | 单步步出 | `threadId` |
| `exception` | 目标端 -> 适配器 | 异常暂停 | `message`、`fatal`、`callStack` |
| `output` | 目标端 -> 适配器 | 输出调试日志 | `level`、`message` |
| `disconnect` | 双向 | 结束会话 | `reason`、`restart` |

### 关键约定

1. `sourceId` 建议直接使用 Workflow 的 `codeIndex`，因为运行时已经以它作为源码映射主键。
2. `frameId` 建议对应 `stackFrames` 的索引，便于把调用栈和变量面板直接对齐。
3. `variablesReference` 需要在适配器侧维护句柄表，避免一次性展开整个对象图。
4. 变量值、路径、源码内容都统一 UTF-8 传输，避免远程环境编码不一致。

## VSCode 端模块清单

| 模块 | 职责 | 主要接口 |
| --- | --- | --- |
| `extension.ts` | 注册调试器、配置默认值和输出通道 | `activate()`、`deactivate()` |
| `debugAdapterServerHost.ts` | 承载内嵌调试服务器 | `start()`、`dispose()` |
| `dapMain.ts` | 调试适配器入口 | `main()` |
| `dapServer.ts` | DAP 会话入口，处理 attach / 断点 / 继续 / 单步 / 栈 / 变量 / 求值 | `initialize`、`attach`、`setBreakpoints`、`continue`、`next`、`stepIn`、`stepOut`、`evaluate` |
| `sessionMachine.ts` | 协议状态机和请求响应转换 | `attach()`、`receiveHello()`、`receiveInitialize()`、`receiveStackTrace()`、`receiveVariables()` |
| `bridgeTransport.ts` | 维护 TCP 连接和消息分帧 | `connect()`、`listen()`、`send()`、`onMessage()` |
| `protocol.ts` / `dapProtocol.ts` | 协议类型和 DAP 类型 | `createRequestEnvelope()`、`createResponseEnvelope()` |
| `sourceMap.ts` | 维护 `codeIndex`、源码路径、行号映射 | `registerSource()`、`resolveByPath()`、`resolveByCodeIndex()` |
| `breakpointMapper.ts` / `breakpointRegistry.ts` | 把 VSCode 断点转换成 Workflow 断点 | `toRemoteBreakpoints()`、`mergeValidatedState()`、`applyBreakpoints()` |
| `stackModel.ts` / `stackInspector.ts` | 把远端调用栈转换成 DAP `StackFrame` | `buildStackFrames()`、`applyException()` |
| `scopeModel.ts` / `variableModel.ts` / `valueInspector.ts` / `handleTable.ts` | 构建变量树和句柄表 | `buildScopes()`、`buildVariables()`、`allocateHandle()` |
| `attachTimeout.ts` | 处理 attach 等待超时 | `normalizeConnectTimeoutMs()`、`waitForOptionalTimeout()` |
| `logFormat.ts` / `diagnosticTrace.ts` | 处理日志格式和诊断跟踪 | `formatDebugMessage()`、`traceDebugMessage()` |

## 主进程内库 / DLL 模块清单

| 模块 | 职责 | 主要接口 |
| --- | --- | --- |
| `WorkflowDebugHost` | 主进程内调试宿主入口 | `initialize()`、`shutdown()`、`createSession()`、`destroySession()` |
| `WorkflowDebugSession` | 单次调试会话对象 | `attach()`、`detach()`、`dispatch()`、`snapshot()` |
| `WorkflowDebugBridge` | 把协议命令映射到 `WfDebugger` | `onInitialize()`、`onSetBreakpoints()`、`onContinue()`、`onStep()`、`onStackTrace()` |
| `WorkflowDebugTransport` | TCP 收发和消息分帧 | `send()`、`receive()`、`close()` |
| `WorkflowDebugSessionState` | 维护会话状态机 | `Connected`、`Ready`、`Paused`、`Running`、`Stopped` |
| `WorkflowDebugSourceCatalog` | 维护 `codeIndex -> 源码路径` 映射 | `registerSource()`、`resolvePath()`、`resolveCodeIndex()` |
| `WorkflowDebugBreakpointRegistry` | 维护断点表并调用 `AddCodeLineBreakPoint` | `applyBreakpoints()`、`clearBreakpoints()` |
| `WorkflowDebugStackInspector` | 读取当前线程上下文和调用栈 | `collectStackTrace()`、`collectCurrentFrame()` |
| `WorkflowDebugValueInspector` | 读取局部、参数、捕获、全局变量 | `collectScopes()`、`collectVariables()` |
| `RemoteWfDebugger` | 绑定运行时线程并接收暂停回调 | `OnStartExecution()`、`OnBlockExecution()`、`OnStopExecution()` |
| `WorkflowDebugRuntimeBinding` | 装配与拆卸 `WfDebugger` | `bindCurrentThread()`、`unbindCurrentThread()` |
| `ExpressionEvaluator` | 断点态安全求值器 | `evaluate()`、`compileExpression()` |
| `LogChannel` | 输出运行时日志和诊断信息 | `emitOutput()`、`emitException()` |

> 说明：`HandleTable` 只存在于 `WorkflowDebugAdapter` 侧，用来把主进程内库返回的远程变量引用转换成 DAP 的本地句柄，不放进 `WorkflowDebugHost`。

## 复用点

| 现有类型 | 用途 | 说明 |
| --- | --- | --- |
| `WfDebugger` | 断点和运行控制 | 直接复用 `Run / Pause / Stop / StepOver / StepInto / StepOut` |
| `WfRuntimeThreadContext` | 当前线程、栈帧、执行状态 | 直接用于栈和断点命中信息 |
| `WfRuntimeCallStackInfo` | 变量视图 | 适合做 DAP 的作用域和变量展开 |
| `WfRuntimeExceptionInfo` | 异常暂停 | 可直接映射成 `exception` stopped reason |
| `WfAssembly` | 源码与指令映射 | 用于 `codeIndex`、行号、源码定位 |

## WorkflowDebugHost 目录结构

### 推荐布局

| 路径 | 角色 | 说明 |
| --- | --- | --- |
| `WorkflowDebugHost/WorkflowDebugHost.h` | 对外入口 | 导出宿主库的初始化、销毁和会话创建接口。 |
| `WorkflowDebugHost/WorkflowDebugHost.cpp` | 库入口实现 | 绑定主进程的脚本执行生命周期。 |
| `WorkflowDebugHost/WorkflowDebugSession.h` | 会话对象声明 | 持有一次调试会话所需的全部状态。 |
| `WorkflowDebugHost/WorkflowDebugSession.cpp` | 会话对象实现 | 负责 attach / detach、消息路由和状态切换。 |
| `WorkflowDebugHost/WorkflowDebugBridge.h` | 协议桥声明 | 把调试协议翻译成 `WfDebugger` 调用。 |
| `WorkflowDebugHost/WorkflowDebugBridge.cpp` | 协议桥实现 | 处理 hello / initialize / setBreakpoints / continue / next / stepIn / stackTrace / scopes / variables / exception。 |
| `WorkflowDebugHost/WorkflowDebugTransport.h` | 传输层声明 | 屏蔽 socket、分帧和收发细节。 |
| `WorkflowDebugHost/WorkflowDebugTransport.cpp` | 传输层实现 | 负责与 VSCode 调试适配器双向通信。 |
| `WorkflowDebugHost/WorkflowDebugSessionState.h` | 状态机声明 | 管理 Connected / Ready / Paused / Running / Stopped。 |
| `WorkflowDebugHost/WorkflowDebugSessionState.cpp` | 状态机实现 | 处理序列号、会话切换和暂停恢复。 |
| `WorkflowDebugHost/WorkflowDebugSourceCatalog.h` | 源码目录声明 | 维护 `codeIndex -> 路径`、路径别名和行号映射。 |
| `WorkflowDebugHost/WorkflowDebugSourceCatalog.cpp` | 源码目录实现 | 处理 `hello` 与 `initialize` 下发的映射信息。 |
| `WorkflowDebugHost/WorkflowDebugBreakpointRegistry.h` | 断点登记声明 | 将 `setBreakpoints` 映射成 `AddCodeLineBreakPoint`。 |
| `WorkflowDebugHost/WorkflowDebugBreakpointRegistry.cpp` | 断点登记实现 | 维护断点校验、清理和复用。 |
| `WorkflowDebugHost/WorkflowDebugStackInspector.h` | 调用栈读取声明 | 从 `WfDebugger` / `WfRuntimeThreadContext` 组装栈帧。 |
| `WorkflowDebugHost/WorkflowDebugStackInspector.cpp` | 调用栈读取实现 | 为 `stackTrace` 和异常暂停生成统一栈数据。 |
| `WorkflowDebugHost/WorkflowDebugValueInspector.h` | 变量读取声明 | 从 `WfRuntimeCallStackInfo` 读取局部、参数、捕获、全局变量。 |
| `WorkflowDebugHost/WorkflowDebugValueInspector.cpp` | 变量读取实现 | 生成 `scopes / variables` 响应所需的数据。 |
| `WorkflowDebugHost/WorkflowDebugRuntimeBinding.h` | 运行时绑定声明 | 管理 `SetDebuggerForCurrentThread` / `ResetDebuggerForCurrentThread`。 |
| `WorkflowDebugHost/WorkflowDebugRuntimeBinding.cpp` | 运行时绑定实现 | 在主进程脚本执行前后装配和拆卸 `WfDebugger`。 |

### 类图

```text
MainProcess
  └─ loads WorkflowDebugHost.dll
      └─ WorkflowDebugHost
          └─ createSession(sessionId)
              └─ WorkflowDebugSession
                  ├─ WorkflowDebugTransport
                  ├─ WorkflowDebugBridge
                  ├─ WorkflowDebugSessionState
                  ├─ WorkflowDebugSourceCatalog
                  ├─ WorkflowDebugBreakpointRegistry
                  ├─ WorkflowDebugStackInspector
                  ├─ WorkflowDebugValueInspector
                  └─ RemoteWfDebugger : WfDebugger

WorkflowDebugTransport <--> VSCode DAP Adapter
WorkflowDebugBridge --> RemoteWfDebugger
RemoteWfDebugger --> WfDebugger
WorkflowDebugStackInspector --> WfDebugger / WfRuntimeThreadContext
WorkflowDebugValueInspector --> WfRuntimeCallStackInfo
WorkflowDebugBreakpointRegistry --> WfDebugger::AddCodeLineBreakPoint
WorkflowDebugRuntimeBinding --> SetDebuggerForCurrentThread / ResetDebuggerForCurrentThread
```

### 职责边界

1. `WorkflowDebugSession` 只管会话生命周期和资源所有权，不直接解析协议。
2. `WorkflowDebugBridge` 只管协议到运行时的翻译，不碰 socket 细节。
3. `WorkflowDebugTransport` 只管消息收发，不理解断点、调用栈或变量语义。
4. `RemoteWfDebugger` 才是真正挂到运行时线程上的调试对象，负责暂停、继续和单步。
5. `WorkflowDebugStackInspector` 和 `WorkflowDebugValueInspector` 只读取运行时快照，不维护 UI 状态。

## 落地顺序

1. 先做桥接协议和会话握手，保证远程 attach 能连上。
2. 再做断点同步、继续、暂停、单步。
3. 然后补调用栈和变量展开。
4. 最后补 `pause`、条件断点、日志断点、表达式求值和异常断点筛选。

## 风险点

1. `pause`、条件断点和日志断点还没有真正接到 DAP 入口，语义需要继续补齐。
2. Workflow 调试信息只有 `codeIndex + row`，没有天然的绝对路径，路径映射必须独立维护。
3. `evaluate` 已采用受限表达式解析器实现，不走 `eval` / `loadstring`。
4. 多线程和协程场景下，`threadId`、`frameId`、暂停恢复逻辑要严格区分。
5. 变量面板不要一次性展开整棵对象图，必须使用适配器侧句柄表按需展开。

## 当前剩余改动范围

1. `WorkflowDebugAdapter/src/dapServer.ts`
2. `WorkflowDebugAdapter/src/sessionMachine.ts`
3. `WorkflowDebugAdapter/src/breakpointRegistry.ts`
4. `WorkflowDebugAdapter/src/breakpointMapper.ts`
5. `WorkflowDebugAdapter/src/protocol.ts`
6. `WorkflowDebugAdapter/package.json`
7. 如果后续要把条件断点真正下沉到运行时，再看 `Source/Runtime/WfRuntimeDebugger.h` 和 `Source/Runtime/WfRuntimeDebugger.cpp`

## 结论

推荐采用“VSCode DAP 适配器 + 主进程内 `WorkflowDebugHost.dll` + 现有 `WfDebugger`”的三层结构。当前仓库里这条主链路已经落地，可用范围是 `attach -> 断点 -> 暂停 -> 调用栈/变量 -> 继续/单步 -> 异常暂停 -> 求值`。剩余工作主要是 `pause`、条件/日志断点、异常断点筛选和 `launch` 入口。

## 第一阶段拆解

> 说明：本节保留原始拆解作为设计背景，当前仓库里任务 1 到任务 8 的主体链路已经完成，现阶段重点是上面的“当前未支持”项。

### 第一阶段目标

先打通一个最小闭环，让 Workflow 可以在 VSCode 里完成以下动作：

1. 远程 attach 成功。
2. 断点可下发、可校验、可命中。
3. 命中后能暂停、继续、单步。
4. 能看到调用栈和变量。

### 当前未支持

1. 不做条件断点。
2. 不做日志断点。
3. 不做 `pause` 请求。
4. 不做 `setExceptionBreakpoints` 的异常类型筛选。
5. 不做 `launch` 入口。
6. 不做复杂对象图的深度展开优化，只做按需句柄展开。

### 任务 1：协议骨架和会话状态机

**目标**

把 VSCode 端和目标端之间的消息格式、序列号、请求响应、心跳和错误码先统一。

**交付物**

1. 一份协议定义草案。
2. VSCode 端会话状态机。
3. 主进程内库会话状态机。

**验收标准**

1. 双方可以完成 `hello -> initialize -> ready`。
2. 任意请求都能带上 `seq` 和 `replyTo`。
3. 断线后不会把旧会话状态误带入新会话。

### 任务 2：桥接传输层

**目标**

封装 TCP 收发、粘包拆包、重连和关闭流程，让上层只看消息对象，不直接碰 socket。

**交付物**

1. VSCode 端 `bridgeTransport`。
2. 主进程内库 `WorkflowDebugTransport`。
3. 消息分帧和超时处理。

**验收标准**

1. 可以稳定收发一问一答消息。
2. 可以处理多条连续消息。
3. 可以处理半包和拆包。

### 任务 3：源码映射和断点同步

**目标**

把 VSCode 的文件路径断点转换成 Workflow 的 `codeIndex + row` 断点。

**交付物**

1. `SourceCatalog`。
2. `breakpointMapper`。
3. 主进程内库 `WorkflowDebugBreakpointRegistry`。

**验收标准**

1. 同一文件的断点能正确映射到 `AddCodeLineBreakPoint`。
2. 断点校验结果可以回传 VSCode。
3. `codeIndex` 与实际源码定位一致。

### 任务 4：暂停、继续、单步

**目标**

把 DAP 的 `continue / next / stepIn` 映射到 `WfDebugger::Run / StepOver / StepInto`。

**交付物**

1. VSCode 端 `continueRequest / nextRequest / stepInRequest`。
2. 主进程内库 `onContinue / onStep`。
3. 暂停事件上报 `stopped`。

**验收标准**

1. 断点命中后能正确进入暂停态。
2. 点击继续后程序恢复执行。
3. 单步后能停在预期行号。

### 任务 5：调用栈

**目标**

把当前线程的调用栈转换成 DAP `StackFrame`。

**交付物**

1. 主进程内库 `WorkflowDebugStackInspector`。
2. VSCode 端 `stackModel`。
3. `stackTraceRequest` 实现。

**验收标准**

1. 调用栈层级与 `stackFrames` 一致。
2. 每一帧能显示正确函数名和行号。
3. 不同帧切换时可以稳定请求变量。

### 任务 6：变量展开

**目标**

把 `local / argument / captured / global` 四类作用域暴露给 VSCode，并支持按需展开。

**交付物**

1. 主进程内库 `WorkflowDebugValueInspector`。
2. VSCode 端 `scopeModel`。
3. VSCode 端 `variableModel` 和句柄表。

**验收标准**

1. 作用域能分成 Local、Argument、Captured、Global。
2. 变量值显示正确。
3. 容器类型可继续展开，但不会一次性递归爆掉。

### 任务 7：异常暂停

**目标**

把 `WfRuntimeExceptionInfo` 映射成 DAP 的异常停止原因。

**交付物**

1. 主进程内异常上报。
2. VSCode 端 `exception` stopped reason。

**验收标准**

1. 运行时异常能在 VSCode 暂停。
2. 异常信息和调用栈能看见。

### 任务 8：测试和验证

**目标**

给第一阶段准备最小可验证样例，保证每个子任务都有可回归的检查点。

**交付物**

1. 远程调试最小样例。
2. 断点、单步、栈、变量的验证清单。
3. 必要时补 RuntimeTest。

**验收标准**

1. 第一阶段闭环能手工跑通。
2. 关键能力有自动化回归测试。
3. 文档里的协议字段和实际实现保持一致。

## 第一阶段依赖关系

1. 任务 1 是所有后续任务的前置。
2. 任务 2 依赖任务 1。
3. 任务 3、4 依赖任务 2。
4. 任务 5、6 依赖任务 3、4。
5. 任务 7 可与任务 5、6 并行推进，但建议在栈和变量稳定后再接入。
6. 任务 8 贯穿整个第一阶段，不能拖到最后才做。

## 第一阶段建议落地顺序

1. 先实现协议骨架和传输层。
2. 再实现断点同步和暂停恢复。
3. 然后实现调用栈。
4. 再实现变量展开。
5. 最后补异常暂停和回归测试。

## 第一阶段文件级清单

### 当前仓库内

| 文件 | 角色 | 第一阶段动作 |
| --- | --- | --- |
| `Source/Runtime/WfRuntime.cpp` | 运行时变量/调用栈辅助实现 | 已完成 `WfRuntimeCallStackInfo::GetVariables` 的空指针逻辑修复，当前可复用异常栈和作用域。 |
| `Test/UnitTest/RuntimeTest/TestDebugger.cpp` | 运行时调试回归测试 | 已补异常、暂停后读取帧信息和变量值的回归用例。 |
| `Source/Runtime/WfRuntimeDebugger.h` | 调试器公开接口 | 已实现 `StepOut`，当前不再是第一阶段待办。 |
| `Source/Runtime/WfRuntimeDebugger.cpp` | 调试器运行控制 | 已实现 `StepOut`，当前不再是第一阶段待办。 |
| `Source/Runtime/WfRuntime.h` | 栈帧与异常数据结构 | 继续复用现有 `WfRuntimeThreadContext` 和 `WfRuntimeCallStackInfo`。 |

### 主进程内库 / DLL 文件清单

| 工程/文件 | 角色 | 第一阶段动作 |
| --- | --- | --- |
| `WorkflowDebugAdapter/package.json` | VSCode 调试扩展清单 | 已注册 `type: workflow` 的 DAP 调试器，当前仅提供 `attach` 配置。 |
| `WorkflowDebugAdapter/src/extension.ts` | VSCode 扩展入口 | 已注册调试器、命令和配置提供器。 |
| `WorkflowDebugAdapter/src/dapMain.ts` | DAP 入口 | 调试适配器进程入口。 |
| `WorkflowDebugAdapter/src/dapServer.ts` | DAP 会话实现 | 已实现 `initialize / attach / disconnect / stackTrace / scopes / variables / evaluate / continue / next / stepIn / stepOut`。 |
| `WorkflowDebugAdapter/src/bridgeTransport.ts` | 网络传输层 | 负责 TCP 连接、消息分帧、超时、断线重连。 |
| `WorkflowDebugAdapter/src/protocol.ts` / `WorkflowDebugAdapter/src/dapProtocol.ts` | 协议类型定义 | 定义 `hello / initialize / setBreakpoints / stopped / stackTrace / scopes / variables` 的消息结构。 |
| `WorkflowDebugAdapter/src/sourceMap.ts` | 源码映射 | 维护 `codeIndex -> 路径`、`路径 -> codeIndex`、行号映射。 |
| `WorkflowDebugAdapter/src/breakpointMapper.ts` / `WorkflowDebugAdapter/src/breakpointRegistry.ts` | 断点转换 | 把 VSCode 断点转换成 Workflow 的 `codeIndex + row` 断点，并保存校验结果。 |
| `WorkflowDebugAdapter/src/stackModel.ts` / `WorkflowDebugAdapter/src/stackInspector.ts` | 调用栈模型 | 把远端线程上下文转换成 DAP `StackFrame`。 |
| `WorkflowDebugAdapter/src/scopeModel.ts` / `WorkflowDebugAdapter/src/variableModel.ts` / `WorkflowDebugAdapter/src/valueInspector.ts` / `WorkflowDebugAdapter/src/handleTable.ts` | 作用域和变量模型 | 构建 Local / Argument / Captured / Global 四类作用域，以及按需展开句柄表。 |
| `WorkflowDebugHost/WorkflowDebugHost.h` | 宿主库入口 | 导出 `initialize()`、`shutdown()`、`createSession()`、`destroySession()`。 |
| `WorkflowDebugHost/WorkflowDebugHost.cpp` | 宿主库入口实现 | 绑定主进程的脚本执行生命周期。 |
| `WorkflowDebugHost/WorkflowDebugSession.h` | 会话对象声明 | 持有一次调试会话所需的全部状态。 |
| `WorkflowDebugHost/WorkflowDebugSession.cpp` | 会话对象实现 | 负责 attach / detach、消息路由和状态切换。 |
| `WorkflowDebugHost/WorkflowDebugBridge.h` | 协议桥声明 | 把调试协议翻译成 `WfDebugger` 调用。 |
| `WorkflowDebugHost/WorkflowDebugBridge.cpp` | 协议桥实现 | 处理 hello / initialize / setBreakpoints / continue / next / stepIn / stackTrace / scopes / variables / exception。 |
| `WorkflowDebugHost/WorkflowDebugTransport.h` | 传输层声明 | 屏蔽 socket、分帧和收发细节。 |
| `WorkflowDebugHost/WorkflowDebugTransport.cpp` | 传输层实现 | 负责与 VSCode 调试适配器双向通信。 |
| `WorkflowDebugHost/WorkflowDebugSessionState.h` | 状态机声明 | 管理 Connected / Ready / Paused / Running / Stopped。 |
| `WorkflowDebugHost/WorkflowDebugSessionState.cpp` | 状态机实现 | 处理序列号、会话切换和暂停恢复。 |
| `WorkflowDebugHost/WorkflowDebugSourceCatalog.h` | 源码目录声明 | 维护 `codeIndex -> 路径`、路径别名和行号映射。 |
| `WorkflowDebugHost/WorkflowDebugSourceCatalog.cpp` | 源码目录实现 | 处理 `hello` 与 `initialize` 下发的映射信息。 |
| `WorkflowDebugHost/WorkflowDebugBreakpointRegistry.h` | 断点登记声明 | 将 `setBreakpoints` 映射成 `AddCodeLineBreakPoint`。 |
| `WorkflowDebugHost/WorkflowDebugBreakpointRegistry.cpp` | 断点登记实现 | 维护断点校验、清理和复用。 |
| `WorkflowDebugHost/WorkflowDebugStackInspector.h` | 调用栈读取声明 | 从 `WfDebugger` / `WfRuntimeThreadContext` 组装栈帧。 |
| `WorkflowDebugHost/WorkflowDebugStackInspector.cpp` | 调用栈读取实现 | 为 `stackTrace` 和异常暂停生成统一栈数据。 |
| `WorkflowDebugHost/WorkflowDebugValueInspector.h` | 变量读取声明 | 从 `WfRuntimeCallStackInfo` 读取局部、参数、捕获、全局变量。 |
| `WorkflowDebugHost/WorkflowDebugValueInspector.cpp` | 变量读取实现 | 生成 `scopes / variables` 响应所需的数据。 |
| `WorkflowDebugHost/WorkflowDebugRuntimeBinding.h` | 运行时绑定声明 | 管理 `SetDebuggerForCurrentThread` / `ResetDebuggerForCurrentThread`。 |
| `WorkflowDebugHost/WorkflowDebugRuntimeBinding.cpp` | 运行时绑定实现 | 在主进程脚本执行前后装配和拆卸 `WfDebugger`。 |

### 当前仓库优先级

1. 先补 `pause`、条件断点和日志断点的 DAP 处理。
2. 再补 `setExceptionBreakpoints` 和 `launch` 入口。
3. 如果后续要做更深层对象图优化，再回到变量模型和句柄表。

### 当前主进程内库优先级

1. 先处理 `WorkflowDebugAdapter` 的剩余 DAP 请求和能力位。
2. 再处理 `WorkflowDebugHost` 侧的条件断点、日志断点和异常断点语义。
3. 如果要补 `launch`，再补启动路径和配置入口。

### 第一阶段验收文件

1. `WorkflowDebugAdapter` 能成功与主进程内库握手。
2. 断点能在主进程内库被校验并命中。
3. VSCode 可以看到调用栈和变量。
4. 运行时的回归测试保持通过。
