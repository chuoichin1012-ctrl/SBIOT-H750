import { useState } from 'preact/hooks';
import type { EdgeConfig } from '../../lib/api';
import { ioPoints, useDevice } from '../../lib/device';
import {
  LINKAGE_CONDITIONS, MAX_EVENTS, allNodeNames, blankLinkage, isFollow, nameError, usesLower, usesUpper,
} from '../../lib/edge';
import type { LinkageEvent } from '../../lib/types';
import type { FieldDef, Options } from '../../lib/validate';
import { confirmAction } from '../../components/feedback';
import { Button, ItemList, Notice, Panel } from '../../components/ui';
import { EditDialog } from './EditDialog';

const ACTIONS: Options = [[0, 'No action'], [1, 'Open (NO)'], [2, 'Close (NC)'], [3, 'Toggle']];
const choose = (list: string[]): Options => [['', 'Choose…'], ...list.map((x) => [x, x] as const)];
const picked = (v: unknown) => (v ? null : 'Choose one.');

/** Accepts integers and decimals; thresholds are not whole numbers only. */
const decimal = (min: number, max: number) => (v: unknown) => {
  const n = Number(v);
  return v === '' || !Number.isFinite(n) ? 'Enter a number.' : n < min || n > max ? `Enter ${min}–${max}.` : null;
};

function linkageDefs(e: LinkageEvent, names: string[], taken: string[], outputs: string[]): FieldDef[] {
  return [
    { id: 'name', label: 'Event name', kind: 'text', maxLength: 16, check: (v) => nameError(String(v), taken) },
    { id: 'enable', label: 'Active', kind: 'check', help: 'Run this event' },
    { id: 'point', label: 'Watch point', kind: 'select', options: choose(names), check: picked },
    { id: 'cond', label: 'Trigger when', kind: 'select', options: LINKAGE_CONDITIONS },
    { id: 'lower', label: 'Lower threshold', kind: 'text', required: true, show: () => usesLower(e.cond), check: decimal(-1e9, 1e9), help: 'AI points are in µA, 0–20000' },
    {
      id: 'upper', label: 'Upper threshold', kind: 'text', required: true, show: () => usesUpper(e.cond), check: (v) =>
        decimal(-1e9, 1e9)(v) ?? (usesLower(e.cond) && Number(v) < Number(e.lower) ? 'Must be at least the lower threshold.' : null),
    },
    { id: 'output', label: 'Output', kind: 'select', options: choose(outputs), check: picked },
    { id: 'action', label: 'Output action', kind: 'select', options: ACTIONS, show: () => !isFollow(e.cond) },
    { id: 'cycle', label: 'Check every', kind: 'number', min: 0, max: 10000, unit: 'ms' },
    { id: 'minInterval', label: 'Minimum gap between triggers', kind: 'number', min: 500, max: 10000, unit: 'ms' },
    { id: 'desc', label: 'Description', kind: 'text', maxLength: 64 },
  ];
}


/** Numeric text fields are kept as strings while typing; store numbers. */
function numbers<T extends object>(e: T, keys: (keyof T)[]): T {
  const out = { ...e };
  for (const k of keys) out[k] = Number(out[k]) as T[keyof T];
  return out;
}

function EventList<T extends { name: string; enable: boolean; desc: string }>(props: {
  title: string;
  noun: string;
  items: T[];
  summary: (e: T) => string;
  canAdd: boolean;
  blocked?: string;
  onAdd: () => void;
  onEdit: (i: number) => void;
  onDelete: (i: number) => void;
}) {
  const full = props.items.length >= MAX_EVENTS;
  return (
    <Panel
      title={props.title}
      flush
      aside={
        <span class="flex items-center gap-3">
          <span class="num text-[13px] text-muted">{props.items.length} of {MAX_EVENTS}</span>
          <Button variant="primary" size="sm" disabled={full || !props.canAdd} onClick={props.onAdd}>Add {props.noun}</Button>
        </span>
      }
    >
      {props.blocked && <p class="px-4 pt-3 text-[13px] text-muted">{props.blocked}</p>}
      <ItemList items={props.items} keyOf={(e) => e.name} empty={`No ${props.noun}s yet.`}
        render={(e, i) => ({
          led: e.enable ? 'ok' : 'off',
          title: e.name,
          detail: `${props.summary(e)}${e.desc ? `. ${e.desc}` : ''}`,
          onEdit: () => props.onEdit(i),
          onDelete: () => props.onDelete(i),
        })} />
    </Panel>
  );
}

export function Linkage(props: { cfg: EdgeConfig; onChange: (cfg: EdgeConfig) => void }) {
  const dev = useDevice();
  const { cfg } = props;
  const [edit, setEdit] = useState<number | 'new' | null>(null);
  const names = allNodeNames(cfg.slaves);
  const outputs = ioPoints(dev, 'do');
  const set = (linkage: LinkageEvent[]) => props.onChange({ ...cfg, linkage });
  const cond = (c: number) => LINKAGE_CONDITIONS.find(([v]) => v === c)?.[1] ?? '';

  return (
    <>
      <Notice>A linkage event switches a digital output when a point meets a condition, without a server round trip.</Notice>
      <EventList
        title="Linkage events"
        noun="event"
        items={cfg.linkage}
        canAdd={names.length > 0 && outputs.length > 0}
        blocked={!names.length ? 'Add points under Data acquisition first.' : !outputs.length ? 'This unit has no digital outputs.' : undefined}
        summary={(e) => `${e.point}: ${cond(e.cond)} → ${e.output}`}
        onAdd={() => setEdit('new')}
        onEdit={setEdit}
        onDelete={async (i) => {
          if (await confirmAction({ title: `Delete ${cfg.linkage[i].name}?`, body: 'The event stops after you save.', confirm: 'Delete event', danger: true })) {
            set(cfg.linkage.filter((_, j) => j !== i));
          }
        }}
      />
      {edit !== null && (
        <EditDialog<LinkageEvent>
          title={edit === 'new' ? 'Add linkage event' : 'Edit linkage event'}
          initial={edit === 'new' ? blankLinkage() : cfg.linkage[edit]}
          defs={(e) => linkageDefs(e, names, cfg.linkage.filter((_, i) => i !== edit).map((x) => x.name), outputs)}
          onCancel={() => setEdit(null)}
          onSave={(raw) => {
            const e = numbers(raw, ['lower', 'upper']);
            set(edit === 'new' ? [...cfg.linkage, e] : cfg.linkage.map((x, i) => (i === edit ? e : x)));
            setEdit(null);
          }}
        />
      )}
    </>
  );
}
