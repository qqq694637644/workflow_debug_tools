import net, { type AddressInfo, type Server, type Socket } from 'node:net';
import { WorkflowDebugDapServer } from './dapServer.js';

export interface WorkflowDebugAdapterServerHostOptions {
  readonly host?: string;
}

export class WorkflowDebugAdapterServerHost {
  private readonly host: string;
  private server: Server | null = null;
  private address: AddressInfo | null = null;

  public constructor(options: WorkflowDebugAdapterServerHostOptions = {}) {
    this.host = options.host ?? '127.0.0.1';
  }

  public async start(): Promise<AddressInfo> {
    if (this.address) {
      return this.address;
    }

    if (this.server) {
      throw new Error('Workflow 调试服务器正在启动中。');
    }

    const server = net.createServer((socket) => {
      socket.setNoDelay(true);
      void this.handleConnection(socket);
    });
    this.server = server;
    server.on('error', (error) => {
      if (!this.address) {
        this.server = null;
        return;
      }

      console.error(`[WorkflowDebugAdapter] 内嵌调试服务器错误：${error instanceof Error ? error.stack ?? error.message : String(error)}`);
    });

    await new Promise<void>((resolve, reject) => {
      server.listen(0, this.host, () => {
        const address = server.address();
        if (!address || typeof address === 'string') {
          this.server = null;
          reject(new Error('无法获取 Workflow 调试服务器端口。'));
          return;
        }

        this.address = address;
        resolve();
      });
    });

    if (!this.address) {
      throw new Error('Workflow 调试服务器地址不可用。');
    }

    return this.address;
  }

  public getPort(): number {
    if (!this.address) {
      throw new Error('Workflow 调试服务器尚未启动。');
    }

    return this.address.port;
  }

  public async dispose(): Promise<void> {
    const server = this.server;
    this.server = null;
    this.address = null;

    if (!server) {
      return;
    }

    await new Promise<void>((resolve, reject) => {
      server.close((error) => {
        if (error) {
          reject(error);
          return;
        }
        resolve();
      });
    });
  }

  // 这里直接把 socket 交给 DAP 服务器处理，原因是 VSCode 的 debugServer 模式本身就是
  // “调试适配器监听端口，VSCode 连接进来”，这样可以避免外部子进程在 initialize 阶段断开。
  private async handleConnection(socket: Socket): Promise<void> {
    socket.on('error', (error) => {
      console.error(`[WorkflowDebugAdapter] 调试连接错误：${error instanceof Error ? error.stack ?? error.message : String(error)}`);
    });

    const dapServer = new WorkflowDebugDapServer({
      input: socket,
      output: socket,
      error: process.stderr
    });

    try {
      await dapServer.run();
    }
    catch (error) {
      console.error(`[WorkflowDebugAdapter] 内嵌调试服务器失败：${error instanceof Error ? error.stack ?? error.message : String(error)}`);
    }
    finally {
      if (!socket.destroyed) {
        socket.destroy();
      }
    }
  }
}
