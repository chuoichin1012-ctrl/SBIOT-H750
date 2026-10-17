// The checks of the Edge model's devices and points: the edit dialogs use them one item at a time, and Save and
// a CSV import on the whole model (the gateway stores what it is sent, spec §9.1). The imports name `.js`, so
// the unit tests run this file in Node as compiled (tests/unit/edgeModel.test.ts).
import { ioPoints, uartOptions } from './device.js';
import {
  DATA_TYPES, LINKAGE_CONDITIONS, MAX_DEVICES, MAX_ETH, FUNCTION_CODES, SOURCES, VIRTUAL_FC, allNodeNames, dataTypesFor,
  exposedSpan, exposedUnit, fixedName, formulaError, ioRegister, isFollow, isReserved, modbusAddress, nameError, regsOf,
  usesLower, usesUpper,
} from './edge.js';
import type { DeviceInfo, EdgeNode, EdgeSlave, LinkageEvent, Values } from './types.js';
import { formErrors, type FieldDef, type Options } from './validate.js';

export const polled = (s: EdgeSlave) => s.source === 'uart' || s.source === 'eth';

/** A slave ID another device is already answered under (Modbus remote access). */
const unitClash = (id: number, others: EdgeSlave[], ioAddr: number) => {
  const o = others.find((x) => exposedUnit(x, ioAddr) === id);
  return o ? `Slave ID ${id} is already answered for ${o.name}.` : null;
};

/**
 * An unmapped device answers on its own address, which a mapped or virtual device may already have taken
 * (spec §9.4, strict check 3: those IDs are unique against every device). Two unmapped devices may share an address.
 */
const takenByMapping = (id: number, others: EdgeSlave[], ioAddr: number) => {
  const o = others.find((x) => (x.mapEn || x.source === 'virtual') && exposedUnit(x, ioAddr) === id);
  return o ? `Slave ID ${id} is already answered for ${o.name}${o.mapEn ? ' (address mapping)' : ''}.` : null;
};

/** `ioAddr`: the IO page's Modbus slave address, which the built-in IO device answers on. */
export function slaveDefs(dev: DeviceInfo, s: EdgeSlave, others: EdgeSlave[], ioAddr: number): FieldDef[] {
  const ethFull = others.filter((x) => x.source === 'eth').length >= MAX_ETH;
  // One built-in IO device and one virtual device at most: hide the choice once one exists.
  const taken = (src: string) => others.some((x) => x.source === src);
  const sources: Options = SOURCES.filter(([v]) =>
    v === s.source || (v === 'eth' ? !ethFull : v === 'io' || v === 'virtual' ? !taken(v) : true));
  const p = () => polled(s);
  return [
    {
      id: 'name', label: 'Device name', kind: 'text', maxLength: 16, required: true,
      disabled: () => fixedName(s.source) !== null,
      help: fixedName(s.source) ? 'Fixed for this kind of device' : undefined,
      check: (v) => isReserved(String(v)) ? 'This name is reserved for the built-in IO and virtual devices.'
        : nameError(String(v), others.map((x) => x.name)),
    },
    {
      id: 'source', label: 'Connected through', kind: 'select', options: sources,
      help: ethFull ? `Modbus TCP is full (${MAX_ETH} devices).`
        : s.source === 'io' ? 'Its points sit on the IO map: coils = DO, discrete inputs = DI, input registers = AI (float32, mA).'
        : s.source === 'virtual' ? 'Its points are input registers (FC04) holding the calculated values.'
        : undefined,
    },
    { id: 'uart', label: 'Serial port', kind: 'select', options: uartOptions(dev), show: () => s.source === 'uart' },
    { id: 'ip', label: 'Device IP address', kind: 'ipv4', show: () => s.source === 'eth' },
    {
      id: 'ethPort', label: 'Device port', kind: 'number', min: 1, max: 65535, show: () => s.source === 'eth',
      check: () => others.some((x) => x.source === 'eth' && x.ip === s.ip && x.ethPort === s.ethPort) ? 'Another device already uses this address and port.' : null,
    },
    { id: 'protocol', label: 'Protocol', kind: 'select', options: [['modbus', 'Modbus'], ['dlt645', 'DL/T 645']], show: () => s.source === 'uart' },
    {
      id: 'addr', label: s.source === 'virtual' || s.source === 'io' ? 'Modbus slave ID' : 'Device address', kind: 'text', required: true,
      show: () => p() || s.source === 'virtual' || s.source === 'io',
      // The built-in IO answers on the IO page's address: shown here, set there.
      disabled: () => s.source === 'io',
      help: s.source === 'io' ? `Set on the IO page (Modbus slave address, now ${ioAddr})`
        : s.source === 'virtual' ? 'The server reads these values from this slave ID, 1–255'
        : s.protocol === 'dlt645' ? '12-digit meter address' : 'Modbus slave ID, 1–255',
      check: (v) => {
        if (s.source === 'io') return null;
        const t = String(v);
        if (s.protocol === 'dlt645' && s.source !== 'virtual') return /^\d{12}$/.test(t) ? null : 'Enter the 12-digit meter address.';
        const n = Number(t);
        if (!(Number.isInteger(n) && n >= 1 && n <= 255)) return 'Enter 1–255.';
        if (s.mapEn) return null;
        if (s.source === 'virtual') return unitClash(n, others, ioAddr);  // a virtual ID is a free choice: keep it unique
        return exposedUnit({ ...s, addr: t }, ioAddr) === null ? null : takenByMapping(n, others, ioAddr);
      },
    },
    { id: 'poll', label: p() ? 'Polling interval' : 'Update interval', kind: 'number', min: 100, max: 65535, unit: 'ms' },
    { id: 'merge', label: 'Batch reads', kind: 'check', help: 'Read neighbouring registers in one request', show: () => p() && s.protocol === 'modbus' },
    // Mapping works alike for every kind of device, the built-in IO and virtual ones included.
    { id: 'mapEn', label: 'Address mapping', kind: 'check', help: 'Expose this device under another slave ID' },
    {
      id: 'mapAddr', label: 'Mapped slave ID', kind: 'number', min: 1, max: 255, show: () => s.mapEn,
      help: p() ? 'The server reaches this device under this ID; the gateway still polls it at its own address'
        : 'The server reaches these values under this ID instead',
      check: (v) => (s.mapEn ? unitClash(Number(v), others, ioAddr) : null),
    },
  ];
}

