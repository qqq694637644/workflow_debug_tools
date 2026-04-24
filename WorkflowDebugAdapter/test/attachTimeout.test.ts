import assert from 'node:assert/strict';
import { setTimeout as delay } from 'node:timers/promises';

import { normalizeConnectTimeoutMs, waitForOptionalTimeout } from '../src/attachTimeout.js';

async function verifyNormalizeConnectTimeoutMs(): Promise<void> {
  assert.equal(normalizeConnectTimeoutMs(undefined), undefined);
  assert.equal(normalizeConnectTimeoutMs(null), undefined);
  assert.equal(normalizeConnectTimeoutMs(0), 0);
  assert.equal(normalizeConnectTimeoutMs(2500), 2500);
  assert.throws(() => normalizeConnectTimeoutMs(-1));
  assert.throws(() => normalizeConnectTimeoutMs(1.5));
  assert.throws(() => normalizeConnectTimeoutMs('1000'));
}

async function verifyWaitForOptionalTimeout(): Promise<void> {
  const result = await waitForOptionalTimeout(delay(20).then(() => 42), 0, '不应超时');
  assert.equal(result, 42);

  const resultWithoutTimeout = await waitForOptionalTimeout(delay(20).then(() => 7), undefined, '不应超时');
  assert.equal(resultWithoutTimeout, 7);
}

async function main(): Promise<void> {
  await verifyNormalizeConnectTimeoutMs();
  await verifyWaitForOptionalTimeout();
  console.log('WorkflowDebugAdapter 连接等待时间检查通过。');
}

try {
  await main();
}
catch (error) {
  console.error('WorkflowDebugAdapter 连接等待时间检查失败。');
  throw error;
}
