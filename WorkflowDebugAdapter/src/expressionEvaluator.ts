export type EvaluationPrimitiveValue = string | number | boolean | null | undefined;

export interface EvaluationValue {
  readonly kind: 'primitive' | 'object';
  readonly value: EvaluationPrimitiveValue;
  readonly display: string;
  readonly type: string;
  readonly variablesReference: number;
  readonly namedVariables: number;
  readonly indexedVariables: number;
}

export interface ExpressionResolver {
  resolveIdentifier(name: string): Promise<EvaluationValue | null>;
  resolveMember(target: EvaluationValue, propertyName: string): Promise<EvaluationValue | null>;
}

export class ExpressionEvaluationError extends Error {
  constructor(message: string) {
    super(message);
    this.name = 'ExpressionEvaluationError';
  }
}

interface Token {
  readonly kind: 'identifier' | 'number' | 'string' | 'operator' | 'punctuation' | 'eof';
  readonly text: string;
  readonly position: number;
  readonly value?: string | number;
}

interface LiteralNode {
  readonly kind: 'literal';
  readonly value: EvaluationPrimitiveValue;
}

interface IdentifierNode {
  readonly kind: 'identifier';
  readonly name: string;
}

interface UnaryNode {
  readonly kind: 'unary';
  readonly operator: '!' | '+' | '-';
  readonly operand: ExpressionNode;
}

interface BinaryNode {
  readonly kind: 'binary';
  readonly operator: '||' | '&&' | '==' | '!=' | '===' | '!==' | '<' | '<=' | '>' | '>=' | '+' | '-' | '*' | '/' | '%';
  readonly left: ExpressionNode;
  readonly right: ExpressionNode;
}

interface MemberNode {
  readonly kind: 'member';
  readonly object: ExpressionNode;
  readonly property: string;
}

interface ComputedMemberNode {
  readonly kind: 'computedMember';
  readonly object: ExpressionNode;
  readonly property: ExpressionNode;
}

interface GroupNode {
  readonly kind: 'group';
  readonly expression: ExpressionNode;
}

type ExpressionNode =
  | LiteralNode
  | IdentifierNode
  | UnaryNode
  | BinaryNode
  | MemberNode
  | ComputedMemberNode
  | GroupNode;

function createPrimitiveValue(value: EvaluationPrimitiveValue, type?: string, display?: string): EvaluationValue {
  const resolvedType = type ?? inferPrimitiveType(value);
  return {
    kind: 'primitive',
    value,
    display: display ?? formatPrimitiveDisplay(value),
    type: resolvedType,
    variablesReference: 0,
    namedVariables: 0,
    indexedVariables: 0
  };
}

function inferPrimitiveType(value: EvaluationPrimitiveValue): string {
  if (value === null) {
    return 'null';
  }

  if (value === undefined) {
    return 'undefined';
  }

  return typeof value;
}

function formatPrimitiveDisplay(value: EvaluationPrimitiveValue): string {
  if (value === null) {
    return 'null';
  }

  if (value === undefined) {
    return 'undefined';
  }

  return String(value);
}

function isDigit(text: string): boolean {
  return text >= '0' && text <= '9';
}

function isIdentifierStart(text: string): boolean {
  return /[$A-Z_a-z]/.test(text);
}

function isIdentifierPart(text: string): boolean {
  return /[$0-9A-Z_a-z]/.test(text);
}

function isWhitespace(text: string): boolean {
  return /\s/u.test(text);
}

function toStringLike(value: EvaluationValue): string {
  if (value.kind === 'primitive') {
    return formatPrimitiveDisplay(value.value);
  }

  return value.display;
}

function toNumberLike(value: EvaluationValue): number {
  if (value.kind !== 'primitive') {
    throw new ExpressionEvaluationError('对象不能参与算术运算。');
  }

  switch (typeof value.value) {
    case 'number':
      return value.value;
    case 'boolean':
      return value.value ? 1 : 0;
    case 'string': {
      const parsed = Number(value.value);
      if (Number.isNaN(parsed)) {
        throw new ExpressionEvaluationError(`无法把字符串 ${value.display} 转换为数字。`);
      }
      return parsed;
    }
    case 'undefined':
      return Number.NaN;
    default:
      return value.value === null ? 0 : Number(value.value);
  }
}

function isTruthy(value: EvaluationValue): boolean {
  if (value.kind !== 'primitive') {
    return true;
  }

  return Boolean(value.value);
}

