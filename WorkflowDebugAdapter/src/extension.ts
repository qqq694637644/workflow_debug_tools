import * as vscode from 'vscode';
import { formatDebugMessage } from './logFormat.js';
import { WorkflowDebugAdapterServerHost } from './debugAdapterServerHost.js';
import { getTraceFilePath, resetTraceLog, traceDebugMessage } from './diagnosticTrace.js';

let outputChannel: any | null = null;
let debugServerHost: WorkflowDebugAdapterServerHost | null = null;

function writeLog(message: string): void {
  const line = `[Workflow 调试器] ${message}`;
  traceDebugMessage('extension', message);
  if (outputChannel) {
    outputChannel.appendLine(line);
    return;
  }

  console.log(line);
}

export async function activate(context: { subscriptions: Array<{ dispose(): void }> }): Promise<void> {
  outputChannel = vscode.window.createOutputChannel('Workflow 调试器');
  outputChannel.show(true);
  resetTraceLog();
  writeLog('扩展已激活。');
  writeLog(`诊断日志文件：${getTraceFilePath()}。`);

  debugServerHost = new WorkflowDebugAdapterServerHost({
    host: '127.0.0.1'
  });
  const address = await debugServerHost.start();
  writeLog(`已启动内嵌调试服务器：127.0.0.1:${address.port}。`);

  const configurationProvider = {
    resolveDebugConfiguration(_folder: unknown, config: any): any {
      if (!config || config.type !== 'workflow') {
        return config;
      }

      // 参考 LuaPanda 的做法，默认打开内置调试控制台，避免 session 启动后用户看不到任何输出。
      if (typeof config.internalConsoleOptions !== 'string' || config.internalConsoleOptions.trim().length === 0) {
        config.internalConsoleOptions = 'openOnSessionStart';
        writeLog('已为 workflow 调试会话默认设置 internalConsoleOptions=openOnSessionStart。');
      }

      // 默认无限等待宿主连接，避免宿主启动稍慢时调试会话被误判为失败。
      if (typeof config.connectTimeoutMs !== 'number' || !Number.isInteger(config.connectTimeoutMs)) {
        config.connectTimeoutMs = 0;
        writeLog('已为 workflow 调试会话默认设置 connectTimeoutMs=0，表示无限等待宿主连接。');
      }

      // 这里不再让 VSCode 拉起外部调试适配器进程，而是像 LuaPanda 一样直接连到内嵌 debugServer。
      config.debugServer = address.port;
      writeLog(`已为 workflow 调试会话绑定内嵌调试服务器端口：${address.port}。`);

      return config;
    }
  };

  const trackerFactory = {
    createDebugAdapterTracker(session: any) {
      writeLog(`创建调试会话追踪器：${session?.type ?? 'workflow'}`);
      return {
        onWillReceiveMessage(message: unknown): void {
          writeLog(`-> ${formatDebugMessage(message)}`);
        },
        onDidSendMessage(message: unknown): void {
          writeLog(`<- ${formatDebugMessage(message)}`);
        },
        onError(error: unknown): void {
          writeLog(`调试适配器错误：${formatDebugMessage(error)}`);
        },
        onExit(code: number | undefined, signal: string | undefined): void {
          writeLog(`调试适配器退出：code=${String(code)} signal=${String(signal)}`);
        }
      };
    }
  };

  context.subscriptions.push(vscode.debug.registerDebugConfigurationProvider('workflow', configurationProvider));
  context.subscriptions.push(vscode.debug.registerDebugAdapterTrackerFactory('workflow', trackerFactory));
  context.subscriptions.push(outputChannel);
}

export async function deactivate(): Promise<void> {
  await debugServerHost?.dispose();
  debugServerHost = null;
  outputChannel?.dispose();
  outputChannel = null;
}
