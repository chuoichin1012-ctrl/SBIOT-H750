// Unit tests for src/lib/edgeModel.ts: the whole-model checks Save runs (code review P2-2, P2-3, P2-4).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { MAX_DEVICES, allNodeNames, blankLinkage, blankNode, blankSlave, fromCsv, normalizeSlaves, toCsv } from '../../src/lib/edge.js';
import { ioAddressClash, linkageCondition, linkageDefs, linkageErrors, linkageOutput, modelErrors, slaveDefs } from '../../src/lib/edgeModel.js';
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

// Review 3 (main@26573da), P2-1: the IO page's Modbus address must not take a slave ID the Edge model maps a
// device to: unit 10 / FC5 would then switch DO1 instead of PumpDevice's coil.
test('the IO address may not move onto an ID the Edge model maps', () => {
  const io: EdgeSlave = { ...blankSlave(), name: 'SBIOT_IO', source: 'io' };
  io.nodes = [];
  const pump = { ...device(1, '1', 10), name: 'PumpDevice' };
  const slaves = [io, pump];
  assert.equal(modelErrors(dev, slaves, 100).join(' | '), '', 'the model is valid with the IO at 100');
  assert.match(ioAddressClash(dev, slaves, 10) ?? '', /PumpDevice/);
  assert.equal(ioAddressClash(dev, slaves, 101), null, 'a free ID is fine');
  assert.equal(ioAddressClash(dev, slaves, 100), null, 'today\'s address is fine');
  // No baseline to go stale (Codex): a clash at the address chosen is refused whenever it is chosen, even back.
  const clashing = [io, { ...pump, mapAddr: 100 }];
  assert.match(ioAddressClash(dev, clashing, 100) ?? '', /PumpDevice/, 'an existing clash at 100 is refused too');
  assert.equal(ioAddressClash(dev, clashing, 101), null);
});

// Review 3, P2-2: a CSV import that drops a watched point leaves an active event the gateway skips at boot.
test('an active event that names a point no longer in the model blocks Save', () => {
  const d = { ...device(1, '1'), nodes: [{ ...device(1, '1').nodes[0], name: 'Temperature' }] };
  const ev = { ...blankLinkage([]), name: 'Overheat', enable: true, point: 'Temperature', output: 'DO1' };
  assert.equal(linkageErrors(dev, [d], [ev]).join(' | '), '', 'valid while Temperature exists');
  const imported = [{ ...d, nodes: [{ ...d.nodes[0], name: 'Pump1' }] }];
  const errs = linkageErrors(dev, imported, [ev]);
  assert.ok(errs.length === 1 && errs[0].startsWith('Overheat: point'), errs.join(' | '));
  assert.equal(linkageErrors(dev, imported, [{ ...ev, enable: false }]).join(' | '), '', 'a disabled event does not block');
  assert.ok(linkageErrors(dev, imported, [{ ...ev, point: 'Pump1', output: 'DO9' }]).some((e) => e.startsWith('Overheat: output')),
    'an output that does not exist');
  // The remedy the page names, "turn off the event", must pass the dialog (Codex): an inactive event needs no point.
  const off = { ...ev, enable: false };
  const dialog = formErrors(linkageDefs(off, allNodeNames(imported), [], ['DO1', 'DO2']), off as unknown as Values);
  assert.equal(Object.keys(dialog).join(','), '', 'unticking Active passes Done');
});

test('a stored Edge model with missing fields is read as the firmware reads it, so every dialog opens (Edit point crashed on no ioPoint)', () => {
  const [s] = normalizeSlaves([{ name: 'plc', addr: '5', uart: 1, poll: 1000, nodes: [{ name: 't', reg: 10, dtype: 'int16', onChange: true }] }]);
  assert.equal(s.source, 'uart');  // edge_dev_from_json: anything but eth/io/virtual is a bus device
  assert.equal(s.protocol, 'modbus');
  assert.equal(s.mapEn, false);
  const [n] = s.nodes;
  assert.equal(n.ioPoint, '');
  assert.equal(n.di, '');
  assert.equal(n.formula, '');
  assert.equal(n.fc, 0);
  assert.equal(n.onChange, true);  // what is there is kept
  assert.equal(n.dtype, 'int16');
  assert.equal(normalizeSlaves([{ name: 'x', nodes: [{ name: 'a' }] }])[0].nodes[0].dtype, 'uint16');  // edge_dtype's default
  assert.equal(normalizeSlaves(null as unknown as []).length, 0);
});

test('a linkage row shows its condition with the thresholds and its output with the action (the user: a table)', () => {
  const e = { ...blankLinkage([]), point: 't', output: 'DO2', lower: 50, upper: 60, action: 2 };
  assert.equal(linkageCondition({ ...e, cond: 2 }), '≥ 50');
  assert.equal(linkageCondition({ ...e, cond: 3 }), '≤ 60');
  assert.equal(linkageCondition({ ...e, cond: 4 }), '50 – 60');
  assert.equal(linkageCondition({ ...e, cond: 5 }), 'outside 50 – 60');
  assert.equal(linkageCondition({ ...e, cond: 6 }), '> 50');
  assert.equal(linkageCondition({ ...e, cond: 7 }), '< 60');
  assert.equal(linkageCondition({ ...e, cond: 0 }), 'follows');
  assert.equal(linkageCondition({ ...e, cond: 1 }), 'opposite');
  assert.equal(linkageOutput({ ...e, cond: 2 }), 'DO2 · Close (NC)');
  assert.equal(linkageOutput({ ...e, cond: 0 }), 'DO2');  // follow/opposite: the output mirrors the point
  assert.equal(linkageOutput({ ...e, cond: 2, output: '' }), '—');
});