function isSameReference(left: EvaluationValue, right: EvaluationValue): boolean {
  return left.kind === 'object'
    && right.kind === 'object'
    && left.variablesReference === right.variablesReference;
}

function strictEquals(left: EvaluationValue, right: EvaluationValue): boolean {
  if (left.kind === 'object' || right.kind === 'object') {
    return isSameReference(left, right);
  }

  return left.value === right.value;
}

function looseEquals(left: EvaluationValue, right: EvaluationValue): boolean {
  if (left.kind === 'object' || right.kind === 'object') {
    if (left.kind === 'object' && right.kind === 'object') {
      return isSameReference(left, right);
    }

    if (left.kind === 'object') {
      return right.value === null || right.value === undefined;
    }

    return left.value === null || left.value === undefined;
  }

  return left.value == right.value;
}

class ExpressionTokenizer {
  private index = 0;

  constructor(private readonly expression: string) {}

  public nextToken(): Token {
    this.skipWhitespace();
    if (this.index >= this.expression.length) {
      return {
        kind: 'eof',
        text: '',
        position: this.index
      };
    }

    const start = this.index;
    const current = this.expression[this.index];
    const next = this.expression[this.index + 1] ?? '';

    if (isIdentifierStart(current)) {
      this.index += 1;
      while (this.index < this.expression.length && isIdentifierPart(this.expression[this.index])) {
        this.index += 1;
      }

      return {
        kind: 'identifier',
        text: this.expression.slice(start, this.index),
        position: start
      };
    }

    if (current === '.' && isDigit(next)) {
      return this.readNumberToken();
    }

    if (isDigit(current)) {
      return this.readNumberToken();
    }

    if (current === '\'' || current === '"') {
      return this.readStringToken(current);
    }

    const operator = this.readOperator();
    if (operator) {
      return {
        kind: operator === '(' || operator === ')' || operator === '[' || operator === ']' || operator === '.'
          ? 'punctuation'
          : 'operator',
        text: operator,
        position: start
      };
    }

    throw new ExpressionEvaluationError(`无法识别的表达式字符：${current}。`);
  }

  private skipWhitespace(): void {
    while (this.index < this.expression.length && isWhitespace(this.expression[this.index])) {
      this.index += 1;
    }
  }

  private readOperator(): string | null {
    const candidates = ['===', '!==', '&&', '||', '==', '!=', '<=', '>=', '(', ')', '[', ']', '.', '+', '-', '*', '/', '%', '<', '>', '!', '='];
    for (const candidate of candidates) {
      if (this.expression.startsWith(candidate, this.index)) {
        this.index += candidate.length;
        return candidate;
      }
    }

    return null;
  }

  private readNumberToken(): Token {
    const start = this.index;
    if (this.expression[this.index] === '0') {
      const prefix = this.expression[this.index + 1];
      if (prefix === 'x' || prefix === 'X' || prefix === 'b' || prefix === 'B' || prefix === 'o' || prefix === 'O') {
        this.index += 2;
        const digitPattern = prefix === 'x' || prefix === 'X'
          ? /^[0-9A-Fa-f]+$/u
          : prefix === 'b' || prefix === 'B'
            ? /^[01]+$/u
            : /^[0-7]+$/u;
        const digitsStart = this.index;
        while (this.index < this.expression.length && /[0-9A-Fa-f]/u.test(this.expression[this.index])) {
          this.index += 1;
        }
        const digits = this.expression.slice(digitsStart, this.index);
        if (!digitPattern.test(digits)) {
          throw new ExpressionEvaluationError(`无效的数字字面量：${this.expression.slice(start, this.index)}。`);
        }
        return {
          kind: 'number',
          text: this.expression.slice(start, this.index),
          value: Number(this.expression.slice(start, this.index)),
          position: start
        };
      }
    }

    if (this.expression[this.index] === '.') {
      this.index += 1;
      while (this.index < this.expression.length && isDigit(this.expression[this.index])) {
        this.index += 1;
      }
    }
    else {
      while (this.index < this.expression.length && isDigit(this.expression[this.index])) {
        this.index += 1;
      }
    }

    if (this.expression[this.index] === '.' && this.expression[this.index - 1] !== '.') {
      this.index += 1;
      while (this.index < this.expression.length && isDigit(this.expression[this.index])) {
        this.index += 1;
      }
    }

    if (this.expression[this.index] === 'e' || this.expression[this.index] === 'E') {
      const exponentStart = this.index;
      this.index += 1;
      if (this.expression[this.index] === '+' || this.expression[this.index] === '-') {
        this.index += 1;
      }

      const digitsStart = this.index;
      while (this.index < this.expression.length && isDigit(this.expression[this.index])) {
        this.index += 1;
      }

      if (digitsStart === this.index) {
        throw new ExpressionEvaluationError(`无效的数字字面量：${this.expression.slice(start, this.index)}。`);
      }

      if (exponentStart >= this.index) {
        throw new ExpressionEvaluationError(`无效的数字字面量：${this.expression.slice(start, this.index)}。`);
      }
    }

    const text = this.expression.slice(start, this.index);
    const value = Number(text);
    if (Number.isNaN(value)) {
      throw new ExpressionEvaluationError(`无效的数字字面量：${text}。`);
    }

    return {
      kind: 'number',
      text,
      value,
      position: start
    };
  }

