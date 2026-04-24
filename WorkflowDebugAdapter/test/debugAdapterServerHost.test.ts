import assert from 'node:assert/strict';
import { once } from 'node:events';
import net, { type Socket } from 'node:net';

import { WorkflowDebugAdapterServerHost } from '../src/debugAdapterServerHost.js';
import { DapMessageReader, createDapRequest, type DapMessage, type DapResponseMessage, writeDapMessage } from '../src/dapProtocol.js';

class DapSocketClient {
  private readonly socket: Socket;
  private readonly reader = new DapMessageReader();
  private readonly receivedMessages: Array<DapMessage> = [];
  private readonly pendingResponses = new Map<number, { resolve: (message: DapResponseMessage) => void; reject: (error: Error) => void }>();
  private nextSeq = 1;

  public constructor(port: number) {
    this.socket = net.createConnection(port, '127.0.0.1');
    this.socket.on('data', (chunk) => {
      for (const message of this.reader.push(chunk)) {
        this.receivedMessages.push(message);
        if (message.type === 'response') {
          const pending = this.pendingResponses.get(message.request_seq);
          if (pending) {
            this.pendingResponses.delete(message.request_seq);
            pending.resolve(message);
          }
        }
      }
    });
    this.socket.on('error', (error) => {
      for (const pending of this.pendingResponses.values()) {
        pending.reject(error);
      }
      this.pendingResponses.clear();
    });
  }

  public async waitForConnect(): Promise<void> {
    if (this.socket.readyState === 'open') {
      return;
    }

    await once(this.socket, 'connect');
  }

  public async request(command: string, args?: unknown): Promise<DapResponseMessage> {
    await this.waitForConnect();
    const seq = this.nextSeq;
    this.nextSeq += 1;

    const responsePromise = new Promise<DapResponseMessage>((resolve, reject) => {
      this.pendingResponses.set(seq, { resolve, reject });
      writeDapMessage(this.socket, createDapRequest(seq, command, args));
    });

    return responsePromise;
  }

  public getReceivedMessages(): ReadonlyArray<DapMessage> {
    return this.receivedMessages;
  }

  public async close(): Promise<void> {
    if (this.socket.destroyed) {
      return;
    }

    this.socket.end();
    await once(this.socket, 'close');
  }
}

async function verifyDebugServerHost(): Promise<void> {
  const host = new WorkflowDebugAdapterServerHost();
  const address = await host.start();
  assert.ok(address.port > 0);

  const client = new DapSocketClient(address.port);
  try {
    const initializeResponse = await client.request('initialize', {
      adapterID: 'workflow'
    });
    assert.equal(initializeResponse.success, true);

    const initializeIndex = client.getReceivedMessages().findIndex((message) => message.type === 'response' && message.request_seq === 1 && message.command === 'initialize');
    assert.ok(initializeIndex >= 0);
  }
  finally {
    await client.close();
    await host.dispose();
  }
}

async function main(): Promise<void> {
  await verifyDebugServerHost();
  console.log('WorkflowDebugAdapter 内嵌调试服务器检查通过。');
}

try {
  await main();
}
catch (error) {
  console.error('WorkflowDebugAdapter 内嵌调试服务器检查失败。');
  throw error;
}
