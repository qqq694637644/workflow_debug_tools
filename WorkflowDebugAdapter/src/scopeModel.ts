import { type ScopeEntryBody, type ScopeKind, type ScopesBody } from './protocol.js';
import { type HandleTable, type HandleTableSnapshot } from './handleTable.js';

export interface ScopeModelEntry {
  readonly handle: number;
  readonly threadId: number;
  readonly frameId: number;
  readonly name: string;
  readonly kind: ScopeKind;
  readonly variablesReference: number;
  readonly canExpand: boolean;
  readonly namedVariables: number;
  readonly indexedVariables: number;
}

export interface ScopeModelState {
  readonly threadId: number;
  readonly frameId: number;
  readonly scopes: ReadonlyArray<ScopeModelEntry>;
}

export class ScopeModelError extends Error {
  constructor(message: string) {
    super(message);
    this.name = 'ScopeModelError';
  }
}

function assertNonNegativeInteger(value: number, name: string): void {
  if (!Number.isInteger(value) || value < 0) {
    throw new ScopeModelError(`${name} 必须是非负整数。`);
  }
}

function createLocalizedScopeName(kind: ScopeKind): string {
  switch (kind) {
    case 'local':
      return '局部';
    case 'argument':
      return '参数';
    case 'captured':
      return '捕获';
    case 'global':
      return '全局';
    case 'object':
      return '对象';
    default:
      return kind;
  }
}

function cloneScopeEntry(entry: ScopeModelEntry): ScopeModelEntry {
  return {
    ...entry
  };
}

function toHandleSnapshot(handle: HandleTableSnapshot | null): number {
  return handle ? handle.handle : 0;
}

export class ScopeModel {
  private readonly scopesByFrame = new Map<string, ScopeModelState>();

  public applyScopes(
    threadId: number,
    response: ScopesBody,
    handleTable: HandleTable
  ): ScopeModelState {
    assertNonNegativeInteger(threadId, 'threadId');
    assertNonNegativeInteger(response.frameId, 'frameId');
    if (!response.scopes) {
      throw new ScopeModelError('scopes 响应必须携带 scopes。');
    }

    const scopes: Array<ScopeModelEntry> = [];
    for (const scope of response.scopes) {
      const handle = scope.canExpand && scope.variablesReference > 0
        ? handleTable.allocate({
            remoteReference: scope.variablesReference,
            threadId,
            frameId: response.frameId,
            kind: scope.kind,
            name: scope.name,
            parentHandle: null
          })
        : null;

      const entry: ScopeModelEntry = {
        handle: toHandleSnapshot(handle),
        threadId,
        frameId: response.frameId,
        name: scope.name,
        kind: scope.kind,
        variablesReference: scope.canExpand ? handle?.handle ?? 0 : 0,
        canExpand: scope.canExpand,
        namedVariables: scope.namedVariables,
        indexedVariables: scope.indexedVariables
      };
      scopes.push(entry);
    }

    const state: ScopeModelState = {
      threadId,
      frameId: response.frameId,
      scopes
    };
    this.scopesByFrame.set(this.buildKey(threadId, response.frameId), state);
    return this.cloneState(state);
  }

  public getScopes(threadId: number, frameId: number): ReadonlyArray<ScopeModelEntry> {
    assertNonNegativeInteger(threadId, 'threadId');
    assertNonNegativeInteger(frameId, 'frameId');
    return this.scopesByFrame.get(this.buildKey(threadId, frameId))?.scopes ?? [];
  }

  public getScope(threadId: number, frameId: number, kind: ScopeKind): ScopeModelEntry | null {
    const scope = this.getScopes(threadId, frameId).find((entry) => entry.kind === kind);
    return scope ? cloneScopeEntry(scope) : null;
  }

  public clear(): void {
    this.scopesByFrame.clear();
  }

  public getLocalizedScopeName(kind: ScopeKind): string {
    return createLocalizedScopeName(kind);
  }

  private buildKey(threadId: number, frameId: number): string {
    return `${threadId}:${frameId}`;
  }

  private cloneState(state: ScopeModelState): ScopeModelState {
    return {
      threadId: state.threadId,
      frameId: state.frameId,
      scopes: state.scopes.map((scope) => cloneScopeEntry(scope))
    };
  }
}
