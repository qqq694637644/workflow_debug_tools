import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

const traceFilePath = path.join(os.tmpdir(), 'workflow-debug-adapter.log');

function formatTimestamp(date: Date): string {
  return date.toISOString();
}

export function getTraceFilePath(): string {
  return traceFilePath;
}

export function resetTraceLog(): void {
  try {
    fs.writeFileSync(traceFilePath, '', { encoding: 'utf8' });
  }
  catch {
    // 诊断日志不能影响调试会话主流程，所以这里静默吞掉文件写入失败。
  }
}

export function traceDebugMessage(component: string, message: string): void {
  const line = `[${formatTimestamp(new Date())}][${component}] ${message}\r\n`;
  try {
    fs.appendFileSync(traceFilePath, line, { encoding: 'utf8' });
  }
  catch {
    // 诊断日志不能影响调试会话主流程，所以这里静默吞掉文件写入失败。
  }
}
