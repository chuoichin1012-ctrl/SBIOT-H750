// Edge computing model helpers: limits, defaults, cross-checks and CSV.

import type { DataType, DeviceInfo, EdgeNode, EdgeSlave, LinkageEvent } from './types';
import type { Options } from './validate';

export const MAX_NODES = 200; // across all slaves (Plan 09e)
export const MAX_ETH = 5; // Modbus TCP sources
export const MAX_DEVICES = 64; // edgeDevices entries the firmware loads (cfgmodel.c)
export const MAX_EVENTS = 10; // linkage events

const NAME = /^[A-Za-z0-9_.-]{1,16}$/;

export function nameError(name: string, taken: string[]): string | null {
  if (!name) return 'Required.';
  if (!NAME.test(name)) return 'Use up to 16 letters, digits, _ . or -.';
  if (taken.includes(name)) return 'This name is already used.';
  return null;
}

export const DATA_TYPES: [DataType, string][] = [
  ['bool', 'Bool'],
  ['uint8', 'UInt8'],
  ['int8', 'Int8'],
  ['uint16', 'UInt16'],
  ['int16', 'Int16'],
  ['uint32_abcd', 'UInt32 (ABCD)'],
  ['uint32_cdab', 'UInt32 (CDAB)'],
  ['int32_abcd', 'Int32 (ABCD)'],
  ['int32_cdab', 'Int32 (CDAB)'],
  ['float32_abcd', 'Float32 (ABCD)'],
  ['float32_cdab', 'Float32 (CDAB)'],
  ['float64', 'Float64'],
];

// ---- Modbus exposure (remote access in Modbus RTU/TCP), mirrors firmware edge_rt.c ----

/** Registers a value of this type occupies. */
export const regsOf = (t: DataType) => (t === 'float64' ? 4 : /32/.test(t) ? 2 : 1);

/**
 * The register of an IO channel, as on the IO page's Modbus slave (and the
 * reference unit's M100_IO): DO = coils, DI = discrete inputs, AI = input
 * registers as float32 (ABCD, mA), two per channel.
 */
export function ioRegister(ch: string): { fc: number; reg: number; dtype: DataType } | null {
  const m = /^(DO|DI|AI)(\d+)$/.exec(ch);
  if (!m) return null;
  const i = Number(m[2]) - 1;
  if (m[1] === 'DO') return { fc: 1, reg: i, dtype: 'bool' };
  if (m[1] === 'DI') return { fc: 2, reg: i, dtype: 'bool' };
  return { fc: 4, reg: i * 2, dtype: 'float32_abcd' };
}

/** Virtual points are input registers (FC04) only. */
export const VIRTUAL_FC = 4;

/**
 * The slave ID the gateway answers for this device, or null when it is not
 * exposed. Mapping works alike for every kind of device; without it the IO
 * device answers on the IO page's address (`ioAddr`) and a virtual one on its own.
 */
export function exposedUnit(s: EdgeSlave, ioAddr: number): number | null {
  if (s.mapEn) return Number(s.mapAddr);
  if (s.source === 'io') return ioAddr;
  const own = (s.source === 'uart' && s.protocol === 'modbus') || s.source === 'eth' || s.source === 'virtual';
  return own ? Number(s.addr) || null : null;
}

/**
 * Where a point is exposed: the function code's space, first register and
 * width. DL/T645 points are holding registers, and only when mapped.
 */
export function exposedSpan(s: EdgeSlave, n: EdgeNode): { fc: number; start: number; width: number } | null {
  const dlt = s.source === 'uart' && s.protocol === 'dlt645';
  if (dlt && !n.mapEn) return null;
  const io = s.source === 'io' ? ioRegister(n.ioPoint) : null;
  if (s.source === 'io' && !io) return null;
  const fc = dlt ? 3 : io ? io.fc : s.source === 'virtual' ? VIRTUAL_FC : Number(n.fc);
  const reg = io ? io.reg : Number(n.reg);
  const dtype = io ? io.dtype : n.dtype;
  return { fc, start: n.mapEn ? Number(n.mapAddr) : reg, width: fc <= 2 ? 1 : regsOf(dtype) };
}

