import {
  createEventEnvelope,
  createRequestEnvelope,
  createResponseEnvelope,
  DEFAULT_CAPABILITIES,
  type CapabilitySet,
  type EventEnvelope,
  type HelloBody,
  type InitializeBody,
  type ProtocolEnvelope,
  type ProtocolCommand,
  type ProtocolCommandMap,
  type ResponseEnvelope,
  type ReadyBody,
  type RequestEnvelope,
  type SessionPhase,
  type SessionRole,
  type ExceptionBody,
  type ScopeKind,
  type ScopeEntryBody,
  type ScopesBody,
  type VariableEntryBody,
  type VariablesBody,
  type SourceMapEntry,
  type StoppedBody,
  type StackFrameBody
} from './protocol.js';
import { type SourceBreakpointInput } from './breakpointMapper.js';
import { BreakpointRegistry } from './breakpointRegistry.js';
import { StackModel, type StackTraceState } from './stackModel.js';
import { ScopeModel, type ScopeModelState } from './scopeModel.js';
import { SourceCatalog } from './sourceMap.js';
import { VariableModel, type VariableModelState } from './variableModel.js';

export class SessionError extends Error {
  constructor(message: string) {
    super(message);
    this.name = 'SessionError';
  }
}

export class SessionStateError extends SessionError {
  constructor(message: string) {
    super(message);
    this.name = 'SessionStateError';
  }
}

export class SessionMismatchError extends SessionError {
  constructor(message: string) {
    super(message);
    this.name = 'SessionMismatchError';
  }
}

export interface SessionSnapshot {
  readonly role: SessionRole;
  readonly phase: SessionPhase;
  readonly sessionId: string | null;
  readonly lastInboundSeq: number;
  readonly lastOutboundSeq: number;
  readonly pendingRequestCount: number;
  readonly remoteHelloRuntimeVersion: string | null;
  readonly sourceMapCount: number;
  readonly workspaceRoot: string | null;
  readonly lastStoppedReason: string | null;
  readonly lastStoppedThreadId: number | null;
  readonly lastStoppedFrameId: number | null;
  readonly lastStoppedSourceId: number | null;
  readonly lastStoppedRow: number | null;
}

export interface PendingRequest {
  readonly seq: number;
  readonly command: ProtocolCommand;
}

abstract class BaseSessionMachine {
  private readonly role: SessionRole;
  protected readonly sourceCatalog = new SourceCatalog();
  private phase: SessionPhase = 'idle';
  private sessionId: string | null = null;
  private nextSeq = 1;
  private lastInboundSeq = 0;
  private lastOutboundSeq = 0;
  private pendingRequests = new Map<number, PendingRequest>();
  protected remoteHello: HelloBody | null = null;
  protected workspaceRoot: string | null = null;
  protected lastStopped: StoppedBody | null = null;

  protected constructor(role: SessionRole) {
    this.role = role;
  }

  public attach(sessionId: string): void {
    if (!sessionId.trim()) {
      throw new SessionStateError('会话标识不能为空。');
    }

    // 切换会话时必须彻底清空旧状态，避免旧消息污染新调试会话。
    this.sessionId = sessionId;
    this.phase = 'connected';
    this.nextSeq = 1;
    this.lastInboundSeq = 0;
    this.lastOutboundSeq = 0;
    this.pendingRequests.clear();
    this.remoteHello = null;
    this.workspaceRoot = null;
    this.lastStopped = null;
    this.sourceCatalog.clear();
  }

  public detach(reason = 'disconnect'): void {
    void reason;
    this.sessionId = null;
    this.phase = 'closed';
    this.nextSeq = 1;
    this.lastInboundSeq = 0;
    this.lastOutboundSeq = 0;
    this.pendingRequests.clear();
    this.remoteHello = null;
    this.workspaceRoot = null;
    this.lastStopped = null;
    this.sourceCatalog.clear();
  }

