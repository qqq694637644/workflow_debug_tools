import { type DapScope } from './dapProtocol.js';
import { type ScopeEntryBody, type ScopeKind, type ScopesBody } from './protocol.js';

export interface ScopeModelState {
  readonly threadId: number;
  readonly frameId: number;
  readonly scopes: ReadonlyArray<DapScope>;
}

export class ScopeModel {
  public clear(): void {
    // Stateless model: nothing to clear.
  }

  public applyScopes(threadId: number, response: ScopesBody): ScopeModelState {
    return {
      threadId,
      frameId: response.frameId,
      scopes: (response.scopes ?? []).map((scope) => this.toDapScope(scope))
    };
  }

  private toDapScope(scope: ScopeEntryBody): DapScope {
    const scopeKind = scope.kind;
    const name = typeof scope.name === 'string' ? scope.name : this.getDefaultScopeName(scopeKind);
    const variablesReference = typeof scope.variablesReference === 'number' ? scope.variablesReference : 0;

    return {
      name,
      variablesReference,
      expensive: false
    };
  }

  private getDefaultScopeName(scopeKind: ScopeKind): string {
    switch (scopeKind) {
      case 'Global':
        return '全局变量';
      case 'Captured':
        return '捕获变量';
      case 'Object':
        return '对象变量';
      case 'Argument':
        return '参数变量';
      case 'Local':
      default:
        return '局部变量';
    }
  }
}
