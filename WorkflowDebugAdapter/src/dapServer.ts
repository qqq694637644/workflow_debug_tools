import { BridgeTransport, type BridgeTransportEndpoint } from './bridgeTransport.js';
import { AdapterSessionMachine } from './sessionMachine.js';
import {
  createDapEvent,
  createDapResponse,
  type DapBreakpoint,
  type DapMessage,
  type DapRequestMessage,
  type DapResponseMessage,
  type DapScope,
  type DapStackFrame,
  type DapVariable,
  DapMessageReader,
  type WorkflowAttachArguments,
  type WorkflowContinueArguments,
  type WorkflowScopesArguments,
  type WorkflowSetBreakpointsArguments,
  type WorkflowStackTraceArguments,
  type WorkflowThreadsArguments,
  type WorkflowVariablesArguments,
  type WorkflowInitializeArguments,
  type WorkflowConfigurationDoneArguments
} from './dapProtocol.js';
import { DEFAULT_CAPABILITIES, type EventEnvelope, type ProtocolCommand, type ProtocolEnvelope, type RequestEnvelope, type ResponseEnvelope } from './protocol.js';
import { type BreakpointSyncState, type SourceBreakpointInput } from './breakpointMapper.js';

interface Deferred<T> {
  readonly promise: Promise<T>;
  resolve(value: T): void;
  reject(reason: unknown): void;
}

function createDeferred<T>(): Deferred<T> {
  let resolve!: (value: T) => void;
  let reject!: (reason: unknown) => void;
  const promise = new Promise<T>((innerResolve, innerReject) => {
    resolve = innerResolve;
    reject = innerReject;
  });

  return {
    promise,
    resolve,
    reject
  };
}

function waitWithTimeout<T>(promise: Promise<T>, timeoutMs: number, message: string): Promise<T> {
  if (!Number.isInteger(timeoutMs) || timeoutMs <= 0) {
    return promise;
  }

  return new Promise<T>((resolve, reject) => {
    const timer = setTimeout(() => {
      reject(new Error(message));
    }, timeoutMs);

    promise.then((value) => {
      clearTimeout(timer);
      resolve(value);
    }, (error) => {
      clearTimeout(timer);
      reject(error);
    });
  });
}

function assertInteger(value: unknown, name: string): asserts value is number {
  if (typeof value !== 'number' || !Number.isInteger(value)) {
    throw new Error(`${name} 必须是整数。`);
  }
}

type WorkflowOutputLevel = 'log' | 'info' | 'warn' | 'error';
type DapOutputCategory = 'console' | 'stdout' | 'stderr' | 'telemetry' | 'important';

function toDapCategory(level: WorkflowOutputLevel): DapOutputCategory {
  switch (level) {
    case 'log':
      return 'console';
    case 'info':
      return 'stdout';
    case 'warn':
    case 'error':
      return 'stderr';
    default:
      return 'console';
  }
}

function getFileName(sourcePath: string): string {
  const match = sourcePath.split(/[\\/]/).filter((item) => item.length > 0);
  return match.length > 0 ? match[match.length - 1] : sourcePath;
}

function normalizePathMappings(rawMappings: ReadonlyArray<{ readonly localPath: string; readonly remotePath: string }> | undefined): ReadonlyArray<{ readonly localPath: string; readonly remotePath: string }> {
  return rawMappings ?? [];
}

function toSourceBreakpoints(breakpoints: ReadonlyArray<DapBreakpoint>): ReadonlyArray<SourceBreakpointInput> {
  return breakpoints.map((breakpoint) => ({
    line: breakpoint.line,
    column: breakpoint.column,
    condition: breakpoint.condition,
    logMessage: breakpoint.logMessage
  }));
}

interface PendingBreakpointBatch {
  readonly sourcePath: string;
  remaining: number;
  readonly resolve: (value: ReadonlyArray<DapBreakpoint>) => void;
  readonly reject: (reason: unknown) => void;
}

interface FrameIdentity {
  readonly threadId: number;
  readonly frameId: number;
}

export interface WorkflowDebugDapServerOptions {
  readonly input?: NodeJS.ReadableStream;
  readonly output?: NodeJS.WritableStream;
  readonly error?: NodeJS.WritableStream;
}

