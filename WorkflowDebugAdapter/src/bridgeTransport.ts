import net, { type AddressInfo, type Server, type Socket } from 'node:net';
import { once } from 'node:events';
import { StringDecoder } from 'node:string_decoder';

// 设计取舍：
// - Adapter 端只需要「listen -> accept 单连接 -> 收发消息 -> close」的最小闭环。
// - 删除 client/reconnect 等扩展点，避免隐式重连导致的状态回滚与错误面扩大。

export type BridgeTransportState = 'idle' | 'listening' | 'connected' | 'closed';

export interface BridgeTransportEndpoint {
  readonly host: string;
  readonly port: number;
}

export interface BridgeTransportOptions<TMessage> {
  readonly name: string;
  readonly endpoint: BridgeTransportEndpoint;
  readonly serialize?: (message: TMessage) => string;
  readonly deserialize?: (line: string) => TMessage;
}

export interface BridgeTransportSnapshot {
  readonly name: string;
  readonly state: BridgeTransportState;
  readonly endpoint: BridgeTransportEndpoint;
  readonly localAddress: AddressInfo | null;
  readonly remoteAddress: string | null;
}

export class BridgeTransportError extends Error {
  constructor(message: string) {
    super(message);
    this.name = 'BridgeTransportError';
  }
}

type Listener<T> = (value: T) => void;

function addListener<T>(listeners: Array<Listener<T>>, listener: Listener<T>): () => void {
  listeners.push(listener);
  return () => {
    const index = listeners.indexOf(listener);
    if (index >= 0) {
      listeners.splice(index, 1);
    }
  };
}

function emit<T>(listeners: ReadonlyArray<Listener<T>>, value: T): void {
  for (const listener of [...listeners]) {
    listener(value);
  }
}

export class LineFramedJsonCodec<TMessage> {
  private readonly serialize: (message: TMessage) => string;
  private readonly deserialize: (line: string) => TMessage;
  private readonly decoder = new StringDecoder('utf8');
  private buffer = '';

  constructor(
    serialize: (message: TMessage) => string = (message: TMessage) => JSON.stringify(message),
    deserialize: (line: string) => TMessage = (line: string) => JSON.parse(line) as TMessage
  ) {
    this.serialize = serialize;
    this.deserialize = deserialize;
  }

  public encode(message: TMessage): string {
    return `${this.serialize(message)}\n`;
  }

  public push(chunk: Buffer | string): Array<TMessage> {
    // 使用 UTF-8 解码器避免半个多字节字符被截断后产生乱码。
    this.buffer += typeof chunk === 'string' ? chunk : this.decoder.write(chunk);
    return this.drain();
  }

  public flush(): Array<TMessage> {
    this.buffer += this.decoder.end();
    const messages = this.drain();
    const tail = this.buffer.trim();
    this.buffer = '';
    if (tail.length > 0) {
      throw new BridgeTransportError('收到未以换行结束的残缺消息。');
    }
    return messages;
  }

  public reset(): void {
    this.buffer = '';
  }

  private drain(): Array<TMessage> {
    const messages: Array<TMessage> = [];
    while (true) {
      const newlineIndex = this.buffer.indexOf('\n');
      if (newlineIndex < 0) {
        break;
      }

      let line = this.buffer.slice(0, newlineIndex);
      this.buffer = this.buffer.slice(newlineIndex + 1);
      if (line.endsWith('\r')) {
        line = line.slice(0, -1);
      }
      if (!line.trim()) {
        continue;
      }

      messages.push(this.deserialize(line));
    }

    return messages;
  }
}

export class BridgeTransport<TMessage> {
  private readonly name: string;
  private readonly endpoint: BridgeTransportEndpoint;
  private readonly codec: LineFramedJsonCodec<TMessage>;
  private readonly messageListeners: Array<Listener<TMessage>> = [];
  private readonly openListeners: Array<Listener<void>> = [];
  private readonly closeListeners: Array<Listener<void>> = [];
  private readonly errorListeners: Array<Listener<Error>> = [];

  private state: BridgeTransportState = 'idle';
  private socket: Socket | null = null;
  private server: Server | null = null;
  private localAddress: AddressInfo | null = null;
  private remoteAddress: string | null = null;
  private listenPromise: Promise<void> | null = null;
  private closeEmitted = false;

  constructor(options: BridgeTransportOptions<TMessage>) {
    this.name = options.name;
    this.endpoint = options.endpoint;
    this.codec = new LineFramedJsonCodec<TMessage>(options.serialize, options.deserialize);
  }

