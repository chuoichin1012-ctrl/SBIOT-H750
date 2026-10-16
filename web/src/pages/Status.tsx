import { useAsync, useInterval } from '../lib/hooks';
import { useDevice } from '../lib/device';
import * as api from '../lib/api';
import type { StatusInfo } from '../lib/types';
import { DataTable, Info, Led, PageHeader, Panel, Pending, Stale, type LedState } from '../components/ui';

function uptime(s: number) {
  const d = Math.floor(s / 86400);
  const h = Math.floor((s % 86400) / 3600);
  const m = Math.floor((s % 3600) / 60);
  return `${d ? `${d} d ` : ''}${h} h ${m} min`;
}

/** CSQ to a plain description; 99 means the modem cannot measure it. */
function signal(csq: number) {
  if (csq === 99 || csq < 0) return 'No measurement';
  const dbm = -113 + 2 * csq;
  const word = csq >= 20 ? 'good' : csq >= 10 ? 'fair' : 'weak';
  return `${csq} (${dbm} dBm, ${word})`;
}

const connState = (s: string): LedState =>
  /connected|established|running/i.test(s) ? 'ok' : /listen|connecting|wait/i.test(s) ? 'warn' : s ? 'off' : 'off';

const bytes = (n: number) => n.toLocaleString('en-US');
const kb = (n: number | undefined) => `${Math.round((n ?? 0) / 1024).toLocaleString('en-US')} KB`;

/** Polling interval; kept modest because every poll is a request the MCU must serve. */
const REFRESH_MS = 5000;

export function StatusPage(_: { path?: string }) {
  const d = useDevice();
  const st = useAsync(api.getStatus);
  useInterval(st.reload, REFRESH_MS);

  if (!st.data) return <Pending error={st.error} retry={st.reload} />;
  const s: StatusInfo = st.data;
  const timeSuspect = s.system.clockSet === false;

  const panel: [string, LedState, string][] = [
    ['Ethernet', s.system.activeNet === 'ETH' ? 'ok' : s.eth.ip ? 'warn' : 'off', s.eth.ip || 'No address'],
    ['Cellular', s.cell.state === 'connected' ? 'ok' : s.cell.state === 'connecting' ? 'warn' : 'off', s.cell.state === 'connected' ? s.cell.netType : 'Not connected'],
    ['MQTT', s.mqtt.enabled ? connState(s.mqtt.state) : 'off', s.mqtt.enabled ? s.mqtt.state : 'Disabled'],
    ['Edge computing', s.edge.enabled ? connState(s.edge.state) : 'off', s.edge.enabled ? s.edge.state : 'Disabled'],
  ];

  return (
    <>
      <PageHeader title="Status" intro={`Live state of the gateway. Refreshes every ${REFRESH_MS / 1000} seconds.`} />
      <Stale error={st.error} />

      <section class="mb-4 grid grid-cols-2 gap-3 md:grid-cols-4" aria-label="Indicators">
        {panel.map(([name, led, text]) => (
          <div key={name} class="rounded-panel border border-line bg-surface px-4 py-3.5 shadow-[inset_0_3px_0_var(--color-brand)]">
            <span class="font-medium"><Led state={led} label={name} /></span>
            <p class="num mt-1 truncate pl-[18px] text-muted">{text}</p>
          </div>
        ))}
      </section>

      <div class="grid gap-x-4 md:grid-cols-2">
        <Panel title="System">
          <dl>
            <Info label="Model">{d.model}</Info>
            <Info label="Firmware">{d.firmware}</Info>
            <Info label="Hardware">{d.hardware}</Info>
            <Info label="Operating system">{s.system.os}</Info>
            <Info label="Uptime">{uptime(s.system.uptime)}</Info>
            <Info label="Free memory">
              {kb(s.system.heapFree)} RAM (lowest {kb(s.system.heapMin)}){s.system.psramFree ? `, ${kb(s.system.psramFree)} PSRAM` : ''}
            </Info>
            <Info label="System time">
              {s.system.time}
              {timeSuspect && <span class="block text-[13px] text-led-warn">Clock not set. Check NTP in Settings.</span>}
            </Info>
            <Info label="Active network">{s.system.activeNet === 'ETH' ? 'Ethernet' : s.system.activeNet === 'LTE' ? 'Cellular' : 'None'}</Info>
            <Info label="MAC">{s.system.mac}</Info>
            <Info label="Serial number">{s.system.sn}</Info>
            <Info label="IMEI">{s.system.imei || '—'}</Info>
          </dl>
        </Panel>

        <div>
          <Panel title="Ethernet">
            <dl>
              <Info label="IP address">{s.eth.ip || '—'}</Info>
              <Info label="Subnet mask">{s.eth.mask || '—'}</Info>
              <Info label="Gateway">{s.eth.gateway || '—'}</Info>
              <Info label="DNS">{[s.eth.dns1, s.eth.dns2].filter(Boolean).join(', ') || '—'}</Info>
            </dl>
          </Panel>
          <Panel title="Cellular">
            <dl>
              <Info label="Connection"><Led state={s.cell.state === 'connected' ? 'ok' : 'off'} label={{ connected: 'Connected', connecting: 'Connecting', disconnected: 'Not connected' }[s.cell.state]} /></Info>
              <Info label="Network type">{s.cell.netType || '—'}</Info>
              <Info label="Signal">{signal(s.cell.signal)}</Info>
              <Info label="IP address">{s.cell.ip || '—'}</Info>
              <Info label="DNS">{[s.cell.dns1, s.cell.dns2].filter(Boolean).join(', ') || '—'}</Info>
              <Info label="ICCID">{s.cell.iccid || '—'}</Info>
            </dl>
          </Panel>
        </div>
      </div>

      <Panel title="Serial port connections" flush>
        <DataTable
          rows={s.ports.slice(0, d.portmax)}
          keyOf={(_, i) => `uart${i + 1}`}
          empty="No serial ports reported."
          columns={[
            { head: 'Port', cell: (_, i) => `UART${i + 1}` },
            { head: 'State', cell: (r) => <Led state={connState(r.state)} label={r.state || 'Not used'} /> },
            { head: 'Sent (bytes)', cell: (r) => bytes(r.tx), align: 'right' },
            { head: 'Received (bytes)', cell: (r) => bytes(r.rx), align: 'right' },
          ]}
        />
        <p class="px-4 py-2 text-[13px] text-muted">
          Listening only means the socket waits for a peer. Rising byte counts confirm that data flows.
        </p>
      </Panel>
    </>
  );
}
