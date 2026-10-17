import { useRef, useState } from 'preact/hooks';
import type { EdgeConfig } from '../../lib/api';
import { useDevice } from '../../lib/device';
import {
  DATA_TYPES, MAX_DEVICES, MAX_NODES, VIRTUAL_FC, allNodeNames, blankNode, blankSlave, exposedSpan, fixedName, fromCsv, ioRegister, isReserved,
  modbusAddress, nextName, nodeCount, readText, referencesTo, toCsv,
} from '../../lib/edge';
import { modelErrors, nodeDefs, slaveDefs } from '../../lib/edgeModel';
import type { EdgeNode, EdgeSlave } from '../../lib/types';
import { confirmAction, toast } from '../../components/feedback';
import { Button, DataTable, Empty, Panel } from '../../components/ui';
import { edgeBodyError } from '../../lib/rules';
import { EditDialog } from './EditDialog';

type Editing =
  | { kind: 'slave'; index: number | null }
  | { kind: 'node'; index: number | null };

/** `ioAddr`: the IO page's Modbus slave address; `problems`: the whole model's errors, which block Save. */
export function Acquisition(props: { cfg: EdgeConfig; ioAddr: number; problems: string[]; onChange: (cfg: EdgeConfig) => void }) {
  const dev = useDevice();
  const { cfg, ioAddr } = props;
  const [sel, setSel] = useState(0);
  const [editing, setEditing] = useState<Editing | null>(null);
  const fileRef = useRef<HTMLInputElement>(null);
  const slave = cfg.slaves[sel] as EdgeSlave | undefined;
  const used = nodeCount(cfg.slaves);
  const sizeError = edgeBodyError(cfg.slaves);
  const setSlaves = (slaves: EdgeSlave[]) => props.onChange({ ...cfg, slaves });

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
      {sizeError && <p class="mb-3 text-[13px] text-led-err" role="alert">{sizeError}</p>}
      {props.problems.length > 0 && (
        <div class="mb-3 text-[13px] text-led-err" role="alert">
          <p>Fix these before saving:</p>
          <ul class="list-disc pl-5">{props.problems.slice(0, 5).map((p) => <li key={p}>{p}</li>)}</ul>
          {props.problems.length > 5 && <p>and {props.problems.length - 5} more.</p>}
        </div>
      )}

      <div class="grid gap-x-4 lg:grid-cols-2">
        <Panel title="Devices" flush
          aside={
            <span class="flex items-center gap-3">
              <span class="num text-[13px] text-muted">{cfg.slaves.length} of {MAX_DEVICES}</span>
              <Button variant="primary" size="sm" disabled={cfg.slaves.length >= MAX_DEVICES}
                onClick={() => setEditing({ kind: 'slave', index: null })}>Add device</Button>
            </span>
          }>
          <DataTable rows={cfg.slaves} keyOf={(s) => s.name} empty="Add the first device the gateway should read from."
            selected={sel} onSelect={setSel}
            onEdit={(i) => setEditing({ kind: 'slave', index: i })} onDelete={removeSlave}
            columns={[
              { head: 'Name', cell: (s) => <span class="font-medium">{s.name}</span> },
              {
                head: 'Port', nowrap: true,
                cell: (s) => (s.source === 'uart' ? `UART${s.uart}` : s.source === 'eth' ? `${s.ip}:${s.ethPort}` : s.source === 'io' ? 'IO' : 'Calculated'),
              },
              { head: 'Address', nowrap: true, cell: (s) => `${s.source === 'io' ? ioAddr : s.addr}${s.mapEn ? ` → ${s.mapAddr}` : ''}` },
            ]} />
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
          initial={editing.index === null ? blankSlave(cfg.slaves.map((x) => x.name))
            : cfg.slaves[editing.index].source === 'io' ? { ...cfg.slaves[editing.index], addr: String(ioAddr) }
            : cfg.slaves[editing.index]}
          defs={(d) => slaveDefs(dev, d, cfg.slaves.filter((_, i) => i !== editing.index), ioAddr)}
          onChange={(id, v, d) => {
            if (id !== 'source') return {};
            const src = v as EdgeSlave['source'];
            // The built-in IO and virtual devices carry fixed names; leaving them frees the name field.
            const name = fixedName(src) ?? (isReserved(d.name) ? nextName('Device', cfg.slaves.map((x) => x.name)) : d.name);
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
          initial={editing.index === null ? blankNode(slave, allNodeNames(cfg.slaves)) : slave.nodes[editing.index]}
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

