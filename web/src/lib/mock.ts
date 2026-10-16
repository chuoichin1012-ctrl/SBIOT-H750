// In-memory gateway for `npm run dev:mock`. Never included in production
// builds (api.ts only imports it when MODE === 'mock'). Values mirror the
// state observed on the surveyed unit (docs/, §2).

import type { ConsoleSocket } from './api';
import type { DeviceInfo, EdgeSlave, IoState, LinkageEvent, StatusInfo, Values } from './types';

const device: DeviceInfo = {
  model: 'SBIOT-H750', firmware: '0.1.0', hardware: 'STM32H750VBT6', portmax: 2, baudMin: 600, baudMax: 230400,
  io: { do: 2, di: 2, ai: 2, ao: 0 }, hostname: 'gw-line-01', wsDirection: 2, wsPort: 6432,
};

const port = (n: number): Values => ({
  baudRate: 115200, dataBits: 8, parity: 0, stopBits: 1, flowControl: 0, packetLength: 0, packetInterval: 0,
  serialHeartbeat: false, serialHeartbeatContent: 0, serialHeartbeatHex: 1, serialHeartbeatData: '', serialHeartbeatInterval: 30,
  workMode: n === 1 ? 3 : 1, socketMode: 0, remoteHost: 'data.example.net', remotePort: 9000, localPort: 0, listenPort: 23,
  maxClients: 8, clientLimitAction: 0, allowedClients: '', udpAnyRemotePort: 0, httpHost: '', httpPort: 80, httpLocalPort: 0, httpMethod: 0,
  httpPath: '/', httpHeaders: '', httpStripHeaders: false, httpTimeout: 10, reconnectInterval: 5, shortConnectionTime: 10,
  modbusPoll: false, modbusPollTimeout: 200, modbusException: false,
  netHeartbeat: false, netHeartbeatContent: 0, netHeartbeatHex: 1, netHeartbeatData: '', netHeartbeatInterval: 60,
  registration: 0, registrationSend: 0, registrationHex: 1, registrationData: '', tls: 0, tlsVerify: 0,
});

const ioConfig: Values = {
  modbusAddress: 100, keepOutputs: 0, do0Mode: 0, do0FlipDelay: 5, do1Mode: 0, do1FlipDelay: 5,
  diDebounce: 50, diInvert: 0,
  ...Object.fromEntries(Array.from({ length: 6 }, (_, t) => [
    [`timer${t}Enabled`, 0], [`timer${t}Time`, '08:00:00'], [`timer${t}Action`, 0], [`timer${t}Output`, 0],
    [`timer${t}OutputAction`, 0],
  ]).flat()),
};

const slaves: EdgeSlave[] = [
  {
    name: 'device01', source: 'uart', uart: 1, ip: '', ethPort: 502, protocol: 'modbus', addr: '1', poll: 1000,
    mapEn: false, mapAddr: 1, merge: false,
    nodes: [
      { name: 'node0101', fc: 3, reg: 0, di: '', dtype: 'uint16', timeout: 200, onChange: false, range: 1, mapEn: false, mapAddr: 0, formula: '', ioPoint: '', ioReport: 0, upper: 0, lower: 0 },
      { name: 'node0102', fc: 3, reg: 1, di: '', dtype: 'uint16', timeout: 200, onChange: false, range: 1, mapEn: false, mapAddr: 0, formula: '=%s*0.1', ioPoint: '', ioReport: 0, upper: 0, lower: 0 },
    ],
  },
];

const topic = (t: string): Values => ({
  enabled: true, mode: 0, topic: t, ports: 1, qos: 1, retain: false, ioControl: false, identifier: '', delimiter: ',',
});

/** Keyed like /api/config/<name>, plus "device" for /api/device. */
const store: Record<string, unknown> = {
  device,
  network: {
    priority: 1, ipMode: 1, dnsMode: 1, ip: '192.168.0.7', netmask: '255.255.255.0', gateway: '192.168.0.1',
    dns1: '119.29.29.29', dns2: '8.8.8.8', apn: '', apnUser: '', apnPassword: '', apnAuth: 0,
    cellDnsMode: 0, cellDns1: '', cellDns2: '',
  },
  port1: port(1),
  port2: port(2),
  mqtt: {
    enabled: 0, version: 1, clientId: 'gw-line-01', host: 'broker.example.net', port: 1883, keepAlive: 60,
    reconnectIfSilent: 0, retryInterval: 5, cleanSession: true, auth: false, username: '', password: '', will: false,
    willTopic: '', willMessage: '', willQos: 0, willRetain: false, tls: 0, tlsVerify: 0, sni: false, sniHost: '',
    customMode: 0, customModePorts: 1,
  },
  mqttPublish: [topic('site/gw-line-01/up')],
  mqttSubscribe: [topic('site/gw-line-01/down')],
  system: {
    hostname: 'gw-line-01', websocketPort: 6432, websocketTarget: 2, webPort: 80, username: 'admin', password: 'admin',
    bufferOffline: 0, idleRestart: 3600, ntp: 1, ntpServer: 'pool.ntp.org', timezone: 7,
    rs485CollisionAvoid: 0, rs485IdleTime: 50,
  },
  io: ioConfig,
  edge: { enabled: true },
  edgeDevices: slaves,
  edgeReport: {
    channel: 1, remoteAccess: 0, remoteWrite: false, remoteFormat: 2, requestTopic: '', requestQos: 0, replyTopic: '', replyQos: 0,
    replyRetain: false, reporting: 1, reportTopic: '', reportQos: 0, reportRetain: false, periodic: true, interval: 5,
    scheduled: false, schedule: 0, scheduleHour: 0, scheduleMinute: 0, failurePlaceholder: false, placeholder: 'null',
    quoteValues: false, template: '{"Current":"node0101","Voltage":"node0102"}',
  },
  edgeLinkage: [] as LinkageEvent[],
};

