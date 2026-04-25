import assert from 'node:assert/strict';

import { shouldFailPendingRequestsOnShutdown } from '../src/dapServer.js';

function verifyShutdownPolicy(): void {
  assert.equal(shouldFailPendingRequestsOnShutdown('attach timeout'), true);
  assert.equal(shouldFailPendingRequestsOnShutdown('stdin closed'), false);
  assert.equal(shouldFailPendingRequestsOnShutdown('disconnect'), false);
}

verifyShutdownPolicy();
console.log('WorkflowDebugAdapter 关闭策略检查通过。');
