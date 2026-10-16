// Unit tests for src/lib/rules.ts (Plan 09b T4): npm run test:unit (tsc to CommonJS, then node --test).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { REPORT_MAX, body, listenClash, systemPortClash, templateError26k, templateSize } from '../../src/lib/rules.js';

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

test('the System page sees a port a UART listens on', () => {
  assert.equal(systemPortClash({ webPort: 4000, websocketPort: 6432 }, server(4000), null),
    'UART1 listens on TCP port 4000, which the web interface uses: choose another port.');
  assert.equal(systemPortClash({ webPort: 80, websocketPort: 23 }, null, server(23)),
    'UART2 listens on TCP port 23, which the WebSocket console uses: choose another port.');
  assert.equal(systemPortClash(sys, server(23), server(24)), null);
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