/** A mapped register range that overlaps another point of the device in the same function code. */
function regClash(s: EdgeSlave, n: EdgeNode, siblings: EdgeNode[]): string | null {
  const me = exposedSpan(s, n);
  if (!me || !n.mapEn) return null;
  if (me.start + me.width > 65536) return 'This data type runs past register 65535.';
  for (const o of siblings) {
    const x = exposedSpan(s, o);
    if (x && x.fc === me.fc && me.start < x.start + x.width && x.start < me.start + me.width) {
      return `Overlaps ${o.name} (registers ${x.start}–${x.start + x.width - 1}).`;
    }
  }
  return null;
}

export function nodeDefs(dev: DeviceInfo, s: EdgeSlave, n: EdgeNode, otherNames: string[], siblings: EdgeNode[]): FieldDef[] {
  const modbus = polled(s) && s.protocol === 'modbus';
  const virt = s.source === 'virtual';
  // Virtual points have a register too (input registers, FC04): the server reads them from the device's slave ID.
  const regs = modbus || virt;
  const fc = virt ? VIRTUAL_FC : n.fc;
  const dlt = polled(s) && s.protocol === 'dlt645';
  const io = s.source === 'io';
  const isAi = n.ioPoint.startsWith('AI');
  // IO points are named after their channel (DO1, DI2, AI1): each channel once.
  const usedChannels = siblings.map((x) => x.ioPoint);
  const points: Options = (['di', 'do', 'ai', 'ao'] as const)
    .flatMap((k) => ioPoints(dev, k))
    .filter((x) => x === n.ioPoint || !usedChannels.includes(x))
    .map((x) => [x, x] as const);
  return [
    {
      id: 'name', label: 'Point name', kind: 'text', maxLength: 16, required: true, show: () => !io,
      check: (v) => nameError(String(v), otherNames), help: 'Used in report templates, formulas and events',
    },
    { id: 'fc', label: 'Function code', kind: 'select', options: FUNCTION_CODES, show: () => modbus },
    {
      id: 'reg', label: virt ? 'Input register' : 'Register offset', kind: 'number', min: 0, max: 65534, show: () => regs,
      help: `Zero-based. ${modbusAddress(fc, Number(n.reg) || 0)} in conventional notation${virt ? ' (FC04)' : ''}`,
      check: (v) => (Number(v) + (fc <= 2 ? 1 : regsOf(n.dtype)) > 65536 ? 'This data type runs past register 65535.' : null),
    },
    { id: 'di', label: 'Data identifier', kind: 'text', required: true, show: () => dlt, check: (v) => /^[0-9A-Fa-f]{8}$/.test(String(v)) ? null : 'Enter 8 hex digits, e.g. 02010100.' },
    {
      id: 'ioPoint', label: 'IO channel', kind: 'select', options: points, placeholder: 'Choose…', required: true, show: () => io,
      help: (() => {
        const r = ioRegister(n.ioPoint);
        const where = r ? `At ${modbusAddress(r.fc, r.reg)}, ${DATA_TYPES.find(([t]) => t === r.dtype)?.[1]}. ` : '';
        return `${where}The point takes the channel's name, for report templates, formulas and events`;
      })(),
      check: (v) => (otherNames.includes(String(v)) ? `Another point is already named ${v}.` : null),
    },
    {
      id: 'dtype', label: 'Data type', kind: 'select', show: () => !io,
      options: () => (regs ? dataTypesFor(fc) : DATA_TYPES.slice(1)),
      check: (v) => (regs && (fc <= 2) !== (v === 'bool') ? (fc <= 2 ? 'Coils and discrete inputs are bool.' : 'Registers hold numbers.') : null),
    },
    { id: 'timeout', label: 'Response timeout', kind: 'number', min: 10, max: 65535, unit: 'ms', show: () => polled(s) },
    {
      id: 'formula', label: s.source === 'virtual' ? 'Formula' : 'Scaling formula', kind: 'text', maxLength: 64, required: s.source === 'virtual',
      show: () => !io, help: s.source === 'virtual' ? 'e.g. =%s*2+%s,node0101,node0102' : 'Optional, e.g. =%s*0.1',
      check: (v) => formulaError(String(v), otherNames, s.source === 'virtual'),
    },
    { id: 'ioReport', label: 'Report when', kind: 'select', show: () => io && n.ioPoint.startsWith('A'), options: [[0, 'Always'], [1, 'Inside the range'], [2, 'Outside the range']] },
    { id: 'lower', label: 'Range low', kind: 'number', min: 0, max: isAi ? 20000 : 65535, unit: isAi ? 'µA' : undefined, show: () => io && n.ioReport > 0 },
    {
      id: 'upper', label: 'Range high', kind: 'number', min: 0, max: isAi ? 20000 : 65535, unit: isAi ? 'µA' : undefined, show: () => io && n.ioReport > 0,
      check: () => (n.upper < n.lower ? 'Must be at least the low value.' : null),
    },
    { id: 'onChange', label: 'Report on change', kind: 'check', help: 'Send at once when the value moves', show: () => n.dtype !== 'bool' || io },
    { id: 'range', label: 'Change threshold', kind: 'number', min: 1, max: 65535, show: () => n.onChange && n.dtype !== 'bool' },
    {
      id: 'mapEn', label: 'Address mapping', kind: 'check', show: () => modbus || dlt || io || virt,
      help: dlt ? 'Expose this value as a holding register (FC3) for Modbus remote access' : 'Expose under another register',
    },
    {
      id: 'mapAddr', label: 'Mapped register', kind: 'number', min: 0, max: 65534, show: () => (modbus || dlt || io || virt) && n.mapEn,
      check: () => regClash(s, n, siblings),
    },
  ];
}