/** Coils and discrete inputs are single bits; registers hold numbers. */
export const dataTypesFor = (fc: number | null): Options =>
  fc === 1 || fc === 2 ? DATA_TYPES.slice(0, 1) : DATA_TYPES.slice(1);

export const FUNCTION_CODES: Options = [
  [1, '01 Read coils'],
  [2, '02 Read discrete inputs'],
  [3, '03 Read holding registers'],
  [4, '04 Read input registers'],
];

/** Conventional Modbus notation for display: FC3 offset 0 → 40001. */
export function modbusAddress(fc: number, reg: number): string {
  const base = { 1: 1, 2: 10001, 3: 40001, 4: 30001 }[fc] ?? 0;
  return String(base + reg).padStart(5, '0');
}

export const SOURCES: Options = [
  ['uart', 'Serial port'],
  ['eth', 'Modbus TCP device'],
  ['io', 'Built-in IO'],
  ['virtual', 'Calculated values'],
];

/**
 * The built-in IO and the virtual (calculated) device: at most one each, under
 * fixed names (as the reference unit's M100_IO / M100_VIRTUAL). The firmware
 * enforces the same (cfgmodel.c).
 */
export const IO_DEVICE = 'SBIOT_IO';
export const VIRTUAL_DEVICE = 'SBIOT_VIRTUAL';
export const fixedName = (source: EdgeSlave['source']) =>
  source === 'io' ? IO_DEVICE : source === 'virtual' ? VIRTUAL_DEVICE : null;
export const isReserved = (name: string) => name === IO_DEVICE || name === VIRTUAL_DEVICE;

export const sourceLabel = (s: EdgeSlave) =>
  s.source === 'uart' ? `UART${s.uart}` : s.source === 'eth' ? `${s.ip}:${s.ethPort}` : s.source === 'io' ? 'Built-in IO' : 'Calculated';

/** The first of prefix1, prefix2, … not in `used`: a new item never starts without a name. */
export function nextName(prefix: string, used: string[]): string {
  for (let i = 1; ; i++) if (!used.includes(`${prefix}${i}`)) return `${prefix}${i}`;
}

// New items mirror the firmware's ITEM_* defaults (components/cfgmodel/cfgmodel.c): no field starts empty.
export function blankSlave(used: string[] = []): EdgeSlave {
  return {
    name: nextName('Device', used), source: 'uart', uart: 1, ip: '192.168.0.201', ethPort: 502, protocol: 'modbus', addr: '1',
    poll: 1000, mapEn: false, mapAddr: 1, merge: false, nodes: [],
  };
}

export function blankNode(slave: EdgeSlave, used: string[] = []): EdgeNode {
  const bitOnly = slave.source === 'io';
  return {
    name: nextName('Node', used), fc: slave.source === 'virtual' ? VIRTUAL_FC : 3, reg: 0, di: '00000000', dtype: bitOnly ? 'bool' : slave.source === 'virtual' ? 'float32_abcd' : 'uint16',
    timeout: 200, onChange: false, range: 1, mapEn: false, mapAddr: 0, formula: '', ioPoint: '',
    ioReport: 0, upper: 0, lower: 0,
  };
}

export const allNodeNames = (slaves: EdgeSlave[]) => slaves.flatMap((s) => s.nodes.map((n) => n.name));
/** Some point reports on change (Plan 10c). */
export const anyOnChange = (slaves: EdgeSlave[]) => slaves.some((s) => s.nodes.some((n) => n.onChange));
export const nodeCount = (slaves: EdgeSlave[]) => slaves.reduce((n, s) => n + s.nodes.length, 0);

/**
 * Formula syntax: `=` expression with `%s` placeholders. Without a name
 * list, the single %s is this node's raw value (`=%s*0.1`). With one
 * (`=%s*2+%s,nodeA,nodeB`), each %s takes the named node's value in order.
 */
