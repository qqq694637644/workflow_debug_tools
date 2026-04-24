export function formatDebugMessage(message: unknown): string {
  if (message instanceof Error) {
    return message.stack ?? `${message.name}: ${message.message}`;
  }

  if (typeof message === 'string') {
    return message;
  }

  try {
    return JSON.stringify(message);
  }
  catch {
    return String(message);
  }
}

const expectedAdapterClosePatterns = [
  'connection closed',
  'write EPIPE',
  'read error',
  '调试会话已关闭。'
];

// 调试会话正常结束时，VSCode 和 Node 可能会把 socket 关闭翻译成错误回调。
// 这里仅在会话已经进入关闭流程时屏蔽这些已知噪声，避免掩盖真正的异常。
export function shouldSuppressAdapterError(message: unknown, sessionClosing: boolean): boolean {
  if (!sessionClosing) {
    return false;
  }

  const formatted = formatDebugMessage(message);
  return expectedAdapterClosePatterns.some((pattern) => formatted.includes(pattern));
}