/** The edit dialogs' checks, on every device and point of a whole model (a CSV import): the gateway stores what it
 * is sent, so nothing the dialogs would refuse may come in through a file (Codex 09b review). */
/** formErrors, plus a select's value among its options: a file can hold any value (Codex 09b review r2). */
function importErrors(defs: FieldDef[], v: Values): Record<string, string> {
  const out = formErrors(defs, v);
  for (const d of defs) {
    if ((d.kind !== 'select' && d.kind !== 'radio') || (d.show && !d.show(v)) || out[d.id] || v[d.id] === undefined) continue;
    const opts = typeof d.options === 'function' ? d.options(v) : d.options ?? [];
    if (!opts.some(([o]) => String(o) === String(v[d.id]))) out[d.id] = `${String(v[d.id])} is not one of the choices.`;
  }
  return out;
}

/** Every device and point checked against the rest of the model; empty when the gateway can load it. */
export function modelErrors(dev: DeviceInfo, slaves: EdgeSlave[], ioAddr: number): string[] {
  const out: string[] = [];
  if (slaves.length > MAX_DEVICES) out.push(`${slaves.length} devices; the limit is ${MAX_DEVICES}.`);
  if (slaves.filter((s) => s.source === 'eth').length > MAX_ETH) out.push(`At most ${MAX_ETH} Modbus TCP devices.`);
  const names = allNodeNames(slaves);
  let at = 0;  // this point's place in `names`: only it is left out, so a name used twice is caught
  slaves.forEach((s, i) => {
    const sErr = importErrors(slaveDefs(dev, s, slaves.filter((_, k) => k !== i), ioAddr), s as unknown as Values);
    for (const [id, e] of Object.entries(sErr)) out.push(`${s.name}: ${id}: ${e}`);
    s.nodes.forEach((n, j) => {
      const siblings = s.nodes.filter((_, k) => k !== j);
      const self = at++;
      const nErr = importErrors(nodeDefs(dev, s, n, names.filter((_, k) => k !== self), siblings), n as unknown as Values);
      for (const [id, e] of Object.entries(nErr)) out.push(`${s.name}/${n.name}: ${id}: ${e}`);
    });
  });
  return out;
}

