import assert from 'node:assert/strict';
import { spawn, type ChildProcessWithoutNullStreams } from 'node:child_process';
import net from 'node:net';
import { setTimeout as delay } from 'node:timers/promises';
import { fileURLToPath } from 'node:url';

import {
  BridgeTransport,
  DEFAULT_CAPABILITIES,
  TargetSessionMachine,
  type RequestEnvelope,
  type ProtocolEnvelope,
  type TargetRuntimeControl
} from '../src/index.js';
import {
  DapMessageReader,
  createDapRequest,
  type DapEventMessage,
  type DapMessage,
  type DapResponseMessage,
  writeDapMessage
} from '../src/dapProtocol.js';
import { createEventEnvelope } from '../src/protocol.js';

const remotePath = 'D:/repos/Workflow-master/Test/Resources/Debugger/RaiseException.txt';
const localPath = 'C:/workspace/Workflow-master/Test/Resources/Debugger/RaiseException.txt';
const normalizedLocalPath = localPath.replace(/^([A-Z]):/, (_, drive: string) => `${drive.toLowerCase()}:`);

const sourceMap = [
  {
    codeIndex: 12,
    sourcePath: remotePath,
    row: 9
  },
  {
    codeIndex: 12,
    sourcePath: remotePath,
    row: 10
  },
  {
    codeIndex: 12,
    sourcePath: remotePath,
    row: 11
  }
] as const;

class RecordingRuntimeControl implements TargetRuntimeControl {
  public readonly calls: Array<{ readonly kind: 'run' | 'stepOver' | 'stepInto'; readonly threadId: number }> = [];

  public run(threadId: number): boolean {
    this.calls.push({
      kind: 'run',
      threadId
    });
    return true;
  }

  public stepOver(threadId: number): boolean {
    this.calls.push({
      kind: 'stepOver',
      threadId
    });
    return true;
  }

  public stepInto(threadId: number): boolean {
    this.calls.push({
      kind: 'stepInto',
      threadId
    });
    return true;
  }
}

function createStackFrames(rowOffset: number) {
  return [
    {
      callStackIndex: 0,
      functionName: 'Main',
      sourceId: 12,
      sourcePath: remotePath,
      row: 9,
      variables: {
        local: [
          {
            name: 'counter',
            type: 'number',
            value: '1'
          }
        ],
        argument: [],
        captured: [],
        global: [
          {
            name: 'appName',
            type: 'string',
            value: 'Workflow'
          }
        ]
      }
    },
    {
      callStackIndex: 1,
      functionName: 'Update',
      sourceId: 12,
      sourcePath: remotePath,
      row: 10,
      variables: {
        local: [
          {
            name: 'flag',
            type: 'boolean',
            value: 'true'
          }
        ],
        argument: [
          {
            name: 'value',
            type: 'string',
            value: 'abc'
          }
        ],
        captured: [],
        global: [
          {
            name: 'appName',
            type: 'string',
            value: 'Workflow'
          }
        ]
      }
    },
    {
      callStackIndex: 2,
      functionName: 'RaiseException',
      sourceId: 12,
      sourcePath: remotePath,
      row: 11 + rowOffset,
      variables: {
        local: [
          {
            name: 'context',
            type: 'Context',
            value: '{...}',
            children: [
              {
                name: 'message',
                type: 'string',
                value: 'Exception'
              }
            ]
          }
        ],
        argument: [
          {
            name: 'input',
            type: 'string',
            value: 'abc'
          }
        ],
        captured: [
          {
            name: 'suffix',
            type: 'string',
            value: '!'
          }
        ],
        global: [
          {
            name: 'appName',
            type: 'string',
            value: 'Workflow'
          }
        ]
      }
    }
  ] as const;
}

async function waitForCondition(
  predicate: () => boolean,
  description: string,
  timeoutMs = 5000
): Promise<void> {
  const start = Date.now();
  while (!predicate()) {
    if (Date.now() - start > timeoutMs) {
      throw new Error(description);
    }
    await delay(20);
  }
}

async function reservePort(): Promise<number> {
  const server = net.createServer();
  await new Promise<void>((resolve, reject) => {
    server.once('error', reject);
    server.listen(0, '127.0.0.1', resolve);
  });

  const address = server.address();
  assert.ok(address && typeof address !== 'string');
  const port = address.port;

  await new Promise<void>((resolve, reject) => {
    server.close((error) => {
      if (error) {
        reject(error);
        return;
      }
      resolve();
    });
  });

  return port;
}

