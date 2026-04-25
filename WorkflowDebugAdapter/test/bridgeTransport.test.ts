import assert from 'node:assert/strict';
import net from 'node:net';
import { once } from 'node:events';
import { setTimeout as delay } from 'node:timers/promises';

import {
  BridgeTransport,
  type EventEnvelope,
  LineFramedJsonCodec,
  createEventEnvelope,
  type ProtocolEnvelope
} from '../src/index.js';

async function waitForCondition(
  predicate: () => boolean,
  description: string,
  timeoutMs = 3000
): Promise<void> {
  const start = Date.now();
  while (!predicate()) {
    if (Date.now() - start > timeoutMs) {
      throw new Error(description);
    }

    await delay(10);
  }
}

function isOutputEnvelope(
  message: ProtocolEnvelope
): message is EventEnvelope<'output'> {
  return message.cmd === 'output';
}

function verifyCodecHandlesSplitAndStickyFrames(): void {
  const codec = new LineFramedJsonCodec<{ readonly kind: string; readonly text: string }>();
  const firstMessage = { kind: 'note', text: '你好，世界' };
  const firstEncoded = Buffer.from(codec.encode(firstMessage), 'utf8');
  const splitIndex = Math.floor(firstEncoded.length / 2);

  assert.deepEqual(codec.push(firstEncoded.subarray(0, splitIndex)), []);
  const splitMessages = codec.push(firstEncoded.subarray(splitIndex));
  assert.equal(splitMessages.length, 1);
  assert.deepEqual(splitMessages[0], firstMessage);

  const secondMessage = { kind: 'first', text: 'A' };
  const thirdMessage = { kind: 'second', text: 'B' };
  const stickyEncoded = Buffer.from(codec.encode(secondMessage) + codec.encode(thirdMessage), 'utf8');
  const stickyMessages = codec.push(stickyEncoded);

  assert.equal(stickyMessages.length, 2);
  assert.deepEqual(stickyMessages[0], secondMessage);
  assert.deepEqual(stickyMessages[1], thirdMessage);
}

async function verifyBridgeRoundTripAndClose(): Promise<void> {
  const sessionId = 'wf-20260423-transport';

  const server = new BridgeTransport<ProtocolEnvelope>({
    name: 'bridge-server',
    endpoint: {
      host: '127.0.0.1',
      port: 0
    }
  });

  const serverMessages: Array<ProtocolEnvelope> = [];
  const clientMessages: Array<ProtocolEnvelope> = [];

  server.onMessage((message) => {
    serverMessages.push(message);
  });

  await server.listen();
  const serverPort = server.snapshot().localAddress?.port;
  assert.ok(serverPort);

  const clientSocket = net.createConnection(serverPort, '127.0.0.1');
  await once(clientSocket, 'connect');

  const clientCodec = new LineFramedJsonCodec<ProtocolEnvelope>();
  clientSocket.on('data', (chunk) => {
    clientMessages.push(...clientCodec.push(chunk));
  });

  let clientClosed = false;
  clientSocket.once('close', () => {
    clientClosed = true;
  });

  try {
    await waitForCondition(() => server.getState() === 'connected', '服务端没有进入 connected 状态。');

    clientSocket.write(
      clientCodec.encode(
        createEventEnvelope(
          sessionId,
          'output',
          {
            level: 'log',
            message: 'client-to-server'
          },
          1
        )
      ),
      'utf8'
    );

    await waitForCondition(() => serverMessages.length >= 1, '服务端没有收到客户端消息。');
    const firstServerMessage = serverMessages[0];
    assert.ok(isOutputEnvelope(firstServerMessage));
    assert.equal(firstServerMessage.body.message, 'client-to-server');

    await server.send(
      createEventEnvelope(
        sessionId,
        'stopped',
        {
          reason: 'breakpoint',
          threadId: 1,
          frameId: 2,
          sourceId: 7,
          row: 19
        },
        2
      )
    );
    await server.send(
      createEventEnvelope(
        sessionId,
        'disconnect',
        {
          reason: 'manual-close',
          restart: false
        },
        3
      )
    );

    await waitForCondition(() => clientMessages.length >= 2, '客户端没有收到服务端消息。');
    assert.deepEqual(
      clientMessages.map((message) => message.cmd),
      ['stopped', 'disconnect']
    );

    await server.close();
    await waitForCondition(() => clientClosed, '客户端没有感知到服务端关闭。');
  } finally {
    if (!clientSocket.destroyed) {
      clientSocket.destroy();
    }
    await server.close().catch(() => undefined);
  }
}

async function main(): Promise<void> {
  verifyCodecHandlesSplitAndStickyFrames();
  await verifyBridgeRoundTripAndClose();
  console.log('WorkflowDebugAdapter 传输层检查通过。');
}

try {
  await main();
} catch (error) {
  console.error('WorkflowDebugAdapter 传输层检查失败。');
  throw error;
}