export class WorkflowDebugDapServer {
  private readonly input: NodeJS.ReadableStream;
  private readonly output: NodeJS.WritableStream;
  private readonly error: NodeJS.WritableStream;
  private readonly reader = new DapMessageReader();
  private readonly adapter = new AdapterSessionMachine();
  private readonly pendingHostResponses = new Map<number, Deferred<ResponseEnvelope<ProtocolCommand>>>();
  private readonly pendingBreakpointBatches = new Map<number, PendingBreakpointBatch>();
  private readonly frameIds = new Map<string, number>();
  private readonly frameIdentityById = new Map<number, FrameIdentity>();
  private nextFrameId = 1;
  private nextSeq = 1;
  private transport: BridgeTransport<ProtocolEnvelope> | null = null;
  private attachArguments: WorkflowAttachArguments | null = null;
  private initializeRequestSeq: number | null = null;
  private readyDeferred: Deferred<void> | null = null;
  private activeThreadId: number | null = null;
  private attached = false;
  private terminated = false;

  constructor(options: WorkflowDebugDapServerOptions = {}) {
    this.input = options.input ?? process.stdin;
    this.output = options.output ?? process.stdout;
    this.error = options.error ?? process.stderr;
  }

  public async run(): Promise<void> {
    this.input.on('data', (chunk: Buffer | string) => {
      try {
        for (const message of this.reader.push(chunk)) {
          void this.handleMessage(message);
        }
      }
      catch (error) {
        this.writeError(`DAP 输入解析失败：${error instanceof Error ? error.message : String(error)}`);
      }
    });
    this.input.on('end', () => {
      void this.shutdown('stdin closed');
    });
    this.input.resume();

    await new Promise<void>((resolve) => {
      this.input.once('close', resolve);
    });
  }

  private async handleMessage(message: DapMessage): Promise<void> {
    if (message.type !== 'request') {
      return;
    }

    try {
      switch (message.command) {
        case 'initialize':
          this.sendResponse(message, this.createInitializeResponse());
          return;
        case 'attach':
          await this.handleAttachRequest(message);
          return;
        case 'configurationDone':
          this.sendResponse(message, {});
          return;
        case 'setBreakpoints':
          await this.handleSetBreakpointsRequest(message);
          return;
        case 'continue':
          await this.handleContinueRequest(message);
          return;
        case 'next':
          await this.handleStepRequest(message, 'next');
          return;
        case 'stepIn':
          await this.handleStepRequest(message, 'stepIn');
          return;
        case 'stackTrace':
          await this.handleStackTraceRequest(message);
          return;
        case 'scopes':
          await this.handleScopesRequest(message);
          return;
        case 'variables':
          await this.handleVariablesRequest(message);
          return;
        case 'threads':
          this.sendResponse(message, this.createThreadsResponse());
          return;
        case 'disconnect':
          await this.handleDisconnectRequest(message);
          return;
        case 'setExceptionBreakpoints':
          this.sendResponse(message, {});
          return;
        case 'pause':
          this.sendErrorResponse(message, '当前版本暂不支持暂停请求。');
          return;
        case 'evaluate':
          this.sendErrorResponse(message, '当前版本暂不支持表达式求值。');
          return;
        default:
          this.sendErrorResponse(message, `未实现的 DAP 请求：${message.command}`);
          return;
      }
    }
    catch (error) {
      this.sendErrorResponse(message, error instanceof Error ? error.message : String(error));
    }
  }

  private async handleAttachRequest(message: DapRequestMessage): Promise<void> {
    const args = this.parseAttachArguments(message.arguments);
    if (this.attached) {
      throw new Error('当前只允许一个 Workflow 调试会话。');
    }

    this.attachArguments = args;
    this.readyDeferred = createDeferred<void>();
    this.initializeRequestSeq = null;
    this.activeThreadId = null;
    this.attached = true;
    this.terminated = false;

    const endpoint: BridgeTransportEndpoint = {
      host: args.host ?? '127.0.0.1',
      port: args.port
    };
    this.transport = new BridgeTransport<ProtocolEnvelope>({
      name: 'workflow-debug-dap',
      mode: 'server',
      endpoint,
      reconnect: {
        enabled: false
      }
    });
    this.installTransportHandlers(this.transport);
    await this.transport.listen();
    this.sendOutput(`Workflow 调试桥接已监听 ${endpoint.host}:${endpoint.port}。`, 'console');

    try {
      await waitWithTimeout(
        this.readyDeferred.promise,
        args.connectTimeoutMs ?? 30000,
        '等待 WorkflowDebugHost 连接超时。'
      );
    }
    catch (error) {
      await this.shutdown('attach timeout');
      throw error;
    }

    this.sendEvent('initialized', {});
    this.sendResponse(message, {});
  }

