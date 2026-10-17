// Unit tests for src/lib/edgeModel.ts: the whole-model checks Save runs (code review P2-2, P2-3, P2-4).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { MAX_DEVICES, blankNode, blankSlave, fromCsv, toCsv } from '../../src/lib/edge.js';
import { modelErrors, slaveDefs } from '../../src/lib/edgeModel.js';
import type { DeviceInfo, EdgeSlave, Values } from '../../src/lib/types.js';
import { formErrors } from '../../src/lib/validate.js';

const dev: DeviceInfo = {
  model: 'SBIOT-H750', firmware: '', hardware: '', portmax: 2, baudMin: 600, baudMax: 921600,
  io: { do: 2, di: 2, ai: 2, ao: 0 }, hostname: 'gw', wsDirection: 0, wsPort: 6432,
};
const IO_ADDR = 100;

/** A UART Modbus device with one point; names follow the index. */
function device(i: number, addr: string, map?: number): EdgeSlave {
  const s: EdgeSlave = { ...blankSlave(), name: `Device${i}`, addr };
  if (map !== undefined) (s.mapEn = true), (s.mapAddr = map);
  s.nodes = [{ ...blankNode(s), name: `node${i}` }];
  return s;
}

test('a valid model has no errors', () => {
  assert.equal(modelErrors(dev, [device(1, '1', 10), device(2, '2')], IO_ADDR).join(' | '), '');
});

test('an unmapped device moved onto a mapped slave ID is refused, in the dialog and at Save', () => {
  const d1 = device(1, '1', 10), d2 = device(2, '10');
  const dialog = formErrors(slaveDefs(dev, d2, [d1], IO_ADDR), d2 as unknown as Values);
  assert.match(dialog.addr ?? '', /Slave ID 10 is already answered for Device1/);
  assert.ok(modelErrors(dev, [d1, d2], IO_ADDR).length > 0);
});

test('two unmapped devices may share an address (different buses)', () => {
  const d1 = device(1, '1'), d2 = { ...device(2, '1'), uart: 2 };
  assert.equal(modelErrors(dev, [d1, d2], IO_ADDR).join(' | '), '');
});

test('empty device and point names are refused', () => {
  const d = device(1, '1');
  assert.ok(modelErrors(dev, [{ ...d, name: '' }], IO_ADDR).length > 0);
  assert.ok(modelErrors(dev, [{ ...d, nodes: [{ ...d.nodes[0], name: '' }] }], IO_ADDR).length > 0);
});

test('an empty or blank device address is refused', () => {
  assert.ok(modelErrors(dev, [device(1, '')], IO_ADDR).length > 0);
  assert.ok(modelErrors(dev, [device(1, '  ')], IO_ADDR).length > 0);
});

test('a point name used twice is refused, in one device and across two', () => {
  const d1 = device(1, '1'), d2 = device(2, '2');
  const twin = { ...d1.nodes[0], reg: 5 };
  assert.ok(modelErrors(dev, [{ ...d1, nodes: [d1.nodes[0], twin] }], IO_ADDR).length > 0);
  assert.ok(modelErrors(dev, [d1, { ...d2, nodes: [{ ...d2.nodes[0], name: d1.nodes[0].name }] }], IO_ADDR).length > 0);
});

test(`more than ${MAX_DEVICES} devices are refused at Save and on import`, () => {
  const many = (n: number) => Array.from({ length: n }, (_, i) => device(i + 1, String((i % 200) + 1)));
  assert.equal(modelErrors(dev, many(MAX_DEVICES), IO_ADDR).join(' | '), '');
  assert.ok(modelErrors(dev, many(MAX_DEVICES + 1), IO_ADDR).some((e) => e.includes(`limit is ${MAX_DEVICES}`)));
  const parsed = fromCsv(toCsv(many(MAX_DEVICES + 1)), dev);
  assert.ok(parsed.errors.some((e) => e.includes(`limit is ${MAX_DEVICES}`)), parsed.errors.join(' | '));
});
