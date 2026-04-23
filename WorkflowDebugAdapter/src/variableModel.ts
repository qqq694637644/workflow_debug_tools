import { type ScopeKind, type VariableEntryBody, type VariablesBody } from './protocol.js';
import { type HandleTable, type HandleTableSnapshot } from './handleTable.js';

export interface VariableModelEntry {
  readonly handle: number;
  readonly parentHandle: number;
  readonly threadId: number;
  readonly frameId: number;
  readonly scopeKind: ScopeKind;
  readonly name: string;
  readonly value: string;
  readonly type: string;
  readonly variablesReference: number;
  readonly canExpand: boolean;
  readonly namedVariables: number;
  readonly indexedVariables: number;
}

export interface VariableModelState {
  readonly threadId: number;
  readonly frameId: number;
  readonly scopeKind: ScopeKind;
  readonly parentHandle: number;
  readonly variables: ReadonlyArray<VariableModelEntry>;
}

export class VariableModelError extends Error {
  constructor(message: string) {
    super(message);
    this.name = 'VariableModelError';
  }
}

function assertNonNegativeInteger(value: number, name: string): void {
  if (!Number.isInteger(value) || value < 0) {
    throw new VariableModelError(`${name} 必须是非负整数。`);
  }
}

function cloneVariable(entry: VariableModelEntry): VariableModelEntry {
  return {
    ...entry
  };
}

function toHandleSnapshot(handle: HandleTableSnapshot | null): number {
  return handle ? handle.handle : 0;
}

export class VariableModel {
  private readonly variablesByParent = new Map<number, VariableModelState>();
  private readonly variablesByHandle = new Map<number, VariableModelEntry>();

  public applyVariables(
    threadId: number,
    frameId: number,
    scopeKind: ScopeKind,
    parentHandle: number,
    response: VariablesBody,
    handleTable: HandleTable
  ): VariableModelState {
    assertNonNegativeInteger(threadId, 'threadId');
    assertNonNegativeInteger(frameId, 'frameId');
    assertNonNegativeInteger(parentHandle, 'parentHandle');
    if (!response.variables) {
      throw new VariableModelError('variables 响应必须携带 variables。');
    }

    const variables: Array<VariableModelEntry> = [];
    for (const variable of response.variables) {
      const handle = variable.canExpand && variable.variablesReference > 0
        ? handleTable.allocate({
            remoteReference: variable.variablesReference,
            threadId,
            frameId,
            kind: 'object',
            name: variable.name,
            parentHandle
          })
        : null;

      const entry: VariableModelEntry = {
        handle: toHandleSnapshot(handle),
        parentHandle,
        threadId,
        frameId,
        scopeKind,
        name: variable.name,
        value: variable.value,
        type: variable.type,
        variablesReference: variable.canExpand ? handle?.handle ?? 0 : 0,
        canExpand: variable.canExpand,
        namedVariables: variable.namedVariables,
        indexedVariables: variable.indexedVariables
      };
      variables.push(entry);
      if (entry.handle > 0) {
        this.variablesByHandle.set(entry.handle, entry);
      }
    }

    const state: VariableModelState = {
      threadId,
      frameId,
      scopeKind,
      parentHandle,
      variables
    };
    this.variablesByParent.set(parentHandle, state);
    return this.cloneState(state);
  }

  public getVariables(parentHandle: number): ReadonlyArray<VariableModelEntry> {
    assertNonNegativeInteger(parentHandle, 'parentHandle');
    return this.variablesByParent.get(parentHandle)?.variables ?? [];
  }

  public getVariable(handle: number): VariableModelEntry | null {
    assertNonNegativeInteger(handle, 'handle');
    const variable = this.variablesByHandle.get(handle);
    return variable ? cloneVariable(variable) : null;
  }

  public getParentHandle(handle: number): number | null {
    const variable = this.getVariable(handle);
    return variable ? variable.parentHandle : null;
  }

  public clear(): void {
    this.variablesByParent.clear();
    this.variablesByHandle.clear();
  }

  private cloneState(state: VariableModelState): VariableModelState {
    return {
      threadId: state.threadId,
      frameId: state.frameId,
      scopeKind: state.scopeKind,
      parentHandle: state.parentHandle,
      variables: state.variables.map((variable) => cloneVariable(variable))
    };
  }
}
