import { type StackFrameBody, type StackTraceBody } from './protocol.js';
import { type FrameScopeValuesInput } from './valueInspector.js';

export interface StackInspectorFrameInput {
  readonly callStackIndex: number;
  readonly functionName: string;
  readonly sourceId: number;
  readonly sourcePath: string;
  readonly row: number;
  readonly column?: number;
  readonly variables?: FrameScopeValuesInput;
}

export interface StackInspectorSnapshot {
  readonly threadId: number;
  readonly totalFrames: number;
  readonly frames: ReadonlyArray<StackFrameBody>;
}

export class StackInspectorError extends Error {
  constructor(message: string) {
    super(message);
    this.name = 'StackInspectorError';
  }
}

interface MutableThreadStack {
  frames: Array<StackFrameBody>;
  framesById: Map<number, StackFrameBody>;
}

function assertNonNegativeInteger(value: number, name: string): void {
  if (!Number.isInteger(value) || value < 0) {
    throw new StackInspectorError(`${name} 必须是非负整数。`);
  }
}

function assertPositiveText(value: string, name: string): string {
  const text = value.trim();
  if (!text) {
    throw new StackInspectorError(`${name} 不能为空。`);
  }

  return text;
}

function normalizeColumn(column: number | undefined): number {
  if (column === undefined) {
    return 1;
  }

  if (!Number.isInteger(column) || column < 0) {
    throw new StackInspectorError('column 必须是非负整数。');
  }

  return column + 1;
}

function cloneFrame(frame: StackFrameBody): StackFrameBody {
  return {
    ...frame
  };
}

export class StackInspector {
  private readonly stacksByThread = new Map<number, MutableThreadStack>();

  public captureStack(
    threadId: number,
    frames: ReadonlyArray<StackInspectorFrameInput>
  ): StackInspectorSnapshot {
    assertNonNegativeInteger(threadId, 'threadId');

    const normalizedFrames: Array<StackFrameBody> = [...frames]
      .map((frame) => {
        assertNonNegativeInteger(frame.callStackIndex, 'callStackIndex');
        assertNonNegativeInteger(frame.sourceId, 'sourceId');
        const functionName = assertPositiveText(frame.functionName, 'functionName');
        const sourcePath = assertPositiveText(frame.sourcePath, 'sourcePath');
        const row = frame.row;
        if (!Number.isInteger(row) || row < 0) {
          throw new StackInspectorError('row 必须是非负整数。');
        }

        const line = row + 1;
        const column = normalizeColumn(frame.column);
        return {
          threadId,
          frameId: frame.callStackIndex,
          callStackIndex: frame.callStackIndex,
          functionName,
          sourceId: frame.sourceId,
          sourcePath,
          row,
          line,
          column,
          canRequestVariables: true
        };
      })
      .sort((left, right) => left.callStackIndex - right.callStackIndex);

    const seen = new Set<number>();
    for (const frame of normalizedFrames) {
      if (seen.has(frame.callStackIndex)) {
        throw new StackInspectorError(`callStackIndex ${frame.callStackIndex} 重复。`);
      }

      seen.add(frame.callStackIndex);
    }

    // 内部按调用顺序整理，展示和请求变量时再把最新帧放到最前面。
    const topFirstFrames = [...normalizedFrames].reverse();
    const framesById = new Map<number, StackFrameBody>();
    for (const frame of topFirstFrames) {
      framesById.set(frame.frameId, frame);
    }

    this.stacksByThread.set(threadId, {
      frames: topFirstFrames,
      framesById
    });

    return this.createSnapshot(threadId, topFirstFrames);
  }

  public capturePoint(
    threadId: number,
    frame: StackInspectorFrameInput
  ): StackInspectorSnapshot {
    return this.captureStack(threadId, [frame]);
  }

  public getSnapshot(threadId: number): StackInspectorSnapshot | null {
    const stored = this.stacksByThread.get(threadId);
    return stored ? this.createSnapshot(threadId, stored.frames) : null;
  }

  public getFrame(threadId: number, frameId: number): StackFrameBody | null {
    assertNonNegativeInteger(threadId, 'threadId');
    assertNonNegativeInteger(frameId, 'frameId');

    const stored = this.stacksByThread.get(threadId);
    const frame = stored?.framesById.get(frameId) ?? null;
    return frame ? cloneFrame(frame) : null;
  }

  public createStackTrace(
    threadId: number,
    startFrame: number,
    levels: number
  ): StackTraceBody {
    assertNonNegativeInteger(threadId, 'threadId');
    assertNonNegativeInteger(startFrame, 'startFrame');
    assertNonNegativeInteger(levels, 'levels');

    const snapshot = this.getSnapshot(threadId);
    if (!snapshot) {
      throw new StackInspectorError(`线程 ${threadId} 没有可用的调用栈。`);
    }

    const frames = snapshot.frames.slice(startFrame, startFrame + levels);
    return {
      threadId,
      startFrame,
      levels,
      totalFrames: snapshot.totalFrames,
      frames
    };
  }

  public clearThread(threadId: number): void {
    assertNonNegativeInteger(threadId, 'threadId');
    this.stacksByThread.delete(threadId);
  }

  public clear(): void {
    this.stacksByThread.clear();
  }

  private createSnapshot(threadId: number, frames: ReadonlyArray<StackFrameBody>): StackInspectorSnapshot {
    return {
      threadId,
      totalFrames: frames.length,
      frames: frames.map((frame) => cloneFrame(frame))
    };
  }
}