  public snapshot(): SessionSnapshot {
    return {
      role: this.role,
      phase: this.phase,
      sessionId: this.sessionId,
      lastInboundSeq: this.lastInboundSeq,
      lastOutboundSeq: this.lastOutboundSeq,
      pendingRequestCount: this.pendingRequests.size,
      remoteHelloRuntimeVersion: this.remoteHello?.runtimeVersion ?? null,
      sourceMapCount: this.sourceCatalog.getEntryCount(),
      workspaceRoot: this.workspaceRoot,
      lastStoppedReason: this.lastStopped?.reason ?? null,
      lastStoppedThreadId: this.lastStopped?.threadId ?? null,
      lastStoppedFrameId: this.lastStopped?.frameId ?? null,
      lastStoppedSourceId: this.lastStopped?.sourceId ?? null,
      lastStoppedRow: this.lastStopped?.row ?? null
    };
  }

  public getSourceCatalog(): SourceCatalog {
    return this.sourceCatalog;
  }

  protected requireSession(): string {
    if (this.sessionId === null) {
      throw new SessionStateError('调试会话尚未建立。');
    }

    return this.sessionId;
  }

  protected requirePhase(allowed: ReadonlyArray<SessionPhase>, message: string): void {
    if (!allowed.includes(this.phase)) {
      throw new SessionStateError(message);
    }
  }

  protected acceptInbound<TCommand extends ProtocolCommand>(
    message: ProtocolEnvelope
  ): ProtocolEnvelope {
    const sessionId = this.requireSession();
    if (message.sessionId !== sessionId) {
      throw new SessionMismatchError(`会话标识不匹配：当前会话是 ${sessionId}，消息属于 ${message.sessionId}。`);
    }

    if (message.seq <= this.lastInboundSeq) {
      throw new SessionStateError('收到的消息序号不是单调递增的。');
    }

    this.lastInboundSeq = message.seq;

    if (typeof message.replyTo === 'number') {
      this.pendingRequests.delete(message.replyTo);
    }

    return message as ProtocolEnvelope;
  }

  protected createRequest<TCommand extends ProtocolCommand>(
    command: TCommand,
    body: ProtocolCommandMap[TCommand],
    replyTo = this.lastInboundSeq
  ): RequestEnvelope<TCommand> {
    const sessionId = this.requireSession();
    if (replyTo <= 0) {
      throw new SessionStateError('请求必须回复一个已存在的上游消息。');
    }

    const seq = this.nextSequence();
    const request = createRequestEnvelope(sessionId, command, body, seq, replyTo);
    this.lastOutboundSeq = seq;
    this.pendingRequests.set(seq, { seq, command });
    return request;
  }

  protected createResponse<TCommand extends ProtocolCommand>(
    command: TCommand,
    body: ProtocolCommandMap[TCommand],
    replyTo = this.lastInboundSeq
  ): ResponseEnvelope<TCommand> {
    const sessionId = this.requireSession();
    if (replyTo <= 0) {
      throw new SessionStateError('响应必须回复一个已存在的上游消息。');
    }

    const seq = this.nextSequence();
    const response = createResponseEnvelope(sessionId, command, body, seq, replyTo);
    this.lastOutboundSeq = seq;
    return response;
  }

  protected createEvent<TCommand extends ProtocolCommand>(
    command: TCommand,
    body: ProtocolCommandMap[TCommand],
    replyTo?: number
  ): EventEnvelope<TCommand> {
    const sessionId = this.requireSession();
    const seq = this.nextSequence();
    const event = createEventEnvelope(
      sessionId,
      command,
      body,
      seq,
      replyTo ?? (this.lastInboundSeq > 0 ? this.lastInboundSeq : undefined)
    );
    this.lastOutboundSeq = seq;
    return event;
  }

  protected setPhase(phase: SessionPhase): void {
    this.phase = phase;
  }

  protected setRemoteHello(hello: HelloBody): void {
    this.remoteHello = hello;
    this.sourceCatalog.registerSources(hello.sourceMap);
  }