  private readStringToken(quote: '\'' | '"'): Token {
    const start = this.index;
    this.index += 1;
    let result = '';

    while (this.index < this.expression.length) {
      const current = this.expression[this.index];
      if (current === quote) {
        this.index += 1;
        return {
          kind: 'string',
          text: this.expression.slice(start, this.index),
          value: result,
          position: start
        };
      }

      if (current === '\\') {
        this.index += 1;
        if (this.index >= this.expression.length) {
          throw new ExpressionEvaluationError('字符串字面量缺少结束引号。');
        }

        const escaped = this.expression[this.index];
        this.index += 1;
        switch (escaped) {
          case '\\':
          case '\'':
          case '"':
            result += escaped;
            break;
          case 'b':
            result += '\b';
            break;
          case 'f':
            result += '\f';
            break;
          case 'n':
            result += '\n';
            break;
          case 'r':
            result += '\r';
            break;
          case 't':
            result += '\t';
            break;
          case 'v':
            result += '\v';
            break;
          case '0':
            result += '\0';
            break;
          case 'x': {
            const hex = this.expression.slice(this.index, this.index + 2);
            if (!/^[0-9A-Fa-f]{2}$/u.test(hex)) {
              throw new ExpressionEvaluationError('字符串字面量中的十六进制转义无效。');
            }
            result += String.fromCharCode(Number.parseInt(hex, 16));
            this.index += 2;
            break;
          }
          case 'u': {
            if (this.expression[this.index] === '{') {
              this.index += 1;
              const codePointStart = this.index;
              while (this.index < this.expression.length && this.expression[this.index] !== '}') {
                this.index += 1;
              }
              if (this.index >= this.expression.length) {
                throw new ExpressionEvaluationError('字符串字面量中的 Unicode 转义缺少结束括号。');
              }
              const codePointText = this.expression.slice(codePointStart, this.index);
              if (!/^[0-9A-Fa-f]+$/u.test(codePointText)) {
                throw new ExpressionEvaluationError('字符串字面量中的 Unicode 码点无效。');
              }
              const codePoint = Number.parseInt(codePointText, 16);
              result += String.fromCodePoint(codePoint);
              this.index += 1;
              break;
            }

            const unicode = this.expression.slice(this.index, this.index + 4);
            if (!/^[0-9A-Fa-f]{4}$/u.test(unicode)) {
              throw new ExpressionEvaluationError('字符串字面量中的 Unicode 转义无效。');
            }
            result += String.fromCharCode(Number.parseInt(unicode, 16));
            this.index += 4;
            break;
          }
          default:
            result += escaped;
            break;
        }
        continue;
      }

      if (current === '\r' || current === '\n') {
        throw new ExpressionEvaluationError('字符串字面量缺少结束引号。');
      }

      result += current;
      this.index += 1;
    }

    throw new ExpressionEvaluationError('字符串字面量缺少结束引号。');
  }
}

class ExpressionParser {
  private readonly tokenizer: ExpressionTokenizer;
  private current: Token;

  constructor(expression: string) {
    this.tokenizer = new ExpressionTokenizer(expression);
    this.current = this.tokenizer.nextToken();
  }

  public parse(): ExpressionNode {
    const expression = this.parseLogicalOr();
    this.expect('eof');
    return expression;
  }