async function assertPortBindable(port: number): Promise<void> {
  const server = net.createServer();
  try {
    await new Promise<void>((resolve, reject) => {
      server.once('error', reject);
      server.listen(port, '127.0.0.1', resolve);
    });
  }
  finally {
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
}

async function waitForPortBindable(port: number, timeoutMs = 5000): Promise<void> {
  const start = Date.now();
  let lastError: unknown = null;
  while (Date.now() - start <= timeoutMs) {
    try {
      await assertPortBindable(port);
      return;
    }
    catch (error) {
      lastError = error;
      await delay(20);
    }
  }

  throw lastError instanceof Error
    ? lastError
    : new Error(`等待端口 ${port} 释放超时。`);
}

class DapClient {
  private readonly child: ChildProcessWithoutNullStreams;
  private readonly reader = new DapMessageReader();
  private readonly receivedMessages: Array<DapMessage> = [];
  private readonly pendingResponses = new Map<number, { resolve: (message: DapResponseMessage) => void; reject: (error: Error) => void }>();
  private readonly pendingEvents = new Array<DapEventMessage>();
  private readonly waiters = new Array<{
    eventName: string;
    predicate: (message: DapEventMessage) => boolean;
    resolve: (message: DapEventMessage) => void;
    reject: (error: Error) => void;
  }>();
  private readonly stderrChunks: Array<string> = [];
  private nextSeq = 1;

  constructor(scriptPath: string) {
    this.child = spawn(process.execPath, [scriptPath], {
      cwd: process.cwd(),
      stdio: ['pipe', 'pipe', 'pipe']
    });

    this.child.stdout.on('data', (chunk) => {
      for (const message of this.reader.push(chunk)) {
        this.handleMessage(message);
      }
    });
    this.child.stderr.on('data', (chunk) => {
      this.stderrChunks.push(chunk.toString('utf8'));
    });
  }

  public async request(command: string, args?: unknown): Promise<DapResponseMessage> {
    const seq = this.nextSeq;
    this.nextSeq += 1;

    const responsePromise = new Promise<DapResponseMessage>((resolve, reject) => {
      this.pendingResponses.set(seq, { resolve, reject });
      writeDapMessage(this.child.stdin, createDapRequest(seq, command, args));
    });

    const response = await Promise.race([
      responsePromise,
      delay(10000).then(() => {
        this.pendingResponses.delete(seq);
        throw new Error(`等待 DAP 请求 ${command} 响应超时。`);
      })
    ]);
    return response;
  }

  public waitForEvent(
    eventName: string,
    predicate: (message: DapEventMessage) => boolean = () => true,
    timeoutMs = 5000
  ): Promise<DapEventMessage> {
    const existing = this.pendingEvents.find((event) => event.event === eventName && predicate(event));
    if (existing) {
      return Promise.resolve(existing);
    }

    return new Promise<DapEventMessage>((resolve, reject) => {
      const timer = setTimeout(() => {
        const index = this.waiters.indexOf(waiter);
        if (index >= 0) {
          this.waiters.splice(index, 1);
        }
        reject(new Error(`等待事件 ${eventName} 超时。`));
      }, timeoutMs);
      const waiter = {
        eventName,
        predicate,
        resolve(message: DapEventMessage): void {
          clearTimeout(timer);
          resolve(message);
        },
        reject(error: Error): void {
          clearTimeout(timer);
          reject(error);
        }
      };
      this.waiters.push(waiter);
    });
  }

  public async close(): Promise<void> {
    if (this.child.exitCode !== null || this.child.signalCode !== null) {
      return;
    }

    const exitPromise = new Promise<void>((resolve) => {
      this.child.once('exit', () => resolve());
    });

    if (this.child.exitCode === null && this.child.signalCode === null) {
      this.child.kill();
    }

    await exitPromise;
  }

  public getStderr(): string {
    return this.stderrChunks.join('');
  }

  public getReceivedMessages(): ReadonlyArray<DapMessage> {
    return this.receivedMessages;
  }

  private handleMessage(message: DapMessage): void {
    this.receivedMessages.push(message);
    if (message.type === 'response') {
      const pending = this.pendingResponses.get(message.request_seq);
      if (pending) {
        this.pendingResponses.delete(message.request_seq);
        pending.resolve(message);
      }
      return;
    }

    if (message.type === 'event') {
      this.pendingEvents.push(message);
      for (let i = 0; i < this.waiters.length; i++) {
        const waiter = this.waiters[i];
        if (waiter.eventName === message.event && waiter.predicate(message)) {
          this.waiters.splice(i, 1);
          waiter.resolve(message);
          return;
        }
      }
    }
  }
}

async function startFakeHost(port: number): Promise<{
  readonly transport: BridgeTransport<ProtocolEnvelope>;
  readonly runtimeControl: RecordingRuntimeControl;
  readonly disconnectRequests: Array<{ readonly reason: string; readonly restart: boolean }>;
  notifyDisconnect(reason?: string, restart?: boolean): Promise<void>;
  close(): Promise<void>;
}> {
  const runtimeControl = new RecordingRuntimeControl();
  const disconnectRequests: Array<{ readonly reason: string; readonly restart: boolean }> = [];
  const target = new TargetSessionMachine({
    runtimeVersion: '0.1.0',
    capabilities: DEFAULT_CAPABILITIES,
    sourceMap,
    runtimeControl
  });
  const sessionId = 'wf-vscode-plugin';
  target.attach(sessionId);

  const transport = new BridgeTransport<ProtocolEnvelope>({
    name: 'fake-workflow-host',
    mode: 'client',
    endpoint: {
      host: '127.0.0.1',
      port
    },
    reconnect: {
      enabled: false
    }
  });

  transport.onMessage((message) => {
    void (async () => {
      if (message.type === 'request' && message.cmd === 'initialize') {
        const initializeRequest = message as RequestEnvelope<'initialize'>;
        assert.equal(initializeRequest.body.stopOnEntry, true);
        const ready = target.receiveInitialize(initializeRequest);
        await transport.send(ready);
        return;
      }

      if (message.type === 'request' && message.cmd === 'setBreakpoints') {
        const setBreakpointsRequest = message as RequestEnvelope<'setBreakpoints'>;
        const validations = target.receiveSetBreakpoints(setBreakpointsRequest);
        for (const validation of validations) {
          await transport.send(validation);
        }
        return;
      }

      if (message.type === 'request' && message.cmd === 'continue') {
        const continueRequest = message as RequestEnvelope<'continue'>;
        target.onContinue(continueRequest);
        const stopped = target.handleExecutionPoint({
          threadId: 1,
          frameId: 2,
          sourceId: 12,
          row: 9,
          functionName: 'RaiseException',
          sourcePath: remotePath,
          stackFrames: createStackFrames(0)
        });
        if (stopped) {
          await transport.send(stopped);
        }
        return;
      }

      if (message.type === 'request' && message.cmd === 'next') {
        const nextRequest = message as RequestEnvelope<'next'>;
        target.onStep(nextRequest);
        const stopped = target.handleExecutionPoint({
          threadId: 1,
          frameId: 2,
          sourceId: 12,
          row: 10,
          functionName: 'RaiseException',
          sourcePath: remotePath,
          stackFrames: createStackFrames(1)
        });
        if (stopped) {
          await transport.send(stopped);
        }
        return;
      }

      if (message.type === 'request' && message.cmd === 'stepIn') {
        const stepInRequest = message as RequestEnvelope<'stepIn'>;
        target.onStep(stepInRequest);
        const stopped = target.handleExecutionPoint({
          threadId: 1,
          frameId: 2,
          sourceId: 12,
          row: 10,
          functionName: 'RaiseException',
          sourcePath: remotePath,
          stackFrames: createStackFrames(1)
        });
        if (stopped) {
          await transport.send(stopped);
        }
        return;
      }

      if (message.type === 'request' && message.cmd === 'stackTrace') {
        const stackTraceRequest = message as RequestEnvelope<'stackTrace'>;
        const response = target.receiveStackTrace(stackTraceRequest);
        await transport.send(response);
        return;
      }

      if (message.type === 'request' && message.cmd === 'scopes') {
        const scopesRequest = message as RequestEnvelope<'scopes'>;
        const response = target.receiveScopes(scopesRequest);
        await transport.send(response);
        return;
      }

      if (message.type === 'request' && message.cmd === 'variables') {
        const variablesRequest = message as RequestEnvelope<'variables'>;
        const response = target.receiveVariables(variablesRequest);
        await transport.send(response);
        return;
      }

      if (message.type === 'request' && message.cmd === 'disconnect') {
        const disconnectRequest = message as RequestEnvelope<'disconnect'>;
        disconnectRequests.push({
          reason: disconnectRequest.body.reason,
          restart: disconnectRequest.body.restart
        });
      }
    })().catch((error) => {
      console.error(error);
    });
  });

  await transport.connect();
  const hello = target.createHello();
  await transport.send(hello);

  return {
    transport,
    runtimeControl,
    disconnectRequests,
    async notifyDisconnect(reason = '会话关闭', restart = false): Promise<void> {
      await transport.send(createEventEnvelope(hello.sessionId, 'disconnect', {
        reason,
        restart
      }, Date.now()));
    },
    async close(): Promise<void> {
      await transport.close();
    }
  };
}

async function verifyWorkflowDebugAdapterPluginFlow(): Promise<void> {
  const port = await reservePort();
  const scriptPath = fileURLToPath(new URL('../src/dapMain.js', import.meta.url));
  const client = new DapClient(scriptPath);

  try {
    const initializeResponse = await client.request('initialize', {
      adapterID: 'workflow'
    });
    assert.equal(initializeResponse.success, true);
    assert.equal((initializeResponse.body as { readonly supportsRestartRequest?: boolean } | undefined)?.supportsRestartRequest, true);
    const receivedMessages = client.getReceivedMessages();
    const initializeResponseIndex = receivedMessages.findIndex((message) => message.type === 'response' && message.request_seq === 1 && message.command === 'initialize');
    assert.ok(initializeResponseIndex >= 0);
    const outputBeforeStart = receivedMessages.find((message) => message.type === 'event' && message.event === 'output');
    assert.equal(outputBeforeStart, undefined);

    const attachRequest = client.request('attach', {
      host: '127.0.0.1',
      port,
      workspaceRoot: 'C:/workspace/Workflow-master',
      pathMapping: [
        {
          localPath,
          remotePath
        }
      ],
      connectTimeoutMs: 10000,
      stopOnEntry: true
    });

    await client.waitForEvent('output', (event) => typeof (event.body as { readonly output?: string } | undefined)?.output === 'string' && (event.body as { readonly output: string }).output.includes('已监听'));

    const attachResponseBeforeHost = await Promise.race([
      attachRequest,
      delay(250).then(() => null)
    ]);
    assert.ok(attachResponseBeforeHost, 'attach 请求不应等待宿主连接完成才返回。');
    assert.equal((attachResponseBeforeHost as DapResponseMessage).success, true);

    const host = await startFakeHost(port);
    try {
      await client.waitForEvent('initialized');

      const setBreakpointsResponse = await client.request('setBreakpoints', {
        source: {
          path: localPath
        },
        breakpoints: [
          {
            line: 10
          }
        ]
      });

      assert.equal(setBreakpointsResponse.success, true);
      const breakpoints = setBreakpointsResponse.body as { readonly breakpoints: Array<{ readonly line: number; readonly verified?: boolean }> };
      assert.equal(breakpoints.breakpoints.length, 1);
      assert.equal(breakpoints.breakpoints[0].line, 10);
      assert.equal(breakpoints.breakpoints[0].verified, true);

      const continueResponse = await client.request('continue', {
        threadId: 1
      });
      assert.equal(continueResponse.success, true);

      const stopped = await client.waitForEvent('stopped', (event) => {
        const body = event.body as { readonly reason?: string } | undefined;
        return body?.reason === 'breakpoint';
      });
      assert.equal((stopped.body as { readonly threadId?: number }).threadId, 1);

      const stackTraceResponse = await client.request('stackTrace', {
        threadId: 1,
        startFrame: 0,
        levels: 20
      });
      assert.equal(stackTraceResponse.success, true);
      const stackTraceBody = stackTraceResponse.body as {
        readonly stackFrames: Array<{ readonly id: number; readonly name: string; readonly source?: { readonly path?: string }; readonly column?: number }>;
      };
      assert.equal(stackTraceBody.stackFrames[0].name, 'RaiseException');
      assert.equal(stackTraceBody.stackFrames[0].source?.path, normalizedLocalPath);
      assert.equal(stackTraceBody.stackFrames[0].column, undefined);

      const scopesResponse = await client.request('scopes', {
        frameId: stackTraceBody.stackFrames[0].id
      });
      assert.equal(scopesResponse.success, true);
      const scopesBody = scopesResponse.body as {
        readonly scopes: Array<{ readonly name: string; readonly variablesReference: number }>;
      };
      assert.deepEqual(
        scopesBody.scopes.map((scope) => scope.name),
        ['局部', '参数', '捕获', '全局']
      );

      const localVariablesResponse = await client.request('variables', {
        variablesReference: scopesBody.scopes[0].variablesReference
      });
      assert.equal(localVariablesResponse.success, true);
      const localVariablesBody = localVariablesResponse.body as {
        readonly variables: Array<{ readonly name: string; readonly variablesReference: number }>;
      };
      assert.equal(localVariablesBody.variables[0].name, 'context');
      assert.ok(localVariablesBody.variables[0].variablesReference > 0);

      const contextVariablesResponse = await client.request('variables', {
        variablesReference: localVariablesBody.variables[0].variablesReference
      });
      assert.equal(contextVariablesResponse.success, true);
      const contextVariablesBody = contextVariablesResponse.body as {
        readonly variables: Array<{ readonly name: string }>;
      };
      assert.equal(contextVariablesBody.variables[0].name, 'message');

      const nextResponse = await client.request('next', {
        threadId: 1
      });
      assert.equal(nextResponse.success, true);

      const stepStopped = await client.waitForEvent('stopped', (event) => {
        const body = event.body as { readonly reason?: string } | undefined;
        return body?.reason === 'step';
      });
      assert.equal((stepStopped.body as { readonly threadId?: number }).threadId, 1);

      assert.deepEqual(host.runtimeControl.calls, [
        {
          kind: 'run',
          threadId: 1
        },
        {
          kind: 'stepOver',
          threadId: 1
        }
      ]);

      const restartResponse = await client.request('restart');
      assert.equal(restartResponse.success, true);
      await delay(100);
      assert.deepEqual(host.disconnectRequests, []);
      const terminatedEvents = client.getReceivedMessages().filter((message) => message.type === 'event' && message.event === 'terminated');
      assert.equal(terminatedEvents.length, 0);
    }
    finally {
      await host.close();
    }
  }
  finally {
    await client.close();
  }
}

async function verifyDisconnectAfterHostClosedIsIdempotent(): Promise<void> {
  const port = await reservePort();
  const scriptPath = fileURLToPath(new URL('../src/dapMain.js', import.meta.url));
  const client = new DapClient(scriptPath);

  try {
    const initializeResponse = await client.request('initialize', {
      adapterID: 'workflow'
    });
    assert.equal(initializeResponse.success, true);

    const attachResponse = await client.request('attach', {
      host: '127.0.0.1',
      port,
      workspaceRoot: 'C:/workspace/Workflow-master',
      pathMapping: [
        {
          localPath,
          remotePath
        }
      ],
      connectTimeoutMs: 10000,
      stopOnEntry: true
    });
    assert.equal(attachResponse.success, true);

    const host = await startFakeHost(port);
    try {
      await client.waitForEvent('initialized');
      await host.notifyDisconnect('会话关闭', false);

      const terminated = await client.waitForEvent('terminated');
      assert.equal((terminated.body as { readonly restart?: boolean } | undefined)?.restart, false);

      const disconnectResponse = await client.request('disconnect', {
        restart: false
      });
      assert.equal(disconnectResponse.success, true);
      assert.deepEqual(host.disconnectRequests, []);
    }
    finally {
      await host.close();
    }
  }
  finally {
    await client.close();
  }
}

async function main(): Promise<void> {
  await verifyWorkflowDebugAdapterPluginFlow();
  await verifyDisconnectAfterHostClosedIsIdempotent();
  console.log('WorkflowDebugAdapter VSCode 插件附加与单步检查通过。');
}

try {
  await main();
}
catch (error) {
  console.error('WorkflowDebugAdapter VSCode 插件附加与单步检查失败。');
  throw error;
}