export function formulaError(f: string, names: string[], required: boolean): string | null {
  if (!f) return required ? 'Calculated values need a formula.' : null;
  const [expr, ...refs] = f.split(',').map((x) => x.trim());
  if (!expr.startsWith('=')) return 'Start the formula with =, e.g. =%s*0.1.';
  if (!/^=[\d\s.%s+\-*/()]+$/.test(expr)) return 'Use numbers, %s, + - * / and brackets only.';
  const slots = (expr.match(/%s/g) ?? []).length;
  if (!slots) return 'Include %s for the value.';
  if (required && !refs.length) return 'List the points to calculate from, e.g. =%s+%s,node1,node2.';
  if (refs.length && refs.length !== slots) return `The formula has ${slots} %s but ${refs.length} point names.`;
  const missing = refs.filter((r) => !names.includes(r));
  return missing.length ? `Unknown point: ${missing.join(', ')}.` : null;
}

/** Where a node name is used, so renames and deletes can warn. */
export function referencesTo(name: string, slaves: EdgeSlave[], template: string, linkage: LinkageEvent[]): string[] {
  const out: string[] = [];
  if (templateNames(template).includes(name)) out.push('the report template');
  for (const s of slaves) for (const n of s.nodes) {
    if (n.name !== name && n.formula.split(',').slice(1).map((x) => x.trim()).includes(name)) out.push(`the formula of ${n.name}`);
  }
  for (const e of linkage) if (e.point === name) out.push(`linkage ${e.name}`);
  return out;
}

/** Node names used as values in a JSON report template (nested objects allowed). */
export function templateNames(template: string): string[] {
  try {
    const out: string[] = [];
    const walk = (x: unknown) => {
      if (typeof x === 'string') out.push(x);
      else if (x && typeof x === 'object') Object.values(x).forEach(walk);
    };
    walk(JSON.parse(template));
    return out;
  } catch {
    return [];
  }
}

/** System values a report template may use, as on the reference unit. sys_time is GMT. */
export const SYSTEM_FIELDS = ['sys_sn', 'sys_mac', 'sys_ver', 'sys_time', 'sys_net_time', 'sys_unix_time'];

/**
 * Template values naming a point or a system field are replaced; any other
 * text is a constant (e.g. "user_define": "bedroom"). Only unknown sys_*
 * names are refused, since they are almost certainly typos.
 */
/** The firmware's cJSON refuses JSON nested deeper (CJSON_NESTING_LIMIT, Plan 10c). */
export const JSON_DEPTH_MAX = 16;
const depth = (v: unknown): number =>
  v && typeof v === 'object' ? 1 + Math.max(0, ...Object.values(v as Record<string, unknown>).map(depth)) : 0;

/**
 * Reporting on, but neither periodic nor scheduled: nothing would ever be sent, unless a point reports on change
 * (`onChange`), which the runtime sends by itself (Plan 10c, Codex Minor 13).
 */
export function reportModeError(v: Record<string, unknown>, anyOnChange: boolean): string | null {
  return Number(v.reporting) === 1 && !v.periodic && !v.scheduled && !anyOnChange
    ? 'Choose periodic or scheduled reporting below, or set a point to report on change.'
    : null;
}

export function templateError(template: string, names: string[]): string | null {
  if (!template.trim()) return 'Enter a template, e.g. {"temp":"node0101"}.';
  let parsed: unknown;
  try {
    parsed = JSON.parse(template);
  } catch (e) {
    return `Not valid JSON: ${(e as Error).message}`;
  }
  if (!parsed || typeof parsed !== 'object' || Array.isArray(parsed)) return 'The template must be a JSON object.';
  if (depth(parsed) > JSON_DEPTH_MAX) return `The template nests more than ${JSON_DEPTH_MAX} levels.`;
  const bytes = new TextEncoder().encode(template).length;
  if (bytes > 2048) return `The template is ${bytes} bytes; the limit is 2048.`;
  const typos = templateNames(template).filter((n) => n.startsWith('sys_') && !SYSTEM_FIELDS.includes(n) && !names.includes(n));
  return typos.length ? `Unknown system field: ${typos.join(', ')}. Use ${SYSTEM_FIELDS.join(', ')}.` : null;
}