  private parseLogicalOr(): ExpressionNode {
    let node = this.parseLogicalAnd();
    while (this.matchOperator('||')) {
      node = {
        kind: 'binary',
        operator: '||',
        left: node,
        right: this.parseLogicalAnd()
      };
    }
    return node;
  }

  private parseLogicalAnd(): ExpressionNode {
    let node = this.parseEquality();
    while (this.matchOperator('&&')) {
      node = {
        kind: 'binary',
        operator: '&&',
        left: node,
        right: this.parseEquality()
      };
    }
    return node;
  }

  private parseEquality(): ExpressionNode {
    let node = this.parseComparison();
    while (true) {
      if (this.matchOperator('===')) {
        node = {
          kind: 'binary',
          operator: '===',
          left: node,
          right: this.parseComparison()
        };
        continue;
      }

      if (this.matchOperator('!==')) {
        node = {
          kind: 'binary',
          operator: '!==',
          left: node,
          right: this.parseComparison()
        };
        continue;
      }

      if (this.matchOperator('==')) {
        node = {
          kind: 'binary',
          operator: '==',
          left: node,
          right: this.parseComparison()
        };
        continue;
      }

      if (this.matchOperator('!=')) {
        node = {
          kind: 'binary',
          operator: '!=',
          left: node,
          right: this.parseComparison()
        };
        continue;
      }

      break;
    }
    return node;
  }

  private parseComparison(): ExpressionNode {
    let node = this.parseAdditive();
    while (true) {
      if (this.matchOperator('<')) {
        node = {
          kind: 'binary',
          operator: '<',
          left: node,
          right: this.parseAdditive()
        };
        continue;
      }

      if (this.matchOperator('<=')) {
        node = {
          kind: 'binary',
          operator: '<=',
          left: node,
          right: this.parseAdditive()
        };
        continue;
      }

      if (this.matchOperator('>')) {
        node = {
          kind: 'binary',
          operator: '>',
          left: node,
          right: this.parseAdditive()
        };
        continue;
      }

      if (this.matchOperator('>=')) {
        node = {
          kind: 'binary',
          operator: '>=',
          left: node,
          right: this.parseAdditive()
        };
        continue;
      }

      break;
    }
    return node;
  }

  private parseAdditive(): ExpressionNode {
    let node = this.parseMultiplicative();
    while (true) {
      if (this.matchOperator('+')) {
        node = {
          kind: 'binary',
          operator: '+',
          left: node,
          right: this.parseMultiplicative()
        };
        continue;
      }

      if (this.matchOperator('-')) {
        node = {
          kind: 'binary',
          operator: '-',
          left: node,
          right: this.parseMultiplicative()
        };
        continue;
      }

      break;
    }
    return node;
  }

  private parseMultiplicative(): ExpressionNode {
    let node = this.parseUnary();
    while (true) {
      if (this.matchOperator('*')) {
        node = {
          kind: 'binary',
          operator: '*',
          left: node,
          right: this.parseUnary()
        };
        continue;
      }

      if (this.matchOperator('/')) {
        node = {
          kind: 'binary',
          operator: '/',
          left: node,
          right: this.parseUnary()
        };
        continue;
      }

      if (this.matchOperator('%')) {
        node = {
          kind: 'binary',
          operator: '%',
          left: node,
          right: this.parseUnary()
        };
        continue;
      }

      break;
    }
    return node;
  }

  private parseUnary(): ExpressionNode {
    if (this.matchOperator('!')) {
      return {
        kind: 'unary',
        operator: '!',
        operand: this.parseUnary()
      };
    }

    if (this.matchOperator('+')) {
      return {
        kind: 'unary',
        operator: '+',
        operand: this.parseUnary()
      };
    }

    if (this.matchOperator('-')) {
      return {
        kind: 'unary',
        operator: '-',
        operand: this.parseUnary()
      };
    }

    return this.parsePostfix();
  }

  private parsePostfix(): ExpressionNode {
    let node = this.parsePrimary();
    while (true) {
      if (this.matchPunctuation('.')) {
        const token = this.expectIdentifier();
        node = {
          kind: 'member',
          object: node,
          property: token.text
        };
        continue;
      }

      if (this.matchPunctuation('[')) {
        const property = this.parseLogicalOr();
        this.expectPunctuation(']');
        node = {
          kind: 'computedMember',
          object: node,
          property
        };
        continue;
      }

      break;
    }

    return node;
  }

