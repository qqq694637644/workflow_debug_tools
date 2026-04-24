import { type ExceptionBody, type StackFrameBody, type StackTraceBody } from './protocol.js';

export interface StackFrameModel {
  readonly threadId: number;
  readonly frameId: number;
  readonly callStackIndex: number;
  readonly name: string;
  readonly sourceId: number;
  readonly sourcePath: string;
  readonly row: number;
  readonly line: number;
  readonly column: number;
  readonly canRequestVariables: boolean;
}

export interface StackTraceState {
  readonly threadId: number;
  readonly startFrame: number;
  readonly levels: number;
  readonly totalFrames: number;
  readonly frames: ReadonlyArray<StackFrameModel>;
}

export class StackModelError extends Error {
  constructor(message: string) {
    super(message);
    this.name = 'StackModelError';
  }
}

interface MutableThreadTrace {
  readonly threadId: number;
  readonly startFrame: number;
  readonly levels: number;
  readonly totalFrames: number;
  readonly frames: Array<StackFrameModel>;
  readonly framesById: Map<number, StackFrameModel>;
}

function assertNonNegativeInteger(value: number, name: string): void {
  if (!Number.isInteger(value) || value < 0) {
    throw new StackModelError(`${name} 必须是非负整数。`);
  }
}

function assertKnownOrUnknownSourceId(value: number, name: string): void {
  if (!Number.isInteger(value) || value < -1) {
    throw new StackModelError(`${name} 必须是大于等于 -1 的整数。`);
  }
}

function cloneFrame(frame: StackFrameModel): StackFrameModel {
  return {
    ...frame
  };
}

function cloneState(trace: MutableThreadTrace): StackTraceState {
  return {
    threadId: trace.threadId,
    startFrame: trace.startFrame,
    levels: trace.levels,
    totalFrames: trace.totalFrames,
    frames: trace.frames.map((frame) => cloneFrame(frame))
  };
}

function normalizeFrames(
  threadId: number,
  frames: ReadonlyArray<StackFrameBody>
): { readonly frames: Array<StackFrameModel>; readonly framesById: Map<number, StackFrameModel> } {
  const normalizedFrames: Array<StackFrameModel> = [];
  const framesById = new Map<number, StackFrameModel>();

  for (const frame of frames) {
    assertNonNegativeInteger(frame.threadId, 'frame.threadId');
    assertNonNegativeInteger(frame.frameId, 'frame.frameId');
    assertNonNegativeInteger(frame.callStackIndex, 'frame.callStackIndex');
    assertKnownOrUnknownSourceId(frame.sourceId, 'frame.sourceId');
    if (frame.threadId !== threadId) {
      throw new StackModelError('栈帧线程标识与调用栈线程标识不一致。');
    }
    if (frame.frameId !== frame.callStackIndex) {
      throw new StackModelError('frameId 必须与 callStackIndex 保持一致。');
    }
    if (frame.line !== frame.row + 1) {
      throw new StackModelError('栈帧行号必须与 row 保持一致。');
    }
    if (!Number.isInteger(frame.column) || frame.column < 1) {
      throw new StackModelError('栈帧列号必须是从 1 开始的正整数。');
    }
    if (typeof frame.functionName !== 'string' || !frame.functionName.trim()) {
      throw new StackModelError('functionName 不能为空。');
    }
    if (typeof frame.sourcePath !== 'string' || !frame.sourcePath.trim()) {
      throw new StackModelError('sourcePath 不能为空。');
    }
    if (framesById.has(frame.frameId)) {
      throw new StackModelError(`frameId ${frame.frameId} 重复。`);
    }

    const model: StackFrameModel = {
      threadId: frame.threadId,
      frameId: frame.frameId,
      callStackIndex: frame.callStackIndex,
      name: frame.functionName,
      sourceId: frame.sourceId,
      sourcePath: frame.sourcePath,
      row: frame.row,
      line: frame.line,
      column: frame.column,
      canRequestVariables: frame.canRequestVariables
    };

    normalizedFrames.push(model);
    framesById.set(model.frameId, model);
  }

  return {
    frames: normalizedFrames,
    framesById
  };
}

export class StackModel {
  private readonly tracesByThread = new Map<number, MutableThreadTrace>();

  public applyStackTrace(trace: StackTraceBody): StackTraceState {
    assertNonNegativeInteger(trace.threadId, 'threadId');
    assertNonNegativeInteger(trace.startFrame, 'startFrame');
    assertNonNegativeInteger(trace.levels, 'levels');

    if (!trace.frames) {
      throw new StackModelError('stackTrace 响应必须携带 frames。');
    }

    const { frames, framesById } = normalizeFrames(trace.threadId, trace.frames);

    const totalFrames = trace.totalFrames ?? frames.length;
    const stored: MutableThreadTrace = {
      threadId: trace.threadId,
      startFrame: trace.startFrame,
      levels: trace.levels,
      totalFrames,
      frames,
      framesById
    };
    this.tracesByThread.set(trace.threadId, stored);
    return cloneState(stored);
  }

  public applyException(exception: ExceptionBody): StackTraceState {
    if (exception.callStack.length === 0) {
      throw new StackModelError('异常调用栈不能为空。');
    }

    const threadId = exception.callStack[0].threadId;
    const { frames, framesById } = normalizeFrames(threadId, exception.callStack);
    const stored: MutableThreadTrace = {
      threadId,
      startFrame: 0,
      levels: frames.length,
      totalFrames: frames.length,
      frames,
      framesById
    };
    this.tracesByThread.set(threadId, stored);
    return cloneState(stored);
  }

  public getStackTrace(threadId: number): StackTraceState | null {
    assertNonNegativeInteger(threadId, 'threadId');
    const trace = this.tracesByThread.get(threadId);
    return trace ? cloneState(trace) : null;
  }

  public getFrames(threadId: number): ReadonlyArray<StackFrameModel> {
    return this.getStackTrace(threadId)?.frames ?? [];
  }

  public getFrame(frameId: number, threadId?: number): StackFrameModel | null {
    assertNonNegativeInteger(frameId, 'frameId');

    if (threadId !== undefined) {
      assertNonNegativeInteger(threadId, 'threadId');
      const trace = this.tracesByThread.get(threadId);
      const frame = trace?.framesById.get(frameId) ?? null;
      return frame ? cloneFrame(frame) : null;
    }

    let found: StackFrameModel | null = null;
    for (const trace of this.tracesByThread.values()) {
      const frame = trace.framesById.get(frameId);
      if (!frame) {
        continue;
      }

      if (found) {
        return null;
      }

      found = frame;
    }

    return found ? cloneFrame(found) : null;
  }

  public clear(threadId?: number): void {
    if (threadId === undefined) {
      this.tracesByThread.clear();
      return;
    }

    assertNonNegativeInteger(threadId, 'threadId');
    this.tracesByThread.delete(threadId);
  }
}
