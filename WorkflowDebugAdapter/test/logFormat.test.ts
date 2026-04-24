import assert from 'node:assert/strict';

import { formatDebugMessage } from '../src/logFormat.js';

function verifyLogFormat(): void {
  const message = new Error('boom');
  message.name = 'FormatError';

  const formattedError = formatDebugMessage(message);
  assert.ok(formattedError.includes('FormatError'));
  assert.ok(formattedError.includes('boom'));

  const formattedObject = formatDebugMessage({
    code: 'EPIPE',
    errno: -4047
  });
  assert.equal(formattedObject, JSON.stringify({
    code: 'EPIPE',
    errno: -4047
  }));
}

verifyLogFormat();
console.log('WorkflowDebugAdapter 日志格式化检查通过。');
