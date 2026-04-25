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

function createGlobalVariables() {
  return [
    {
      name: 'appName',
      type: 'string',
      value: 'Workflow'
    }
  ] as const;
}

function createMainVariables() {
  return {
    local: [
      {
        name: 'count',
        type: 'number',
        value: '3'
      },
      {
        name: 'person',
        type: 'Person',
        value: '{...}',
        children: [
          {
            name: 'name',
            type: 'string',
            value: 'Alice'
          },
          {
            name: 'address',
            type: 'Address',
            value: '{...}',
            children: [
              {
                name: 'street',
                type: 'string',
                value: 'Lake'
              },
              {
                name: 'zip',
                type: 'string',
                value: '100000'
              }
            ]
          }
        ]
      }
    ],
    argument: [
      {
        name: 'items',
        type: 'array',
        value: '[2]',
        children: [
          {
            name: '0',
            type: 'number',
            value: '1'
          },
          {
            name: '1',
            type: 'number',
            value: '2'
          }
        ]
      }
    ],
    captured: [
      {
        name: 'suffix',
        type: 'string',
        value: '!'
      }
    ],
    global: createGlobalVariables()
  } as const;
}

function createHelperVariables() {
  return {
    local: [
      {
        name: 'flag',
        type: 'boolean',
        value: 'true'
      },
      {
        name: 'note',
        type: 'string',
        value: 'helper'
      }
    ],
    argument: [
      {
        name: 'message',
        type: 'string',
        value: 'hello'
      }
    ],
    captured: [],
    global: createGlobalVariables()
  } as const;
}

function createLeafVariables() {
  return {
    local: [
      {
        name: 'result',
        type: 'string',
        value: 'done'
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
        name: 'outer',
        type: 'number',
        value: '4'
      }
    ],
    global: createGlobalVariables()
  } as const;
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

  const sessionId = 'wf-20260423-variable';
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

function pauseWithVariables(): {
  readonly adapter: AdapterSessionMachine;
  readonly target: TargetSessionMachine;
} {
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
        row: 3,
        variables: createMainVariables()
      },
      {
        callStackIndex: 1,
        functionName: 'helper',
        sourceId: 7,
        sourcePath: remotePath,
        row: 8,
        variables: createHelperVariables()
      },
      {
        callStackIndex: 2,
        functionName: 'leaf',
        sourceId: 7,
        sourcePath: remotePath,
        row: 13,
        variables: createLeafVariables()
      }
    ]
  });

  assert.ok(stopped);
  adapter.receiveStopped(stopped!);

  const stackTraceRequest = adapter.createStackTrace(1, 0, 20);
  const stackTraceResponse = target.receiveStackTrace(stackTraceRequest);
  adapter.receiveStackTrace(stackTraceResponse);

  return {
    adapter,
    target
  };
}

function verifyVariableScopesAndExpansion(): void {
  const { adapter, target } = pauseWithVariables();

  const mainScopesResponse = target.receiveScopes(adapter.createScopes(0));
  const mainScopesState = adapter.receiveScopes(mainScopesResponse);
  assert.deepEqual(
    mainScopesState.scopes.map((scope) => scope.name),
    ['局部', '参数', '捕获', '全局']
  );

  const mainLocalScope = adapter.getScopeModel().getScope(1, 0, 'local');
  assert.ok(mainLocalScope);
  assert.equal(mainLocalScope?.canExpand, true);
  assert.ok(mainLocalScope && mainLocalScope.variablesReference > 0);

  const mainLocalResponse = target.receiveVariables(
    adapter.createVariables(mainLocalScope!.variablesReference)
  );
  const mainLocalState = adapter.receiveVariables(mainLocalResponse);
  assert.equal(mainLocalState.scopeKind, 'local');
  assert.deepEqual(
    mainLocalState.variables.map((variable) => variable.name),
    ['count', 'person']
  );

  const mainLocalVariables = adapter.getVariableModel().getVariables(mainLocalScope!.handle);
  const personVariable = mainLocalVariables.find((variable) => variable.name === 'person');
  assert.ok(personVariable);
  assert.equal(personVariable?.canExpand, true);
  assert.ok(personVariable && personVariable.variablesReference > 0);
  assert.equal(adapter.getHandleTable().resolve(personVariable!.variablesReference)?.frameId, 0);

  const personResponse = target.receiveVariables(
    adapter.createVariables(personVariable!.variablesReference)
  );
  const personState = adapter.receiveVariables(personResponse);
  assert.equal(personState.scopeKind, 'object');
  assert.deepEqual(
    personState.variables.map((variable) => variable.name),
    ['name', 'address']
  );

  const personChildren = adapter.getVariableModel().getVariables(personVariable!.handle);
  const addressVariable = personChildren.find((variable) => variable.name === 'address');
  assert.ok(addressVariable);
  assert.equal(addressVariable?.canExpand, true);
  assert.ok(addressVariable && addressVariable.variablesReference > 0);

  const addressResponse = target.receiveVariables(
    adapter.createVariables(addressVariable!.variablesReference)
  );
  const addressState = adapter.receiveVariables(addressResponse);
  assert.deepEqual(
    addressState.variables.map((variable) => variable.name),
    ['street', 'zip']
  );

  const helperScopesResponse = target.receiveScopes(adapter.createScopes(1));
  const helperScopesState = adapter.receiveScopes(helperScopesResponse);
  assert.deepEqual(
    helperScopesState.scopes.map((scope) => scope.name),
    ['局部', '参数', '捕获', '全局']
  );

  const helperLocalScope = adapter.getScopeModel().getScope(1, 1, 'local');
  assert.ok(helperLocalScope);
  assert.equal(helperLocalScope?.canExpand, true);
  const helperLocalResponse = target.receiveVariables(
    adapter.createVariables(helperLocalScope!.variablesReference)
  );
  const helperLocalState = adapter.receiveVariables(helperLocalResponse);
  assert.deepEqual(
    helperLocalState.variables.map((variable) => variable.name),
    ['flag', 'note']
  );

  const personResponseAgain = target.receiveVariables(
    adapter.createVariables(personVariable!.variablesReference)
  );
  const personStateAgain = adapter.receiveVariables(personResponseAgain);
  assert.deepEqual(
    personStateAgain.variables.map((variable) => variable.name),
    ['name', 'address']
  );
  assert.equal(adapter.getVariableModel().getParentHandle(personVariable!.handle), mainLocalScope!.handle);
}

function main(): void {
  verifyVariableScopesAndExpansion();
  console.log('WorkflowDebugAdapter 变量展开检查通过。');
}

try {
  main();
} catch (error) {
  console.error('WorkflowDebugAdapter 变量展开检查失败。');
  throw error;
}
