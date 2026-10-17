import type { ComponentChildren } from 'preact';
import { useMemo, useState } from 'preact/hooks';
import * as api from '../lib/api';
import { ioPoints, range, useDevice } from '../lib/device';
import { ioAddressClash } from '../lib/edgeModel';
import { useAsync, useForm, useInterval } from '../lib/hooks';
import { useSaver } from '../lib/save';
import type { DeviceInfo, Values } from '../lib/types';
import { EN_DIS, ON_OFF, type FieldDef } from '../lib/validate';
import { attempt, toast } from '../components/feedback';
import { Button, ItemList, PageHeader, Panel, Pending, SaveBar, Stale, Tabs } from '../components/ui';
import { Fields, TextInput } from '../components/form';

const TIMERS = 6;
const DO_ACTION = [[0, 'Open (NO)'], [1, 'Close (NC)'], [2, 'Toggle']] as const;

function timerDefs(d: DeviceInfo, t: number): FieldDef[] {
  const en = (v: Values) => Number(v[`timer${t}Enabled`]) === 1;
  const doAct = (v: Values) => en(v) && Number(v[`timer${t}Action`]) === 1;
  return [
    { id: `timer${t}Enabled`, label: 'Timer', kind: 'select', options: EN_DIS },
    { id: `timer${t}Time`, label: 'Time of day', kind: 'time', required: true, show: en, help: 'hh:mm:ss, gateway local time' },
    { id: `timer${t}Action`, label: 'Action', kind: 'select', options: d.io.do ? [[0, 'Restart gateway'], [1, 'Set an output']] : [[0, 'Restart gateway']], show: en },
    { id: `timer${t}Output`, label: 'Output', kind: 'select', options: range(d.io.do).map((n) => [n - 1, `DO${n}`] as const), show: doAct },
    { id: `timer${t}OutputAction`, label: 'Output action', kind: 'select', options: DO_ACTION, show: doAct },
  ];
}

function functionDefs(d: DeviceInfo) {
  const general: FieldDef[] = [
    {
      id: 'modbusAddress', label: 'Modbus slave address', kind: 'number', min: 1, max: 247,
      help: 'Address the IO answers on: coils = DO, discrete inputs = DI, input registers = AI (float32, mA)',
    },
  ];
  const outputs: FieldDef[] = d.io.do
    ? [
        {
          id: 'keepOutputs', label: 'Keep outputs after restart', kind: 'select', options: ON_OFF,
          help: 'Relays keep their state through a restart or a watchdog reset; after a power loss every output starts off.',
        },
        ...range(d.io.do).flatMap((n): FieldDef[] => [
          {
            id: `do${n - 1}Mode`, label: `DO${n} action`, kind: 'select',
            options: [[0, 'No action'], [1, 'Output hold (pulse, then back)'], [2, 'Timer flip (flips by itself)']],
          },
          {
            id: `do${n - 1}FlipDelay`, label: `DO${n} time`, kind: 'number', min: 1, max: 65535, unit: 's', show: (v) => Number(v[`do${n - 1}Mode`]) > 0,
            help: 'Output hold: how long a switch lasts. Timer flip: the output flips every this many seconds, from start-up; a command sets it now and the flipping goes on.',
          },
        ]),
      ]
    : [];
  const inputs: FieldDef[] = d.io.di
    ? [
        { id: 'diDebounce', label: 'Debounce time', kind: 'number', min: 10, max: 65535, unit: 'ms' },
        { id: 'diInvert', label: 'Invert inputs', kind: 'select', options: EN_DIS },
      ]
    : [];
  const analog: FieldDef[] = range(d.io.ao).map((n) => ({
    id: `ao${n - 1}Mode`, label: `AO${n} output type`, kind: 'select', options: [[0, 'Current (mA)'], [1, 'Voltage (V)']],
  }));
  const timers = range(TIMERS).map((n) => timerDefs(d, n - 1));
  return { general, outputs, inputs, analog, timers, all: [...general, ...outputs, ...inputs, ...analog, ...timers.flat()] };
}

// ---- IO control --------------------------------------------------------------------

const POLL_MS = 1000;

