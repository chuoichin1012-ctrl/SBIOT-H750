// Unit tests for src/lib/rules.ts (Plan 09b T4): npm run test:unit (tsc to CommonJS, then node --test).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { EDGE_BODY_MAX, REPORT_MAX, body, edgeBodyError, listenClash, systemPortClash, templateError26k, templateSize } from '../../src/lib/rules.js';
import { reportModeError, templateError } from '../../src/lib/edge.js';
import { convertData, fieldError } from '../../src/lib/validate.js';

const server = (port: number) => ({ workMode: 3, listenPort: port });
const sys = { webPort: 80, websocketPort: 6432 };

test('two UARTs on one listen port clash, with the firmware loader message', () => {
  const m = 'UART1 and UART2 both listen on TCP port 4000: give each its own port.';
  assert.equal(listenClash(1, server(4000), server(4000), sys), m);
  assert.equal(listenClash(2, server(4000), server(4000), sys), m);
  assert.equal(listenClash(1, server(4000), server(4001), sys), null);
});

test('a UART on the web or WebSocket port clashes', () => {
  assert.equal(listenClash(1, server(80), null, sys), 'UART1 listens on TCP port 80, which the web interface uses: choose another port.');
  assert.equal(listenClash(2, server(6432), null, sys), 'UART2 listens on TCP port 6432, which the WebSocket console uses: choose another port.');
});

test('only a TCP server listens', () => {
  assert.equal(listenClash(1, { workMode: 1, listenPort: 80 }, server(80), sys), null);
  assert.equal(listenClash(1, server(4000), { workMode: 0, listenPort: 4000 }, sys), null);
  assert.equal(listenClash(1, server(4000), null, null), null);
});

test('the System page sees a port a UART listens on, as that field\'s error', () => {
  assert.equal(systemPortClash({ webPort: 4000, websocketPort: 6432 }, server(4000), null).webPort,
    'UART1 listens on TCP port 4000, which the web interface uses: choose another port.');
  assert.equal(systemPortClash({ webPort: 80, websocketPort: 23 }, null, server(23)).websocketPort,
    'UART2 listens on TCP port 23, which the WebSocket console uses: choose another port.');
  assert.equal(Object.keys(systemPortClash(sys, server(23), server(24))).length, 0);
});

test('the template size is edge_tmpl_size: literal bytes + 242 per slot + 1', () => {
  // {"t":<slot>,"k":"const"} printed minified: {"t": (5) and ,"k":"const"} (13), 18 literal bytes, one slot.
  assert.equal(templateSize('{ "t" : "t", "k": "const" }', ['t']), 18 + 242 + 1);
  // system fields are slots; strings that name nothing stay literal; nested values count
  assert.equal(templateSize('{"a":["sys_sn",{"b":"t"}],"c":"x"}', ['t']), '{"a":[,{"b":}],"c":"x"}'.length + 2 * 242 + 1);
  // keys are never slots
  assert.equal(templateSize('{"t":1}', ['t']), '{"t":1}'.length + 1);
  // UTF-8 counts in bytes
  assert.equal(templateSize('{"é":"t"}', ['t']), new TextEncoder().encode('{"é":}').length + 242 + 1);
  assert.equal(templateSize('[1]', []), null);
  assert.equal(templateSize('not json', []), null);
});

test('a template over 26 624 bytes is refused, at the limit it is not', () => {
  const names = Array.from({ length: 120 }, (_, i) => `p${i}`);
  const tmpl = (n: number) => JSON.stringify(Object.fromEntries(names.slice(0, n).map((p) => [p, p])));
  let n = 1;
  while ((templateSize(tmpl(n + 1), names) ?? 0) <= REPORT_MAX) n++;
  assert.equal(templateError26k(tmpl(n), names), null);
  assert.match(templateError26k(tmpl(n + 1), names) ?? '', /needs too many values/);
  assert.equal(REPORT_MAX, 26624);
});

