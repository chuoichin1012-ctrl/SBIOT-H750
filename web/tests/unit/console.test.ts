// Unit tests for src/lib/console.ts: the WebSocket page's lines, TX and RX told apart (the user, 2026-10-17).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { appendLines, lineKind } from '../../src/lib/console.js';

test('each frame line is told TX or RX by its prefix; anything else is plain text', () => {
  assert.equal(lineKind('TX 04 04 00 00 00 02 70 5E'), 'tx');
  assert.equal(lineKind('RX 04 04 04 41 C8 00 00'), 'rx');
  assert.equal(lineKind('connected to UART2'), 'text');
  assert.equal(lineKind('TXT'), 'text');
});

test('messages are split into lines; a line cut across two messages is joined; the oldest go past the limit', () => {
  let s = appendLines({ lines: [], partial: '' }, 'connected to UART2\nTX 01\n', 100);
  assert.equal(s.lines.join('|'), 'connected to UART2|TX 01');
  s = appendLines(s, 'RX 0', 100);
  assert.equal(s.partial, 'RX 0');
  s = appendLines(s, '2\n', 100);
  assert.equal(s.lines.join('|'), 'connected to UART2|TX 01|RX 02');
  s = appendLines(s, 'a\nb\nc\n', 3);
  assert.equal(s.lines.join('|'), 'a|b|c');
});