  private async handleSetBreakpointsRequest(message: DapRequestMessage): Promise<void> {
    this.requireAttached();
    const args = this.parseSetBreakpointsArguments(message.arguments);
    const sourcePath = args.source.path;
    if (!sourcePath || !sourcePath.trim()) {
      throw new Error('setBreakpoints 必须携带 source.path。');
    }

    const requestedBreakpoints = toSourceBreakpoints(args.breakpoints ?? []);
    const request = this.adapter.createSetBreakpoints(sourcePath, requestedBreakpoints);
    const pending = createDeferred<ReadonlyArray<DapBreakpoint>>();
    this.pendingBreakpointBatches.set(request.seq, {
      sourcePath,
      remaining: request.body.breakpoints.length,
      resolve: pending.resolve,
      reject: pending.reject
    });

    // 断点下发在主进程侧是事件驱动的：目标端只回 breakpointValidated，不回普通 response。
    await this.sendHostCommand(request);

    if (request.body.breakpoints.length === 0) {
      this.pendingBreakpointBatches.delete(request.seq);
      this.sendResponse(message, {
        breakpoints: []
      });
      return;
    }

    const breakpoints = await pending.promise;
    this.pendingBreakpointBatches.delete(request.seq);
    this.sendResponse(message, {
      breakpoints
    });
  }

  private async handleContinueRequest(message: DapRequestMessage): Promise<void> {
    // attach 场景下脚本可能还没进入暂停态，continue 需要允许从 ready 直接启动运行。
    this.requireAttached();
    const args = this.parseContinueArguments(message.arguments);
    const request = this.adapter.createContinue(args.threadId);
    await this.sendHostCommand(request);
    this.sendResponse(message, {
      allThreadsContinued: true
    });
    if (args.threadId > 0) {
      this.sendEvent('continued', {
        threadId: args.threadId,
        allThreadsContinued: true
      });
    }
  }

  private async handleStepRequest(message: DapRequestMessage, command: 'next' | 'stepIn'): Promise<void> {
    this.requirePaused();
    const args = this.parseContinueArguments(message.arguments);
    const request = command === 'next'
      ? this.adapter.createNext(args.threadId)
      : this.adapter.createStepIn(args.threadId);
    await this.sendHostCommand(request);
    this.sendResponse(message, {});
    this.sendEvent('continued', {
      threadId: args.threadId,
      allThreadsContinued: true
    });
  }

  private async handleStackTraceRequest(message: DapRequestMessage): Promise<void> {
    this.requirePaused();
    const args = this.parseStackTraceArguments(message.arguments);
    const request = this.adapter.createStackTrace(args.threadId, args.startFrame ?? 0, args.levels ?? 20);
    const response = await this.sendHostRequest(request);
    const state = this.adapter.receiveStackTrace(response);
    const stackFrames = state.frames.map((frame) => this.toDapStackFrame(frame));
    this.sendResponse(message, {
      stackFrames,
      totalFrames: state.totalFrames
    });
  }

  private async handleScopesRequest(message: DapRequestMessage): Promise<void> {
    this.requirePaused();
    const args = this.parseScopesArguments(message.arguments);
    const frameIdentity = this.frameIdentityById.get(args.frameId);
    if (!frameIdentity) {
      throw new Error('找不到对应的栈帧。');
    }

    const request = this.adapter.createScopes(frameIdentity.frameId);
    const response = await this.sendHostRequest(request);
    const state = this.adapter.receiveScopes(response);
    const scopes = state.scopes.map((scope) => this.toDapScope(scope.name, scope.variablesReference));
    this.sendResponse(message, {
      scopes
    });
  }

  private async handleVariablesRequest(message: DapRequestMessage): Promise<void> {
    this.requirePaused();
    const args = this.parseVariablesArguments(message.arguments);
    const handle = this.adapter.getHandleTable().resolve(args.variablesReference);
    if (!handle) {
      throw new Error('变量句柄不存在。');
    }

    const request = this.adapter.createVariables(args.variablesReference);
    const response = await this.sendHostRequest(request);
    const state = this.adapter.receiveVariables(response);
    const variables = state.variables.map((variable) => this.toDapVariable(variable.name, variable.value, variable.type, variable.variablesReference, variable.namedVariables, variable.indexedVariables));
    this.sendResponse(message, {
      variables
    });
  }

