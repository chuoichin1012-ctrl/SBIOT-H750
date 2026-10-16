// The two node modules the unit tests use (no @types/node in this project).
declare module 'node:test' {
  export function test(name: string, fn: () => void | Promise<void>): void;
}
declare module 'node:assert/strict' {
  const assert: {
    equal(actual: unknown, expected: unknown, message?: string): void;
    match(actual: string, re: RegExp, message?: string): void;
    ok(value: unknown, message?: string): void;
  };
  export default assert;
}