  public getState(): BridgeTransportState {
    return this.state;
  }

  public snapshot(): BridgeTransportSnapshot {
    return {
      name: this.name,
      state: this.state,
      endpoint: this.endpoint,
      localAddress: this.localAddress,
      remoteAddress: this.remoteAddress
    };
  }

  public onMessage(listener: Listener<TMessage>): () => void {
    return addListener(this.messageListeners, listener);
  }

  public onOpen(listener: Listener<void>): () => void {
    return addListener(this.openListeners, listener);
  }

  public onClose(listener: Listener<void>): () => void {
    return addListener(this.closeListeners, listener);
  }

  public onError(listener: Listener<Error>): () => void {
    return addListener(this.errorListeners, listener);
  }

  public async listen(): Promise<void> {
    if (this.state === 'closed') {
      throw new BridgeTransportError('传输层已关闭，不能再次 listen。');
    }

    if (this.state !== 'idle') {
      return;
    }

    if (this.listenPromise) {
      return this.listenPromise;
    }

    this.state = 'listening';

    this.listenPromise = new Promise<void>((resolve, reject) => {
      const server = net.createServer((socket) => {
        // 单连接策略：只接受第一个连接，其余直接丢弃。
        if (this.state === 'closed') {
          socket.destroy();
          return;
        }

        if (this.socket) {
          socket.destroy();
          return;
        }

        this.attachSocket(socket);

        // 已接入连接后不再接受更多连接。
        this.server = null;
        if (server.listening) {
          server.close();
        }
      });

      this.server = server;

      server.once('error', (error) => {
        const wrapped = this.asError(error);
        if (this.state !== 'closed') {
          this.emitError(wrapped);
        }
        reject(wrapped);
      });

      server.listen(this.endpoint.port, this.endpoint.host, () => {
        const address = server.address();
        this.localAddress = typeof address === 'object' && address ? (address as AddressInfo) : null;
        resolve();
      });
    });

    try {
      await this.listenPromise;
    } finally {
      this.listenPromise = null;
    }
  }

  public async send(message: TMessage): Promise<void> {
    const socket = this.socket;
    if (!socket || socket.destroyed || this.state !== 'connected') {
      throw new BridgeTransportError('当前没有可用的活动连接。');
    }

    const payload = this.codec.encode(message);
    if (!socket.write(payload, 'utf8')) {
      await once(socket, 'drain');
    }
  }

  public async close(): Promise<void> {
    if (this.state === 'closed') {
      return;
    }

    const socket = this.socket;
    const server = this.server;

    this.socket = null;
    this.server = null;
    this.remoteAddress = null;

    if (socket && !socket.destroyed) {
      socket.end();
      socket.destroy();
    }
    if (server) {
      if (server.listening) {
        await new Promise<void>((resolve, reject) => {
          try {
            server.close((error) => {
              if (error) {
                reject(this.asError(error));
                return;
              }
              resolve();
            });
          } catch {
            resolve();
          }
        });
      }
    }

    this.codec.reset();
    this.state = 'closed';
    this.emitCloseOnce();
  }

  private attachSocket(socket: Socket): void {
    this.socket = socket;
    this.codec.reset();
    this.state = 'connected';
    this.remoteAddress = this.formatRemoteAddress(socket);
    emit(this.openListeners, undefined);

    socket.on('data', (chunk) => {
      try {
        const messages = this.codec.push(chunk);
        for (const message of messages) {
          emit(this.messageListeners, message);
        }
      } catch (error) {
        const wrapped = this.asError(error);
        this.emitError(wrapped);
        socket.destroy();
      }
    });

    socket.once('close', () => {
      // close 事件可能由远端断开或本地 close() 触发。
      this.socket = null;
      this.remoteAddress = null;
      this.codec.reset();
      this.state = 'closed';
      this.emitCloseOnce();
    });

    socket.on('error', (error) => {
      this.emitError(this.asError(error));
    });
  }

  private emitCloseOnce(): void {
    if (this.closeEmitted) {
      return;
    }

    this.closeEmitted = true;
    emit(this.closeListeners, undefined);
  }

  private formatRemoteAddress(socket: Socket): string {
    const remoteAddress = socket.remoteAddress ?? '';
    const remotePort = socket.remotePort ?? 0;
    return `${remoteAddress}:${remotePort}`;
  }

  private emitError(error: Error): void {
    emit(this.errorListeners, error);
  }

  private asError(error: unknown): Error {
    return error instanceof Error ? error : new BridgeTransportError(String(error));
  }
}
