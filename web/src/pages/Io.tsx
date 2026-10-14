import { useMemo, useState } from 'preact/hooks';
import * as api from '../lib/api';
import { ioPoints, range, useDevice } from '../lib/device';
import { useAsync, useForm, useInterval } from '../lib/hooks';
import { useSaver } from '../lib/save';
import type { DeviceInfo, Values } from '../lib/types';
import { EN_DIS, ON_OFF, type FieldDef } from '../lib/validate';
import { attempt, toast } from '../components/feedback';
import { Button, ItemList, Notice, PageHeader, Panel, Pending, SaveBar, Stale, Tabs } from '../components/ui';
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
        { id: 'keepOutputs', label: 'Keep outputs after restart', kind: 'select', options: ON_OFF },
        ...range(d.io.do).flatMap((n): FieldDef[] => [
          {
            id: `do${n - 1}Mode`, label: `DO${n} action`, kind: 'select',
            options: [[0, 'No action'], [1, 'Output hold (pulse, then back)'], [2, 'Timer flip (blink while on)']],
          },
          {
            id: `do${n - 1}FlipDelay`, label: `DO${n} time`, kind: 'number', min: 1, max: 65535, unit: 's', show: (v) => Number(v[`do${n - 1}Mode`]) > 0,
            help: 'Output hold: how long a switch lasts. Timer flip: the toggle period.',
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

function Control() {
  const d = useDevice();
  const io = useAsync(api.readIo);
  const [aoDraft, setAoDraft] = useState<Record<number, string>>({});
  useInterval(io.reload, 2000);

  if (!io.data) return <Pending error={io.error} retry={io.reload} />;
  const s = io.data;

  const switchDo = async (i: number, on: boolean) => {
    if (await attempt(() => api.writeDo(i, on).then(() => true), `DO${i + 1} switched ${on ? 'on' : 'off'}.`)) io.reload();
  };

  const writeAo = (i: number) => {
    const v = Number(aoDraft[i]);
    if (!Number.isInteger(v) || v < 0 || v > 20000) return toast(`AO${i + 1}: enter 0–20000.`, 'err');
    attempt(() => api.writeAo(i, v), `AO${i + 1} set to ${v}.`);
  };

  return (
    <>
      <Stale error={io.error} />
      <div class="grid gap-x-4 md:grid-cols-2">
        <Panel title="Digital outputs" flush>
          <ItemList items={ioPoints(d, 'do')} keyOf={(n) => n} empty="This unit has no digital outputs."
            render={(name, i) => ({
              led: s.do[i] ? 'ok' : 'off',
              title: `${name}: ${s.do[i] ? 'on' : 'off'}`,
              aside: <Button size="sm" onClick={() => switchDo(i, !s.do[i])}>Switch {s.do[i] ? 'off' : 'on'}</Button>,
            })} />
        </Panel>
        <Panel title="Digital inputs" flush>
          <ItemList items={ioPoints(d, 'di')} keyOf={(n) => n} empty="This unit has no digital inputs."
            render={(name, i) => ({ led: s.di[i] ? 'ok' : 'off', title: `${name}: ${s.di[i] ? 'high' : 'low'}` })} />
        </Panel>
        <Panel title="Analog inputs" flush>
          <ItemList items={ioPoints(d, 'ai')} keyOf={(n) => n} empty="This unit has no analog inputs."
            render={(name, i) => ({ title: name, aside: <span class="num">{((s.ai[i] ?? 0) / 1000).toFixed(3)} mA</span> })} />
        </Panel>
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
      </div>
    </>
  );
}

function Functions() {
  const d = useDevice();
  const defs = useMemo(() => functionDefs(d), [d]);
  const f = useForm(() => api.getValues('io'), defs.all);
  const save = useSaver();

  if (!f.ready) return <Pending error={f.error} retry={f.reload} />;
  const p = { values: f.values, errors: f.errors, onChange: f.set };

  return (
    <>
      <Panel><Fields defs={defs.general} {...p} /></Panel>
      <Panel title="Scheduled actions">
        <p class="pt-1 text-[13px] text-muted">Timers run on the gateway clock. Turn on NTP in Settings so they fire at the right time.</p>
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
        invalid={!f.valid}
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
      <Tabs base="/io" active={tab} tabs={[['control', 'Live control'], ['functions', 'Behaviour']]} />
      {tab === 'control' ? (
        <>
          <Notice tone="warn">Switching an output acts on the connected equipment immediately.</Notice>
          <Control />
        </>
      ) : (
        <Functions />
      )}
    </>
  );
}
