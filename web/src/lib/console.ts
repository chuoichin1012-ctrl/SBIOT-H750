// The WebSocket page's received text as lines (the user, 2026-10-17): the console sends one line per UART frame,
// "TX ..." or "RX ...", so the page colours each direction. Pure, unit-tested (tests/unit/console.test.ts).

export type LineKind = 'tx' | 'rx' | 'text';

export const lineKind = (line: string): LineKind => (line.startsWith('TX ') ? 'tx' : line.startsWith('RX ') ? 'rx' : 'text');

export interface Lines {
  lines: string[];
  /** Text after the last newline: the start of a line the next message ends. */
  partial: string;
}

/** `chunk` appended: whole lines kept, at most `max` of them (the oldest go). */
export function appendLines(s: Lines, chunk: string, max: number): Lines {
  const parts = (s.partial + chunk).split('\n');
  const partial = parts.pop() ?? '';
  const lines = [...s.lines, ...parts];
  return { lines: lines.length > max ? lines.slice(lines.length - max) : lines, partial };
}