function Card(props: { children: ComponentChildren; on?: boolean }) {
  return (
    <div class={`rounded-panel border p-4 transition-colors ${props.on ? 'border-led-ok/60 bg-led-ok/[0.06]' : 'border-line bg-surface'}`}>
      {props.children}
    </div>
  );
}

function DoCard(props: { name: string; on: boolean; busy: boolean; onSwitch: (on: boolean) => void }) {
  const { name, on, busy } = props;
  return (
    <Card on={on}>
      <div class="flex items-center justify-between gap-3">
        <div>
          <div class="text-[13px] font-semibold tracking-wide text-muted">{name}</div>
          <div class={`text-[20px] leading-tight font-semibold ${on ? 'text-led-ok' : 'text-ink'}`}>{on ? 'ON' : 'OFF'}</div>
        </div>
        <button
          type="button" role="switch" aria-checked={on} aria-label={`${name} relay`} disabled={busy}
          onClick={() => props.onSwitch(!on)}
          class={`relative h-8 w-16 shrink-0 rounded-full border transition-colors disabled:cursor-wait disabled:opacity-60 ${on ? 'border-led-ok bg-led-ok' : 'border-line bg-led-off/40'}`}
        >
          <span class={`absolute top-0.5 size-6.5 rounded-full bg-white shadow transition-[left] ${on ? 'left-8.5' : 'left-0.5'}`} />
        </button>
      </div>
    </Card>
  );
}

function DiCard(props: { name: string; active: boolean }) {
  const { name, active } = props;
  return (
    <Card on={active}>
      <div class="flex items-center gap-4">
        <span aria-hidden="true"
          class={`inline-block size-9 shrink-0 rounded-full border-2 transition-colors ${active ? 'border-led-ok bg-led-ok shadow-[0_0_12px_2px_var(--color-led-ok)]' : 'border-led-off bg-canvas'}`} />
        <div>
          <div class="text-[13px] font-semibold tracking-wide text-muted">{name}</div>
          <div class={`text-[20px] leading-tight font-semibold ${active ? 'text-led-ok' : 'text-ink'}`}>{active ? 'Active' : 'Inactive'}</div>
        </div>
      </div>
    </Card>
  );
}

/** An analog input in µA, as the API and Edge give it. */
function AiCard(props: { name: string; ua: number }) {
  return (
    <Card>
      <div class="text-[13px] font-semibold tracking-wide text-muted">{props.name}</div>
      <div class="num text-[26px] leading-tight font-semibold">
        {props.ua.toLocaleString('en-US').replace(/,/g, ' ')}<span class="ml-1 text-[15px] font-medium text-muted">µA</span>
      </div>
    </Card>
  );
}

function Control() {
  const d = useDevice();
  const io = useAsync(api.readIo);
  const [busy, setBusy] = useState<number | null>(null);
  const [aoDraft, setAoDraft] = useState<Record<number, string>>({});
  useInterval(io.reload, POLL_MS);

  if (!io.data) return <Pending error={io.error} retry={io.reload} />;
  const s = io.data;

  const switchDo = async (i: number, on: boolean) => {
    setBusy(i);
    const ok = await attempt(() => api.writeDo(i, on).then(() => true), `DO${i + 1} switched ${on ? 'on' : 'off'}.`);
    if (ok) io.setData({ ...s, do: s.do.map((v, k) => (k === i ? on : v)) });  // at once; the next poll confirms
    setBusy(null);
    io.reload();
  };

  const writeAo = (i: number) => {
    const v = Number(aoDraft[i]);
    if (!Number.isInteger(v) || v < 0 || v > 20000) return toast(`AO${i + 1}: enter 0–20000.`, 'err');
    attempt(() => api.writeAo(i, v), `AO${i + 1} set to ${v}.`);
  };

  return (
    <>
      <Stale error={io.error} />
      {d.io.do > 0 && (
        <Panel title="Digital outputs (relays)">
          <div class="grid gap-3 py-2 sm:grid-cols-2">
            {ioPoints(d, 'do').map((name, i) => (
              <DoCard key={name} name={name} on={!!s.do[i]} busy={busy === i} onSwitch={(on) => switchDo(i, on)} />
            ))}
          </div>
        </Panel>
      )}
      {d.io.di > 0 && (
        <Panel title="Digital inputs">
          <div class="grid gap-3 py-2 sm:grid-cols-2">
            {ioPoints(d, 'di').map((name, i) => <DiCard key={name} name={name} active={!!s.di[i]} />)}
          </div>
        </Panel>
      )}
      {d.io.ai > 0 && (
        <Panel title="Analog inputs">
          <div class="grid gap-3 py-2 sm:grid-cols-2">
            {ioPoints(d, 'ai').map((name, i) => <AiCard key={name} name={name} ua={s.ai[i] ?? 0} />)}
          </div>
        </Panel>
      )}
      {d.io.ao > 0 && (
        <Panel title="Analog outputs" flush>
          <ItemList items={ioPoints(d, 'ao')} keyOf={(n) => n} empty=""
            render={(name, i) => ({
              title: name,
              detail: `now ${s.ao[i] ?? 0}`,
              aside: (
                <>
                  <TextInput class="num w-28" inputMode="numeric" aria-label={`${name} value`} placeholder="0–20000"
                    value={aoDraft[i] ?? ''} onInput={(v) => setAoDraft({ ...aoDraft, [i]: v })} />
                  <Button size="sm" onClick={() => writeAo(i)}>Set</Button>
                </>
              ),
            })} />
        </Panel>
      )}
    </>
  );
}

