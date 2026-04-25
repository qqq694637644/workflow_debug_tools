import { type DapVariable } from './dapProtocol.js';
import { type VariableEntryBody, type VariablesBody } from './protocol.js';

export interface VariableModelState {
  readonly threadId: number;
  readonly frameId: number;
  readonly variablesReference: number;
  readonly variables: ReadonlyArray<DapVariable>;
}

export class VariableModel {
  public clear(): void {
    // Stateless model: nothing to clear.
  }

  public applyVariables(threadId: number, response: VariablesBody): VariableModelState {
    return {
      threadId,
      frameId: response.frameId ?? 0,
      variablesReference: response.variablesReference,
      variables: (response.variables ?? []).map((variable) => this.toDapVariable(variable))
    };
  }

  private toDapVariable(variable: VariableEntryBody): DapVariable {
    const name = typeof variable.name === 'string' ? variable.name : '';
    const value = typeof variable.value === 'string' ? variable.value : '';
    const type = typeof variable.type === 'string' ? variable.type : undefined;
    const variablesReference = typeof variable.variablesReference === 'number' && variable.variablesReference > 0
      ? variable.variablesReference
      : 0;

    return {
      name,
      value,
      type,
      variablesReference
    };
  }
}