  private async handleDisconnectRequest(message: DapRequestMessage): Promise<void> {
    await this.shutdown('disconnect');
    this.sendResponse(message, {});
  }

  private installTransportHandlers(transport: BridgeTransport<ProtocolEnvelope>): void {
    transport.onOpen(() => {
      this.sendOutput('WorkflowDebugHost 已连接到调试适配器。', 'stdout');
    });

    transport.onClose(() => {
      if (this.terminated) {
        return;
      }

      this.terminated = true;
      this.failPendingRequests(new Error('调试桥接已断开。'));
      this.sendEvent('terminated', {});
    });

    transport.onError((error) => {
      this.writeError(`Workflow 调试桥接错误：${error.message}`);
      this.sendOutput(error.message, 'stderr');
    });

    transport.onMessage((message) => {
      void this.handleHostMessage(message);
    });
  }

  private async handleHostMessage(message: ProtocolEnvelope): Promise<void> {
    try {
      if (message.type === 'response') {
        const pending = this.pendingHostResponses.get(message.replyTo);
        if (pending) {
          this.pendingHostResponses.delete(message.replyTo);
          pending.resolve(message as ResponseEnvelope<ProtocolCommand>);
          return;
        }
      }

      if (message.type === 'event' && message.cmd === 'hello') {
        const helloMessage = message as EventEnvelope<'hello'>;
        this.adapter.attach(helloMessage.sessionId);
        this.adapter.receiveHello(helloMessage);
        if (!this.attachArguments) {
          throw new Error('调试参数尚未准备好。');
        }

        const initializeRequest = this.adapter.createInitialize({
          workspaceRoot: this.attachArguments.workspaceRoot,
          pathMapping: normalizePathMappings(this.attachArguments.pathMapping),
          supports: DEFAULT_CAPABILITIES
        });
        this.initializeRequestSeq = initializeRequest.seq;
        await this.sendHostCommand(initializeRequest);
        return;
      }

      if (message.type === 'event' && message.cmd === 'ready') {
        const readyMessage = message as EventEnvelope<'ready'>;
        this.adapter.receiveReady(readyMessage);
        if (message.replyTo === this.initializeRequestSeq && this.readyDeferred) {
          this.readyDeferred.resolve();
          this.readyDeferred = null;
        }
        return;
      }

      if (message.type === 'event' && message.cmd === 'breakpointValidated') {
        const validationMessage = message as EventEnvelope<'breakpointValidated'>;
        this.adapter.receiveBreakpointValidated(validationMessage);
        const replyTo = validationMessage.replyTo;
        if (typeof replyTo === 'number') {
          const batch = this.pendingBreakpointBatches.get(replyTo);
          if (batch) {
            batch.remaining -= 1;
            if (batch.remaining <= 0) {
              const states = this.adapter.getBreakpointRegistry().getBreakpoints(batch.sourcePath);
              const breakpoints = states.map((state) => this.toDapBreakpoint(state));
              batch.resolve(breakpoints);
              this.pendingBreakpointBatches.delete(replyTo);
            }
          }
        }
        return;
      }

      if (message.type === 'event' && message.cmd === 'stopped') {
        const stoppedMessage = message as EventEnvelope<'stopped'>;
        this.adapter.receiveStopped(stoppedMessage);
        this.activeThreadId = stoppedMessage.body.threadId;
        this.clearFrameMapping();
        this.sendEvent('stopped', {
          reason: stoppedMessage.body.reason,
          threadId: stoppedMessage.body.threadId,
          description: this.describeStoppedReason(stoppedMessage.body.reason),
          allThreadsStopped: true
        });
        return;
      }

      if (message.type === 'event' && message.cmd === 'exception') {
        const exceptionMessage = message as EventEnvelope<'exception'>;
        const stackState = this.adapter.receiveException(exceptionMessage);
        this.activeThreadId = stackState.threadId;
        this.clearFrameMapping();
        this.sendEvent('stopped', {
          reason: 'exception',
          threadId: stackState.threadId,
          description: exceptionMessage.body.message,
          allThreadsStopped: true
        });
        this.sendOutput(exceptionMessage.body.message, 'stderr');
        return;
      }

      if (message.type === 'event' && message.cmd === 'output') {
        const outputMessage = message as EventEnvelope<'output'>;
        const category = toDapCategory(outputMessage.body.level as WorkflowOutputLevel);
        this.sendOutput(outputMessage.body.message, category);
        return;
      }

      if (message.type === 'event' && message.cmd === 'disconnect') {
        this.terminated = true;
        this.sendEvent('terminated', {});
        return;
      }
    }
    catch (error) {
      this.writeError(`处理被调试端消息失败：${error instanceof Error ? error.message : String(error)}`);
    }
  }

