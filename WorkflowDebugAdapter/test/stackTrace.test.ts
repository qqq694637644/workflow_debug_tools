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
    row: 3
  },
  {
    codeIndex: 7,
    sourcePath: remotePath,
    row: 8
  },
  {
    codeIndex: 7,
    sourcePath: remotePath,
    row: 13
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

function setupSession(): {
  readonly adapter: AdapterSessionMachine;
  readonly target: TargetSessionMachine;
} {
  const runtimeControl = new RecordingRuntimeControl();
  const target = new TargetSessionMachine({
    runtimeVersion: '0.1.0',
    capabilities: DEFAULT_CAPABILITIES,
    sourceMap,
    runtimeControl
  });
  const adapter = new AdapterSessionMachine();

  const sessionId = 'wf-20260423-stack';
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
    target
  };
}

function verifyStackTraceRequestAndModel(): void {
  const { adapter, target } = setupSession();
  const setBreakpoints = adapter.createSetBreakpoints(localPath, [
    {
      line: 14
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
    frameId: 2,
    sourceId: 7,
    row: 13,
    functionName: 'leaf',
    sourcePath: remotePath,
    stackFrames: [
      {
        callStackIndex: 0,
        functionName: 'main',
        sourceId: 7,
        sourcePath: remotePath,
        row: 3
      },
      {
        callStackIndex: 1,
        functionName: 'helper',
        sourceId: 7,
        sourcePath: remotePath,
        row: 8
      },
      {
        callStackIndex: 2,
        functionName: 'leaf',
        sourceId: 7,
        sourcePath: remotePath,
        row: 13
      }
    ]
  });

  assert.ok(stopped);
  adapter.receiveStopped(stopped!);

  const request = adapter.createStackTrace(1, 0, 20);
  const response = target.receiveStackTrace(request);
  const stackState = adapter.receiveStackTrace(response);

  assert.equal(response.body.totalFrames, 3);
  assert.equal(response.body.frames?.length, 3);
  assert.deepEqual(
    stackState.frames.map((frame) => frame.name),
    ['leaf', 'helper', 'main']
  );
  assert.deepEqual(
    stackState.frames.map((frame) => frame.line),
    [14, 9, 4]
  );
  assert.deepEqual(
    stackState.frames.map((frame) => frame.frameId),
    [2, 1, 0]
  );

  const helperFrame = adapter.getStackModel().getFrame(1, 1);
  assert.ok(helperFrame);
  assert.equal(helperFrame?.name, 'helper');
  assert.equal(helperFrame?.line, 9);
  assert.equal(helperFrame?.canRequestVariables, true);
  assert.equal(target.getStackInspector().getFrame(1, 1)?.functionName, 'helper');
}

function verifyStackTraceWindowingKeepsFrameIdsStable(): void {
  const { adapter, target } = setupSession();
  const setBreakpoints = adapter.createSetBreakpoints(localPath, [
    {
      line: 14
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
    frameId: 2,
    sourceId: 7,
    row: 13,
    functionName: 'leaf',
    sourcePath: remotePath,
    stackFrames: [
      {
        callStackIndex: 0,
        functionName: 'main',
        sourceId: 7,
        sourcePath: remotePath,
        row: 3
      },
      {
        callStackIndex: 1,
        functionName: 'helper',
        sourceId: 7,
        sourcePath: remotePath,
        row: 8
      },
      {
        callStackIndex: 2,
        functionName: 'leaf',
        sourceId: 7,
        sourcePath: remotePath,
        row: 13
      }
    ]
  });

  adapter.receiveStopped(stopped!);

  const request = adapter.createStackTrace(1, 1, 1);
  const response = target.receiveStackTrace(request);
  const stackState = adapter.receiveStackTrace(response);

  assert.equal(stackState.frames.length, 1);
  assert.equal(stackState.frames[0].frameId, 1);
  assert.equal(stackState.frames[0].name, 'helper');
  assert.equal(stackState.frames[0].line, 9);
  assert.equal(adapter.getStackModel().getFrame(1, 1)?.name, 'helper');
  assert.equal(adapter.getStackModel().getFrame(1, 2), null);
}

function verifyUnknownSourceFramesDoNotBreakStackTrace(): void {
  const { adapter, target } = setupSession();
  const setBreakpoints = adapter.createSetBreakpoints(localPath, [
    {
      line: 14
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
    frameId: 2,
    sourceId: 7,
    row: 13,
    functionName: 'leaf',
    sourcePath: remotePath,
    stackFrames: [
      {
        callStackIndex: 0,
        functionName: 'main',
        sourceId: 7,
        sourcePath: remotePath,
        row: 3
      },
      {
        callStackIndex: 1,
        functionName: 'helper',
        sourceId: 7,
        sourcePath: remotePath,
        row: 8
      },
      {
        callStackIndex: 2,
        functionName: 'leaf',
        sourceId: 7,
        sourcePath: remotePath,
        row: 13
      }
    ]
  });

  assert.ok(stopped);
  adapter.receiveStopped(stopped!);

  const stepInRequest = adapter.createStepIn(1);
  target.onStep(stepInRequest);
  const unknownStopped = target.handleExecutionPoint({
    threadId: 1,
    frameId: 3,
    sourceId: -1,
    row: 0,
    functionName: 'builtin',
    sourcePath: 'unknown://source/frame/3',
    stackFrames: [
      {
        callStackIndex: 0,
        functionName: 'builtin',
        sourceId: -1,
        sourcePath: 'unknown://source/frame/3',
        row: 0
      }
    ]
  });

  assert.ok(unknownStopped);
  adapter.receiveStopped(unknownStopped!);

  const request = adapter.createStackTrace(1, 0, 20);
  const response = target.receiveStackTrace(request);
  const stackState = adapter.receiveStackTrace(response);

  assert.equal(response.body.totalFrames, 1);
  assert.equal(stackState.frames.length, 1);
  assert.equal(stackState.frames[0].sourceId, -1);
  assert.equal(stackState.frames[0].sourcePath, 'unknown://source/frame/3');
  assert.equal(adapter.getStackModel().getFrame(0, 1)?.sourceId, -1);
}

function main(): void {
  verifyStackTraceRequestAndModel();
  verifyStackTraceWindowingKeepsFrameIdsStable();
  verifyUnknownSourceFramesDoNotBreakStackTrace();
  console.log('WorkflowDebugAdapter 调用栈检查通过。');
}

try {
  main();
} catch (error) {
  console.error('WorkflowDebugAdapter 调用栈检查失败。');
  throw error;
}
