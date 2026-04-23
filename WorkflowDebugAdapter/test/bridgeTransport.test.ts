import assert from 'node:assert/strict';
import net, { type Socket } from 'node:net';
import { setTimeout as delay } from 'node:timers/promises';

import {
  BridgeTransport,
  type EventEnvelope,
  LineFramedJsonCodec,
  DEFAULT_CAPABILITIES,
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

async function createRawLineServer(
  port: number,
  onConnection: (socket: Socket, messages: Array<ProtocolEnvelope>) => void
): Promise<{
  readonly port: number;
  readonly messages: Array<ProtocolEnvelope>;
  close(): Promise<void>;
}> {
  const messages: Array<ProtocolEnvelope> = [];
  const server = net.createServer((socket) => {
    onConnection(socket, messages);
  });
  let closed = false;

  await new Promise<void>((resolve, reject) => {
    server.once('error', reject);
    server.listen(port, '127.0.0.1', resolve);
  });

  const address = server.address();
  assert.ok(address && typeof address !== 'string');

  return {
    port: address.port,
    messages,
    async close(): Promise<void> {
      if (closed) {
        return;
      }

      closed = true;
      await new Promise<void>((resolve, reject) => {
        server.close((error) => {
          if (error) {
            reject(error);
            return;
          }

          resolve();
        });
      });
    }
  };
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
    mode: 'server',
    endpoint: {
      host: '127.0.0.1',
      port: 0
    }
  });
  const serverMessages: Array<ProtocolEnvelope> = [];
  const clientMessages: Array<ProtocolEnvelope> = [];
  let connectedClient: BridgeTransport<ProtocolEnvelope> | null = null;

  server.onMessage((message) => {
    serverMessages.push(message);
  });

  try {
    await server.listen();
    const serverPort = server.snapshot().localAddress?.port;
    assert.ok(serverPort);

    connectedClient = new BridgeTransport<ProtocolEnvelope>({
      name: 'bridge-client-connected',
      mode: 'client',
      endpoint: {
        host: '127.0.0.1',
        port: serverPort
      },
      reconnect: {
        enabled: false
      }
    });

    connectedClient.onMessage((message) => {
      clientMessages.push(message);
    });
    const activeClient = connectedClient!;

    await activeClient.connect();
    await waitForCondition(() => server.getState() === 'connected', '服务端没有进入 connected 状态。');

    assert.equal(server.getState(), 'connected');
    assert.equal(activeClient.getState(), 'connected');

    await activeClient.send(
      createEventEnvelope(
        sessionId,
        'output',
        {
          level: 'log',
          message: 'client-to-server'
        },
        1
      )
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
    await waitForCondition(() => activeClient.getState() === 'closed', '客户端没有感知到服务端关闭。');
    assert.equal(activeClient.getState(), 'closed');
  } finally {
    if (connectedClient && connectedClient.getState() !== 'closed') {
      await connectedClient.close().catch(() => undefined);
    }

    if (server.getState() !== 'closed') {
      await server.close().catch(() => undefined);
    }
  }
}

async function verifyReconnect(): Promise<void> {
  const sessionId = 'wf-20260423-reconnect';
  const firstHello = createEventEnvelope(
    sessionId,
    'hello',
    {
      runtimeVersion: '0.1.0',
      protocolVersion: 1,
      capabilities: DEFAULT_CAPABILITIES,
      sourceMap: []
    },
    1
  );

  const firstServer = await createRawLineServer(0, (socket) => {
    socket.write(`${JSON.stringify(firstHello)}\n`, 'utf8');
    socket.end();
  });

  const clientMessages: Array<ProtocolEnvelope> = [];
  const client = new BridgeTransport<ProtocolEnvelope>({
    name: 'reconnect-client',
    mode: 'client',
    endpoint: {
      host: '127.0.0.1',
      port: firstServer.port
    },
    reconnect: {
      enabled: false
    }
  });

  client.onMessage((message) => {
    clientMessages.push(message);
  });

  try {
    await client.connect();
    await waitForCondition(() => clientMessages.length >= 1, '客户端没有收到首次 hello。');
    assert.equal(clientMessages[0].cmd, 'hello');
    await waitForCondition(() => client.getState() === 'closed', '客户端没有感知到首次连接关闭。');
    assert.equal(client.getState(), 'closed');

    await firstServer.close();

    const secondServer = await createRawLineServer(firstServer.port, (socket, messages) => {
      const codec = new LineFramedJsonCodec<ProtocolEnvelope>();
      socket.on('data', (chunk) => {
        messages.push(...codec.push(chunk));
      });
    });

    try {
      await client.reconnect();
      assert.equal(client.getState(), 'connected');

      await client.send(
        createEventEnvelope(
          sessionId,
          'output',
          {
            level: 'info',
            message: 'reconnected'
          },
          2
        )
      );

      await waitForCondition(() => secondServer.messages.length >= 1, '重连后的服务端没有收到消息。');
      const secondServerMessage = secondServer.messages[0];
      assert.ok(isOutputEnvelope(secondServerMessage));
      assert.equal(secondServerMessage.body.message, 'reconnected');

      await client.close();
    } finally {
      if (client.getState() !== 'closed') {
        await client.close().catch(() => undefined);
      }

      await secondServer.close().catch(() => undefined);
    }
  } finally {
    if (client.getState() !== 'closed') {
      await client.close().catch(() => undefined);
    }

    await firstServer.close().catch(() => undefined);
  }
}

async function main(): Promise<void> {
  verifyCodecHandlesSplitAndStickyFrames();
  await verifyBridgeRoundTripAndClose();
  await verifyReconnect();
  console.log('WorkflowDebugAdapter 传输层检查通过。');
}

try {
  await main();
} catch (error) {
  console.error('WorkflowDebugAdapter 传输层检查失败。');
  throw error;
}