  private async sendHostRequest<T extends ProtocolCommand>(request: RequestEnvelope<T>): Promise<ResponseEnvelope<T>> {
    const transport = this.requireTransport();
    const deferred = createDeferred<ResponseEnvelope<ProtocolCommand>>();
    this.pendingHostResponses.set(request.seq, deferred);
    await transport.send(request);
    const response = await deferred.promise;
    if (response.type !== 'response' || response.cmd !== request.cmd) {
      throw new Error('收到的响应与请求不匹配。');
    }

    return response as ResponseEnvelope<T>;
  }

  private async sendHostCommand<T extends ProtocolCommand>(request: RequestEnvelope<T>): Promise<void> {
    const transport = this.requireTransport();
    await transport.send(request);
  }

  private createInitializeResponse(): Record<string, unknown> {
    return {
      supportsConfigurationDoneRequest: true,
      supportsContinueOnTerminateRequest: false,
      supportsEvaluateForHovers: false,
      supportsFunctionBreakpoints: false,
      supportsConditionalBreakpoints: false,
      supportsHitConditionalBreakpoints: false,
      supportsLogPoints: false,
      supportsSetVariable: false,
      supportsStepBack: false,
      supportsStepInTargetsRequest: false,
      supportsTerminateRequest: true,
      supportsThreadsRequest: true,
      supportsStackTraceRequest: true,
      supportsScopesRequest: true,
      supportsVariablesRequest: true
    };
  }

  private createThreadsResponse(): Record<string, unknown> {
    const threadId = this.activeThreadId ?? 1;
    return {
      threads: [
        {
          id: threadId,
          name: threadId === 1 ? 'Workflow 主线程' : `Workflow 线程 ${threadId}`
        }
      ]
    };
  }

  private toDapStackFrame(frame: {
    readonly threadId: number;
    readonly frameId: number;
    readonly callStackIndex: number;
    readonly name: string;
    readonly sourceId: number;
    readonly sourcePath: string;
    readonly row: number;
    readonly line: number;
    readonly column: number;
    readonly canRequestVariables: boolean;
  }): DapStackFrame {
    const id = this.getStackFrameId(frame.threadId, frame.frameId);
    return {
      id,
      name: frame.name,
      source: {
        path: this.adapter.getSourceCatalog().resolveDisplayPath(frame.sourcePath),
        name: getFileName(frame.sourcePath)
      },
      line: frame.line,
      column: frame.column + 1,
      presentationHint: 'normal'
    };
  }

  private toDapScope(name: string, variablesReference: number): DapScope {
    return {
      name,
      variablesReference,
      expensive: false
    };
  }

  private toDapVariable(
    name: string,
    value: string,
    type: string,
    variablesReference: number,
    namedVariables: number,
    indexedVariables: number
  ): DapVariable {
    return {
      name,
      value,
      type,
      variablesReference,
      namedVariables,
      indexedVariables
    };
  }

  private toDapBreakpoint(state: BreakpointSyncState): DapBreakpoint & { readonly verified?: boolean; readonly message?: string } {
    return {
      line: state.line,
      column: state.column,
      condition: state.condition,
      logMessage: state.logMessage,
      verified: state.verified ?? false,
      message: state.reason
    };
  }

  private getStackFrameId(threadId: number, frameId: number): number {
    const key = `${threadId}:${frameId}`;
    const existing = this.frameIds.get(key);
    if (existing !== undefined) {
      return existing;
    }

    const id = this.nextFrameId;
    this.nextFrameId += 1;
    this.frameIds.set(key, id);
    this.frameIdentityById.set(id, {
      threadId,
      frameId
    });
    return id;
  }

