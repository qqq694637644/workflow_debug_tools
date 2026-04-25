# Beta v4 Turn 0：Workflow 远程调试方案收敛迭代

## 0. 背景与边界

本轮只收敛“Workflow 远程调试方案框架”相关代码，范围限定为：

- `WorkflowDebugAdapter/`（全部代码）
- `Source/WorkflowDebugHost/`（全部代码）
- `Source/Runtime/WfRuntimeDebugger.cpp/.h`

目标不是做“架构评论”，而是基于现有仓库做一次**面向收敛**的精简迭代：删除不必要复杂度、收紧语义闭环、降低错误面，并产出可应用的 diff。

---

## 1. 现状问题（从第一性原理出发）

### 1.1 Host Transport 存在“TCP + 内存队列”双语义

`WorkflowDebugTransport` 同时存在：

- TCP 连接语义（Connect + socket 收发）
- “骨架阶段”的内存队列语义（Open + outgoing 队列 + TryPopOutgoing/QueueIncoming）

这会导致：

- **状态分叉**：`IsOpen/Send/TryReceive` 在不同模式下语义不一致（有时“成功”仅代表写入内存队列）。
- **隐式行为**：调用方无法仅从接口判断当前是真连接还是“假会话”。
- **错误面扩大**：存在一套未被主流程使用的队列逻辑，但仍会被维护与测试路径触发。

第一性原则：远程调试的最小闭环必须以“可观测、可验证”的 TCP 链路为核心；内存模式属于早期 scaffolding，应删。

### 1.2 Host SessionState 存在未被主流程消费的“诊断状态”扩散

`WorkflowDebugSessionState` 中包含并维护：

- `workspaceRoot/sourceMapCount/pendingRequestCount` 等字段

但在当前主流程中没有稳定的消费点（写入存在、读取缺失），会造成：

- **状态噪音**：快照/状态对象携带大量“看起来重要但不影响主流程”的字段。
- **维护成本**：未来改动容易误以为这些字段有语义约束，从而引入错误耦合。

第一性原则：状态只应包含“协议与闭环所必需的最小信息”。

### 1.3 Host Bridge 存在不属于主流程方向的入站处理器

Host 侧 `WorkflowDebugBridge::Dispatch()` 仍保留：

- `HandleHello`（入站 hello）
- `HandleException`（入站 exception）

但在当前主流程中：

- `hello/exception` 都是 **Host → Adapter** 方向事件

保留这些入站分支会扩大命令表与测试面，且容易引入“对称协议”的伪扩展点。

### 1.4 Adapter BridgeTransport 过度工程化

`WorkflowDebugAdapter/src/bridgeTransport.ts` 同时支持：

- server/client 双模式
- reconnect 策略与计时器
- 多 promise 分叉（connectPromise/listenPromise）与状态机（connecting/connected/closing）

而 VSCode attach 的实际主路径只需要：

- Adapter **listen** 一个端口
- Host 连接进来（单连接）
- 断开即结束会话

多余的 reconnect/client 模式不仅增加代码体积，还会带来“自动重连导致的隐式状态回滚/重复握手”等问题。

---

## 2. 本轮收敛策略

### 2.1 删除优先：移除未证明必要的分支与伪扩展点

- Host Transport：删除 `Open()` 与 outgoing 内存队列路径，统一为 TCP 语义。
- Host SessionState：删除未消费字段与 API，收敛 snapshot。
- Host Bridge：删除不属于主流程方向的入站 handler。
- Adapter Transport：删除 client/reconnect 逻辑，收敛为 server-only 单连接。

### 2.2 保留当前阶段“最小闭环”

最小闭环定义（必须保留、且可验证）：

- Adapter 能 listen；Host 能 connect
- 双方能通过“行分隔 JSON”可靠收发 envelope
- Host 能发 `hello`，Adapter 能处理并进入初始化/Ready
- 断链时双方能明确结束，不出现隐式重连带来的状态分叉

---

## 3. 将被删除/合并/收敛的模块或文件（明确清单）

### 3.1 WorkflowDebugHost

- `Source/WorkflowDebugHost/WorkflowDebugTransport.h/.cpp`
  - 删除：`Open()`、`TryPopOutgoing()`、`QueueIncoming()`、outgoing 队列与“内存模式”行为
  - 收敛：`IsOpen/Send/TryReceive` 只表达 TCP 链路语义

- `Source/WorkflowDebugHost/WorkflowDebugSession.cpp`
  - `Attach()` 不再走 `Open()` 分支，要求 endpoint port > 0
  - `SendHello()` 不再写入已移除的 `sourceMapCount` 状态字段

- `Source/WorkflowDebugHost/WorkflowDebugSessionState.h/.cpp`
  - 删除未被主流程消费的字段与方法：`workspaceRoot/sourceMapCount/pendingRequestCount`
  - snapshot 只保留协议/DAP bridging 所需最小字段（phase/sessionId/lastInboundSeq/lastStopped）
  - 保留协议必需的 seq 追踪：`lastInboundSeq/lastOutboundSeq`

- `Source/WorkflowDebugHost/WorkflowDebugBridge.h/.cpp`
  - 删除对已移除状态字段（workspaceRoot/sourceMapCount）的写入
  - 删除：`HandleHello()`、`HandleException()` 及其 Dispatch 分支（避免伪对称协议扩展点）

### 3.2 WorkflowDebugAdapter

- `WorkflowDebugAdapter/src/bridgeTransport.ts`
  - 只保留 `server` 监听模式：listen → accept 1 connection → close
  - 删除：client mode、reconnect、计时器与多 promise 分叉
  - 状态收敛为：`idle` → `listening` → `connected` → `closed`

- `WorkflowDebugAdapter/src/dapServer.ts`
  - 适配新的 `BridgeTransport` 构造参数（去掉 mode/reconnect）
  - 断链/错误处理收敛为单一路径：输出日志 + 结束会话，避免自动重连导致的隐式状态回滚

- `WorkflowDebugAdapter/dist/`
  - 由于 extension 运行时入口指向 dist，本轮在修改 src 后同步更新 dist 输出，避免‘源码已收敛但运行时仍走旧逻辑’的隐式分叉

- `WorkflowDebugAdapter/test/`
  - 删除与 client/reconnect 相关的用例，保留 server-only 单连接与收发闭环测试

### 3.3 WfRuntimeDebugger

- `Source/Runtime/WfRuntimeDebugger.cpp/.h`
  - 本轮只做“远程调试框架收敛”相关改动；若无直接耦合点，则保持不动（避免误删核心调试能力）。

---

## 4. 修改后的目标结构（简述）

- Host：Transport 只有 TCP 语义；SessionState 只有最小字段；Bridge 只处理主流程命令方向
- Adapter：Transport 只有 server 单连接；DAP Server 无重连/回滚分支

结果应当是：系统更小、更硬、更清晰、更不容易出错。
