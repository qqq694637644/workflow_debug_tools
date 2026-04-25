import assert from 'node:assert/strict';

import {
  AdapterSessionMachine,
  DEFAULT_CAPABILITIES,
  TargetSessionMachine,
  type TargetRuntimeControl
} from '../src/index.js';

const remotePath = 'D:/repos/Workflow-master/Test/Resources/Debugger/RaiseException.txt';
const localPath = 'C:/workspace/Workflow-master/Test/Resources/Debugger/RaiseException.txt';

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

  const sessionId = 'wf-20260423-full-flow';
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

function verifyFullFlowSmokeTest(): void {
  const { adapter, target, runtimeControl } = setupSession();
  const setBreakpoints = adapter.createSetBreakpoints(localPath, [
    {
      line: 12
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
    sourceId: 12,
    row: 11,
    functionName: 'RaiseException',
    sourcePath: remotePath,
    stackFrames: createStackFrames(0)
  });

  assert.ok(stopped);
  assert.equal(stopped?.body.reason, 'breakpoint');
  adapter.receiveStopped(stopped!);

  const stackTraceRequest = adapter.createStackTrace(1, 0, 20);
  const stackTraceResponse = target.receiveStackTrace(stackTraceRequest);
  const stackState = adapter.receiveStackTrace(stackTraceResponse);

  assert.deepEqual(
    stackState.frames.map((frame) => frame.name),
    ['RaiseException', 'Update', 'Main']
  );

  const topFrame = stackState.frames[0];
  const scopesResponse = target.receiveScopes(adapter.createScopes(topFrame.frameId));
  const scopesState = adapter.receiveScopes(scopesResponse);
  assert.deepEqual(
    scopesState.scopes.map((scope) => scope.name),
    ['局部', '参数', '捕获', '全局']
  );

  const localScope = adapter.getScopeModel().getScope(1, topFrame.frameId, 'Local');
  assert.ok(localScope);
  assert.equal(localScope?.canExpand, true);
  assert.ok(localScope && localScope.variablesReference > 0);

  const localResponse = target.receiveVariables(adapter.createVariables(localScope!.variablesReference));
  const localState = adapter.receiveVariables(localResponse);
  assert.deepEqual(
    localState.variables.map((variable) => variable.name),
    ['context']
  );

  const contextVariable = adapter.getVariableModel().getVariables(localScope!.handle)[0];
  assert.ok(contextVariable);
  assert.equal(contextVariable?.canExpand, true);
  assert.ok(contextVariable && contextVariable.variablesReference > 0);

  const contextResponse = target.receiveVariables(
    adapter.createVariables(contextVariable!.variablesReference)
  );
  const contextState = adapter.receiveVariables(contextResponse);
  assert.deepEqual(
    contextState.variables.map((variable) => variable.name),
    ['message']
  );

  const nextRequest = adapter.createNext(1);
  target.onStep(nextRequest);
  const stepStopped = target.handleExecutionPoint({
    threadId: 1,
    frameId: 2,
    sourceId: 12,
    row: 12,
    functionName: 'RaiseException',
    sourcePath: remotePath,
    stackFrames: createStackFrames(1)
  });

  assert.ok(stepStopped);
  assert.equal(stepStopped?.body.reason, 'step');
  adapter.receiveStopped(stepStopped!);
  assert.equal(adapter.snapshot().phase, 'paused');

  const continueRequest2 = adapter.createContinue(1);
  target.onContinue(continueRequest2);
  const exception = target.handleException({
    message: 'Exception',
    fatal: false,
    callStack: [
      {
        threadId: 1,
        frameId: 2,
        callStackIndex: 2,
        functionName: 'RaiseException',
        sourceId: 12,
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
        functionName: 'Update',
        sourceId: 12,
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
        functionName: 'Main',
        sourceId: 12,
        sourcePath: remotePath,
        row: 9,
        line: 10,
        column: 1,
        canRequestVariables: true
      }
    ]
  });

  assert.equal(exception.body.message, 'Exception');
  assert.equal(exception.body.fatal, false);
  assert.equal(exception.body.callStack.length, 3);

  const exceptionState = adapter.receiveException(exception);
  assert.equal(adapter.snapshot().lastStoppedReason, 'exception');
  assert.equal(target.snapshot().lastStoppedReason, 'exception');
  assert.deepEqual(
    exceptionState.frames.map((frame) => frame.name),
    ['RaiseException', 'Update', 'Main']
  );
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
      kind: 'run',
      threadId: 1
    }
  ]);
}

function main(): void {
  verifyFullFlowSmokeTest();
  console.log('WorkflowDebugAdapter 第一阶段烟雾测试通过。');
}

try {
  main();
} catch (error) {
  console.error('WorkflowDebugAdapter 第一阶段烟雾测试失败。');
  throw error;
}
