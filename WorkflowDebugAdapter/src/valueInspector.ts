import {
  type ScopeEntryBody,
  type ScopeKind,
  type ScopesBody,
  type VariableEntryBody,
  type VariablesBody
} from './protocol.js';

export interface InspectableVariableInput {
  readonly name: string;
  readonly type: string;
  readonly value: string;
  readonly children?: ReadonlyArray<InspectableVariableInput>;
}

export interface FrameScopeValuesInput {
  readonly local?: ReadonlyArray<InspectableVariableInput>;
  readonly argument?: ReadonlyArray<InspectableVariableInput>;
  readonly captured?: ReadonlyArray<InspectableVariableInput>;
  readonly global?: ReadonlyArray<InspectableVariableInput>;
}

interface VariableNode {
  reference: number;
  threadId: number;
  frameId: number;
  kind: ScopeKind;
  name: string;
  type: string;
  value: string;
  namedVariables: number;
  indexedVariables: number;
  children: Array<VariableNode>;
}

interface FrameSnapshot {
  threadId: number;
  frameId: number;
  roots: Record<'local' | 'argument' | 'captured' | 'global', VariableNode>;
}

export class ValueInspectorError extends Error {
  constructor(message: string) {
    super(message);
    this.name = 'ValueInspectorError';
  }
}

const scopeKinds: ReadonlyArray<'local' | 'argument' | 'captured' | 'global'> = [
  'local',
  'argument',
  'captured',
  'global'
];

function assertNonNegativeInteger(value: number, name: string): void {
  if (!Number.isInteger(value) || value < 0) {
    throw new ValueInspectorError(`${name} 必须是非负整数。`);
  }
}