  private parsePrimary(): ExpressionNode {
    if (this.current.kind === 'identifier') {
      const token = this.current;
      this.advance();
      switch (token.text) {
        case 'true':
          return {
            kind: 'literal',
            value: true
          };
        case 'false':
          return {
            kind: 'literal',
            value: false
          };
        case 'null':
          return {
            kind: 'literal',
            value: null
          };
        case 'undefined':
          return {
            kind: 'literal',
            value: undefined
          };
        default:
          return {
            kind: 'identifier',
            name: token.text
          };
      }
    }

    if (this.current.kind === 'number') {
      const token = this.current;
      this.advance();
      return {
        kind: 'literal',
        value: token.value as number
      };
    }

    if (this.current.kind === 'string') {
      const token = this.current;
      this.advance();
      return {
        kind: 'literal',
        value: token.value as string
      };
    }

    if (this.matchPunctuation('(')) {
      const expression = this.parseLogicalOr();
      this.expectPunctuation(')');
      return {
        kind: 'group',
        expression
      };
    }

    throw new ExpressionEvaluationError(`无法解析表达式起始位置的符号：${this.current.text || '结束符'}。`);
  }

  private matchOperator(text: string): boolean {
    if (this.current.kind === 'operator' && this.current.text === text) {
      this.advance();
      return true;
    }

    return false;
  }

  private matchPunctuation(text: string): boolean {
    if (this.current.kind === 'punctuation' && this.current.text === text) {
      this.advance();
      return true;
    }

    return false;
  }

  private expect(kind: Token['kind']): Token {
    if (this.current.kind !== kind) {
      throw new ExpressionEvaluationError(`表达式在位置 ${this.current.position} 处缺少 ${kind}。`);
    }

    const token = this.current;
    this.advance();
    return token;
  }

  private expectPunctuation(text: string): Token {
    if (this.current.kind !== 'punctuation' || this.current.text !== text) {
      throw new ExpressionEvaluationError(`表达式在位置 ${this.current.position} 处缺少符号 ${text}。`);
    }

    const token = this.current;
    this.advance();
    return token;
  }

  private expectIdentifier(): Token {
    if (this.current.kind !== 'identifier') {
      throw new ExpressionEvaluationError(`表达式在位置 ${this.current.position} 处缺少标识符。`);
    }

    const token = this.current;
    this.advance();
    return token;
  }

  private advance(): void {
    this.current = this.tokenizer.nextToken();
  }
}

async function evaluateNode(node: ExpressionNode, resolver: ExpressionResolver): Promise<EvaluationValue> {
  switch (node.kind) {
    case 'literal':
      return createPrimitiveValue(node.value);
    case 'identifier': {
      const resolved = await resolver.resolveIdentifier(node.name);
      if (!resolved) {
        throw new ExpressionEvaluationError(`未找到变量 ${node.name}。`);
      }
      return resolved;
    }
    case 'group':
      return evaluateNode(node.expression, resolver);
    case 'member': {
      const target = await evaluateNode(node.object, resolver);
      const resolved = await resolver.resolveMember(target, node.property);
      if (!resolved) {
        throw new ExpressionEvaluationError(`无法解析属性 ${node.property}。`);
      }
      return resolved;
    }
    case 'computedMember': {
      const target = await evaluateNode(node.object, resolver);
      const property = await evaluateNode(node.property, resolver);
      if (property.kind !== 'primitive') {
        throw new ExpressionEvaluationError('下标表达式必须先计算为原始值。');
      }

      const resolved = await resolver.resolveMember(target, formatPrimitiveDisplay(property.value));
      if (!resolved) {
        throw new ExpressionEvaluationError(`无法解析属性 ${formatPrimitiveDisplay(property.value)}。`);
      }
      return resolved;
    }
    case 'unary': {
      const operand = await evaluateNode(node.operand, resolver);
      switch (node.operator) {
        case '!':
          return createPrimitiveValue(!isTruthy(operand), 'boolean');
        case '+':
          return createPrimitiveValue(toNumberLike(operand), 'number');
        case '-':
          return createPrimitiveValue(-toNumberLike(operand), 'number');
        default:
          throw new ExpressionEvaluationError('不支持的一元运算符。');
      }
    }
    case 'binary':
      return evaluateBinaryNode(node, resolver);
    default:
      throw new ExpressionEvaluationError('无法计算该表达式。');
  }
}

