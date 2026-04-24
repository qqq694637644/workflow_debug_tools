import net, { type AddressInfo, type Server, type Socket } from 'node:net';
import { WorkflowDebugDapServer } from './dapServer.js';
import { traceDebugMessage } from './diagnosticTrace.js';

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
      traceDebugMessage('debugServer', `收到调试连接：${socket.remoteAddress ?? 'unknown'}:${socket.remotePort ?? 0}。`);
      void this.handleConnection(socket);
    });
    this.server = server;
    traceDebugMessage('debugServer', `开始监听 ${this.host}:0。`);

    await new Promise<void>((resolve, reject) => {
      server.once('error', (error) => {
        this.server = null;
        traceDebugMessage('debugServer', `监听失败：${error instanceof Error ? error.stack ?? error.message : String(error)}`);
        reject(error);
      });

      server.listen(0, this.host, () => {
        const address = server.address();
        if (!address || typeof address === 'string') {
          this.server = null;
          traceDebugMessage('debugServer', '无法获取监听地址。');
          reject(new Error('无法获取 Workflow 调试服务器端口。'));
          return;
        }

        this.address = address;
        traceDebugMessage('debugServer', `监听成功：${address.address}:${address.port}。`);
        resolve();
      });
    });

    if (!this.address) {
      throw new Error('Workflow 调试服务器地址不可用。');
    }

    server.on('error', (error) => {
      traceDebugMessage('debugServer', `运行时错误：${error instanceof Error ? error.stack ?? error.message : String(error)}`);
      console.error(`[WorkflowDebugAdapter] 内嵌调试服务器错误：${error instanceof Error ? error.stack ?? error.message : String(error)}`);
    });

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

    traceDebugMessage('debugServer', '准备关闭内嵌调试服务器。');
    await new Promise<void>((resolve, reject) => {
      server.close((error) => {
        if (error) {
          traceDebugMessage('debugServer', `关闭失败：${error instanceof Error ? error.stack ?? error.message : String(error)}`);
          reject(error);
          return;
        }
        traceDebugMessage('debugServer', '内嵌调试服务器已关闭。');
        resolve();
      });
    });
  }

  // 这里直接把 socket 交给 DAP 服务器处理，原因是 VSCode 的 debugServer 模式本身就是
  // “调试适配器监听端口，VSCode 连接进来”，这样可以避免外部子进程在 initialize 阶段断开。
  private async handleConnection(socket: Socket): Promise<void> {
    socket.on('error', (error) => {
      traceDebugMessage('debugServer', `连接错误：${error instanceof Error ? error.stack ?? error.message : String(error)}`);
      console.error(`[WorkflowDebugAdapter] 调试连接错误：${error instanceof Error ? error.stack ?? error.message : String(error)}`);
    });

    traceDebugMessage('debugServer', '开始处理 DAP 连接。');
    const dapServer = new WorkflowDebugDapServer({
      input: socket,
      output: socket,
      error: process.stderr
    });

    try {
      await dapServer.run();
      traceDebugMessage('debugServer', 'DAP 连接处理结束。');
    }
    catch (error) {
      traceDebugMessage('debugServer', `DAP 连接处理异常：${error instanceof Error ? error.stack ?? error.message : String(error)}`);
      console.error(`[WorkflowDebugAdapter] 内嵌调试服务器失败：${error instanceof Error ? error.stack ?? error.message : String(error)}`);
    }
    finally {
      if (!socket.destroyed) {
        traceDebugMessage('debugServer', '销毁未关闭的 socket。');
        socket.destroy();
      }
    }
  }
}