const ACTIONS: Options = [[0, 'No action'], [1, 'Open (NO)'], [2, 'Close (NC)'], [3, 'Toggle']];
const choices = (list: string[]): Options => list.map((x) => [x, x] as const);

/** Accepts integers and decimals; thresholds are not whole numbers only. */
const decimal = (min: number, max: number) => (v: unknown) => {
  const n = Number(v);
  return v === '' || !Number.isFinite(n) ? 'Enter a number.' : n < min || n > max ? `Enter ${min}–${max}.` : null;
};

/** One linkage event's fields (the Linkage dialog, and the check Save runs: review 3 P2-2). */
export function linkageDefs(e: LinkageEvent, names: string[], taken: string[], outputs: string[]): FieldDef[] {
  return [
    { id: 'name', label: 'Event name', kind: 'text', maxLength: 16, required: true, check: (v) => nameError(String(v), taken) },
    { id: 'enable', label: 'Active', kind: 'check', help: 'Run this event' },
    // An inactive event needs no point or output (Codex): turning it off is how an event broken by an import is kept.
    { id: 'point', label: 'Watch point', kind: 'select', options: choices(names), placeholder: 'Choose…', required: !!e.enable },
    { id: 'cond', label: 'Trigger when', kind: 'select', options: LINKAGE_CONDITIONS },
    { id: 'lower', label: 'Lower threshold', kind: 'text', required: true, show: () => usesLower(e.cond), check: decimal(-1e9, 1e9), help: 'AI points are in µA, 0–20000' },
    {
      id: 'upper', label: 'Upper threshold', kind: 'text', required: true, show: () => usesUpper(e.cond), check: (v) =>
        decimal(-1e9, 1e9)(v) ?? (usesLower(e.cond) && Number(v) < Number(e.lower) ? 'Must be at least the lower threshold.' : null),
    },
    { id: 'output', label: 'Output', kind: 'select', options: choices(outputs), placeholder: 'Choose…', required: !!e.enable },
    { id: 'action', label: 'Output action', kind: 'select', options: ACTIONS, show: () => !isFollow(e.cond) },
    { id: 'cycle', label: 'Check every', kind: 'number', min: 0, max: 10000, unit: 'ms' },
    { id: 'minInterval', label: 'Minimum gap between triggers', kind: 'number', min: 500, max: 10000, unit: 'ms' },
    { id: 'desc', label: 'Description', kind: 'text', maxLength: 64 },
  ];
}

/** Every active linkage event checked against the model as it is now (review 3 P2-2): a CSV import may drop the
 *  point an event watches, and the gateway then skips the event at boot while the UI still shows it on. */
export function linkageErrors(dev: DeviceInfo, slaves: EdgeSlave[], linkage: LinkageEvent[]): string[] {
  const names = allNodeNames(slaves);
  const outputs = ioPoints(dev, 'do');
  const out: string[] = [];
  linkage.forEach((e, i) => {
    if (!e.enable) return;
    const taken = linkage.filter((_, k) => k !== i).map((x) => x.name);
    const errs = importErrors(linkageDefs(e, names, taken, outputs), e as unknown as Values);
    for (const [id, msg] of Object.entries(errs)) out.push(`${e.name}: ${id}: ${msg}`);
  });
  return out;
}

/** The IO page's Modbus address against the saved Edge model (review 3 P2-1): a slave ID another device is
 *  answered on or mapped to (the router picks the first match, so a write would reach the IO). No baseline to go
 *  stale (Codex): a clash at the address chosen is refused whenever it is chosen. */
export function ioAddressClash(dev: DeviceInfo, slaves: EdgeSlave[], addr: number): string | null {
  const clash = modelErrors(dev, slaves, addr).find((e) => e.includes(`Slave ID ${addr} `));
  return clash ? `Slave ID ${addr} clashes with the Edge model: ${clash}` : null;
}