  private clearFrameMapping(): void {
    this.frameIds.clear();
    this.frameIdentityById.clear();
    this.nextFrameId = 1;
  }

  private describeStoppedReason(reason: string): string {
    switch (reason) {
      case 'breakpoint':
        return '命中断点。';
      case 'step':
        return '单步暂停。';
      case 'exception':
        return '异常暂停。';
      case 'entry':
        return '入口暂停。';
      default:
        return '程序暂停。';
    }
  }

  private requireTransport(): BridgeTransport<ProtocolEnvelope> {
    if (!this.transport) {
      throw new Error('调试桥接尚未启动。');
    }

    return this.transport;
  }

  private requireAttached(): void {
    if (!this.attachArguments || !this.transport) {
      throw new Error('调试会话尚未附加。');
    }
  }

  private requirePaused(): void {
    if (this.adapter.snapshot().phase !== 'paused') {
      throw new Error('当前只有在暂停状态下才能执行该操作。');
    }
  }

  private parseAttachArguments(args: unknown): WorkflowAttachArguments {
    if (typeof args !== 'object' || args === null) {
      throw new Error('attach 请求必须携带参数。');
    }

    const record = args as Record<string, unknown>;
    assertInteger(record.port, 'port');
    if (record.port <= 0 || record.port > 65535) {
      throw new Error('port 必须介于 1 和 65535 之间。');
    }

    const workspaceRoot = typeof record.workspaceRoot === 'string' ? record.workspaceRoot.trim() : '';
    if (!workspaceRoot) {
      throw new Error('workspaceRoot 不能为空。');
    }

    const host = typeof record.host === 'string' && record.host.trim().length > 0
      ? record.host.trim()
      : undefined;

    const pathMapping = Array.isArray(record.pathMapping)
      ? record.pathMapping
        .map((item) => {
          if (typeof item !== 'object' || item === null) {
            throw new Error('pathMapping 必须是对象数组。');
          }
          const mapping = item as Record<string, unknown>;
          const localPath = typeof mapping.localPath === 'string' ? mapping.localPath.trim() : '';
          const remotePath = typeof mapping.remotePath === 'string' ? mapping.remotePath.trim() : '';
          if (!localPath || !remotePath) {
            throw new Error('pathMapping 项必须同时包含 localPath 和 remotePath。');
          }
          return {
            localPath,
            remotePath
          };
        })
      : [];

    const connectTimeoutMs = typeof record.connectTimeoutMs === 'number' && Number.isInteger(record.connectTimeoutMs) && record.connectTimeoutMs > 0
      ? record.connectTimeoutMs
      : undefined;

    return {
      host,
      port: record.port,
      workspaceRoot,
      pathMapping,
      connectTimeoutMs
    };
  }

  private parseSetBreakpointsArguments(args: unknown): WorkflowSetBreakpointsArguments {
    if (typeof args !== 'object' || args === null) {
      throw new Error('setBreakpoints 请求必须携带参数。');
    }

    const record = args as Record<string, unknown>;
    if (typeof record.source !== 'object' || record.source === null) {
      throw new Error('setBreakpoints 必须携带 source。');
    }

    const source = record.source as Record<string, unknown>;
    const sourcePath = typeof source.path === 'string' ? source.path : undefined;
    const breakpoints = Array.isArray(record.breakpoints)
      ? record.breakpoints.map((item) => {
        if (typeof item !== 'object' || item === null) {
          throw new Error('breakpoints 必须是对象数组。');
        }
        const breakpoint = item as Record<string, unknown>;
        assertInteger(breakpoint.line, 'line');
        if (breakpoint.line <= 0) {
          throw new Error('断点行号必须从 1 开始。');
        }
        const line = breakpoint.line;
        const column = typeof breakpoint.column === 'number' && Number.isInteger(breakpoint.column) && breakpoint.column > 0
          ? breakpoint.column
          : undefined;
        const condition = typeof breakpoint.condition === 'string' && breakpoint.condition.trim().length > 0
          ? breakpoint.condition
          : undefined;
        const logMessage = typeof breakpoint.logMessage === 'string' && breakpoint.logMessage.trim().length > 0
          ? breakpoint.logMessage
          : undefined;
        return {
          line,
          column,
          condition,
          logMessage
        };
      })
      : [];

    return {
      source: {
        path: sourcePath
      },
      breakpoints
    };
  }

