import assert from 'node:assert/strict';

import {
  AdapterSessionMachine,
  DEFAULT_CAPABILITIES,
  TargetSessionMachine,
  type TargetRuntimeControl
} from '../src/index.js';

const remotePath = 'D:/repos/Workflow-master/Test/Resources/Rpc/RequestService.txt';
const localPath = 'C:/workspace/Workflow-master/Test/Resources/Rpc/RequestService.txt';

const sourceMap = [
  {
    codeIndex: 7,
    sourcePath: remotePath,
    row: 9
  },
  {
    codeIndex: 7,
    sourcePath: remotePath,
    row: 10
  },
  {
    codeIndex: 7,
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

function setupSession(): {
  readonly adapter: AdapterSessionMachine;
  readonly target: TargetSessionMachine;
  readonly runtimeControl: RecordingRuntimeControl;
} {
  const runtimeControl = new RecordingRuntimeControl();
  const target = new TargetSessionMachine({
    runtimeVersion: '0.1.0',
    capabilities: DEFAULT_CAPABILITIES,
    sourceMap,
    runtimeControl
  });
  const adapter = new AdapterSessionMachine();

  const sessionId = 'wf-20260423-exec';
  target.attach(sessionId);
  adapter.attach(sessionId);

  const hello = target.createHello();
  adapter.receiveHello(hello);
  const initialize = adapter.createInitialize({
    workspaceRoot: 'C:/workspace/Workflow-master',
    pathMapping: [
      {
        localPath,
        remotePath
      }
    ],
    supports: DEFAULT_CAPABILITIES
  });
  const ready = target.receiveInitialize(initialize);
  adapter.receiveReady(ready);

  return {
    adapter,
    target,
    runtimeControl
  };
}

function verifyBreakpointPauseAndContinue(): void {
  const { adapter, target, runtimeControl } = setupSession();
  const setBreakpoints = adapter.createSetBreakpoints(localPath, [
    {
      line: 10
    }
  ]);

  const validations = target.receiveSetBreakpoints(setBreakpoints);
  for (const validation of validations) {
    adapter.receiveBreakpointValidated(validation);
  }

  assert.equal(target.getBreakpointRegistry().hasBreakpoint(7, 9), true);

  const continueRequest = adapter.createContinue(1);
  target.onContinue(continueRequest);
  assert.deepEqual(runtimeControl.calls, [
    {
      kind: 'run',
      threadId: 1
    }
  ]);
  assert.equal(adapter.snapshot().phase, 'running');
  assert.equal(target.snapshot().phase, 'running');

  const stopped = target.handleExecutionPoint({
    threadId: 1,
    frameId: 0,
    sourceId: 7,
    row: 9
  });
  assert.ok(stopped);
  assert.equal(stopped?.body.reason, 'breakpoint');
  assert.equal(stopped?.body.row, 9);

  adapter.receiveStopped(stopped!);
  assert.equal(adapter.snapshot().phase, 'paused');
  assert.equal(target.snapshot().phase, 'paused');
  assert.equal(adapter.snapshot().lastStoppedRow, 9);
  assert.equal(target.snapshot().lastStoppedRow, 9);
}

function verifyStepMapping(): void {
  const { adapter, target, runtimeControl } = setupSession();
  const setBreakpoints = adapter.createSetBreakpoints(localPath, [
    {
      line: 10
    }
  ]);

  const validations = target.receiveSetBreakpoints(setBreakpoints);
  for (const validation of validations) {
    adapter.receiveBreakpointValidated(validation);
  }

  const continueRequest = adapter.createContinue(1);
  target.onContinue(continueRequest);
  const stopped = target.handleExecutionPoint({
    threadId: 1,
    frameId: 0,
    sourceId: 7,
    row: 9
  });
  adapter.receiveStopped(stopped!);

  const nextRequest = adapter.createNext(1);
  target.onStep(nextRequest);
  const stepOverStopped = target.handleExecutionPoint({
    threadId: 1,
    frameId: 0,
    sourceId: 7,
    row: 10
  });
  assert.ok(stepOverStopped);
  assert.equal(stepOverStopped?.body.reason, 'step');
  assert.equal(stepOverStopped?.body.row, 10);
  adapter.receiveStopped(stepOverStopped!);

  const stepInRequest = adapter.createStepIn(1);
  target.onStep(stepInRequest);
  const stepInStopped = target.handleExecutionPoint({
    threadId: 1,
    frameId: 1,
    sourceId: 7,
    row: 11
  });
  assert.ok(stepInStopped);
  assert.equal(stepInStopped?.body.reason, 'step');
  assert.equal(stepInStopped?.body.row, 11);
  adapter.receiveStopped(stepInStopped!);

  assert.deepEqual(runtimeControl.calls, [
    {
      kind: 'run',
      threadId: 1
    },
    {
      kind: 'stepOver',
      threadId: 1
    },
    {
      kind: 'stepInto',
      threadId: 1
    }
  ]);

  assert.equal(adapter.snapshot().phase, 'paused');
  assert.equal(target.snapshot().phase, 'paused');
  assert.equal(adapter.snapshot().lastStoppedRow, 11);
  assert.equal(target.snapshot().lastStoppedRow, 11);
}

function main(): void {
  verifyBreakpointPauseAndContinue();
  verifyStepMapping();
  console.log('WorkflowDebugAdapter 暂停、继续、单步检查通过。');
}

try {
  main();
} catch (error) {
  console.error('WorkflowDebugAdapter 暂停、继续、单步检查失败。');
  throw error;
}
