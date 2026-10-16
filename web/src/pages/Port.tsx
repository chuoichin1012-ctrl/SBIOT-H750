import { useMemo } from 'preact/hooks';
import * as api from '../lib/api';
import { useDevice } from '../lib/device';
import { useAsync, useForm } from '../lib/hooks';
import { listenClash } from '../lib/rules';
import { useSaver } from '../lib/save';
import type { DeviceInfo, Val, Values } from '../lib/types';
import {
  HEX_ASCII, IDENTITY, ON_OFF, TLS, TLS_VERIFY, allowListError, isHex, isIpv4, type FieldDef, type Options,
} from '../lib/validate';
import { CertUpload } from '../components/CertUpload';
import { PageHeader, Panel, Pending, SaveBar, Tabs } from '../components/ui';
import { Fields } from '../components/form';

// Working modes of the UART's network socket (`workMode`).
const UDP_CLIENT = 0, TCP_CLIENT = 1, UDP_SERVER = 2, TCP_SERVER = 3, HTTP_CLIENT = 4;
// Extra socket modes (`socketMode`).
const NONE = 0, MODBUS = 1, SHORT = 2, MODBUS_SHORT = 3, MULTICAST = 4;

const wm = (v: Values) => Number(v.workMode);
const sm = (v: Values) => Number(v.socketMode);
const is = (...modes: number[]) => (v: Values) => modes.includes(wm(v));
const isClient = is(UDP_CLIENT, TCP_CLIENT);
const modbusAck = (v: Values) => is(TCP_CLIENT, TCP_SERVER)(v) && (sm(v) === MODBUS || sm(v) === MODBUS_SHORT);

// Extra modes offered per working mode; UDP server and HTTP client have none.
const SOCK_MODES: Record<number, Options> = {
  [UDP_CLIENT]: [[NONE, 'None'], [MULTICAST, 'Multicast']],
  [TCP_CLIENT]: [[NONE, 'None'], [MODBUS, 'Modbus TCP gateway'], [SHORT, 'Short connection'], [MODBUS_SHORT, 'Modbus TCP + short connection']],
  [TCP_SERVER]: [[NONE, 'None'], [MODBUS, 'Modbus TCP gateway']],
};

/** Multicast group range accepted by the gateway. */
function multicastError(addr: string): string | null {
  if (!isIpv4(addr)) return 'Enter a multicast IPv4 address, 224.0.0.2–239.255.255.255.';
  const [a, b, c, d] = addr.split('.').map(Number);
  const n = ((a * 256 + b) * 256 + c) * 256 + d;
  const lo = ((224 * 256 + 0) * 256 + 0) * 256 + 2, hi = ((239 * 256 + 255) * 256 + 255) * 256 + 255;
  return n >= lo && n <= hi ? null : 'Multicast groups are 224.0.0.2–239.255.255.255.';
}

const hexCheck = (fmt: string) => (val: Val, v: Values) =>
  Number(v[fmt]) === 1 && val && !isHex(String(val)) ? 'Enter pairs of hex digits, e.g. 7E 01 FF.' : null;

