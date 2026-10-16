import { useRef, useState } from 'preact/hooks';
import * as api from '../../lib/api';
import type { EdgeConfig } from '../../lib/api';
import { useAsync } from '../../lib/hooks';
import { ioPoints, uartOptions, useDevice } from '../../lib/device';
import {
  DATA_TYPES, MAX_ETH, MAX_NODES, FUNCTION_CODES, SOURCES, allNodeNames, blankNode, blankSlave, dataTypesFor, exposedSpan, exposedUnit,
  VIRTUAL_FC, fixedName, formulaError, fromCsv, ioRegister, isReserved, modbusAddress, nameError, nodeCount, readText, referencesTo, sourceLabel, toCsv, regsOf,
} from '../../lib/edge';
import type { DeviceInfo, EdgeNode, EdgeSlave, Values } from '../../lib/types';
import { formErrors, type FieldDef, type Options } from '../../lib/validate';
import { confirmAction, toast } from '../../components/feedback';
import { Button, DataTable, Empty, ItemList, Panel } from '../../components/ui';
import { EditDialog } from './EditDialog';

const polled = (s: EdgeSlave) => s.source === 'uart' || s.source === 'eth';

/** A slave ID another device is already answered under (Modbus remote access). */
const unitClash = (id: number, others: EdgeSlave[], ioAddr: number) => {
  const o = others.find((x) => exposedUnit(x, ioAddr) === id);
  return o ? `Slave ID ${id} is already answered for ${o.name}.` : null;
};