function Functions() {
  const d = useDevice();
  const defs = useMemo(() => functionDefs(d), [d]);
  const f = useForm(() => api.getValues('io'), defs.all);
  const save = useSaver();
  // The new Modbus address against the saved Edge model (review 3 P2-1): it must not take a slave ID a device is
  // mapped to, or a write meant for that device would switch a local DO. Unknown model: Save waits.
  const edge = useAsync(api.getEdge);
  const saved = useAsync(() => api.getValues('io'));

  if (!f.ready) return <Pending error={f.error} retry={f.reload} />;
  const checked = !!edge.data && !!saved.data;
  const clash = checked && edge.data!.enabled
    ? ioAddressClash(d, edge.data!.slaves, Number(f.values.modbusAddress), Number(saved.data!.modbusAddress) || 100)
    : null;
  const errors = clash ? { ...f.errors, modbusAddress: clash } : f.errors;
  const p = { values: f.values, errors, onChange: f.set };

  return (
    <>
      <Panel><Fields defs={defs.general} {...p} /></Panel>
      <Panel title="Scheduled actions">
        <p class="pt-1 text-[13px] text-muted">Timers run on the gateway clock: network time once set, else the time since power-on (2020-01-01 00:00 at start). Turn on NTP in Settings for real times.</p>
        <div class="grid gap-x-6 lg:grid-cols-2">
          {defs.timers.map((t, i) => (
            <fieldset key={i} class="mt-2 border-t border-line pt-1">
              <legend class="pt-2 text-[13px] font-semibold">Timer {i + 1}</legend>
              <Fields defs={t} {...p} />
            </fieldset>
          ))}
        </div>
      </Panel>
      {defs.outputs.length > 0 && <Panel title="Digital outputs"><Fields defs={defs.outputs} {...p} /></Panel>}
      {defs.inputs.length > 0 && <Panel title="Digital inputs"><Fields defs={defs.inputs} {...p} /></Panel>}
      {defs.analog.length > 0 && <Panel title="Analog outputs"><Fields defs={defs.analog} {...p} /></Panel>}
      <SaveBar
        busy={save.busy}
        invalid={!f.valid || !!clash || !checked}
        note={edge.error || saved.error ? 'The Edge model could not be read to check the Modbus address: reload the page.' : undefined}
        onSave={() => save.run(() => api.saveValues('io', f.values), { restart: true, after: f.reload })}
      />
    </>
  );
}

export function IoPage(props: { path?: string; tab?: string }) {
  const tab = props.tab === 'functions' ? 'functions' : 'control';
  return (
    <>
      <PageHeader title="IO" intro="Built-in digital and analog channels." />
      <Tabs base="/io" active={tab} tabs={[['control', 'IO control'], ['functions', 'IO function']]} />
      {tab === 'control' ? (
        <Control />
      ) : (
        <Functions />
      )}
    </>
  );
}