function serialDefs(d: DeviceInfo): FieldDef[] {
  const beat = (v: Values) => !!v.serialHeartbeat;
  return [
    { id: 'baudRate', label: 'Baud rate', kind: 'number', min: d.baudMin, max: d.baudMax, unit: 'bps' },
    { id: 'dataBits', label: 'Data bits', kind: 'select', options: [[7, '7'], [8, '8']] },
    // No Mark/Space: the ESP32 UART has no such parity modes.
    { id: 'parity', label: 'Parity', kind: 'select', options: [[0, 'None'], [1, 'Odd'], [2, 'Even']] },
    { id: 'stopBits', label: 'Stop bits', kind: 'select', options: [[1, '1'], [2, '2']] },
    // RS485 is half duplex: the driver uses RTS for DE, and there is no CTS line.
    { id: 'flowControl', label: 'Flow control', kind: 'select', options: [[0, 'None'], [2, 'XON/XOFF']] },
    { id: 'packetLength', label: 'Packet length', kind: 'number', min: 0, max: 1460, unit: 'bytes', help: '0 lets the gateway decide' },
    { id: 'packetInterval', label: 'Packet interval', kind: 'number', min: 0, max: 255, unit: 'ms', help: 'Idle gap that ends a packet; 0 = automatic' },
    { id: 'serialHeartbeat', label: 'Serial heartbeat', kind: 'check', help: 'Send a heartbeat to the serial device' },
    { id: 'serialHeartbeatContent', label: 'Heartbeat content', kind: 'select', options: IDENTITY, show: beat },
    { id: 'serialHeartbeatHex', label: 'Data format', kind: 'radio', options: HEX_ASCII, show: (v) => beat(v) && Number(v.serialHeartbeatContent) === 0 },
    {
      id: 'serialHeartbeatData', label: 'Heartbeat data', kind: 'text', maxLength: 40, required: true,
      show: (v) => beat(v) && Number(v.serialHeartbeatContent) === 0, check: hexCheck('serialHeartbeatHex'),
    },
    { id: 'serialHeartbeatInterval', label: 'Heartbeat interval', kind: 'number', min: 1, max: 65535, unit: 's', show: beat },
  ];
}

/**
 * Network side of a UART. Which fields appear follows the working mode, as on
 * the reference gateway. TLS is available on every UART.
 */
