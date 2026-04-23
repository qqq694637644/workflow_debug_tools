import assert from 'node:assert/strict';

import {
  AdapterSessionMachine,
  BreakpointRegistry,
  DEFAULT_CAPABILITIES,
  SourceCatalog,
  TargetSessionMachine
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
  }
] as const;

function verifySourceCatalogAndBreakpointRegistry(): void {
  const sessionId = 'wf-20260423-source-map';
  const target = new TargetSessionMachine({
    runtimeVersion: '0.1.0',
    capabilities: DEFAULT_CAPABILITIES,
    sourceMap
  });
  const adapter = new AdapterSessionMachine();

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

  const catalog = adapter.getSourceCatalog();
  assert.equal(catalog.getEntryCount(), 2);
  assert.equal(catalog.resolveCodeIndex(localPath), 7);
  assert.equal(catalog.resolveSourcePath(7), remotePath);
  assert.deepEqual(catalog.resolveByCodeIndex(7)?.rows, [9, 10]);

  const registry = new BreakpointRegistry(catalog);
  const setBreakpoints = registry.applyBreakpoints(localPath, [
    {
      line: 10
    },
    {
      line: 11,
      condition: 'count > 0'
    }
  ]);

  assert.equal(setBreakpoints.sourcePath, remotePath);
  assert.equal(setBreakpoints.codeIndex, 7);
  assert.deepEqual(
    setBreakpoints.breakpoints.map((breakpoint) => breakpoint.row),
    [9, 10]
  );
  assert.notEqual(
    setBreakpoints.breakpoints[0].breakpointId,
    setBreakpoints.breakpoints[1].breakpointId
  );

  const firstValidated = registry.mergeValidatedState({
    breakpointId: setBreakpoints.breakpoints[0].breakpointId,
    verified: true
  });
  const secondValidated = registry.mergeValidatedState({
    breakpointId: setBreakpoints.breakpoints[1].breakpointId,
    verified: false,
    reason: '未找到可执行行。'
  });

  assert.equal(firstValidated?.verified, true);
  assert.equal(secondValidated?.verified, false);
  assert.equal(secondValidated?.reason, '未找到可执行行。');

  const snapshot = registry.getBreakpoints(localPath);
  assert.equal(snapshot.length, 2);
  assert.equal(snapshot[0].line, 10);
  assert.equal(snapshot[1].line, 11);
  assert.equal(snapshot[1].reason, '未找到可执行行。');

  registry.applyBreakpoints(localPath, [
    {
      line: 10
    }
  ]);
  assert.equal(registry.getBreakpoints(localPath).length, 1);

  assert.equal(adapter.snapshot().sourceMapCount, 2);
  assert.equal(target.snapshot().sourceMapCount, 2);
}

function verifyCatalogRejectsConflicts(): void {
  const catalog = new SourceCatalog();
  catalog.registerSource({
    codeIndex: 1,
    sourcePath: 'D:/workspace/A.txt',
    row: 0
  });

  assert.throws(
    () =>
      catalog.registerSource({
        codeIndex: 1,
        sourcePath: 'D:/workspace/B.txt',
        row: 0
      }),
    /不同的源码路径/
  );

  catalog.registerPathMapping({
    localPath: 'C:/workspace/A.txt',
    remotePath: 'D:/workspace/A.txt'
  });
  assert.equal(catalog.resolveCodeIndex('C:/workspace/A.txt'), 1);
}

function main(): void {
  verifySourceCatalogAndBreakpointRegistry();
  verifyCatalogRejectsConflicts();
  console.log('WorkflowDebugAdapter 源码映射与断点同步检查通过。');
}

try {
  main();
} catch (error) {
  console.error('WorkflowDebugAdapter 源码映射与断点同步检查失败。');
  throw error;
}