/** `ioAddr`: the IO page's Modbus slave address, which the built-in IO device answers on. */
function slaveDefs(dev: DeviceInfo, s: EdgeSlave, others: EdgeSlave[], ioAddr: number): FieldDef[] {
  const ethFull = others.filter((x) => x.source === 'eth').length >= MAX_ETH;
  // One built-in IO device and one virtual device at most: hide the choice once one exists.
  const taken = (src: string) => others.some((x) => x.source === src);
  const sources: Options = SOURCES.filter(([v]) =>
    v === s.source || (v === 'eth' ? !ethFull : v === 'io' || v === 'virtual' ? !taken(v) : true));
  const p = () => polled(s);
  return [
    {
      id: 'name', label: 'Device name', kind: 'text', maxLength: 16,
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
      id: 'addr', label: s.source === 'virtual' || s.source === 'io' ? 'Modbus slave ID' : 'Device address', kind: 'text',
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
        return s.source === 'virtual' && !s.mapEn ? unitClash(n, others, ioAddr) : null;  // a virtual ID is a free choice: keep it unique
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

function nodeDefs(dev: DeviceInfo, s: EdgeSlave, n: EdgeNode, otherNames: string[], siblings: EdgeNode[]): FieldDef[] {
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
      id: 'name', label: 'Point name', kind: 'text', maxLength: 16, show: () => !io,
      check: (v) => nameError(String(v), otherNames), help: 'Used in report templates, formulas and events',
    },
    { id: 'fc', label: 'Function code', kind: 'select', options: FUNCTION_CODES, show: () => modbus },
    {
      id: 'reg', label: virt ? 'Input register' : 'Register offset', kind: 'number', min: 0, max: 65534, show: () => regs,
      help: `Zero-based. ${modbusAddress(fc, Number(n.reg) || 0)} in conventional notation${virt ? ' (FC04)' : ''}`,
      check: (v) => (Number(v) + (fc <= 2 ? 1 : regsOf(n.dtype)) > 65536 ? 'This data type runs past register 65535.' : null),
    },
    { id: 'di', label: 'Data identifier', kind: 'text', show: () => dlt, check: (v) => /^[0-9A-Fa-f]{8}$/.test(String(v)) ? null : 'Enter 8 hex digits, e.g. 02010100.' },
    {
      id: 'ioPoint', label: 'IO channel', kind: 'select', options: [['', 'Choose…'], ...points], show: () => io,
      help: (() => {
        const r = ioRegister(n.ioPoint);
        const where = r ? `At ${modbusAddress(r.fc, r.reg)}, ${DATA_TYPES.find(([t]) => t === r.dtype)?.[1]}. ` : '';
        return `${where}The point takes the channel's name, for report templates, formulas and events`;
      })(),
      check: (v) => (!v ? 'Choose a channel.' : otherNames.includes(String(v)) ? `Another point is already named ${v}.` : null),
    },
    {
      id: 'dtype', label: 'Data type', kind: 'select', show: () => !io,
      options: () => (regs ? dataTypesFor(fc) : DATA_TYPES.slice(1)),
      check: (v) => (regs && (fc <= 2) !== (v === 'bool') ? (fc <= 2 ? 'Coils and discrete inputs are bool.' : 'Registers hold numbers.') : null),
    },
    { id: 'timeout', label: 'Response timeout', kind: 'number', min: 10, max: 65535, unit: 'ms', show: () => polled(s) },
    {
      id: 'formula', label: s.source === 'virtual' ? 'Formula' : 'Scaling formula', kind: 'text', maxLength: 64,
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

type Editing =
  | { kind: 'slave'; index: number | null }
  | { kind: 'node'; index: number | null };

/** The edit dialogs' checks, on every device and point of a whole model (a CSV import): the gateway stores what it
 * is sent, so nothing the dialogs would refuse may come in through a file (Codex 09b review). */
function modelErrors(dev: DeviceInfo, slaves: EdgeSlave[], ioAddr: number): string[] {
  const out: string[] = [];
  const names = allNodeNames(slaves);
  slaves.forEach((s, i) => {
    const sErr = formErrors(slaveDefs(dev, s, slaves.filter((_, k) => k !== i), ioAddr), s as unknown as Values);
    for (const [id, e] of Object.entries(sErr)) out.push(`${s.name}: ${id}: ${e}`);
    s.nodes.forEach((n, j) => {
      const siblings = s.nodes.filter((_, k) => k !== j);
      const nErr = formErrors(nodeDefs(dev, s, n, names.filter((x) => x !== n.name), siblings), n as unknown as Values);
      for (const [id, e] of Object.entries(nErr)) out.push(`${s.name}/${n.name}: ${id}: ${e}`);
    });
  });
  return out;
}

export function Acquisition(props: { cfg: EdgeConfig; onChange: (cfg: EdgeConfig) => void }) {
  const dev = useDevice();
  const { cfg } = props;
  const [sel, setSel] = useState(0);
  const [editing, setEditing] = useState<Editing | null>(null);
  const fileRef = useRef<HTMLInputElement>(null);
  const slave = cfg.slaves[sel] as EdgeSlave | undefined;
  const used = nodeCount(cfg.slaves);
  const setSlaves = (slaves: EdgeSlave[]) => props.onChange({ ...cfg, slaves });
  const ioCfg = useAsync(() => api.getValues('io'));
  const ioAddr = Number(ioCfg.data?.modbusAddress) || 100;

  const refsWarning = (names: string[]) => {
    const refs = names.flatMap((n) => referencesTo(n, cfg.slaves, String(cfg.report.template ?? ''), cfg.linkage));
    return refs.length ? ` It is still used by ${[...new Set(refs)].join(', ')}; update those too.` : '';
  };

  const removeSlave = async (i: number) => {
    const s = cfg.slaves[i];
    const ok = await confirmAction({
      title: `Delete ${s.name}?`,
      body: `This removes the device and its ${s.nodes.length} points.${refsWarning(s.nodes.map((n) => n.name))}`,
      confirm: 'Delete device',
      danger: true,
    });
    if (ok) {
      setSlaves(cfg.slaves.filter((_, j) => j !== i));
      setSel(Math.max(0, Math.min(sel, cfg.slaves.length - 2)));
    }
  };

  const removeNode = async (i: number) => {
    if (!slave) return;
    const n = slave.nodes[i];
    const ok = await confirmAction({ title: `Delete ${n.name}?`, body: `This removes the point.${refsWarning([n.name])}`, confirm: 'Delete point', danger: true });
    if (ok) setSlaves(cfg.slaves.map((s, j) => (j === sel ? { ...s, nodes: s.nodes.filter((_, k) => k !== i) } : s)));
  };

  const exportCsv = () => {
    const blob = new Blob([toCsv(cfg.slaves)], { type: 'text/csv' });
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = `${dev.hostname || 'gateway'}-points.csv`;
    a.click();
    URL.revokeObjectURL(a.href);
  };

  const importCsv = async (file: File) => {
    const parsed = fromCsv(await readText(file), dev);
    const { slaves } = parsed;
    const errors = parsed.errors.length ? parsed.errors : modelErrors(dev, slaves, ioAddr);
    if (errors.length) return toast(`Import stopped. ${errors.slice(0, 3).join(' ')}`, 'err');
    const ok = await confirmAction({
      title: 'Replace all devices and points?',
      body: `${file.name} holds ${slaves.length} devices and ${nodeCount(slaves)} points. They replace the current list once you save.`,
      confirm: 'Replace',
    });
    if (ok) {
      setSlaves(slaves);
      setSel(0);
      toast('Points imported. Save to apply them.');
    }
  };

  return (
    <>
      <div class="mb-3 flex flex-wrap items-center gap-2">
        <p class="num mr-auto text-muted">{used} of {MAX_NODES} points used</p>
        <input ref={fileRef} type="file" accept=".csv,text/csv" class="hidden"
          onChange={(e) => {
            const f = e.currentTarget.files?.[0];
            e.currentTarget.value = '';
            if (f) importCsv(f);
          }} />
        <Button onClick={() => fileRef.current?.click()}>Import CSV</Button>
        <Button onClick={exportCsv} disabled={!cfg.slaves.length}>Export CSV</Button>
      </div>

      <div class="grid gap-x-4 lg:grid-cols-[minmax(0,2fr)_minmax(0,3fr)]">
        <Panel title="Devices" flush
          aside={<Button variant="primary" size="sm" onClick={() => setEditing({ kind: 'slave', index: null })}>Add device</Button>}>
          <ItemList items={cfg.slaves} keyOf={(s) => s.name} empty="Add the first device the gateway should read from."
            render={(s, i) => ({
              title: s.name,
              detail: `${sourceLabel(s)}${polled(s) ? `, address ${s.addr}` : s.source === 'virtual' ? `, slave ID ${s.addr}`
                : s.source === 'io' ? `, slave ID ${ioAddr}` : ''}${s.mapEn ? ` → ${s.mapAddr}` : ''}, ${s.nodes.length} points`,
              selected: i === sel,
              onSelect: () => setSel(i),
              onEdit: () => setEditing({ kind: 'slave', index: i }),
              onDelete: () => removeSlave(i),
            })} />
        </Panel>

        <Panel
          title={slave ? `Points of ${slave.name}` : 'Points'}
          flush
          aside={slave && (
            <Button variant="primary" size="sm" disabled={used >= MAX_NODES} onClick={() => setEditing({ kind: 'node', index: null })}>
              Add point
            </Button>
          )}
        >
          {!slave ? (
            <Empty>Select a device to see its points.</Empty>
          ) : (
            <DataTable
              rows={slave.nodes}
              keyOf={(n) => n.name}
              empty="No points yet. Add the registers or channels to read."
              onEdit={(i) => setEditing({ kind: 'node', index: i })}
              onDelete={removeNode}
              columns={[
                { head: 'Name', cell: (n) => <span class="font-medium">{n.name}</span> },
                {
                  head: 'Source',
                  cell: (n) => {
                    const span = exposedSpan(slave, n);
                    const mapped = span && n.mapEn ? ` → ${modbusAddress(span.fc, span.start)}` : '';
                    if (slave.source === 'io') {
                      const r = ioRegister(n.ioPoint);
                      return `${n.ioPoint}${r ? ` · ${modbusAddress(r.fc, r.reg)}` : ''}${mapped}`;
                    }
                    if (slave.source === 'virtual') return `${modbusAddress(VIRTUAL_FC, n.reg)}${mapped} · ${n.formula}`;
                    return `${slave.protocol === 'dlt645' ? n.di : modbusAddress(n.fc, n.reg)}${mapped}`;
                  },
                },
                {
                  head: 'Type',
                  cell: (n) => DATA_TYPES.find(([t]) => t === (slave.source === 'io' ? ioRegister(n.ioPoint)?.dtype : n.dtype))?.[1],
                },
              ]}
            />
          )}
        </Panel>
      </div>

      {editing?.kind === 'slave' && (
        <EditDialog<EdgeSlave>
          title={editing.index === null ? 'Add device' : 'Edit device'}
          initial={editing.index === null ? blankSlave()
            : cfg.slaves[editing.index].source === 'io' ? { ...cfg.slaves[editing.index], addr: String(ioAddr) }
            : cfg.slaves[editing.index]}
          defs={(d) => slaveDefs(dev, d, cfg.slaves.filter((_, i) => i !== editing.index), ioAddr)}
          onChange={(id, v, d) => {
            if (id !== 'source') return {};
            const src = v as EdgeSlave['source'];
            // The built-in IO and virtual devices carry fixed names; leaving them frees the name field.
            const name = fixedName(src) ?? (isReserved(d.name) ? '' : d.name);
            if (src === 'eth') return { protocol: 'modbus', name };
            if (src === 'io') return { protocol: 'modbus', addr: String(ioAddr), name };
            // A slave ID unlikely to clash with a real device on the bus
            if (src === 'virtual') return { protocol: 'modbus', addr: '200', name };
            return { name };
          }}
          onCancel={() => setEditing(null)}
          onSave={(s) => {
            const old = editing.index === null ? null : cfg.slaves[editing.index];
            // Points of another kind (e.g. registers on an IO device) would be meaningless.
            const keep = old && old.source === s.source && old.protocol === s.protocol ? s.nodes : [];
            if (old && keep.length !== old.nodes.length) toast(`${old.nodes.length} points were removed because the connection type changed.`);
            const next = { ...s, nodes: keep };
            setSlaves(editing.index === null ? [...cfg.slaves, next] : cfg.slaves.map((x, i) => (i === editing.index ? next : x)));
            if (editing.index === null) setSel(cfg.slaves.length);
            setEditing(null);
          }}
        />
      )}

      {editing?.kind === 'node' && slave && (
        <EditDialog<EdgeNode>
          title={editing.index === null ? `Add point to ${slave.name}` : 'Edit point'}
          initial={editing.index === null ? blankNode(slave) : slave.nodes[editing.index]}
          defs={(n) => {
            const self = editing.index === null ? null : slave.nodes[editing.index].name;
            const siblings = slave.nodes.filter((_, i) => i !== editing.index);
            return nodeDefs(dev, slave, n, allNodeNames(cfg.slaves).filter((x) => x !== self), siblings);
          }}
          onChange={(id, v, n) => {
            if (id === 'fc') return { dtype: (Number(v) <= 2 ? 'bool' : n.dtype === 'bool' ? 'uint16' : n.dtype) as EdgeNode['dtype'] };
            if (id === 'ioPoint') {
              const r = ioRegister(String(v));
              return r ? { name: String(v), fc: r.fc, reg: r.reg, dtype: r.dtype } : { name: String(v) };
            }
            return {};
          }}
          onCancel={() => setEditing(null)}
          onSave={(saved) => {
            // An IO point is named after its channel and sits at its register; a virtual one is an input register.
            const r = slave.source === 'io' ? ioRegister(saved.ioPoint) : null;
            const n = r ? { ...saved, name: saved.ioPoint, fc: r.fc, reg: r.reg, dtype: r.dtype }
              : slave.source === 'virtual' ? { ...saved, fc: VIRTUAL_FC }
              : saved;
            const old = editing.index === null ? null : slave.nodes[editing.index];
            if (old && old.name !== n.name) {
              const refs = referencesTo(old.name, cfg.slaves, String(cfg.report.template ?? ''), cfg.linkage);
              if (refs.length) toast(`${old.name} was renamed. Update ${[...new Set(refs)].join(', ')}.`, 'err');
            }
            const nodes = editing.index === null ? [...slave.nodes, n] : slave.nodes.map((x, i) => (i === editing.index ? n : x));
            setSlaves(cfg.slaves.map((s, j) => (j === sel ? { ...s, nodes } : s)));
            setEditing(null);
          }}
        />
      )}
    </>
  );
}