// ---- Linkage -------------------------------------------------------------

export const LINKAGE_CONDITIONS: Options = [
  [0, 'Follows the point (bit points)'],
  [1, 'Opposite of the point (bit points)'],
  [2, '≥ lower threshold'],
  [3, '≤ upper threshold'],
  [4, 'Between thresholds'],
  [5, 'Outside thresholds'],
  [6, '> lower threshold'],
  [7, '< upper threshold'],
];

export const usesLower = (c: number) => [2, 4, 5, 6].includes(c);
export const usesUpper = (c: number) => [3, 4, 5, 7].includes(c);
export const isFollow = (c: number) => c === 0 || c === 1;

export const blankLinkage = (used: string[] = []): LinkageEvent => ({
  name: nextName('Event', used), enable: true, cond: 2, point: '', cycle: 1000, minInterval: 1000, upper: 0, lower: 0, output: '',
  action: 1, desc: 'Linkage event',
});


// ---- CSV (data acquisition only) ------------------------------------------

/** Kept identical to the original export, including the empty column. */
export const CSV_HEADER =
  'Device-name,Port-number,Slave-address,Polling-interval,Mapped-address,Merge-collection,,Node-name,Function-code,Register-Address,Data-type,Response-Timeout,Reporting-on-change,Variation-range,Mapped-address,Formula';

