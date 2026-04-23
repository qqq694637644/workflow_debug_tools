import net, { type AddressInfo, type Server, type Socket } from 'node:net';
import { StringDecoder } from 'node:string_decoder';
import { once } from 'node:events';

export type BridgeTransportMode = 'client' | 'server';

export type BridgeTransportState =
  | 'idle'
  | 'listening'
  | 'connecting'
  | 'connected'
  | 'reconnecting'
  | 'closing'
  | 'closed';

export interface BridgeTransportReconnectOptions {
  readonly enabled: boolean;
  readonly delayMs: number;
  readonly maxAttempts: number;
}

export interface BridgeTransportEndpoint {
  readonly host: string;
  readonly port: number;
}

export interface BridgeTransportOptions<TMessage> {
  readonly name: string;
  readonly mode: BridgeTransportMode;
  readonly endpoint: BridgeTransportEndpoint;
  readonly reconnect?: Partial<BridgeTransportReconnectOptions>;
  readonly serialize?: (message: TMessage) => string;
  readonly deserialize?: (line: string) => TMessage;
}

export interface BridgeTransportSnapshot {
  readonly name: string;
  readonly mode: BridgeTransportMode;
  readonly state: BridgeTransportState;
  readonly endpoint: BridgeTransportEndpoint;
  readonly localAddress: AddressInfo | null;
  readonly remoteAddress: string | null;
  readonly reconnectAttempts: number;
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

function normalizeReconnectOptions(options?: Partial<BridgeTransportReconnectOptions>): BridgeTransportReconnectOptions {
  return {
    enabled: options?.enabled ?? false,
    delayMs: options?.delayMs ?? 250,
    maxAttempts: options?.maxAttempts ?? Number.POSITIVE_INFINITY
  };
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
  private readonly mode: BridgeTransportMode;
  private readonly endpoint: BridgeTransportEndpoint;
  private readonly reconnectOptions: BridgeTransportReconnectOptions;
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
  private reconnectAttempts = 0;
  private reconnectTimer: NodeJS.Timeout | null = null;
  private manualClose = false;
  private connectPromise: Promise<void> | null = null;
  private listenPromise: Promise<void> | null = null;

  constructor(options: BridgeTransportOptions<TMessage>) {
    this.name = options.name;
    this.mode = options.mode;
    this.endpoint = options.endpoint;
    this.reconnectOptions = normalizeReconnectOptions(options.reconnect);
    this.codec = new LineFramedJsonCodec<TMessage>(options.serialize, options.deserialize);
  }

  public getState(): BridgeTransportState {
    return this.state;
  }

  public snapshot(): BridgeTransportSnapshot {
    return {
      name: this.name,
      mode: this.mode,
      state: this.state,
      endpoint: this.endpoint,
      localAddress: this.localAddress,
      remoteAddress: this.remoteAddress,
      reconnectAttempts: this.reconnectAttempts
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

  public async connect(): Promise<void> {
    if (this.mode !== 'client') {
      throw new BridgeTransportError('只有客户端模式才能调用 connect()。');
    }

    if (this.connectPromise) {
      return this.connectPromise;
    }

    this.manualClose = false;
    this.state = 'connecting';
    this.connectPromise = this.openClientSocket();
    try {
      await this.connectPromise;
    } finally {
      this.connectPromise = null;
    }
  }

  public async listen(): Promise<void> {
    if (this.mode !== 'server') {
      throw new BridgeTransportError('只有服务端模式才能调用 listen()。');
    }

    if (this.listenPromise) {
      return this.listenPromise;
    }

    this.manualClose = false;
    this.state = 'listening';
    this.listenPromise = new Promise<void>((resolve, reject) => {
      const server = net.createServer((socket) => {
        this.attachSocket(socket, true);
      });
      this.server = server;
      server.once('error', (error) => {
        if (this.state !== 'closed') {
          this.emitError(this.asError(error));
        }
        reject(this.asError(error));
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

  public async reconnect(): Promise<void> {
    if (this.mode !== 'client') {
      throw new BridgeTransportError('只有客户端模式支持 reconnect()。');
    }

    this.clearReconnectTimer();
    const socket = this.socket;
    this.manualClose = true;

    if (socket && !socket.destroyed) {
      socket.end();
      socket.destroy();
      await once(socket, 'close');
    }

    this.manualClose = false;
    this.state = 'reconnecting';
    this.reconnectAttempts = 0;
    this.connectPromise = this.openClientSocket();
    try {
      await this.connectPromise;
    } finally {
      this.connectPromise = null;
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
    this.manualClose = true;
    this.clearReconnectTimer();
    this.state = 'closing';

    const socket = this.socket;
    const server = this.server;
    const hadSocket = socket !== null;

    this.socket = null;
    this.server = null;
    this.remoteAddress = null;

    if (socket && !socket.destroyed) {
      socket.end();
      socket.destroy();
    }

    if (server) {
      await new Promise<void>((resolve, reject) => {
        server.close((error) => {
          if (error) {
            reject(this.asError(error));
            return;
          }
          resolve();
        });
      });
    }

    this.codec.reset();
    this.state = 'closed';

    // 没有活动套接字时，close() 需要主动通知上层；有套接字时则交给 close 事件回调统一触发。
    if (!hadSocket) {
      emit(this.closeListeners, undefined);
    }
  }

  private async openClientSocket(): Promise<void> {
    return new Promise<void>((resolve, reject) => {
      const socket = net.createConnection(this.endpoint.port, this.endpoint.host);
      let settled = false;

      this.attachSocket(socket);

      const fail = (error: Error): void => {
        if (settled) {
          this.emitError(error);
          return;
        }

        settled = true;
        this.socket = null;
        this.remoteAddress = null;
        this.codec.reset();
        socket.destroy();
        this.state = 'closed';
        reject(error);
      };

      socket.once('connect', () => {
        settled = true;
        this.state = 'connected';
        this.remoteAddress = this.formatRemoteAddress(socket);
        this.reconnectAttempts = 0;
        emit(this.openListeners, undefined);
        resolve();
      });

      socket.once('error', (error) => {
        fail(this.asError(error));
      });
    });
  }

  private attachSocket(socket: Socket, emitOpen = false): void {
    this.socket = socket;
    this.remoteAddress = this.formatRemoteAddress(socket);
    socket.setEncoding('utf8');
    socket.on('data', (chunk: string | Buffer) => {
      try {
        const messages = this.codec.push(typeof chunk === 'string' ? chunk : Buffer.from(chunk));
        for (const message of messages) {
          emit(this.messageListeners, message);
        }
      } catch (error) {
        this.emitError(this.asError(error));
      }
    });
    socket.once('close', () => {
      this.handleSocketClose();
    });
    socket.once('error', (error) => {
      this.emitError(this.asError(error));
    });
    if (emitOpen) {
      this.state = 'connected';
      this.reconnectAttempts = 0;
      emit(this.openListeners, undefined);
    }
  }

  private handleSocketClose(): void {
    this.codec.reset();
    this.socket = null;
    this.remoteAddress = null;
    emit(this.closeListeners, undefined);

    if (this.manualClose) {
      this.state = 'closed';
      return;
    }

    if (this.mode === 'server' && this.server) {
      this.state = 'listening';
      return;
    }

    this.state = this.reconnectOptions.enabled ? 'reconnecting' : 'closed';
    if (!this.reconnectOptions.enabled) {
      return;
    }

    this.reconnectAttempts += 1;
    if (this.reconnectAttempts > this.reconnectOptions.maxAttempts) {
      this.state = 'closed';
      this.emitError(new BridgeTransportError(`重连次数超过上限：${this.reconnectOptions.maxAttempts}。`));
      return;
    }

    this.clearReconnectTimer();
    this.reconnectTimer = setTimeout(() => {
      void this.openClientSocket().catch((error) => {
        this.emitError(error);
        this.handleReconnectFailure();
      });
    }, this.reconnectOptions.delayMs);
  }

  private handleReconnectFailure(): void {
    if (this.manualClose || this.state === 'closed') {
      return;
    }

    if (!this.reconnectOptions.enabled) {
      this.state = 'closed';
      return;
    }

    if (this.reconnectAttempts >= this.reconnectOptions.maxAttempts) {
      this.state = 'closed';
      return;
    }

    this.reconnectAttempts += 1;
    this.clearReconnectTimer();
    this.reconnectTimer = setTimeout(() => {
      void this.openClientSocket().catch((error) => {
        this.emitError(error);
        this.handleReconnectFailure();
      });
    }, this.reconnectOptions.delayMs);
  }

  private clearReconnectTimer(): void {
    if (this.reconnectTimer) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
    }
  }

  private emitError(error: Error): void {
    emit(this.errorListeners, error);
  }

  private formatRemoteAddress(socket: Socket): string | null {
    const address = socket.remoteAddress;
    const port = socket.remotePort;
    if (!address || !port) {
      return null;
    }

    return `${address}:${port}`;
  }

  private asError(error: unknown): Error {
    return error instanceof Error ? error : new BridgeTransportError(String(error));
  }
}
