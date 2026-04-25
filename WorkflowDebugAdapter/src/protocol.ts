export const PROTOCOL_VERSION = 1 as const;

export type SessionRole = 'adapter' | 'target';

export type SessionPhase =
  | 'idle'
  | 'connected'
  | 'negotiating'
  | 'initializing'
  | 'ready'
  | 'paused'
  | 'running'
  | 'closed';

export type MessageType = 'request' | 'response' | 'event';

export type ProtocolCommand =
  | 'hello'
  | 'initialize'
  | 'ready'
  | 'setBreakpoints'
  | 'breakpointValidated'
  | 'stopped'
  | 'stackTrace'
  | 'scopes'
  | 'variables'
  | 'evaluate'
  | 'continue'
  | 'next'
  | 'stepIn'
  | 'stepOut'
  | 'exception'
  | 'output'
  | 'disconnect'
  | 'error';

export interface CapabilitySet {
  readonly supportsRemoteAttach: boolean;
  readonly supportsBreakpoints: boolean;
  readonly supportsContinue: boolean;
  readonly supportsStepOver: boolean;
  readonly supportsStepIn: boolean;
  readonly supportsStepOut: boolean;
  readonly supportsStackTrace: boolean;
  readonly supportsVariables: boolean;
}

export const DEFAULT_CAPABILITIES: CapabilitySet = Object.freeze({
  supportsRemoteAttach: true,
  supportsBreakpoints: true,
  supportsContinue: true,
  supportsStepOver: true,
  supportsStepIn: true,
  supportsStepOut: true,
  supportsStackTrace: true,
  supportsVariables: true
});

export interface SourceMapEntry {
  readonly codeIndex: number;
  readonly sourcePath: string;
  readonly row: number;
  readonly column?: number;
}

export type ScopeKind = 'local' | 'argument' | 'captured' | 'global' | 'object';

export interface PathMappingRule {
  readonly localPath: string;
  readonly remotePath: string;
}

export interface BreakpointSpec {
  readonly breakpointId: string;
  readonly row: number;
  readonly column?: number;
  readonly condition?: string;
  readonly logMessage?: string;
}

export interface HelloBody {
  readonly runtimeVersion: string;
  readonly protocolVersion: number;
  readonly capabilities: CapabilitySet;
  readonly sourceMap: ReadonlyArray<SourceMapEntry>;
}

export interface InitializeBody {
  readonly workspaceRoot: string;
  readonly pathMapping: ReadonlyArray<PathMappingRule>;
  readonly stopOnEntry?: boolean;
  readonly supports: CapabilitySet;
}

export interface ReadyBody {
  readonly sessionId: string;
  readonly runtimeVersion: string;
  readonly accepted: boolean;
  readonly capabilities: CapabilitySet;
}

export interface SetBreakpointsBody {
  readonly sourcePath: string;
  readonly codeIndex: number;
  readonly breakpoints: ReadonlyArray<BreakpointSpec>;
}

export interface BreakpointValidatedBody {
  readonly breakpointId: string;
  readonly verified: boolean;
  readonly reason?: string;
}

export type StoppedReason = 'breakpoint' | 'pause' | 'step' | 'exception' | 'entry';

export interface StoppedBody {
  readonly reason: StoppedReason;
  readonly threadId: number;
  readonly frameId: number;
  readonly sourceId: number;
  readonly row: number;
}

// `frameId` 直接沿用调用栈索引，适配器后续可以稳定回到同一帧请求变量。
export interface StackFrameBody {
  readonly threadId: number;
  readonly frameId: number;
  readonly callStackIndex: number;
  readonly functionName: string;
  readonly sourceId: number;
  readonly sourcePath: string;
  readonly row: number;
  readonly line: number;
  readonly column: number;
  readonly canRequestVariables: boolean;
}

export interface ScopeEntryBody {
  readonly name: string;
  readonly kind: ScopeKind;
  readonly variablesReference: number;
  readonly canExpand: boolean;
  readonly namedVariables: number;
  readonly indexedVariables: number;
}

export interface VariableEntryBody {
  readonly name: string;
  readonly value: string;
  readonly type: string;
  readonly variablesReference: number;
  readonly canExpand: boolean;
  readonly namedVariables: number;
  readonly indexedVariables: number;
}