function socketDefs(): FieldDef[] {
  const tlsMode = is(TCP_CLIENT, HTTP_CLIENT);
  const beat = is(UDP_CLIENT, TCP_CLIENT, UDP_SERVER);
  return [
    {
      id: 'workMode', label: 'Working mode', kind: 'select',
      options: [[UDP_CLIENT, 'UDP client'], [TCP_CLIENT, 'TCP client'], [UDP_SERVER, 'UDP server'], [TCP_SERVER, 'TCP server'], [HTTP_CLIENT, 'HTTP client']],
    },
    { id: 'socketMode', label: 'Extra mode', kind: 'select', options: (v) => SOCK_MODES[wm(v)] ?? [], show: (v) => wm(v) in SOCK_MODES },

    // Remote peer: clients, and the UDP server's default peer.
    {
      id: 'remoteHost', label: 'Remote address', kind: 'text', maxLength: 64, required: true,
      show: is(UDP_CLIENT, TCP_CLIENT, UDP_SERVER),
      help: 'IP address or domain name',
      check: (a, v) => (wm(v) === UDP_CLIENT && sm(v) === MULTICAST ? multicastError(String(a)) : null),
    },
    { id: 'remotePort', label: 'Remote port', kind: 'number', min: 1, max: 65535, show: is(UDP_CLIENT, TCP_CLIENT, UDP_SERVER) },
    {
      id: 'localPort', label: 'Local port', kind: 'number', min: 0, max: 65535, show: is(UDP_CLIENT, TCP_CLIENT, UDP_SERVER),
      help: '0 picks a random port (clients only)',
      check: (p, v) => (wm(v) === UDP_SERVER && Number(p) === 0 ? 'A server needs a fixed port, 1–65535.' : null),
    },
    {
      id: 'udpAnyRemotePort', label: 'Accept from any remote port', kind: 'select', options: ON_OFF,
      show: (v) => wm(v) === UDP_CLIENT && sm(v) !== MULTICAST,
    },

    // TCP server
    { id: 'listenPort', label: 'Listening port', kind: 'number', min: 1, max: 65535, show: is(TCP_SERVER) },
    {
      id: 'maxClients', label: 'Maximum clients', kind: 'select', show: is(TCP_SERVER),
      options: Array.from({ length: 16 }, (_, i) => [i + 1, String(i + 1)] as const),
    },
    { id: 'clientLimitAction', label: 'When the limit is reached', kind: 'select', options: [[0, 'Drop the oldest client'], [1, 'Refuse the new client']], show: is(TCP_SERVER) },
    {
      id: 'allowedClients', label: 'Allowed clients', kind: 'text', maxLength: 140, show: is(TCP_SERVER, UDP_SERVER),
      help: 'Empty: anyone. Otherwise up to 8 IPv4 addresses or ranges, e.g. 192.168.1.0/24, 10.0.0.5',
      check: (s) => allowListError(String(s ?? '')),
    },

    // HTTP client
    { id: 'httpHost', label: 'HTTP server address', kind: 'text', maxLength: 64, required: true, show: is(HTTP_CLIENT) },
    { id: 'httpPort', label: 'HTTP server port', kind: 'number', min: 1, max: 65535, show: is(HTTP_CLIENT) },
    { id: 'httpLocalPort', label: 'Local port', kind: 'number', min: 0, max: 65535, show: is(HTTP_CLIENT), help: '0 picks a random port' },
    { id: 'httpMethod', label: 'HTTP method', kind: 'select', options: [[0, 'GET'], [1, 'POST']], show: is(HTTP_CLIENT) },
    { id: 'httpPath', label: 'URL path', kind: 'text', maxLength: 100, show: is(HTTP_CLIENT), help: 'e.g. /api/data?' },
    { id: 'httpHeaders', label: 'Request headers', kind: 'textarea', maxLength: 180, show: is(HTTP_CLIENT) },
    { id: 'httpStripHeaders', label: 'Response headers', kind: 'check', help: 'Strip headers, forward only the body to the serial port', show: is(HTTP_CLIENT) },
    { id: 'httpTimeout', label: 'Response timeout', kind: 'number', min: 3, max: 255, unit: 's', show: is(HTTP_CLIENT) },

    // Connection behaviour (TCP client)
    { id: 'reconnectInterval', label: 'Reconnect interval', kind: 'number', min: 0, max: 99999, unit: 's', show: is(TCP_CLIENT) },
    {
      id: 'shortConnectionTime', label: 'Short connection lifetime', kind: 'number', min: 3, max: 255, unit: 's',
      show: (v) => wm(v) === TCP_CLIENT && (sm(v) === SHORT || sm(v) === MODBUS_SHORT),
    },

    // Modbus: polling is available on any TCP socket; the exception reply needs the Modbus TCP gateway.
    { id: 'modbusPoll', label: 'Modbus polling', kind: 'check', help: 'Gateway polls the serial slave in order', show: is(TCP_CLIENT, TCP_SERVER) },
    { id: 'modbusPollTimeout', label: 'Polling timeout', kind: 'number', min: 10, max: 9999, unit: 'ms', show: (v) => is(TCP_CLIENT, TCP_SERVER)(v) && !!v.modbusPoll },
    { id: 'modbusException', label: 'Modbus TCP exception', kind: 'check', help: 'Reply with an exception when the slave does not answer', show: modbusAck },

    // Network heartbeat: UDP client, TCP client, UDP server.
    { id: 'netHeartbeat', label: 'Network heartbeat', kind: 'check', help: 'Send a heartbeat to the peer', show: beat },
    { id: 'netHeartbeatContent', label: 'Heartbeat content', kind: 'select', options: IDENTITY, show: (v) => beat(v) && !!v.netHeartbeat },
    { id: 'netHeartbeatHex', label: 'Data format', kind: 'radio', options: HEX_ASCII, show: (v) => beat(v) && !!v.netHeartbeat && Number(v.netHeartbeatContent) === 0 },
    {
      id: 'netHeartbeatData', label: 'Heartbeat data', kind: 'text', maxLength: 40, required: true, check: hexCheck('netHeartbeatHex'),
      show: (v) => beat(v) && !!v.netHeartbeat && Number(v.netHeartbeatContent) === 0,
    },
    { id: 'netHeartbeatInterval', label: 'Heartbeat interval', kind: 'number', min: 1, max: 65535, unit: 's', show: (v) => beat(v) && !!v.netHeartbeat },

    // Registration packet: clients only.
    {
      id: 'registration', label: 'Registration packet', kind: 'select', show: isClient,
      options: [[0, 'None'], [1, 'Custom data'], [2, 'MAC address'], [3, 'IMEI'], [4, 'Serial number'], [5, 'ICCID']],
    },
    {
      id: 'registrationSend', label: 'Send registration', kind: 'select', show: (v) => isClient(v) && Number(v.registration) !== 0,
      options: [[0, 'Once, on connect'], [1, 'Before every packet'], [2, 'Both']],
    },
    { id: 'registrationHex', label: 'Data format', kind: 'radio', options: HEX_ASCII, show: (v) => isClient(v) && Number(v.registration) === 1 },
    {
      id: 'registrationData', label: 'Registration data', kind: 'text', maxLength: 40, required: true, check: hexCheck('registrationHex'),
      show: (v) => isClient(v) && Number(v.registration) === 1,
    },

    // TLS: TCP client and HTTP client (HTTPS), on every UART.
    { id: 'tls', label: 'TLS', kind: 'select', options: TLS, show: tlsMode },
    { id: 'tlsVerify', label: 'Certificate check', kind: 'select', options: TLS_VERIFY, show: (v) => tlsMode(v) && Number(v.tls) > 0 },
  ];
}