  protected setWorkspaceRoot(workspaceRoot: string): void {
    this.workspaceRoot = workspaceRoot;
  }

  protected setLastStopped(stopped: StoppedBody | null): void {
    this.lastStopped = stopped;
  }

  protected nextSequence(): number {
    const seq = this.nextSeq;
    this.nextSeq += 1;
    return seq;
  }
}

export class AdapterSessionMachine extends BaseSessionMachine {
  private readonly breakpointRegistry = new BreakpointRegistry(this.sourceCatalog);
  private readonly stackModel = new StackModel();
  private readonly scopeModel = new ScopeModel();
  private readonly variableModel = new VariableModel();

  constructor() {
    super('adapter');
  }

  public override attach(sessionId: string): void {
    super.attach(sessionId);
    this.stackModel.clear();
    this.scopeModel.clear();
    this.variableModel.clear();
    this.breakpointRegistry.clear();
  }

  public override detach(reason = 'disconnect'): void {
    super.detach(reason);
    this.stackModel.clear();
    this.scopeModel.clear();
    this.variableModel.clear();
    this.breakpointRegistry.clear();
  }

  public receiveHello(message: EventEnvelope<'hello'>): void {
    this.acceptInbound(message);
    this.setRemoteHello(message.body);
    this.setPhase('negotiating');
  }

  public createInitialize(body: InitializeBody): RequestEnvelope<'initialize'> {
    this.requirePhase(['negotiating'], '收到 hello 之前不能发送 initialize。');
    this.setWorkspaceRoot(body.workspaceRoot);
    this.sourceCatalog.registerPathMappings(body.pathMapping);
    this.setPhase('initializing');
    return this.createRequest('initialize', body);
  }

  public createSetBreakpoints(
    sourcePath: string,
    breakpoints: ReadonlyArray<SourceBreakpointInput>
  ): RequestEnvelope<'setBreakpoints'> {
    this.requirePhase(['ready', 'paused'], '只能在就绪或暂停状态下设置断点。');
    const body = this.breakpointRegistry.applyBreakpoints(sourcePath, breakpoints);
    return this.createRequest('setBreakpoints', body);
  }

  public createContinue(threadId: number): RequestEnvelope<'continue'> {
    this.requirePhase(['ready', 'paused'], '只能在就绪或暂停状态下继续执行。');
    const request = this.createRequest('continue', {
      threadId
    });
    this.setPhase('running');
    return request;
  }

  public createNext(threadId: number): RequestEnvelope<'next'> {
    this.requirePhase(['paused'], '只能在暂停状态下单步越过。');
    const request = this.createRequest('next', {
      threadId
    });
    this.setPhase('running');
    return request;
  }

  public createStepIn(threadId: number): RequestEnvelope<'stepIn'> {
    this.requirePhase(['paused'], '只能在暂停状态下单步进入。');
    const request = this.createRequest('stepIn', {
      threadId
    });
    this.setPhase('running');
    return request;
  }

  public createStepOut(threadId: number): RequestEnvelope<'stepOut'> {
    this.requirePhase(['paused'], '只能在暂停状态下单步步出。');
    const request = this.createRequest('stepOut', {
      threadId
    });
    this.setPhase('running');
    return request;
  }

  public createDisconnect(reason: string): RequestEnvelope<'disconnect'> {
    this.requirePhase(['negotiating', 'initializing', 'ready', 'paused', 'running'], '只能在调试会话建立后断开。');
    return this.createRequest('disconnect', {
      reason
    });
  }

  public createStackTrace(
    threadId: number,
    startFrame = 0,
    levels = 20
  ): RequestEnvelope<'stackTrace'> {
    this.requirePhase(['paused'], '只能在暂停状态下请求调用栈。');
    return this.createRequest('stackTrace', {
      threadId,
      startFrame,
      levels
    });
  }

