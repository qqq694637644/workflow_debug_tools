import { type ScopeKind } from './protocol.js';

export interface HandleRecord {
  readonly handle: number;
  readonly remoteReference: number;
  readonly threadId: number;
  readonly frameId: number;
  readonly kind: ScopeKind;
  readonly name: string;
  readonly parentHandle: number | null;
}

export class HandleTableError extends Error {
  constructor(message: string) {
    super(message);
    this.name = 'HandleTableError';
  }
}

export interface HandleTableSnapshot {
  readonly handle: number;
  readonly remoteReference: number;
  readonly threadId: number;
  readonly frameId: number;
  readonly kind: ScopeKind;
  readonly name: string;
  readonly parentHandle: number | null;
}

function cloneRecord(record: HandleRecord): HandleTableSnapshot {
  return {
    ...record
  };
}

function assertPositiveReference(remoteReference: number): void {
  if (!Number.isInteger(remoteReference) || remoteReference <= 0) {
    throw new HandleTableError('远程句柄必须是正整数。');
  }
}

function assertNonNegativeInteger(value: number, name: string): void {
  if (!Number.isInteger(value) || value < 0) {
    throw new HandleTableError(`${name} 必须是非负整数。`);
  }
}

function assertText(value: string, name: string): string {
  const text = value.trim();
  if (!text) {
    throw new HandleTableError(`${name} 不能为空。`);
  }

  return text;
}

export class HandleTable {
  private readonly recordsByHandle = new Map<number, HandleRecord>();
  private readonly handlesByRemoteReference = new Map<number, number>();
  private nextHandle = 1;

  public allocate(record: Omit<HandleRecord, 'handle'>): HandleRecord {
    assertPositiveReference(record.remoteReference);
    assertNonNegativeInteger(record.threadId, 'threadId');
    assertNonNegativeInteger(record.frameId, 'frameId');
    const name = assertText(record.name, 'name');

    const existingHandle = this.handlesByRemoteReference.get(record.remoteReference);
    if (existingHandle !== undefined) {
      const existing = this.recordsByHandle.get(existingHandle);
      if (!existing) {
        throw new HandleTableError('句柄表内部状态已损坏。');
      }

      return existing;
    }

    const handle = this.nextHandle;
    this.nextHandle += 1;
    const created: HandleRecord = {
      handle,
      remoteReference: record.remoteReference,
      threadId: record.threadId,
      frameId: record.frameId,
      kind: record.kind,
      name,
      parentHandle: record.parentHandle
    };
    this.recordsByHandle.set(handle, created);
    this.handlesByRemoteReference.set(record.remoteReference, handle);
    return created;
  }

  public resolve(handle: number): HandleTableSnapshot | null {
    assertNonNegativeInteger(handle, 'handle');
    const record = this.recordsByHandle.get(handle);
    return record ? cloneRecord(record) : null;
  }

  public resolveRemoteReference(remoteReference: number): HandleTableSnapshot | null {
    assertPositiveReference(remoteReference);
    const handle = this.handlesByRemoteReference.get(remoteReference);
    if (handle === undefined) {
      return null;
    }

    return this.resolve(handle);
  }

  public clear(): void {
    this.recordsByHandle.clear();
    this.handlesByRemoteReference.clear();
    this.nextHandle = 1;
  }
}
