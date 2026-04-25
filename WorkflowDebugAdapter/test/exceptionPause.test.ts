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
  public readonly calls: Array<{ readonly kind: 'run' | 'stepOver' | 'stepInto' | 'stepOut'; readonly threadId: number }> = [];

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

  public stepOut(threadId: number): boolean {
    this.calls.push({
      kind: 'stepOut',
      threadId
    });
    return true;
  }
}

function createExceptionCallStack() {
  return [
    {
      threadId: 1,
      frameId: 2,
      callStackIndex: 2,
      functionName: 'leaf',
      sourceId: 7,
      sourcePath: remotePath,
      row: 11,
      line: 12,
      column: 1,
      canRequestVariables: true
    },
    {
      threadId: 1,
      frameId: 1,
      callStackIndex: 1,
      functionName: 'helper',
      sourceId: 7,
      sourcePath: remotePath,
      row: 10,
      line: 11,
      column: 1,
      canRequestVariables: true
    },
    {
      threadId: 1,
      frameId: 0,
      callStackIndex: 0,
      functionName: 'main',
      sourceId: 7,
      sourcePath: remotePath,
      row: 9,
      line: 10,
      column: 1,
      canRequestVariables: true
    }
  ] as const;
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

  const sessionId = 'wf-20260423-exception';
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

function verifyExceptionPauseAndStackTrace(): void {
  const { adapter, target, runtimeControl } = setupSession();
  const continueRequest = adapter.createContinue(1);
  target.onContinue(continueRequest);

  const exception = target.handleException({
    message: 'Exception',
    fatal: false,
    callStack: createExceptionCallStack()
  });

  assert.equal(exception.cmd, 'exception');
  assert.equal(exception.replyTo, continueRequest.seq);
  assert.equal(exception.body.message, 'Exception');
  assert.equal(exception.body.fatal, false);
  assert.equal(exception.body.callStack.length, 3);

  const exceptionState = adapter.receiveException(exception);
  assert.equal(adapter.snapshot().phase, 'paused');
  assert.equal(target.snapshot().phase, 'paused');
  assert.equal(adapter.snapshot().lastStoppedReason, 'exception');
  assert.equal(target.snapshot().lastStoppedReason, 'exception');
  assert.equal(adapter.snapshot().lastStoppedThreadId, 1);
  assert.equal(adapter.snapshot().lastStoppedFrameId, 2);
  assert.equal(adapter.snapshot().lastStoppedSourceId, 7);
  assert.equal(adapter.snapshot().lastStoppedRow, 11);
  assert.deepEqual(
    exceptionState.frames.map((frame) => frame.name),
    ['leaf', 'helper', 'main']
  );
  assert.deepEqual(
    exceptionState.frames.map((frame) => frame.line),
    [12, 11, 10]
  );
  assert.deepEqual(
    target.getStackInspector().getSnapshot(1)?.frames.map((frame) => frame.functionName),
    ['leaf', 'helper', 'main']
  );

  const stackTraceRequest = adapter.createStackTrace(1, 0, 20);
  const stackTraceResponse = target.receiveStackTrace(stackTraceRequest);
  const stackTraceState = adapter.receiveStackTrace(stackTraceResponse);

  assert.deepEqual(
    stackTraceState.frames.map((frame) => frame.name),
    ['leaf', 'helper', 'main']
  );
  assert.deepEqual(
    stackTraceState.frames.map((frame) => frame.line),
    [12, 11, 10]
  );
  assert.deepEqual(runtimeControl.calls, [
    {
      kind: 'run',
      threadId: 1
    }
  ]);
}

function main(): void {
  verifyExceptionPauseAndStackTrace();
  console.log('WorkflowDebugAdapter 异常暂停检查通过。');
}

try {
  main();
} catch (error) {
  console.error('WorkflowDebugAdapter 异常暂停检查失败。');
  throw error;
}
