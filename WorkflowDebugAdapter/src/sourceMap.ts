import { type PathMappingRule, type SourceMapEntry } from './protocol.js';

export interface SourceCatalogEntry {
  readonly codeIndex: number;
  readonly sourcePath: string;
  readonly rows: ReadonlyArray<number>;
}

interface MutableSourceCatalogEntry {
  codeIndex: number;
  sourcePath: string;
  rows: Set<number>;
}

export class SourceCatalogError extends Error {
  constructor(message: string) {
    super(message);
    this.name = 'SourceCatalogError';
  }
}

function normalizePathKey(sourcePath: string): string {
  const trimmed = sourcePath.trim();
  if (!trimmed) {
    throw new SourceCatalogError('源码路径不能为空。');
  }

  // 只做最小化归一化，避免把远端路径语义误改掉。
  let normalized = trimmed.replace(/\\/g, '/');
  if (/^[A-Za-z]:\//.test(normalized)) {
    normalized = normalized[0].toLowerCase() + normalized.slice(1);
  }

  while (normalized.length > 1 && normalized.endsWith('/')) {
    normalized = normalized.slice(0, -1);
  }

  return normalized;
}

function snapshotEntry(entry: MutableSourceCatalogEntry): SourceCatalogEntry {
  return {
    codeIndex: entry.codeIndex,
    sourcePath: entry.sourcePath,
    rows: [...entry.rows].sort((left, right) => left - right)
  };
}

export class SourceCatalog {
  private readonly entriesByCodeIndex = new Map<number, MutableSourceCatalogEntry>();
  private readonly entriesByPath = new Map<string, MutableSourceCatalogEntry>();
  private readonly aliases = new Map<string, string>();
  private entryCount = 0;

  public clear(): void {
    this.entriesByCodeIndex.clear();
    this.entriesByPath.clear();
    this.aliases.clear();
    this.entryCount = 0;
  }

  public registerSource(entry: SourceMapEntry): void {
    if (!Number.isInteger(entry.codeIndex) || entry.codeIndex < 0) {
      throw new SourceCatalogError('codeIndex 必须是非负整数。');
    }

    if (!Number.isInteger(entry.row) || entry.row < 0) {
      throw new SourceCatalogError('row 必须是非负整数。');
    }

    const normalizedPath = normalizePathKey(entry.sourcePath);
    const existingByCodeIndex = this.entriesByCodeIndex.get(entry.codeIndex);
    const existingByPath = this.entriesByPath.get(normalizedPath);

    if (existingByCodeIndex && normalizePathKey(existingByCodeIndex.sourcePath) !== normalizedPath) {
      throw new SourceCatalogError(`codeIndex ${entry.codeIndex} 已绑定到不同的源码路径。`);
    }

    if (existingByPath && existingByPath.codeIndex !== entry.codeIndex) {
      throw new SourceCatalogError(`源码路径 ${entry.sourcePath} 已绑定到不同的 codeIndex。`);
    }

    const record = existingByCodeIndex ?? existingByPath ?? {
      codeIndex: entry.codeIndex,
      sourcePath: entry.sourcePath,
      rows: new Set<number>()
    };

    const beforeSize = record.rows.size;
    record.rows.add(entry.row);
    if (record.rows.size !== beforeSize) {
      this.entryCount += 1;
    }

    this.entriesByCodeIndex.set(entry.codeIndex, record);
    this.entriesByPath.set(normalizedPath, record);
  }

  public registerSources(entries: ReadonlyArray<SourceMapEntry>): void {
    for (const entry of entries) {
      this.registerSource(entry);
    }
  }

  public registerPathMapping(rule: PathMappingRule): void {
    const localPath = normalizePathKey(rule.localPath);
    const remotePath = normalizePathKey(rule.remotePath);
    if (localPath === remotePath) {
      return;
    }

    const existing = this.aliases.get(localPath);
    if (existing && existing !== remotePath) {
      throw new SourceCatalogError(`路径映射冲突：${rule.localPath} 已经映射到其他目标路径。`);
    }

    this.aliases.set(localPath, remotePath);
  }

  public registerPathMappings(rules: ReadonlyArray<PathMappingRule>): void {
    for (const rule of rules) {
      this.registerPathMapping(rule);
    }
  }

  public resolveByPath(sourcePath: string): SourceCatalogEntry | null {
    const canonicalPath = this.resolveCanonicalPath(sourcePath);
    const entry = this.entriesByPath.get(canonicalPath);
    return entry ? snapshotEntry(entry) : null;
  }

  public resolveByCodeIndex(codeIndex: number): SourceCatalogEntry | null {
    const entry = this.entriesByCodeIndex.get(codeIndex);
    return entry ? snapshotEntry(entry) : null;
  }

  public resolveCodeIndex(sourcePath: string): number | null {
    return this.resolveByPath(sourcePath)?.codeIndex ?? null;
  }

  public resolveSourcePath(codeIndex: number): string | null {
    return this.resolveByCodeIndex(codeIndex)?.sourcePath ?? null;
  }

  public findRow(codeIndex: number, row: number): SourceMapEntry | null {
    const entry = this.entriesByCodeIndex.get(codeIndex);
    if (!entry || !entry.rows.has(row)) {
      return null;
    }

    return {
      codeIndex: entry.codeIndex,
      sourcePath: entry.sourcePath,
      row
    };
  }

  public hasRow(codeIndex: number, row: number): boolean {
    return this.findRow(codeIndex, row) !== null;
  }

  public getEntryCount(): number {
    return this.entryCount;
  }

  private resolveCanonicalPath(sourcePath: string): string {
    let current = normalizePathKey(sourcePath);
    const visited = new Set<string>();

    while (this.aliases.has(current)) {
      if (visited.has(current)) {
        throw new SourceCatalogError(`路径映射存在循环：${sourcePath}。`);
      }

      visited.add(current);
      current = this.aliases.get(current) as string;
    }

    return current;
  }
}
