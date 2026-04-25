import { type PathMappingRule } from './protocol.js';

export interface DapRequestMessage {
  readonly seq: number;
  readonly type: 'request';
  readonly command: string;
  readonly arguments?: unknown;
}

export interface DapResponseMessage {
  readonly seq: number;
  readonly type: 'response';
  readonly request_seq: number;
  readonly success: boolean;
  readonly command: string;
  readonly message?: string;
  readonly body?: unknown;
}

export interface DapEventMessage {
  readonly seq: number;
  readonly type: 'event';
  readonly event: string;
  readonly body?: unknown;
}

export type DapMessage = DapRequestMessage | DapResponseMessage | DapEventMessage;

export interface DapSource {
  readonly path?: string;
  readonly name?: string;
  readonly sourceReference?: number;
}

export interface DapBreakpoint {
  readonly line: number;
  readonly column?: number;
  readonly condition?: string;
  readonly logMessage?: string;
}

export interface DapStackFrame {
  readonly id: number;
  readonly name: string;
  readonly source?: DapSource;
  readonly line: number;
  readonly column?: number;
  readonly presentationHint?: 'normal' | 'label' | 'subtle';
}

export interface DapScope {
  readonly name: string;
  readonly variablesReference: number;
  readonly expensive: boolean;
}

export interface DapVariable {
  readonly name: string;
  readonly value: string;
  readonly type?: string;
  readonly variablesReference: number;
  readonly namedVariables?: number;
  readonly indexedVariables?: number;
}

export interface WorkflowAttachArguments {
  readonly host?: string;
  readonly port: number;
  readonly workspaceRoot: string;
  readonly pathMapping?: ReadonlyArray<PathMappingRule>;
  readonly connectTimeoutMs?: number;
  readonly stopOnEntry?: boolean;
}

export interface WorkflowInitializeArguments {
  readonly adapterID?: string;
}

export interface WorkflowConfigurationDoneArguments {
  readonly restart?: boolean;
}

export interface WorkflowContinueArguments {
  readonly threadId: number;
}

export interface WorkflowStackTraceArguments {
  readonly threadId: number;
  readonly startFrame?: number;
  readonly levels?: number;
}

export interface WorkflowScopesArguments {
  readonly frameId: number;
}

export interface WorkflowVariablesArguments {
  readonly variablesReference: number;
}

export interface WorkflowEvaluateArguments {
  readonly expression: string;
  readonly frameId?: number;
  readonly context?: string;
}

export interface WorkflowEvaluateResponseBody {
  readonly result: string;
  readonly type?: string;
  readonly variablesReference: number;
  readonly namedVariables?: number;
  readonly indexedVariables?: number;
}

export interface WorkflowSetBreakpointsArguments {
  readonly source: DapSource;
  readonly breakpoints?: ReadonlyArray<DapBreakpoint>;
}

export interface WorkflowThreadsArguments {
  readonly restart?: boolean;
}

export interface WorkflowOutputEventBody {
  readonly category: 'console' | 'stdout' | 'stderr' | 'telemetry' | 'important';
  readonly output: string;
}

export class DapProtocolError extends Error {
  constructor(message: string) {
    super(message);
    this.name = 'DapProtocolError';
  }
}

export class DapMessageReader {
  private buffer: Buffer<ArrayBufferLike> = Buffer.alloc(0) as Buffer<ArrayBufferLike>;

  public push(chunk: Buffer | string): Array<DapMessage> {
    const incoming = (typeof chunk === 'string' ? Buffer.from(chunk, 'utf8') : Buffer.from(chunk)) as Buffer<ArrayBufferLike>;
    const current = this.buffer;
    this.buffer = current.length === 0 ? incoming : Buffer.concat([current, incoming]) as Buffer<ArrayBufferLike>;

    const messages: Array<DapMessage> = [];
    while (true) {
      const headerEnd = this.buffer.indexOf('\r\n\r\n');
      if (headerEnd < 0) {
        break;
      }

      const headerText = this.buffer.subarray(0, headerEnd).toString('utf8');
      const contentLength = this.parseContentLength(headerText);
      const bodyStart = headerEnd + 4;
      const bodyEnd = bodyStart + contentLength;
      if (this.buffer.length < bodyEnd) {
        break;
      }

      const bodyText = this.buffer.subarray(bodyStart, bodyEnd).toString('utf8');
      this.buffer = this.buffer.subarray(bodyEnd);
      messages.push(this.parseMessage(bodyText));
    }

    return messages;
  }