const cell = (v: string | number) => {
  const s = String(v);
  return /[",\n]/.test(s) ? `"${s.replace(/"/g, '""')}"` : s;
};

function portCell(s: EdgeSlave) {
  return s.source === 'uart' ? `UART${s.uart}` : s.source === 'eth' ? `${s.ip}:${s.ethPort}` : s.source.toUpperCase();
}

export function toCsv(slaves: EdgeSlave[]): string {
  const lines = [CSV_HEADER];
  for (const s of slaves) {
    const head = [s.name, portCell(s), s.addr, s.poll, s.mapEn ? s.mapAddr : '', s.merge ? 1 : 0, ''];
    const rows = s.nodes.length ? s.nodes : [null];
    for (const n of rows) {
      const node = n
        ? [
            n.name,
            s.source === 'io' || s.protocol === 'dlt645' ? '' : n.fc,
            s.source === 'io' ? n.ioPoint : s.protocol === 'dlt645' ? n.di : n.reg,
            n.dtype, n.timeout, n.onChange ? 1 : 0, n.onChange ? n.range : '', n.mapEn ? n.mapAddr : '', n.formula,
          ]
        : ['', '', '', '', '', '', '', '', ''];
      lines.push([...head, ...node].map(cell).join(','));
    }
  }
  return `${lines.join('\r\n')}\r\n`;
}

function parseCsv(text: string): string[][] {
  const rows: string[][] = [];
  let row: string[] = [], cur = '', quoted = false;
  for (let i = 0; i < text.length; i++) {
    const c = text[i];
    if (quoted) {
      if (c === '"' && text[i + 1] === '"') (cur += '"'), i++;
      else if (c === '"') quoted = false;
      else cur += c;
    } else if (c === '"') quoted = true;
    else if (c === ',') row.push(cur), (cur = '');
    else if (c === '\n' || c === '\r') {
      if (c === '\r' && text[i + 1] === '\n') i++;
      row.push(cur), rows.push(row), (row = []), (cur = '');
    } else cur += c;
  }
  if (cur || row.length) row.push(cur), rows.push(row);
  return rows.filter((r) => r.some((x) => x.trim()));
}

/** Reads a file as UTF-8, falling back to GBK for files saved by older tools. */
export async function readText(file: File): Promise<string> {
  const buf = await file.arrayBuffer();
  const utf8 = new TextDecoder('utf-8').decode(buf);
  if (!utf8.includes('�')) return utf8.replace(/^﻿/, '');
  try {
    return new TextDecoder('gbk').decode(buf);
  } catch {
    return utf8;
  }
}

/** Parses an exported CSV. Rows with an empty Device-name belong to the slave above. */
export function fromCsv(text: string, dev: DeviceInfo): { slaves: EdgeSlave[]; errors: string[] } {
  const rows = parseCsv(text);
  const errors: string[] = [];
  if (!rows.length || rows[0].join(',').trim() !== CSV_HEADER) {
    return { slaves: [], errors: ['The first line is not the expected header. Start from a file exported here.'] };
  }
  const slaves: EdgeSlave[] = [];
  const dtypes = DATA_TYPES.map(([d]) => d as string);
  // A file's own device names (e.g. the reference unit's M100_IO) -> the device they became.
  const byFileName = new Map<string, EdgeSlave>();
  rows.slice(1).forEach((r, i) => {
    const line = i + 2;
    const [dname, port, addr, poll, smap, merge, , nname, fc, reg, dtype, timeout, onch, range, nmap, formula] = r.map((x) => (x ?? '').trim());
    let s = byFileName.get(dname) ?? (dname ? undefined : slaves[slaves.length - 1]);
    if (!s) {
      if (!dname) return void errors.push(`Line ${line}: no device name.`);
      s = { ...blankSlave(), name: dname, addr: addr || '1', poll: Number(poll) || 1000, merge: merge === '1' };
      if (smap) (s.mapEn = true), (s.mapAddr = Number(smap));
      const m = /^UART(\d+)$/i.exec(port), eth = /^(.+):(\d+)$/.exec(port);
      if (m && Number(m[1]) >= 1 && Number(m[1]) <= dev.portmax) (s.source = 'uart'), (s.uart = Number(m[1]));
      else if (eth) (s.source = 'eth'), (s.ip = eth[1]), (s.ethPort = Number(eth[2]));
      else if (/^IO$/i.test(port)) s.source = 'io';
      else if (/^VIRTUAL$/i.test(port)) s.source = 'virtual';
      else return void errors.push(`Line ${line}: unknown port "${port}".`);
      if (/^\d{12}$/.test(s.addr)) s.protocol = 'dlt645';
      const fixed = fixedName(s.source);
      if (fixed) {  // the built-in IO / virtual device: one each, under its fixed name
        if (slaves.some((x) => x.source === s!.source)) return void errors.push(`Line ${line}: a second ${s.source === 'io' ? 'built-in IO' : 'virtual'} device.`);
        s.name = fixed;
        if (s.source === 'virtual' && !addr) s.addr = '200';
      } else if (isReserved(s.name)) {
        return void errors.push(`Line ${line}: "${s.name}" is reserved for the built-in IO and virtual devices.`);
      }
      byFileName.set(dname, s);
      slaves.push(s);
    }
    if (!nname) return;
    if (!dtypes.includes(dtype)) return void errors.push(`Line ${line}: unknown data type "${dtype}".`);
    const n: EdgeNode = {
      ...blankNode(s), name: nname, dtype: dtype as DataType, timeout: Number(timeout) || 200,
      onChange: onch === '1', range: Number(range) || 1, formula,
    };
    if (nmap) (n.mapEn = true), (n.mapAddr = Number(nmap));
    if (s.source === 'io') {  // named after its channel, each channel once
      if (s.nodes.some((x) => x.ioPoint === reg)) return void errors.push(`Line ${line}: ${reg} appears twice.`);
      (n.ioPoint = reg), (n.name = reg);
      const r = ioRegister(reg);
      if (!r) return void errors.push(`Line ${line}: unknown IO channel "${reg}".`);
      (n.fc = r.fc), (n.reg = r.reg), (n.dtype = r.dtype);
    }
    else if (s.protocol === 'dlt645') n.di = reg;
    else if (s.source === 'virtual') (n.fc = VIRTUAL_FC), (n.reg = Number(reg) || 0);
    else (n.fc = Number(fc)), (n.reg = Number(reg));
    s.nodes.push(n);
  });
  const names = allNodeNames(slaves);
  const dup = names.filter((n, i) => names.indexOf(n) !== i);
  if (dup.length) errors.push(`Duplicate point names: ${[...new Set(dup)].join(', ')}.`);
  if (names.length > MAX_NODES) errors.push(`The file has ${names.length} points; the limit is ${MAX_NODES}.`);
  if (slaves.filter((s) => s.source === 'eth').length > MAX_ETH) errors.push(`At most ${MAX_ETH} Modbus TCP devices.`);
  if (slaves.length > MAX_DEVICES) errors.push(`The file has ${slaves.length} devices; the limit is ${MAX_DEVICES}.`);
  return { slaves, errors };
}

// ---- the stored model, as the firmware reads it ----------------------------------------------------------

// cfgmodel.c ITEM_SLAVE and ITEM_NODE: what the gateway's loader starts each device and point from.
const ITEM_SLAVE = { name: 'Device1', source: 'uart', uart: 1, ip: '192.168.0.201', ethPort: 502, protocol: 'modbus', addr: '1',
  poll: 1000, mapEn: false, mapAddr: 1, merge: false };
const ITEM_NODE = { name: 'Node1', fc: 3, reg: 0, di: '00000000', dtype: 'uint16', timeout: 200, onChange: false, range: 1,
  mapEn: false, mapAddr: 0, formula: '', ioPoint: '', ioReport: 0, upper: 0, lower: 0 };

/** One field as cfgmodel's take() coerces it: a bool from a bool, a nonzero number, "1" or "true"; a number from a
 *  number, a bool or a numeric string; text from text. Anything else keeps the item's value. */
function coerce(base: unknown, v: unknown): unknown {
  if (v === undefined || v === null) return base;
  if (typeof base === 'boolean') {
    if (typeof v === 'boolean') return v;
    if (typeof v === 'number') return v !== 0;
    if (typeof v === 'string') return v === '1' || v === 'true';
    return base;
  }
  if (typeof base === 'number') {
    if (typeof v === 'number' && Number.isFinite(v)) return v;
    if (typeof v === 'boolean') return v ? 1 : 0;
    if (typeof v === 'string' && v.trim() !== '' && Number.isFinite(Number(v))) return Number(v);
    return base;
  }
  return typeof v === 'string' ? v : base;
}

function overlay<T extends Record<string, unknown>>(base: T, raw: unknown): T {
  const src = raw && typeof raw === 'object' ? (raw as Record<string, unknown>) : {};
  const out: Record<string, unknown> = {};
  for (const k of Object.keys(base)) out[k] = coerce(base[k], src[k]);
  return out as T;
}

/** edgeDevices as GET returns them, read as the gateway's loader reads them: every field the dialogs need, a missing
 *  one from cfgmodel's item defaults, the loader's coercions. The UI writes whole documents, but a stored one may
 *  come from elsewhere (the bench, an API client). */
export function normalizeSlaves(raw: unknown): EdgeSlave[] {
  if (!Array.isArray(raw)) return [];
  return raw.map((d) => {
    const nodes = d && typeof d === 'object' && Array.isArray((d as { nodes?: unknown }).nodes) ? (d as { nodes: unknown[] }).nodes : [];
    const slave = { ...overlay(ITEM_SLAVE, d), nodes: nodes.map((n) => overlay(ITEM_NODE, n)) } as unknown as EdgeSlave;
    // The loader's names and registers for the built-in IO and the calculated device (cfgmodel.c, Codex r2).
    const fixed = fixedName(slave.source);
    if (fixed) slave.name = fixed;
    if (slave.source === 'io') {
      slave.nodes = slave.nodes.map((n) => {
        const r = ioRegister(n.ioPoint);
        return r ? { ...n, name: n.ioPoint, fc: r.fc, reg: r.reg, dtype: r.dtype } : n;
      });
    } else if (slave.source === 'virtual') {
      slave.nodes = slave.nodes.map((n) => ({ ...n, fc: VIRTUAL_FC }));
    }
    return slave;
  });
}