  public createScopes(frameId: number): RequestEnvelope<'scopes'> {
    this.requirePhase(['paused'], '只能在暂停状态下请求作用域。');
    this.requireStoppedThread();
    const frame = this.stackModel.getFrame(frameId, this.lastStopped?.threadId ?? undefined);
    if (!frame) {
      throw new SessionStateError('调用栈中不存在指定帧。');
    }

    return this.createRequest('scopes', {
      frameId
    });
  }

  public createVariables(variablesReference: number): RequestEnvelope<'variables'> {
    this.requirePhase(['paused'], '只能在暂停状态下请求变量。');
    this.requireStoppedThread();

    if (!Number.isInteger(variablesReference) || variablesReference < 0) {
      throw new SessionStateError('variablesReference 不合法。');
    }

    return this.createRequest('variables', {
      variablesReference
    });
  }

  public receiveBreakpointValidated(message: EventEnvelope<'breakpointValidated'>): void {
    this.acceptInbound(message);
    this.breakpointRegistry.mergeValidatedState(message.body);
  }

  public receiveStopped(message: EventEnvelope<'stopped'>): void {
    this.acceptInbound(message);
    this.resetPauseModels();
    this.setLastStopped(message.body);
    this.setPhase('paused');
  }

  public receiveException(message: EventEnvelope<'exception'>): StackTraceState {
    this.acceptInbound(message);
    this.resetPauseModels();

    const stackState = this.stackModel.applyException(message.body);
    const topFrame = stackState.frames[0];
    if (!topFrame) {
      throw new SessionStateError('异常调用栈不能为空。');
    }

    this.setLastStopped({
      reason: 'exception',
      threadId: topFrame.threadId,
      frameId: topFrame.frameId,
      sourceId: topFrame.sourceId,
      row: topFrame.row
    });
    this.setPhase('paused');
    return stackState;
  }

  public receiveDisconnect(message: EventEnvelope<'disconnect'>): void {
    this.acceptInbound(message);
    this.detach('disconnect');
  }

  public receiveStackTrace(message: ResponseEnvelope<'stackTrace'>): StackTraceState {
    this.requirePhase(['paused'], '只有在暂停状态下才能接收调用栈。');
    this.acceptInbound(message);
    return this.stackModel.applyStackTrace(message.body);
  }

  public receiveScopes(message: ResponseEnvelope<'scopes'>): ScopeModelState {
    this.requirePhase(['paused'], '只有在暂停状态下才能接收作用域。');
    this.acceptInbound(message);
    const threadId = this.requireStoppedThread();
    return this.scopeModel.applyScopes(threadId, message.body);
  }

  public receiveVariables(message: ResponseEnvelope<'variables'>): VariableModelState {
    this.requirePhase(['paused'], '只有在暂停状态下才能接收变量。');
    this.acceptInbound(message);
    const threadId = this.requireStoppedThread();
    return this.variableModel.applyVariables(threadId, message.body);
  }

  public receiveReady(message: EventEnvelope<'ready'>): void {
    this.requirePhase(['initializing'], '只有在发送 initialize 之后才能接收 ready。');
    this.acceptInbound(message);
    if (!message.body.accepted) {
      throw new SessionStateError('目标端拒绝了调试会话。');
    }
    this.setPhase('ready');
  }

  public getBreakpointRegistry(): BreakpointRegistry {
    return this.breakpointRegistry;
  }

  public getStackModel(): StackModel {
    return this.stackModel;
  }

  public getScopeModel(): ScopeModel {
    return this.scopeModel;
  }

  public getVariableModel(): VariableModel {
    return this.variableModel;
  }
  private resetPauseModels(): void {
    // 异常或正常暂停都会切到新的现场，旧的栈、句柄和变量缓存必须一起清掉。
    this.stackModel.clear();
    this.scopeModel.clear();
    this.variableModel.clear();
  }

  private requireStoppedThread(): number {
    if (!this.lastStopped) {
      throw new SessionStateError('当前没有已暂停的线程。');
    }

    return this.lastStopped.threadId;
  }
}