// `stackTrace` 同时用于请求与响应：请求只使用查询字段，响应再附带 `totalFrames` 和 `frames`。
export interface StackTraceBody {
  readonly threadId: number;
  readonly startFrame: number;
  readonly levels: number;
  readonly totalFrames?: number;
  readonly frames?: ReadonlyArray<StackFrameBody>;
}

export interface ScopesBody {
  readonly frameId: number;
  readonly scopes?: ReadonlyArray<ScopeEntryBody>;
}

export interface VariablesBody {
  readonly variablesReference: number;
  readonly scopeKind: ScopeKind;
  readonly frameId: number;
  readonly variables?: ReadonlyArray<VariableEntryBody>;
}

export interface EvaluateBody {
  readonly expression: string;
  readonly frameId: number;
  readonly scopeKind: 'local' | 'argument' | 'captured' | 'global';
}

export interface ContinueBody {
  readonly threadId: number;
}

export interface StepBody {
  readonly threadId: number;
}

export interface ExceptionBody {
  readonly message: string;
  readonly fatal: boolean;
  readonly callStack: ReadonlyArray<StackFrameBody>;
}

export interface OutputBody {
  readonly level: 'log' | 'info' | 'warn' | 'error';
  readonly message: string;
}

export interface DisconnectBody {
  readonly reason: string;
  readonly restart: boolean;
}

export interface ErrorBody {
  readonly code: string;
  readonly message: string;
  readonly details?: string;
}

export interface ProtocolCommandMap {
  readonly hello: HelloBody;
  readonly initialize: InitializeBody;
  readonly ready: ReadyBody;
  readonly setBreakpoints: SetBreakpointsBody;
  readonly breakpointValidated: BreakpointValidatedBody;
  readonly stopped: StoppedBody;
  readonly stackTrace: StackTraceBody;
  readonly scopes: ScopesBody;
  readonly variables: VariablesBody;
  readonly evaluate: EvaluateBody;
  readonly continue: ContinueBody;
  readonly next: StepBody;
  readonly stepIn: StepBody;
  readonly stepOut: StepBody;
  readonly exception: ExceptionBody;
  readonly output: OutputBody;
  readonly disconnect: DisconnectBody;
  readonly error: ErrorBody;
}

export interface EnvelopeBase<TType extends MessageType, TCommand extends ProtocolCommand> {
  readonly type: TType;
  readonly seq: number;
  readonly replyTo?: number;
  readonly sessionId: string;
  readonly cmd: TCommand;
  readonly body: ProtocolCommandMap[TCommand];
}

export type RequestEnvelope<TCommand extends ProtocolCommand> = EnvelopeBase<'request', TCommand> & {
  readonly replyTo: number;
};

export type ResponseEnvelope<TCommand extends ProtocolCommand> = EnvelopeBase<'response', TCommand> & {
  readonly replyTo: number;
};

export type EventEnvelope<TCommand extends ProtocolCommand> = EnvelopeBase<'event', TCommand>;

export type ProtocolEnvelope =
  | RequestEnvelope<ProtocolCommand>
  | ResponseEnvelope<ProtocolCommand>
  | EventEnvelope<ProtocolCommand>;

export function createRequestEnvelope<TCommand extends ProtocolCommand>(
  sessionId: string,
  command: TCommand,
  body: ProtocolCommandMap[TCommand],
  seq: number,
  replyTo: number
): RequestEnvelope<TCommand> {
  return {
    type: 'request',
    seq,
    replyTo,
    sessionId,
    cmd: command,
    body
  };
}

export function createResponseEnvelope<TCommand extends ProtocolCommand>(
  sessionId: string,
  command: TCommand,
  body: ProtocolCommandMap[TCommand],
  seq: number,
  replyTo: number
): ResponseEnvelope<TCommand> {
  return {
    type: 'response',
    seq,
    replyTo,
    sessionId,
    cmd: command,
    body
  };
}

export function createEventEnvelope<TCommand extends ProtocolCommand>(
  sessionId: string,
  command: TCommand,
  body: ProtocolCommandMap[TCommand],
  seq: number,
  replyTo?: number
): EventEnvelope<TCommand> {
  return {
    type: 'event',
    seq,
    replyTo,
    sessionId,
    cmd: command,
    body
  };
}
