// Rules the gateway no longer checks when a config is saved (Plan 09b): the rules that span configs, and the
// report template's size. The firmware stores what it is sent and runs with it; the UI keeps it right. Pure
// functions, unit-tested (tests/unit/rules.test.ts). The per-field checks stay with each page's FieldDefs.

type Obj = Record<string, unknown>;

/** The report buffer's limit: edge_rt's REPORT_MAX (26 KB). */
export const REPORT_MAX = 26 * 1024;
const PER_SLOT = 2 + 6 * 40; // edge_tmpl_size: an escaped, quoted text value of EDGE_TEXT_MAX (40)
const SYSTEM_FIELDS = ['sys_sn', 'sys_mac', 'sys_ver', 'sys_time', 'sys_net_time', 'sys_unix_time'];
const SLOT = '\u0001'; // stands for a slot, as edgecore's MARK

/** A UART's listen port: only a TCP server (workMode 3) listens. 0 when it does not. */
const listenOf = (p: Obj | null | undefined) => (p && Number(p.workMode) === 3 ? Number(p.listenPort) || 0 : 0);

const WEB = [['webPort', 'the web interface'], ['websocketPort', 'the WebSocket console']] as const;

/** UART n's listen port against the other UART and the web's ports, with the firmware's former messages. */
export function listenClash(n: number, port: Obj, other: Obj | null, system: Obj | null): string | null {
  const mine = listenOf(port);
  if (!mine) return null;
  if (mine === listenOf(other)) return `UART1 and UART2 both listen on TCP port ${mine}: give each its own port.`;
  for (const [key, what] of WEB) {
    if (system && Number(system[key]) === mine) return `UART${n} listens on TCP port ${mine}, which ${what} uses: choose another port.`;
  }
  return null;
}

/** The System page: a UART that listens on the web's or the WebSocket's port, as each field's error. */
export function systemPortClash(system: Obj, port1: Obj | null, port2: Obj | null): Record<string, string> {
  const out: Record<string, string> = {};
  for (const [key, what] of WEB) {
    const p = Number(system[key]);
    for (const [n, port] of [[1, port1], [2, port2]] as const) {
      if (p && listenOf(port) === p && !out[key]) out[key] = `UART${n} listens on TCP port ${p}, which ${what} uses: choose another port.`;
    }
  }
  return out;
}

/** Replaces each string value naming a point or a system field by a slot mark; keys are never slots. */
function markSlots(v: unknown, names: Set<string>, count: { n: number }): unknown {
  if (typeof v === 'string') {
    if (!names.has(v) && !SYSTEM_FIELDS.includes(v)) return v;
    count.n++;
    return SLOT;
  }
  if (Array.isArray(v)) return v.map((x) => markSlots(x, names, count));
  if (v && typeof v === 'object') return Object.fromEntries(Object.entries(v).map(([k, x]) => [k, markSlots(x, names, count)]));
  return v;
}

/**
 * The report buffer a template needs, as edge_tmpl_size: its minified text without the slots, plus 242 bytes per
 * slot, plus 1. NULL when it is not a JSON object (the gateway then sends no reports).
 */
export function templateSize(template: string, names: string[]): number | null {
  let doc: unknown;
  try {
    doc = JSON.parse(template);
  } catch {
    return null;
  }
  if (!doc || typeof doc !== 'object' || Array.isArray(doc)) return null;
  const count = { n: 0 };
  const printed = JSON.stringify(markSlots(doc, new Set(names), count)).split(JSON.stringify(SLOT)).join('');
  return new TextEncoder().encode(printed).length + count.n * PER_SLOT + 1;
}

/** The template field's size check: the gateway turns reports off for a template over 26 KB. */
export function templateError26k(template: string, names: string[]): string | null {
  const size = templateSize(template, names);
  return size !== null && size > REPORT_MAX ? 'The report template needs too many values for this gateway.' : null;
}

/** A body for the gateway: minified JSON (Plan 09b ruling 8), as stored byte for byte. */
export const body = (value: unknown) => JSON.stringify(value);