  public reset(): void {
    this.buffer = Buffer.alloc(0);
  }

  private parseContentLength(headerText: string): number {
    const lines = headerText.split(/\r?\n/);
    for (const line of lines) {
      const separator = line.indexOf(':');
      if (separator < 0) {
        continue;
      }

      const name = line.slice(0, separator).trim().toLowerCase();
      const value = line.slice(separator + 1).trim();
      if (name === 'content-length') {
        const parsed = Number(value);
        if (!Number.isInteger(parsed) || parsed < 0) {
          throw new DapProtocolError('Content-Length 不是有效的非负整数。');
        }
        return parsed;
      }
    }

    throw new DapProtocolError('缺少 Content-Length 头。');
  }

  private parseMessage(text: string): DapMessage {
    let message: unknown;
    try {
      message = JSON.parse(text);
    }
    catch (error) {
      throw new DapProtocolError(`无法解析 DAP 消息：${error instanceof Error ? error.message : String(error)}`);
    }

    if (typeof message !== 'object' || message === null) {
      throw new DapProtocolError('DAP 消息必须是对象。');
    }

    const record = message as Record<string, unknown>;
    const type = record.type;
    if (type === 'request') {
      if (typeof record.seq !== 'number' || typeof record.command !== 'string') {
        throw new DapProtocolError('DAP 请求缺少 seq 或 command。');
      }
      return {
        seq: record.seq,
        type: 'request',
        command: record.command,
        arguments: record.arguments
      };
    }

    if (type === 'response') {
      if (typeof record.seq !== 'number' || typeof record.request_seq !== 'number' || typeof record.command !== 'string') {
        throw new DapProtocolError('DAP 响应缺少 seq、request_seq 或 command。');
      }
      return {
        seq: record.seq,
        type: 'response',
        request_seq: record.request_seq,
        success: record.success !== false,
        command: record.command,
        message: typeof record.message === 'string' ? record.message : undefined,
        body: record.body
      };
    }

    if (type === 'event') {
      if (typeof record.seq !== 'number' || typeof record.event !== 'string') {
        throw new DapProtocolError('DAP 事件缺少 seq 或 event。');
      }
      return {
        seq: record.seq,
        type: 'event',
        event: record.event,
        body: record.body
      };
    }

    throw new DapProtocolError(`未知的 DAP 消息类型：${String(type)}。`);
  }
}

export function createDapRequest(
  seq: number,
  command: string,
  args?: unknown
): DapRequestMessage {
  return {
    seq,
    type: 'request',
    command,
    arguments: args
  };
}

export function createDapResponse(
  seq: number,
  requestSeq: number,
  command: string,
  body?: unknown,
  success = true,
  message?: string
): DapResponseMessage {
  return {
    seq,
    type: 'response',
    request_seq: requestSeq,
    success,
    command,
    message,
    body
  };
}

export function createDapEvent(
  seq: number,
  event: string,
  body?: unknown
): DapEventMessage {
  return {
    seq,
    type: 'event',
    event,
    body
  };
}

export function serializeDapMessage(message: DapMessage): Buffer {
  const json = JSON.stringify(message);
  const header = `Content-Length: ${Buffer.byteLength(json, 'utf8')}\r\n\r\n`;
  return Buffer.from(header + json, 'utf8');
}

export function writeDapMessage(
  stream: NodeJS.WritableStream,
  message: DapMessage
): void {
  stream.write(serializeDapMessage(message));
}
