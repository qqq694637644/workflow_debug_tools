import {
  type BreakpointSpec,
  type BreakpointValidatedBody,
  type SetBreakpointsBody
} from './protocol.js';
import { SourceCatalog } from './sourceMap.js';

// 本阶段仅支持“行断点”。
export interface SourceBreakpointInput {
  readonly line: number;
}

export interface BreakpointSyncState {
  readonly breakpointId: string;
  readonly requestedSourcePath: string;
  readonly sourcePath: string;
  readonly codeIndex: number;
  readonly line: number;
  readonly row: number;
  readonly verified: boolean | null;
  readonly reason?: string;
}

export interface BreakpointMappingResult {
  readonly requestedSourcePath: string;
  readonly sourcePath: string;
  readonly codeIndex: number;
  readonly body: SetBreakpointsBody;
  readonly states: ReadonlyArray<BreakpointSyncState>;
}

export class BreakpointMappingError extends Error {
  constructor(message: string) {
    super(message);
    this.name = 'BreakpointMappingError';
  }
}

export type BreakpointIdFactory = (fingerprint: string) => string;

export function createBreakpointIdFactory(prefix = 'wf-bp'): BreakpointIdFactory {
  let nextId = 1;
  const ids = new Map<string, string>();

  return (fingerprint: string) => {
    const existing = ids.get(fingerprint);
    if (existing) {
      return existing;
    }

    const breakpointId = `${prefix}-${nextId}`;
    nextId += 1;
    ids.set(fingerprint, breakpointId);
    return breakpointId;
  };
}

function createFingerprint(sourcePath: string, codeIndex: number, breakpoint: SourceBreakpointInput): string {
  return JSON.stringify({
    sourcePath,
    codeIndex,
    line: breakpoint.line
  });
}

export function mapBreakpoints(
  sourceCatalog: SourceCatalog,
  requestedSourcePath: string,
  breakpoints: ReadonlyArray<SourceBreakpointInput>,
  createBreakpointId: BreakpointIdFactory
): BreakpointMappingResult {
  const source = sourceCatalog.resolveByPath(requestedSourcePath);
  if (!source) {
    throw new BreakpointMappingError(`未找到源码路径 ${requestedSourcePath} 对应的 codeIndex。`);
  }

  const remoteBreakpoints: Array<BreakpointSpec> = [];
  const states: Array<BreakpointSyncState> = [];

  for (const breakpoint of breakpoints) {
    if (!Number.isInteger(breakpoint.line) || breakpoint.line <= 0) {
      throw new BreakpointMappingError('断点行号必须是从 1 开始的正整数。');
    }

    // DAP 的行号从 1 开始，而 Workflow 运行时使用的 row 从 0 开始。
    const row = breakpoint.line - 1;
    const breakpointId = createBreakpointId(createFingerprint(source.sourcePath, source.codeIndex, breakpoint));

    remoteBreakpoints.push({
      breakpointId,
      row
    });

    states.push({
      breakpointId,
      requestedSourcePath,
      sourcePath: source.sourcePath,
      codeIndex: source.codeIndex,
      line: breakpoint.line,
      row,
      verified: null
    });
  }

  return {
    requestedSourcePath,
    sourcePath: source.sourcePath,
    codeIndex: source.codeIndex,
    body: {
      sourcePath: source.sourcePath,
      codeIndex: source.codeIndex,
      breakpoints: remoteBreakpoints
    },
    states
  };
}

export function mergeValidatedState(
  statesById: Map<string, BreakpointSyncState>,
  validation: BreakpointValidatedBody
): BreakpointSyncState | null {
  const current = statesById.get(validation.breakpointId);
  if (!current) {
    return null;
  }

  const updated: BreakpointSyncState = {
    ...current,
    verified: validation.verified,
    reason: validation.reason
  };
  statesById.set(validation.breakpointId, updated);
  return updated;
}
