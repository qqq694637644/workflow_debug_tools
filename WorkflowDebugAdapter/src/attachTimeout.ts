export function normalizeConnectTimeoutMs(value: unknown): number | undefined {
  if (value === undefined || value === null) {
    return undefined;
  }

  if (typeof value !== 'number' || !Number.isInteger(value) || value < 0) {
    throw new Error('connectTimeoutMs 必须是非负整数。');
  }

  return value;
}

export async function waitForOptionalTimeout<T>(
  promise: Promise<T>,
  timeoutMs: number | undefined,
  message: string
): Promise<T> {
  // 约定：0 或未配置都表示无限等待，避免宿主程序启动较慢时把 attach 误判成失败。
  const effectiveTimeoutMs = timeoutMs ?? 0;
  if (!Number.isInteger(effectiveTimeoutMs) || effectiveTimeoutMs <= 0) {
    return promise;
  }

  return new Promise<T>((resolve, reject) => {
    const timer = setTimeout(() => {
      reject(new Error(message));
    }, effectiveTimeoutMs);

    promise.then((value) => {
      clearTimeout(timer);
      resolve(value);
    }, (error) => {
      clearTimeout(timer);
      reject(error);
    });
  });
}