  private parseContinueArguments(args: unknown): WorkflowContinueArguments {
    if (typeof args !== 'object' || args === null) {
      throw new Error('继续执行请求必须携带参数。');
    }

    const record = args as Record<string, unknown>;
    assertInteger(record.threadId, 'threadId');
    if (record.threadId < 0) {
      throw new Error('threadId 不能为负数。');
    }

    return {
      threadId: record.threadId
    };
  }

  private parseStackTraceArguments(args: unknown): WorkflowStackTraceArguments {
    if (typeof args !== 'object' || args === null) {
      throw new Error('stackTrace 请求必须携带参数。');
    }

    const record = args as Record<string, unknown>;
    assertInteger(record.threadId, 'threadId');
    const threadId = record.threadId;
    const startFrame = typeof record.startFrame === 'number' && Number.isInteger(record.startFrame) && record.startFrame >= 0
      ? record.startFrame
      : 0;
    const levels = typeof record.levels === 'number' && Number.isInteger(record.levels) && record.levels >= 0
      ? record.levels
      : 20;

    return {
      threadId,
      startFrame,
      levels
    };
  }

  private parseScopesArguments(args: unknown): WorkflowScopesArguments {
    if (typeof args !== 'object' || args === null) {
      throw new Error('scopes 请求必须携带参数。');
    }

    const record = args as Record<string, unknown>;
    assertInteger(record.frameId, 'frameId');
    if (record.frameId < 0) {
      throw new Error('frameId 不能为负数。');
    }

    return {
      frameId: record.frameId
    };
  }

  private parseVariablesArguments(args: unknown): WorkflowVariablesArguments {
    if (typeof args !== 'object' || args === null) {
      throw new Error('variables 请求必须携带参数。');
    }

    const record = args as Record<string, unknown>;
    assertInteger(record.variablesReference, 'variablesReference');
    if (record.variablesReference <= 0) {
      throw new Error('variablesReference 必须是正整数。');
    }

    return {
      variablesReference: record.variablesReference
    };
  }

  private createDapBody(message: DapRequestMessage, body: unknown, success = true, errorMessage?: string): DapResponseMessage {
    return createDapResponse(this.nextSequence(), message.seq, message.command, body, success, errorMessage);
  }

  private sendResponse(message: DapRequestMessage, body: unknown, success = true, errorMessage?: string): void {
    this.writeMessage(this.createDapBody(message, body, success, errorMessage));
  }

  private sendErrorResponse(message: DapRequestMessage, errorMessage: string): void {
    this.sendResponse(message, undefined, false, errorMessage);
  }

  private sendEvent(event: string, body?: unknown): void {
    this.writeMessage(createDapEvent(this.nextSequence(), event, body));
  }

  private sendOutput(output: string, category: DapOutputCategory): void {
    this.sendEvent('output', {
      category,
      output: output.endsWith('\n') ? output : `${output}\n`
    });
  }

  private writeMessage(message: DapMessage): void {
    this.output.write(serializeDapMessage(message));
  }

  private failPendingRequests(error: Error): void {
    if (this.readyDeferred) {
      this.readyDeferred.reject(error);
      this.readyDeferred = null;
    }

    for (const pending of this.pendingHostResponses.values()) {
      pending.reject(error);
    }
    this.pendingHostResponses.clear();

    for (const batch of this.pendingBreakpointBatches.values()) {
      batch.reject(error);
    }
    this.pendingBreakpointBatches.clear();
  }

  private async shutdown(reason: string): Promise<void> {
    void reason;
    if (this.terminated) {
      return;
    }

    this.terminated = true;
    this.failPendingRequests(new Error('调试会话已关闭。'));

    if (this.transport) {
      try {
        await this.transport.close();
      }
      catch (error) {
        this.writeError(`关闭调试桥接失败：${error instanceof Error ? error.message : String(error)}`);
      }
      this.transport = null;
    }
  }

  private nextSequence(): number {
    const seq = this.nextSeq;
    this.nextSeq += 1;
    return seq;
  }

  private writeError(message: string): void {
    this.error.write(`${message}\n`);
  }

}

function serializeDapMessage(message: DapMessage): Buffer {
  const json = JSON.stringify(message);
  const header = `Content-Length: ${Buffer.byteLength(json, 'utf8')}\r\n\r\n`;
  return Buffer.from(header + json, 'utf8');
}
