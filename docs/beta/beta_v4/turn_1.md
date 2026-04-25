# beta_v4 turn_1：Workflow 远程调试方案收敛（面向最小闭环）

> 约束：本轮只允许精简/重构 **# Workflow 远程调试方案框架内代码**。
>
> 覆盖范围：
> - `WorkflowDebugAdapter/`（全部代码与配置）
> - `Source/WorkflowDebugHost/`（全部代码）
> - `Source/Runtime/WfRuntimeDebugger.cpp`
> - `Source/Runtime/WfRuntimeDebugger.h`
>
> 目标：删除非必要复杂度，保留可工作的最小远程调试闭环，并降低错误面。

---

## 1. 现状问题（冗余 / 分叉 / 过度抽象）

### 1.1 WorkflowDebugAdapter（VSCode DAP 侧）

1) **“扩展宿主内置 debugServer”属于额外分叉路径**
- 当前 `extension.ts` 会启动 `debugAdapterServerHost.ts`，通过 `debugServer` 让 VSCode 走“连接本地调试服务器”模式。
- 但同时 `package.json.debuggers.program` 已定义标准 DAP 进程入口 `dapMain.ts`。
- 这导致：同一套调试能力存在两条启动路径（stdio / debugServer），增加状态与问题定位成本。

2) **命令 / Keybinding Hack 增加隐式行为**
- `workflow.consumeF9` + `F2/F9` 绑定属于“侵入式行为”，会改变用户全局调试习惯。
- 对最小闭环无贡献，且扩大错误面（键位冲突、激活条件、语言判断）。

3) **生产代码中夹带测试模拟目标端的实现（TargetSessionMachine / Inspector）**
- `sessionMachine.ts` 同时包含 Adapter 与 Target 两套状态机，并导出 `TargetSessionMachine`、`StackInspector`、`ValueInspector`。
- 这属于“把测试桩当作产品 API 暴露”，扩大维护面与误用风险。

4) **协议与断点模型含大量未落地的字段：column/condition/logMessage/restart/evaluate/error**
- 对外看起来“支持高级能力”，但实际并未闭环，属于伪扩展点。
- 每多一个字段就多一条解析/合并/序列化路径，增加错误面。

5) **仓库内包含构建产物与依赖快照**
- `node_modules/` 与 `*.vsix` 属于可再生产物。
- 在收敛阶段应删除，降低文件数量、减少 diff 噪音、减少误提交风险。

### 1.2 WorkflowDebugHost（C++ 宿主侧）

1) **断点记录结构携带大量未使用字段**
- `column/beforeCodegen/condition/logMessage` 当前不参与运行时断点安装/命中。

2) **字符串 Key 拼接用于索引（threadId/frameId/scope）**
- `StackInspector`、`ValueInspector` 使用 `WString` 拼 key。
- 这是不必要的状态与分配，易引入格式错误与性能浪费。

3) **变量/作用域引用生成依赖可变字典与自增计数**
- `ValueInspector` 用 `scopeReferences` + `nextScopeReference` 维护引用稳定性。
- 实际可用确定性编码（threadId/frameId/kind）生成稳定引用，删除额外状态与同步点。

4) **RemoteWfDebugger 持有 condition_variable 但实际采用轮询**
- 目前 `OnBlockExecution()` 使用 `sleep_for` 轮询等待状态变化，`condition_variable` 并未提供真实价值。
- 属于“看起来更高级但不必要”的复杂度。

5) **会话 Phase 存在未使用的枚举值（Stopped）**
- 状态空间越大，分叉越多；未使用状态应删。

### 1.3 WfRuntimeDebugger（运行时调试器核心）

1) **支持大量断点类型（读写全局/属性/事件/方法/创建对象）但当前远程调试闭环只用“指令断点”**
- 这些类型在当前仓库未被调用，且运行时回调路径多，增加维护与错误面。

2) **条件断点与动作（IWfBreakPointAction）属于未闭环扩展点**
- 远程调试协议并不支持 condition/logMessage。
- 保留条件动作只会带来更多状态（evaluatingBreakPoint）与不可预期行为。

---

## 2. 本轮删减策略（系统收敛 + 最小闭环）

### 2.1 只保留“一个启动路径”：标准 DAP 进程（stdio）
- 删除 VSCode 扩展宿主中的 `debugServer` 启动方案。
- 仅保留 `dapMain.ts` 作为 debug adapter 入口，由 VSCode 按 `package.json.debuggers.program` 启动。

### 2.2 删除侵入式命令与 Keybinding
- 移除 `workflow.consumeF9` 命令与相关 keybindings。
- 不再修改用户默认调试快捷键。

### 2.3 协议收敛：去掉“未闭环字段/命令”
- 断点：只保留 **行断点**（row + breakpointId），移除 column/condition/logMessage。
- 断开：只保留 disconnect（reason），移除 restart。
- 移除 `evaluate`、`error` 协议命令（evaluate 不走宿主协议、error 不做双通道）。

### 2.4 测试与构建产物从“产品代码面”移除
- 删除 `WorkflowDebugAdapter/test/` 及其依赖。
- 删除 `WorkflowDebugAdapter/node_modules/` 与 `workflow-debug-adapter-*.vsix`。
- 删除 `src/index.ts` 对外导出面（debug adapter 不是通用库）。

### 2.5 宿主侧收敛：删除无用状态、改为确定性引用
- `BreakpointRegistry`：移除未使用字段。
- `StackInspector`：用 `vint` 作为 key，不再拼字符串。
- `ValueInspector`：
  - `framesByKey` 使用 `(threadId, frameId)` 作为 key（Tuple）。
  - `scopeReferences/nextScopeReference` 删除，改用确定性编码生成 `variablesReference`。