export function PortPage(props: { path?: string; n?: string; tab?: string }) {
  const d = useDevice();
  const n = Math.min(Math.max(Number(props.n) || 1, 1), d.portmax);
  const tab = props.tab === 'socket' ? 'socket' : 'serial';
  const serial = useMemo(() => serialDefs(d), [d]);
  const socket = useMemo(() => socketDefs(), []);
  const all = useMemo(() => [...serial, ...socket], [serial, socket]);
  const f = useForm(() => api.getValues(`port${n}`), all, [n]);
  // The listen port across configs (Plan 09b): the gateway stores what it is sent, so the UI checks it.
  const others = useAsync(() => Promise.all([api.getValues(`port${n === 1 ? 2 : 1}`).catch(() => null), api.getValues('system').catch(() => null)]), [n]);
  const save = useSaver();

  if (!f.ready) return <Pending error={f.error} retry={f.reload} />;

  const onChange = (id: string, v: Val) => {
    f.set(id, v);
    // Keep the extra mode valid when the working mode changes.
    if (id === 'workMode' && !(SOCK_MODES[Number(v)] ?? []).some(([m]) => m === sm(f.values))) f.set('socketMode', NONE);
  };
  // The listen port's clash with the other UART or the web, shown on the field as it is edited (Plan 09b).
  const clash = listenClash(n, f.values, others.data?.[0] ?? null, others.data?.[1] ?? null);
  const errors = clash ? { ...f.errors, listenPort: clash } : f.errors;
  const p = { values: f.values, errors, onChange };
  const serialErr = serial.some((x) => errors[x.id]);
  const socketErr = socket.some((x) => errors[x.id]);
  const tls = socket.some((x) => x.id === 'tls' && x.show?.(f.values)) && Number(f.values.tls) > 0;

  return (
    <>
      <PageHeader title={`UART${n} (RS485)`} intro="Serial settings, and how serial data is carried over the network." />
      <Tabs base={`/port/${n}`} active={tab} tabs={[['serial', `Serial${serialErr ? ' (check)' : ''}`], ['socket', `Network${socketErr ? ' (check)' : ''}`]]} />
      {tab === 'serial' ? (
        <Panel title="Serial line"><Fields defs={serial} {...p} /></Panel>
      ) : (
        <Panel title="Network socket">
          <Fields defs={socket} {...p} />
          {tls && <CertUpload key={`port${n}`} scope={`port${n}`} verify={Number(f.values.tlsVerify)} />}
        </Panel>
      )}
      <SaveBar
        busy={save.busy}
        invalid={!f.valid || !!clash}
        tabbed
        onSave={() => save.run(() => api.saveValues(`port${n}`, f.values), { restart: true, after: f.reload })}
      />
    </>
  );
}
