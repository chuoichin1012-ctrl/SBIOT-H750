import { useMemo, useState } from 'preact/hooks';
import * as api from '../lib/api';
import { range, useDevice } from '../lib/device';
import { useAsync, useForm, useInterval } from '../lib/hooks';
import { systemPortClash } from '../lib/rules';
import { useSaver } from '../lib/save';
import type { DeviceInfo } from '../lib/types';
import { ON_OFF, type FieldDef } from '../lib/validate';
import { attempt, useAction } from '../components/feedback';
import { Button, PageHeader, Panel, Pending, SaveBar } from '../components/ui';
import { Fields, Row } from '../components/form';

const TIMEZONES = Array.from({ length: 25 }, (_, i) => {
  const off = 12 - i;
  return [off, `UTC${off >= 0 ? '+' : ''}${off}`] as const;
});

function defs(d: DeviceInfo) {
  const device: FieldDef[] = [
    { id: 'hostname', label: 'Host name', kind: 'text', maxLength: 16, required: true },
    { id: 'bufferOffline', label: 'Buffer serial data while offline', kind: 'select', options: ON_OFF },
    { id: 'idleRestart', label: 'Restart when idle', kind: 'number', min: 60, max: 65535, zeroOk: true, unit: 's', help: 'Restart if no data flows for this long; 0 = never' },
    { id: 'rs485CollisionAvoid', label: 'RS-485 collision avoidance', kind: 'select', options: ON_OFF, help: 'Hold outgoing data until the bus is idle' },
    { id: 'rs485IdleTime', label: 'Bus idle time', kind: 'number', min: 0, max: 2500, unit: 'ms', show: (v) => Number(v.rs485CollisionAvoid) === 1 },
  ];
  const access: FieldDef[] = [
    { id: 'username', label: 'User name', kind: 'text', maxLength: 16, required: true },
    { id: 'password', label: 'Password', kind: 'password', maxLength: 16, required: true },
    { id: 'webPort', label: 'Web server port', kind: 'number', min: 1, max: 65535, help: 'Changing it moves this page to a new address' },
    { id: 'websocketPort', label: 'Websocket port', kind: 'number', min: 1, max: 65535 },
    {
      id: 'websocketTarget', label: 'Websocket connects to', kind: 'select',
      options: [[0, 'Off'], ...range(d.portmax).map((n) => [n, `UART${n}`] as const), [d.portmax + 1, 'Gateway log and shell']],
    },
  ];
  const time: FieldDef[] = [
    { id: 'ntp', label: 'Network time (NTP)', kind: 'select', options: ON_OFF },
    { id: 'ntpServer', label: 'NTP server', kind: 'text', maxLength: 128, required: true, show: (v) => Number(v.ntp) === 1 },
    { id: 'timezone', label: 'Time zone', kind: 'select', options: TIMEZONES },
  ];
  return { device, access, time, all: [...device, ...access, ...time] };
}

const stamp = (t: Date) => {
  const p = (n: number) => String(n).padStart(2, '0');
  return `${t.getFullYear()}-${p(t.getMonth() + 1)}-${p(t.getDate())} ${p(t.getHours())}:${p(t.getMinutes())}:${p(t.getSeconds())}`;
};

function ClockSync() {
  const [now, setNow] = useState(new Date());
  const sync = useAction();
  useInterval(() => setNow(new Date()), 1000);
  return (
    <Row label="Set from this computer" help="Writes the browser time to the gateway now, without waiting for Save.">
      <div class="flex items-center gap-3">
        <span class="num">{stamp(now)}</span>
        <Button size="sm" disabled={sync.busy} onClick={() => sync.run(() => api.syncTime(stamp(new Date())), 'Gateway clock set.')}>
          Set clock
        </Button>
      </div>
    </Row>
  );
}

export function SystemPage(_: { path?: string }) {
  const d = useDevice();
  const g = useMemo(() => defs(d), [d]);
  const f = useForm(() => api.getValues('system'), g.all);
  const ports = useAsync(() => Promise.all([api.getValues('port1'), d.portmax > 1 ? api.getValues('port2') : Promise.resolve(null)]), []);
  const save = useSaver();

  if (!f.ready) return <Pending error={f.error} retry={f.reload} />;
  // A port a UART listens on, shown on the web or WebSocket port field as it is edited (Plan 09b).
  const clash = systemPortClash(f.values, ports.data?.[0] ?? null, ports.data?.[1] ?? null);
  const errors = { ...f.errors, ...clash };
  const p = { values: f.values, errors, onChange: f.set };

  return (
    <>
      <PageHeader title="Settings" intro="Identity, access, services and clock." />
      <Panel title="Device"><Fields defs={g.device} {...p} /></Panel>
      <Panel title="Access"><Fields defs={g.access} {...p} /></Panel>
      <Panel title="Clock">
        <Fields defs={g.time} {...p} />
        <ClockSync />
      </Panel>
      <Panel title="Log">
        <Row label="Gateway log">
          <Button onClick={() => attempt(() => api.downloadLog(`${d.hostname || 'gateway'}-log.txt`))}>Download log</Button>
        </Row>
      </Panel>
      <SaveBar
        busy={save.busy}
        invalid={Object.keys(errors).length > 0 || !ports.data}
        note={ports.error ? 'The UART settings could not be read to check the ports: reload the page.' : undefined}
        onSave={() => save.run(() => api.saveValues('system', f.values), { restart: true, after: f.reload })}
      />
    </>
  );
}
