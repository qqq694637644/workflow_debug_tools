import assert from 'node:assert/strict';

import { DEFAULT_CAPABILITIES, AdapterSessionMachine, TargetSessionMachine } from '../src/index.js';

const sampleSourceMap = [
  {
    codeIndex: 7,
    sourcePath: 'D:/repos/Workflow-master/Test/Resources/Rpc/RequestService.txt',
    row: 1
  }
];

const sampleInitialize = {
  workspaceRoot: 'D:/repos/Workflow-master',
  pathMapping: [
    {
      localPath: 'D:/repos/Workflow-master',
      remotePath: 'D:/repos/Workflow-master'
    }
  ],
  supports: DEFAULT_CAPABILITIES
} as const;

function verifyHandshake(): void {
  const sessionId = 'wf-20260423-001';
  const target = new TargetSessionMachine({
    runtimeVersion: '0.1.0',
    capabilities: DEFAULT_CAPABILITIES,
    sourceMap: sampleSourceMap
  });
  const adapter = new AdapterSessionMachine();

  target.attach(sessionId);
  adapter.attach(sessionId);

  const hello = target.createHello();
  assert.equal(hello.type, 'event');
  assert.equal(hello.cmd, 'hello');
  assert.equal(hello.seq, 1);
  assert.equal(hello.replyTo, undefined);

  adapter.receiveHello(hello);
  const initialize = adapter.createInitialize(sampleInitialize);
  assert.equal(initialize.type, 'request');
  assert.equal(initialize.cmd, 'initialize');
  assert.equal(initialize.seq, 1);
  assert.equal(initialize.replyTo, hello.seq);

  const ready = target.receiveInitialize(initialize);
  assert.equal(ready.type, 'event');
  assert.equal(ready.cmd, 'ready');
  assert.equal(ready.replyTo, initialize.seq);
  assert.equal(ready.body.accepted, true);

  adapter.receiveReady(ready);

  const adapterSnapshot = adapter.snapshot();
  const targetSnapshot = target.snapshot();
  assert.equal(adapterSnapshot.phase, 'ready');
  assert.equal(targetSnapshot.phase, 'ready');
  assert.equal(adapterSnapshot.pendingRequestCount, 0);
  assert.equal(adapterSnapshot.remoteHelloRuntimeVersion, '0.1.0');
  assert.equal(adapterSnapshot.sourceMapCount, 1);
  assert.equal(targetSnapshot.workspaceRoot, 'D:/repos/Workflow-master');
}

function verifyInitializeRequiresHello(): void {
  const adapter = new AdapterSessionMachine();
  adapter.attach('wf-20260423-002');

  assert.throws(
    () =>
      adapter.createInitialize({
        workspaceRoot: 'D:/repos/Workflow-master',
        pathMapping: [],
        supports: DEFAULT_CAPABILITIES
      }),
    /hello/
  );
}

function verifySessionReset(): void {
  const target = new TargetSessionMachine({
    runtimeVersion: '0.1.0',
    capabilities: DEFAULT_CAPABILITIES,
    sourceMap: sampleSourceMap
  });
  const adapter = new AdapterSessionMachine();

  target.attach('wf-20260423-a');
  adapter.attach('wf-20260423-a');

  const oldHello = target.createHello();
  adapter.receiveHello(oldHello);
  const oldInitialize = adapter.createInitialize(sampleInitialize);

  assert.equal(oldInitialize.replyTo, oldHello.seq);
  assert.equal(adapter.snapshot().pendingRequestCount, 1);

  target.detach();
  adapter.detach();

  target.attach('wf-20260423-b');
  adapter.attach('wf-20260423-b');

  assert.equal(adapter.snapshot().pendingRequestCount, 0);
  assert.equal(adapter.snapshot().lastInboundSeq, 0);
  assert.equal(adapter.snapshot().lastOutboundSeq, 0);

  const newHello = target.createHello();
  assert.equal(newHello.seq, 1);

  adapter.receiveHello(newHello);
  const newInitialize = adapter.createInitialize(sampleInitialize);
  assert.equal(newInitialize.seq, 1);
  assert.equal(newInitialize.replyTo, newHello.seq);
}

function main(): void {
  verifyHandshake();
  verifyInitializeRequiresHello();
  verifySessionReset();
  console.log('WorkflowDebugAdapter 协议与会话状态机检查通过。');
}

try {
  main();
} catch (error) {
  console.error('WorkflowDebugAdapter 协议与会话状态机检查失败。');
  throw error;
}
