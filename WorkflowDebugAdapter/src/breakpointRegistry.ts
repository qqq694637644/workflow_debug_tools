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

  public applyBreakpoints(
    sourcePath: string,
    breakpoints: ReadonlyArray<SourceBreakpointInput>
  ): SetBreakpointsBody {
    const result = mapBreakpoints(
      this.sourceCatalog,
      sourcePath,
      breakpoints,
      this.createBreakpointId
    );

    this.clearSource(result.sourcePath);
    for (const state of result.states) {
      this.statesById.set(state.breakpointId, state);
    }

    return result.body;
  }

  public mergeValidatedState(validation: BreakpointValidatedBody): BreakpointSyncState | null {
    return mergeValidatedState(this.statesById, validation);
  }

  public getBreakpoints(sourcePath: string): ReadonlyArray<BreakpointSyncState> {
    const canonicalPath = this.sourceCatalog.resolveByPath(sourcePath)?.sourcePath ?? sourcePath.trim().replace(/\\/g, '/');
    return [...this.statesById.values()]
      .filter((state) => state.sourcePath === canonicalPath)
      .sort((left, right) => {
        if (left.line !== right.line) {
          return left.line - right.line;
        }

        if ((left.column ?? 0) !== (right.column ?? 0)) {
          return (left.column ?? 0) - (right.column ?? 0);
        }

        return left.breakpointId.localeCompare(right.breakpointId);
      })
      .map((state) => ({ ...state }));
  }

  public getBreakpoint(breakpointId: string): BreakpointSyncState | null {
    const state = this.statesById.get(breakpointId);
    return state ? { ...state } : null;
  }

  public clearSource(sourcePath: string): void {
    const canonicalPath = this.sourceCatalog.resolveByPath(sourcePath)?.sourcePath ?? sourcePath.trim().replace(/\\/g, '/');
    for (const [breakpointId, state] of this.statesById) {
      if (state.sourcePath === canonicalPath) {
        this.statesById.delete(breakpointId);
      }
    }
  }

  public clear(): void {
    this.statesById.clear();
  }
}