test('bodies are minified JSON: no whitespace outside strings', () => {
  assert.equal(body({ a: 1, b: [1, 2], c: { d: 'x y' } }), '{"a":1,"b":[1,2],"c":{"d":"x y"}}');
  assert.equal(body([{ topic: 'a/b' }]), '[{"topic":"a/b"}]');
});

test('a text limit counts UTF-8 bytes, as the firmware buffers do', () => {
  const def = { id: 'username', label: 'User name', kind: 'text', maxLength: 16, required: true } as const;
  assert.equal(fieldError(def, { username: 'é'.repeat(8) }), null);  // 16 bytes
  assert.match(fieldError(def, { username: 'é'.repeat(9) }) ?? '', /16 bytes/);  // 18 bytes, 9 characters
  assert.equal(fieldError(def, { username: 'a'.repeat(16) }), null);
  assert.match(fieldError(def, { username: 'é'.repeat(8) + ' ' }) ?? '', /16 bytes/);  // 17 bytes as sent, untrimmed
});

test('the Edge model is refused over 96 000 bytes, counted as the body the gateway receives (Plan 09d)', () => {
  assert.equal(EDGE_BODY_MAX, 96000);
  const slaves = (name: string) => [{ name, nodes: [] }];
  const fixed = body(slaves('')).length;
  assert.equal(edgeBodyError(slaves('a'.repeat(96000 - fixed))), null);
  assert.equal(edgeBodyError(slaves('a'.repeat(96001 - fixed))),
    'The devices and points take 96 001 bytes; the gateway takes at most 96 000. Remove points or shorten names and formulas.');
  // UTF-8 bytes, not characters: "é" is 2 bytes.
  assert.ok(edgeBodyError(slaves('é'.repeat((96000 - fixed) / 2 + 1))) !== null);
});

test('a data field follows its HEX/ASCII format switch, and back (the user, 2026-10-17)', () => {
  assert.equal(convertData('5342494F54', false), 'SBIOT');
  assert.equal(convertData('53 42 49 4f 54', false), 'SBIOT');
  assert.equal(convertData('SBIOT', true), '5342494F54');
  assert.equal(convertData(convertData('www.sbiot.vn', true)!, false), 'www.sbiot.vn');
  assert.equal(convertData('', true), '');
});

test('data that was never hex is left as typed', () => {
  assert.equal(convertData('not hex', false), 'not hex');  // switched to ASCII from an invalid HEX field
});

test('bytes that are not printable text cannot become ASCII: null, the switch is refused (Codex)', () => {
  assert.equal(convertData('7E01FF', false), null);  // as ASCII it would send six characters, not three bytes
  assert.equal(convertData('C328', false), null);    // not UTF-8
});

test('a round trip keeps every byte, a BOM too (Codex)', () => {
  for (const hex of ['EFBBBF41', '5342494F54', 'E1BBA9']) assert.equal(convertData(convertData(hex, false)!, true), hex);
});

test('a template nested past 16 levels is refused, as the firmware refuses it (Plan 10c)', () => {
  const nest = (d: number) => '{"a":'.repeat(d) + '"node1"' + '}'.repeat(d);
  assert.equal(templateError(nest(16), ['node1']), null);
  assert.match(templateError(nest(17), ['node1']) ?? '', /16 levels/);
});

test('the WebSocket console on the web port clashes while the console is on (Plan 10c, Codex Minor 11)', () => {
  assert.equal(systemPortClash({ webPort: 80, websocketPort: 80, websocketTarget: 3 }, null, null).websocketPort,
    'The web interface uses TCP port 80: choose another port for the WebSocket console.');
  assert.equal(systemPortClash({ webPort: 80, websocketPort: 80, websocketTarget: 0 }, null, null).websocketPort, undefined);
});

test('reporting only on change is allowed when a point reports on change (Plan 10c, Codex Minor 13)', () => {
  const v = { reporting: 1, periodic: false, scheduled: false };
  assert.match(reportModeError(v, false) ?? '', /periodic or scheduled/);
  assert.equal(reportModeError(v, true), null);
  assert.equal(reportModeError({ ...v, periodic: true }, false), null);
});