const io: IoState = { do: [false, true], di: [true, false], ai: [12500, 8000], ao: [] };
const started = Date.now();

function status(): StatusInfo {
  const t = Math.floor((Date.now() - started) / 1000);
  return {
    system: {
      uptime: 86400 * 3 + 7200 + t, os: 'FreeRTOS', mac: 'D4:AD:20:00:12:34', imei: '', sn: 'SB2410000123',
      activeNet: 'ETH', time: new Date().toISOString().replace('T', ' ').slice(0, 19), clockSet: true,
      heapFree: 182000, heapMin: 151000, psramFree: 3900000,
    },
    eth: { ip: '192.168.0.7', mask: '255.255.255.0', gateway: '192.168.0.1', dns1: '119.29.29.29', dns2: '8.8.8.8' },
    cell: { iccid: '89840200011761973724', ip: '10.110.205.176', dns1: '203.113.131.1', dns2: '203.113.131.2', signal: 18, netType: 'LTE', state: 'connected' },
    ports: [
      { state: 'LISTEN', tx: 1820 + t * 16, rx: 960 + t * 8 },
      { state: 'CONNECTED(1)', tx: 400 + t * 4, rx: 220 + t * 2 },
    ],
    mqtt: { enabled: false, state: '' },
    edge: { enabled: true, state: 'LISTEN' },
  };
}

const json = (data: unknown, status = 200) =>
  new Response(JSON.stringify(data), { status, headers: { 'Content-Type': 'application/json' } });
const wait = (ms: number) => new Promise((r) => setTimeout(r, ms));

/** Mock login: admin / admin, or whatever was last saved in Settings. */
function authorized(init?: RequestInit) {
  const sys = store.system as Values;
  const h = (init?.headers ?? {}) as Record<string, string>;
  return h.Authorization === `Basic ${btoa(`${sys.username}:${sys.password}`)}`;
}

const LOG = '[00:00:01] boot ok\n[00:00:03] eth up 192.168.0.7\n';

/** In-memory implementation of the /api contract documented in api.ts. */
export async function mockFetch(url: string, init?: RequestInit): Promise<Response> {
  await wait(150);
  if (!authorized(init)) return new Response('unauthorized', { status: 401 });
  const path = new URL(url, location.href).pathname.replace(/^.*\/api\//, '');
  const post = init?.method === 'POST';
  const body = post && typeof init?.body === 'string' ? JSON.parse(init.body) : undefined;

  if (path === 'device') return json(store.device);
  if (path === 'status') return json(status());
  if (path === 'log') return new Response(LOG, { status: 200 });
  if (path === 'io') {
    if (post) {
      if ('do' in body) io.do[Number(body.do)] = !!body.value;
      if ('ao' in body) io.ao[Number(body.ao)] = Number(body.value);
      return json({ ok: true });
    }
    io.ai = io.ai.map((v) => Math.max(0, Math.min(20000, v + Math.round((Math.random() - 0.5) * 200))));
    return json(io);
  }
  if (path === 'time' || path === 'restart' || path === 'factory-reset') return json({ ok: true });
  if (path.startsWith('config/')) {
    const key = path.slice('config/'.length);
    if (!(key in store) || key === 'device') return json({ error: `Unknown config ${key}.` }, 404);
    if (post) store[key] = body;
    return json(store[key]);
  }
  return json({ error: 'Not found.' }, 404);
}

/** Firmware and certificate uploads land here. */
export async function mockUpload(url: string, _body: Blob, onProgress?: (pct: number) => void): Promise<unknown> {
  for (let p = 0; p <= 100; p += 20) {
    onProgress?.(p);
    await wait(120);
  }
  if (!url.includes('/api/cert')) return undefined;
  const key = url.includes('kind=key');
  return key ? { keyType: 'EC', pairMatches: true, restartNeeded: url.includes('scope=mqtt') }
    : { subject: 'CN=broker.example', validTo: '2027-01-31', expired: false, restartNeeded: url.includes('scope=mqtt') };
}

export function mockSocket(): ConsoleSocket {
  const sock: ConsoleSocket = { send: () => {}, close: () => {}, onopen: null, onclose: null, onerror: null, onmessage: null };
  let timer = 0;
  sock.send = (d) => {
    const text = typeof d === 'string' ? d : `[${new Uint8Array(d).length} bytes]`;
    setTimeout(() => sock.onmessage?.(`> ${text}\nok\n`), 100);
  };
  sock.close = () => {
    clearInterval(timer);
    sock.onclose?.();
  };
  setTimeout(() => {
    sock.onopen?.();
    timer = window.setInterval(() => sock.onmessage?.(`[${new Date().toISOString().slice(11, 19)}] edge: poll device01 ok\n`), 2000);
  }, 200);
  return sock;
}