function localizedScopeName(kind: ScopeKind): string {
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

function buildEmptyScopeNode(threadId: number, frameId: number, kind: ScopeKind): VariableNode {
  return {
    reference: 0,
    threadId,
    frameId,
    kind,
    name: localizedScopeName(kind),
    type: 'scope',
    value: '',
    namedVariables: 0,
    indexedVariables: 0,
    children: []
  };
}

function cloneVariableEntry(entry: VariableEntryBody): VariableEntryBody {
  return {
    ...entry
  };
}

function cloneScopeEntry(entry: ScopeEntryBody): ScopeEntryBody {
  return {
    ...entry
  };
}

function cloneVariablesBody(body: VariablesBody): VariablesBody {
  return {
    ...body,
    variables: body.variables?.map((variable) => cloneVariableEntry(variable))
  };
}

function cloneScopesBody(body: ScopesBody): ScopesBody {
  return {
    ...body,
    scopes: body.scopes?.map((scope) => cloneScopeEntry(scope))
  };
}

export class ValueInspector {
  private readonly framesByKey = new Map<string, FrameSnapshot>();
  private readonly nodesByReference = new Map<number, VariableNode>();
  private nextReference = 1;

  public captureFrameVariables(
    threadId: number,
    frameId: number,
    values: FrameScopeValuesInput = {}
  ): void {
    assertNonNegativeInteger(threadId, 'threadId');
    assertNonNegativeInteger(frameId, 'frameId');

    const snapshot: FrameSnapshot = {
      threadId,
      frameId,
      roots: {
        local: this.buildScopeNode(threadId, frameId, 'local', values.local ?? []),
        argument: this.buildScopeNode(threadId, frameId, 'argument', values.argument ?? []),
        captured: this.buildScopeNode(threadId, frameId, 'captured', values.captured ?? []),
        global: this.buildScopeNode(threadId, frameId, 'global', values.global ?? [])
      }
    };
    this.framesByKey.set(this.buildFrameKey(threadId, frameId), snapshot);
  }

  public createScopes(threadId: number, frameId: number): ScopesBody {
    assertNonNegativeInteger(threadId, 'threadId');
    assertNonNegativeInteger(frameId, 'frameId');

    const snapshot = this.framesByKey.get(this.buildFrameKey(threadId, frameId));
    const scopes = scopeKinds.map((kind) => {
      const root = snapshot?.roots[kind] ?? buildEmptyScopeNode(threadId, frameId, kind);
      return {
        name: localizedScopeName(kind),
        kind,
        variablesReference: root.reference,
        canExpand: root.reference > 0,
        namedVariables: root.namedVariables,
        indexedVariables: root.indexedVariables
      };
    });

    return cloneScopesBody({
      frameId,
      scopes
    });
  }

  public createVariables(
    threadId: number,
    frameId: number,
    scopeKind: ScopeKind,
    variablesReference: number
  ): VariablesBody {
    assertNonNegativeInteger(threadId, 'threadId');
    assertNonNegativeInteger(frameId, 'frameId');
    if (!Number.isInteger(variablesReference) || variablesReference < 0) {
      throw new ValueInspectorError('variablesReference 必须是非负整数。');
    }

    const node = variablesReference > 0 ? this.nodesByReference.get(variablesReference) ?? null : null;
    if (!node) {
      throw new ValueInspectorError('变量句柄不存在。');
    }

    if (node.threadId !== threadId || node.frameId !== frameId) {
      throw new ValueInspectorError('变量句柄不属于当前线程或当前帧。');
    }

    if (node.kind !== scopeKind && !(scopeKind === 'object' && node.kind === 'object')) {
      throw new ValueInspectorError('变量句柄类型与请求类型不一致。');
    }

    const variables = node.children.map((child) => this.toVariableEntry(child));
    return cloneVariablesBody({
      variablesReference,
      scopeKind,
      frameId,
      variables
    });
  }

  public hasFrame(threadId: number, frameId: number): boolean {
    assertNonNegativeInteger(threadId, 'threadId');
    assertNonNegativeInteger(frameId, 'frameId');
    return this.framesByKey.has(this.buildFrameKey(threadId, frameId));
  }

  public clearThread(threadId: number): void {
    assertNonNegativeInteger(threadId, 'threadId');
    for (const key of [...this.framesByKey.keys()]) {
      if (key.startsWith(`${threadId}:`)) {
        this.framesByKey.delete(key);
      }
    }
  }

  public clear(): void {
    this.framesByKey.clear();
    this.nodesByReference.clear();
    this.nextReference = 1;
  }

  private buildScopeNode(
    threadId: number,
    frameId: number,
    kind: 'local' | 'argument' | 'captured' | 'global',
    values: ReadonlyArray<InspectableVariableInput>
  ): VariableNode {
    const children = values.map((value) => this.buildVariableNode(threadId, frameId, value));
    const hasChildren = children.length > 0;
    const node: VariableNode = {
      reference: hasChildren ? this.nextReference++ : 0,
      threadId,
      frameId,
      kind,
      name: localizedScopeName(kind),
      type: 'scope',
      value: '',
      namedVariables: children.length,
      indexedVariables: 0,
      children
    };

    if (node.reference > 0) {
      this.nodesByReference.set(node.reference, node);
    }

    return node;
  }

  private buildVariableNode(
    threadId: number,
    frameId: number,
    input: InspectableVariableInput
  ): VariableNode {
    const children = (input.children ?? []).map((child) => this.buildVariableNode(threadId, frameId, child));
    const hasChildren = children.length > 0;
    const node: VariableNode = {
      reference: hasChildren ? this.nextReference++ : 0,
      threadId,
      frameId,
      kind: 'object',
      name: input.name,
      type: input.type,
      value: input.value,
      namedVariables: children.length,
      indexedVariables: 0,
      children
    };

    if (node.reference > 0) {
      this.nodesByReference.set(node.reference, node);
    }

    return node;
  }

  private toVariableEntry(node: VariableNode): VariableEntryBody {
    return {
      name: node.name,
      value: node.value,
      type: node.type,
      variablesReference: node.reference,
      canExpand: node.reference > 0,
      namedVariables: node.namedVariables,
      indexedVariables: node.indexedVariables
    };
  }

  private buildFrameKey(threadId: number, frameId: number): string {
    return `${threadId}:${frameId}`;
  }
}
