import { WorkflowDebugDapServer } from './dapServer.js';

function writeStartupLog(message: string): void {
  process.stderr.write(`[WorkflowDebugAdapter] ${message}\n`);
}

process.on('uncaughtException', (error) => {
  writeStartupLog(`未捕获异常：${error instanceof Error ? error.stack ?? error.message : String(error)}`);
});

process.on('unhandledRejection', (reason) => {
  writeStartupLog(`未处理的 Promise 拒绝：${reason instanceof Error ? reason.stack ?? reason.message : String(reason)}`);
});

writeStartupLog('DAP 主进程启动。');

try {
  const server = new WorkflowDebugDapServer();
  await server.run();
  writeStartupLog('DAP 主进程退出。');
}
catch (error) {
  writeStartupLog(`DAP 主进程失败：${error instanceof Error ? error.stack ?? error.message : String(error)}`);
  process.exitCode = 1;
}