- `RemoteWfDebugger`：移除无效的 `condition_variable` 与 mutex，保留最小的暂停快照开关。
- `SessionState`：删除未使用 `Stopped` phase。
- `Transport`：移除兼容字段（kind/command），只接受标准字段（type/cmd）。

### 2.6 运行时调试器收敛：只支持指令断点
- 删除 `IWfBreakPointAction` 与条件动作相关状态。
- `WfBreakPoint` 仅保留 `Instruction`。
- `WfDebugger` 只维护 `insBreakPoints`。
- 其它回调（BreakRead/BreakWrite/BreakGet/...）保留签名以兼容运行时，但统一返回 false（本阶段不支持该类断点）。

---

## 3. 保留的最小闭环定义（必须可工作）

本轮保留并保证语义闭环的能力：

1) VSCode 通过标准 DAP 启动 debug adapter（stdio）。
2) debug adapter 按配置 `host/port/workspaceRoot/pathMapping/stopOnEntry` 监听并接受宿主连接。
3) 断点：支持 **行断点**（setBreakpoints），并能下发到运行时（AddCodeLineBreakPoint）。
4) 执行控制：continue / next / stepIn / stepOut / disconnect。
5) 停止事件：breakpoint / step / exception / entry（stopOnEntry）。
6) 查看现场：stackTrace / scopes / variables（当前保持“平铺变量”，不承诺对象展开）。
7) 输出：output 事件透传到 VSCode。

明确不在本轮闭环的能力（删除/禁用）：
- 条件断点、logpoint、列断点
- evaluate / hover 表达式求值
- restart / restartRequest 语义
- 运行时高级断点类型（全局/属性/事件/方法/创建对象）
- VSCode 扩展宿主内置 debugServer 方案

---

## 4. 将被删除/合并/收敛的模块或文件（清单）

### 4.1 WorkflowDebugAdapter

**删除（产物/依赖/测试/非必要入口）**
- `WorkflowDebugAdapter/node_modules/`（整个目录）
- `WorkflowDebugAdapter/workflow-debug-adapter-0.1.2.vsix`
- `WorkflowDebugAdapter/test/`（整个目录）
- `WorkflowDebugAdapter/src/extension.ts`
- `WorkflowDebugAdapter/src/debugAdapterServerHost.ts`
- `WorkflowDebugAdapter/src/debugSessionTracker.ts`
- `WorkflowDebugAdapter/src/logFormat.ts`
- `WorkflowDebugAdapter/src/index.ts`
- `WorkflowDebugAdapter/src/stackInspector.ts`
- `WorkflowDebugAdapter/src/valueInspector.ts`
- `WorkflowDebugAdapter/src/expressionEvaluator.ts`
- `WorkflowDebugAdapter/src/vscode.d.ts`

**修改（协议/断点/会话机/DAP 入口）**
- `WorkflowDebugAdapter/package.json`
- `WorkflowDebugAdapter/tsconfig.json`
- `WorkflowDebugAdapter/src/protocol.ts`
- `WorkflowDebugAdapter/src/breakpointRegistry.ts`
- `WorkflowDebugAdapter/src/breakpointMapper.ts`
- `WorkflowDebugAdapter/src/sessionMachine.ts`
- `WorkflowDebugAdapter/src/dapServer.ts`

### 4.2 WorkflowDebugHost

**修改（删除无用字段/确定性引用/状态收敛）**
- `Source/WorkflowDebugHost/WorkflowDebugBreakpointRegistry.h`
- `Source/WorkflowDebugHost/WorkflowDebugBreakpointRegistry.cpp`
- `Source/WorkflowDebugHost/WorkflowDebugBridge.cpp`
- `Source/WorkflowDebugHost/WorkflowDebugStackInspector.h`
- `Source/WorkflowDebugHost/WorkflowDebugStackInspector.cpp`
- `Source/WorkflowDebugHost/WorkflowDebugValueInspector.h`
- `Source/WorkflowDebugHost/WorkflowDebugValueInspector.cpp`
- `Source/WorkflowDebugHost/WorkflowDebugRuntimeBinding.h`
- `Source/WorkflowDebugHost/WorkflowDebugRuntimeBinding.cpp`
- `Source/WorkflowDebugHost/WorkflowDebugSessionState.h`
- `Source/WorkflowDebugHost/WorkflowDebugTransport.cpp`

### 4.3 Runtime

**修改（仅保留指令断点能力）**
- `Source/Runtime/WfRuntimeDebugger.h`
- `Source/Runtime/WfRuntimeDebugger.cpp`

---

## 5. 修改后的目标结构（简述）

### 5.1 WorkflowDebugAdapter（只保留 DAP + Bridge 核心）

- `src/dapMain.ts`：唯一入口
- `src/dapServer.ts`：DAP 处理（不含 evaluate/restart）
- `src/sessionMachine.ts`：仅保留 AdapterSessionMachine
- `src/protocol.ts`：精简后的宿主协议
- 其余：breakpoint / transport / models

不再包含：extension 侧代码、debugServer host、测试桩 target machine、构建产物。

### 5.2 WorkflowDebugHost

- 断点记录精简
- inspector 不再依赖字符串 key 与自增引用
- 运行时绑定删除无效同步原语

### 5.3 WfRuntimeDebugger

- 仅支持指令断点与异常中断
- 高级断点回调保留签名但不实现（返回 false）

---

## 6. 验收点（本轮收敛后的可验证行为）

1) VSCode 启动调试时只走 `dapMain.ts`（stdio）。
2) setBreakpoints -> 宿主收到并安装（AddCodeLineBreakPoint）。
3) continue / step 能改变运行状态并触发 stopped。
4) stopped 后 stackTrace/scopes/variables 能返回数据。
5) disconnect 能触发宿主结束会话并清理状态。

