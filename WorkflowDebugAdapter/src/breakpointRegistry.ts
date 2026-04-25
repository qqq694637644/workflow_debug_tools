import { type BreakpointValidatedBody, type SetBreakpointsBody } from './protocol.js';
import {
  createBreakpointIdFactory,
  mapBreakpoints,
  mergeValidatedState,
  type BreakpointSyncState,
  type SourceBreakpointInput
} from './breakpointMapper.js';
import { SourceCatalog } from './sourceMap.js';

export class BreakpointRegistry {
  private readonly createBreakpointId = createBreakpointIdFactory();
  private readonly statesById = new Map<string, BreakpointSyncState>();

  constructor(private readonly sourceCatalog: SourceCatalog) {}

  public applyBreakpoints(sourcePath: string, breakpoints: ReadonlyArray<SourceBreakpointInput>): SetBreakpointsBody {
    const result = mapBreakpoints(this.sourceCatalog, sourcePath, breakpoints, this.createBreakpointId);

    this.clearSource(result.sourcePath);
    for (const state of result.states) {
      this.statesById.set(state.breakpointId, state);
    }

    return result.body;
  }

  public mergeValidatedState(validation: BreakpointValidatedBody): BreakpointSyncState | null {
    return mergeValidatedState(this.statesById, validation);
  }

  public registerRemoteBreakpoints(body: SetBreakpointsBody): ReadonlyArray<BreakpointValidatedBody> {
    const canonicalPath = this.resolveCanonicalPath(body.sourcePath);
    const source = this.sourceCatalog.resolveByPath(canonicalPath);
    const validations: Array<BreakpointValidatedBody> = [];

    if (source && source.codeIndex !== body.codeIndex) {
      throw new Error(`源码路径 ${body.sourcePath} 的 codeIndex 不匹配。`);
    }

    this.clearSource(canonicalPath);

    for (const breakpoint of body.breakpoints) {
      const verified = this.sourceCatalog.hasRow(body.codeIndex, breakpoint.row);
      const reason = verified ? undefined : `源码 ${body.sourcePath} 不包含第 ${breakpoint.row + 1} 行。`;

      const state: BreakpointSyncState = {
        breakpointId: breakpoint.breakpointId,
        requestedSourcePath: body.sourcePath,
        sourcePath: canonicalPath,
        codeIndex: body.codeIndex,
        line: breakpoint.row + 1,
        row: breakpoint.row,
        verified,
        reason
      };

      this.statesById.set(breakpoint.breakpointId, state);
      validations.push({
        breakpointId: breakpoint.breakpointId,
        verified,
        reason
      });
    }

    return validations;
  }

  public getBreakpoints(sourcePath: string): ReadonlyArray<BreakpointSyncState> {
    const canonicalPath = this.resolveCanonicalPath(sourcePath);
    return [...this.statesById.values()]
      .filter((state) => state.sourcePath === canonicalPath)
      .sort((left, right) => {
        if (left.line !== right.line) {
          return left.line - right.line;
        }

        return left.breakpointId.localeCompare(right.breakpointId);
      })
      .map((state) => ({ ...state }));
  }

  public getBreakpoint(breakpointId: string): BreakpointSyncState | null {
    const state = this.statesById.get(breakpointId);
    return state ? { ...state } : null;
  }

  public findBreakpoint(codeIndex: number, row: number): BreakpointSyncState | null {
    for (const state of this.statesById.values()) {
      if (state.codeIndex === codeIndex && state.row === row) {
        return { ...state };
      }
    }

    return null;
  }

  public hasBreakpoint(codeIndex: number, row: number): boolean {
    const state = this.findBreakpoint(codeIndex, row);
    return state !== null && state.verified === true;
  }

  public clearSource(sourcePath: string): void {
    const canonicalPath = this.resolveCanonicalPath(sourcePath);
    for (const [breakpointId, state] of this.statesById) {
      if (state.sourcePath === canonicalPath) {
        this.statesById.delete(breakpointId);
      }
    }
  }

  public clear(): void {
    this.statesById.clear();
  }

  private resolveCanonicalPath(sourcePath: string): string {
    return this.sourceCatalog.resolveByPath(sourcePath)?.sourcePath ?? sourcePath.trim().replace(/\\/g, '/');
  }
}
