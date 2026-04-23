import {
  createEventEnvelope,
  createRequestEnvelope,
  DEFAULT_CAPABILITIES,
  type CapabilitySet,
  type EventEnvelope,
  type HelloBody,
  type InitializeBody,
  type ProtocolEnvelope,
  type ProtocolCommand,
  type ProtocolCommandMap,
  type ReadyBody,
  type RequestEnvelope,
  type SessionPhase,
  type SessionRole,
  type SourceMapEntry
} from './protocol.js';
import { SourceCatalog } from './sourceMap.js';

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
}

interface PendingRequest {
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
      workspaceRoot: this.workspaceRoot
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

  protected createEvent<TCommand extends ProtocolCommand>(
    command: TCommand,
    body: ProtocolCommandMap[TCommand],
    replyTo?: number
  ): EventEnvelope<TCommand> {
    const sessionId = this.requireSession();
    const seq = this.nextSequence();
    const event = createEventEnvelope(sessionId, command, body, seq, replyTo ?? (this.lastInboundSeq > 0 ? this.lastInboundSeq : undefined));
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

  protected nextSequence(): number {
    const seq = this.nextSeq;
    this.nextSeq += 1;
    return seq;
  }
}

export interface TargetSessionOptions {
  readonly runtimeVersion: string;
  readonly capabilities?: CapabilitySet;
  readonly sourceMap?: ReadonlyArray<SourceMapEntry>;
}

export class AdapterSessionMachine extends BaseSessionMachine {
  constructor() {
    super('adapter');
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

  public receiveReady(message: EventEnvelope<'ready'>): void {
    this.requirePhase(['initializing'], '只有在发送 initialize 之后才能接收 ready。');
    this.acceptInbound(message);
    if (!message.body.accepted) {
      throw new SessionStateError('目标端拒绝了调试会话。');
    }
    this.setPhase('ready');
  }
}

export class TargetSessionMachine extends BaseSessionMachine {
  private readonly runtimeVersion: string;
  private readonly capabilities: CapabilitySet;
  private readonly sourceMap: ReadonlyArray<SourceMapEntry>;

  constructor(options: TargetSessionOptions) {
    super('target');
    this.runtimeVersion = options.runtimeVersion;
    this.capabilities = options.capabilities ?? DEFAULT_CAPABILITIES;
    this.sourceMap = options.sourceMap ?? [];
  }

  public createHello(): EventEnvelope<'hello'> {
    this.requirePhase(['connected'], '调试会话必须先建立连接才能发送 hello。');
    this.sourceCatalog.registerSources(this.sourceMap);
    this.setPhase('negotiating');
    return this.createEvent('hello', {
      runtimeVersion: this.runtimeVersion,
      protocolVersion: 1,
      capabilities: this.capabilities,
      sourceMap: this.sourceMap
    });
  }

  public receiveInitialize(message: RequestEnvelope<'initialize'>): EventEnvelope<'ready'> {
    this.requirePhase(['negotiating'], '必须先发送 hello，再处理 initialize。');
    this.acceptInbound(message);
    this.setWorkspaceRoot(message.body.workspaceRoot);
    this.setPhase('ready');
    return this.createEvent(
      'ready',
      {
        sessionId: message.sessionId,
        runtimeVersion: this.runtimeVersion,
        accepted: true,
        capabilities: this.capabilities
      },
      message.seq
    );
  }
}