async function evaluateBinaryNode(node: BinaryNode, resolver: ExpressionResolver): Promise<EvaluationValue> {
  switch (node.operator) {
    case '&&': {
      const left = await evaluateNode(node.left, resolver);
      if (!isTruthy(left)) {
        return left;
      }
      return evaluateNode(node.right, resolver);
    }
    case '||': {
      const left = await evaluateNode(node.left, resolver);
      if (isTruthy(left)) {
        return left;
      }
      return evaluateNode(node.right, resolver);
    }
    case '+': {
      const left = await evaluateNode(node.left, resolver);
      const right = await evaluateNode(node.right, resolver);
      if (left.kind === 'object' || right.kind === 'object') {
        if (left.kind === 'primitive' && typeof left.value === 'string') {
          return createPrimitiveValue(`${toStringLike(left)}${toStringLike(right)}`, 'string');
        }

        if (right.kind === 'primitive' && typeof right.value === 'string') {
          return createPrimitiveValue(`${toStringLike(left)}${toStringLike(right)}`, 'string');
        }

        throw new ExpressionEvaluationError('对象不能参与加法运算。');
      }

      if (typeof left.value === 'string' || typeof right.value === 'string') {
        return createPrimitiveValue(`${toStringLike(left)}${toStringLike(right)}`, 'string');
      }

      return createPrimitiveValue((left.value as number) + (right.value as number), 'number');
    }
    case '-':
      return createPrimitiveValue(toNumberLike(await evaluateNode(node.left, resolver)) - toNumberLike(await evaluateNode(node.right, resolver)), 'number');
    case '*':
      return createPrimitiveValue(toNumberLike(await evaluateNode(node.left, resolver)) * toNumberLike(await evaluateNode(node.right, resolver)), 'number');
    case '/':
      return createPrimitiveValue(toNumberLike(await evaluateNode(node.left, resolver)) / toNumberLike(await evaluateNode(node.right, resolver)), 'number');
    case '%':
      return createPrimitiveValue(toNumberLike(await evaluateNode(node.left, resolver)) % toNumberLike(await evaluateNode(node.right, resolver)), 'number');
    case '<':
      return createPrimitiveValue(await compareOrder(node, resolver, (left, right) => left < right), 'boolean');
    case '<=':
      return createPrimitiveValue(await compareOrder(node, resolver, (left, right) => left <= right), 'boolean');
    case '>':
      return createPrimitiveValue(await compareOrder(node, resolver, (left, right) => left > right), 'boolean');
    case '>=':
      return createPrimitiveValue(await compareOrder(node, resolver, (left, right) => left >= right), 'boolean');
    case '===':
      return createPrimitiveValue(strictEquals(await evaluateNode(node.left, resolver), await evaluateNode(node.right, resolver)), 'boolean');
    case '!==':
      return createPrimitiveValue(!strictEquals(await evaluateNode(node.left, resolver), await evaluateNode(node.right, resolver)), 'boolean');
    case '==':
      return createPrimitiveValue(looseEquals(await evaluateNode(node.left, resolver), await evaluateNode(node.right, resolver)), 'boolean');
    case '!=':
      return createPrimitiveValue(!looseEquals(await evaluateNode(node.left, resolver), await evaluateNode(node.right, resolver)), 'boolean');
    default:
      throw new ExpressionEvaluationError('不支持的二元运算符。');
  }
}

function compareOrder(
  node: BinaryNode,
  resolver: ExpressionResolver,
  compare: (left: number | string, right: number | string) => boolean
): Promise<boolean> {
  return (async () => {
    const left = await evaluateNode(node.left, resolver);
    const right = await evaluateNode(node.right, resolver);
    if (left.kind === 'object' || right.kind === 'object') {
      throw new ExpressionEvaluationError('对象不能参与大小比较。');
    }

    const leftValue = typeof left.value === 'string' ? left.value : toNumberLike(left);
    const rightValue = typeof right.value === 'string' ? right.value : toNumberLike(right);
    return compare(leftValue, rightValue);
  })();
}

export async function evaluateExpression(expression: string, resolver: ExpressionResolver): Promise<EvaluationValue> {
  const trimmed = expression.trim();
  if (trimmed.length === 0) {
    throw new ExpressionEvaluationError('表达式不能为空。');
  }

  const parser = new ExpressionParser(trimmed);
  const tree = parser.parse();
  return evaluateNode(tree, resolver);
}
