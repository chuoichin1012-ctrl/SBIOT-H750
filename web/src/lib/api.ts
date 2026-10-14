// Every request to the gateway goes through this module.
//
// Contract (implemented by firmware/components/webapi):
//   GET  /api/device                 DeviceInfo
//   GET  /api/status                 StatusInfo
//   GET  /api/config/<name>          saved settings (object, or array for lists)
//   POST /api/config/<name>          replace settings; JSON body, same shape as GET
//   GET  /api/io                     IoState
//   POST /api/io                     { "do": i, "value": bool } | { "ao": i, "value": µA }
//   POST /api/time                   { "time": "YYYY-MM-DD hh:mm:ss" }
//   POST /api/restart                (no body)
//   POST /api/factory-reset          (no body)
//   POST /api/ota                    raw firmware image (application/octet-stream)
//   POST /api/cert?scope=&kind=      raw PEM file (application/octet-stream)
//   GET  /api/log                    text/plain
// DO/AO indexes are 0-based; UART numbers in names are 1-based (`port1`).
// Errors: non-2xx with an optional { "error": "message" } body.
// See types.ts and each page's FieldDefs for the payload fields.

import { authHeaders, encodeCredentials, session, signIn, signOut } from './auth';
import type {
  DeviceInfo, EdgeSlave, IoState, LinkageEvent, StatusInfo, Values,
} from './types';

export class ApiError extends Error {}

/** Config names accepted by /api/config/<name>. */
export type ConfigName =
  | 'network' | `port${number}` | 'mqtt' | 'mqttPublish' | 'mqttSubscribe' | 'system' | 'io'
  | 'edge' | 'edgeDevices' | 'edgeReport' | 'edgeLinkage';

interface Req {
  method?: 'GET' | 'POST';
  body?: BodyInit;
  type?: string;
  token?: string | null;
}

async function http(url: string, req: Req = {}): Promise<Response> {
  const headers: Record<string, string> = authHeaders(req.token ?? undefined);
  if (req.type) headers['Content-Type'] = req.type;
  const init: RequestInit = { method: req.method ?? 'GET', cache: 'no-store', headers, body: req.body };
  if (import.meta.env.MODE === 'mock') {
    const { mockFetch } = await import('./mock');
    return mockFetch(url, init);
  }
  return fetch(url, init);
}

const SESSION_ENDED = 'Your session ended, or the login was changed. Sign in again.';

async function failure(res: Response, url: string): Promise<ApiError> {
  let detail = '';
  try {
    detail = ((await res.json()) as { error?: string }).error ?? '';
  } catch {
    /* no JSON body */
  }
  return new ApiError(detail || `The gateway rejected ${url.split('?')[0]} (HTTP ${res.status}).`);
}

async function call(url: string, req?: Req): Promise<Response> {
  let res: Response;
  try {
    res = await http(url, req);
  } catch {
    throw new ApiError('The gateway did not respond. Check the network connection.');
  }
  if (res.status === 401) {
    signOut(SESSION_ENDED);
    throw new ApiError(SESSION_ENDED);
  }
  if (!res.ok) throw await failure(res, url);
  return res;
}

async function getJson<T>(url: string): Promise<T> {
  const res = await call(url);
  try {
    return (await res.json()) as T;
  } catch {
    throw new ApiError(`${url} did not return valid JSON.`);
  }
}

const postJson = (url: string, data?: unknown) =>
  call(url, {
    method: 'POST',
    body: data === undefined ? undefined : JSON.stringify(data),
    type: 'application/json',
  }).then(() => {});

/** Checks the credentials against the gateway, then starts the session. */
export async function login(user: string, password: string): Promise<void> {
  const token = encodeCredentials(user, password);
  let res: Response;
  try {
    res = await http('./api/device', { token });
  } catch {
    throw new ApiError('The gateway did not respond. Check that this computer can reach it.');
  }
  if (res.status === 401) throw new ApiError('Wrong user name or password.');
  if (!res.ok) throw new ApiError(`The gateway could not sign you in (HTTP ${res.status}).`);
  signIn(token);
}

/** Raw-body upload with progress (XHR, because fetch has no upload progress). */
function upload<T = unknown>(url: string, body: Blob, onProgress?: (pct: number) => void): Promise<T> {
  if (import.meta.env.MODE === 'mock') {
    return import('./mock').then((m) => m.mockUpload(url, body, onProgress) as Promise<T>);
  }
  return new Promise((resolve, reject) => {
    const xhr = new XMLHttpRequest();
    xhr.open('POST', url);
    for (const [k, v] of Object.entries(authHeaders())) xhr.setRequestHeader(k, v);
    xhr.setRequestHeader('Content-Type', 'application/octet-stream');
    xhr.upload.onprogress = (e) => {
      if (e.lengthComputable && onProgress) onProgress(Math.round((e.loaded / e.total) * 100));
    };
    xhr.onload = () => {
      if (xhr.status >= 200 && xhr.status < 300) {
        try {
          return resolve(JSON.parse(xhr.responseText) as T);
        } catch {
          return resolve(undefined as T);
        }
      }
      if (xhr.status === 401) signOut(SESSION_ENDED);
      let detail = '';
      try {
        detail = (JSON.parse(xhr.responseText) as { error?: string }).error ?? '';
      } catch {
        /* plain text */
      }
      reject(new ApiError(xhr.status === 401 ? SESSION_ENDED : detail || `Upload rejected (HTTP ${xhr.status}).`));
    };
    xhr.onerror = () => reject(new ApiError('Upload interrupted. Check the connection and try again.'));
    xhr.send(body);
  });
}

// ---- Reads ---------------------------------------------------------------

export const getDevice = () => getJson<DeviceInfo>('./api/device');
export const getStatus = () => getJson<StatusInfo>('./api/status');
export const getValues = (name: ConfigName) => getJson<Values>(`./api/config/${name}`);
export const getList = (name: ConfigName) => getJson<Values[]>(`./api/config/${name}`);
export const readIo = () => getJson<IoState>('./api/io');

export interface EdgeConfig {
  enabled: boolean;
  slaves: EdgeSlave[];
  report: Values;
  linkage: LinkageEvent[];
}

export async function getEdge(): Promise<EdgeConfig> {
  const [edge, slaves, report, linkage] = await Promise.all([
    getJson<{ enabled: boolean }>('./api/config/edge'),
    getJson<EdgeSlave[]>('./api/config/edgeDevices'),
    getJson<Values>('./api/config/edgeReport'),
    getJson<LinkageEvent[]>('./api/config/edgeLinkage'),
  ]);
  return { enabled: !!edge.enabled, slaves, report, linkage };
}

// ---- Writes --------------------------------------------------------------

export const saveValues = (name: ConfigName, values: Values) => postJson(`./api/config/${name}`, values);
export const saveList = (name: ConfigName, rows: Values[]) => postJson(`./api/config/${name}`, rows);

/**
 * Saves the Edge model part by part and stops at the first failure, so the
 * caller can report exactly which part was not saved. The enable switch is
 * written last, after all models were accepted.
 */
export async function saveEdge(cfg: EdgeConfig, step?: (label: string) => void): Promise<void> {
  const parts: [string, ConfigName, unknown][] = [
    ['data acquisition', 'edgeDevices', cfg.slaves],
    ['linkage events', 'edgeLinkage', cfg.linkage],
    ['reporting', 'edgeReport', cfg.report],
  ];
  for (const [label, name, data] of parts) {
    step?.(label);
    try {
      await postJson(`./api/config/${name}`, data);
    } catch (e) {
      throw new ApiError(`Saving ${label} failed: ${(e as Error).message} Parts before it were saved.`);
    }
  }
  step?.('enable switch');
  await postJson('./api/config/edge', { enabled: cfg.enabled });
}

export const writeDo = (index: number, on: boolean) => postJson('./api/io', { do: index, value: on });
export const writeAo = (index: number, value: number) => postJson('./api/io', { ao: index, value });
export const syncTime = (time: string) => postJson('./api/time', { time });
export const restart = () => postJson('./api/restart');
export const factoryReset = () => postJson('./api/factory-reset');

/** A plain link cannot carry the auth header, so the log is fetched, then saved. */
export async function downloadLog(filename: string) {
  const blob = await (await call('./api/log')).blob();
  const a = document.createElement('a');
  a.href = URL.createObjectURL(blob);
  a.download = filename;
  a.click();
  URL.revokeObjectURL(a.href);
}

export const uploadFirmware = (file: File, onProgress: (pct: number) => void) => upload<void>('./api/ota', file, onProgress);

/** scope: `mqtt`, `port1` or `port2`; kind: server CA, client cert, client key. */
/** What the gateway read from an uploaded TLS file. */
export interface CertInfo {
  subject?: string;
  validTo?: string; // YYYY-MM-DD
  expired?: boolean; // absent while the gateway clock is not set
  keyType?: string;
  pairMatches?: boolean; // client certificate vs key, when both are stored
  restartNeeded?: boolean;
}
export const uploadCert = (scope: string, kind: 'ca' | 'cert' | 'key', file: File) =>
  upload<CertInfo>(`./api/cert?scope=${encodeURIComponent(scope)}&kind=${kind}`, file);

/** Minimal socket surface used by the Websocket page (real or mocked). */
export interface ConsoleSocket {
  send(data: string | ArrayBuffer): void;
  close(): void;
  onopen: (() => void) | null;
  onclose: (() => void) | null;
  onerror: (() => void) | null;
  onmessage: ((data: string | ArrayBuffer) => void) | null;
}

export async function openConsole(port: number): Promise<ConsoleSocket> {
  if (import.meta.env.MODE === 'mock') return (await import('./mock')).mockSocket();
  // Browsers cannot set headers on a WebSocket; the credentials travel as a
  // query parameter instead and the firmware checks them on the upgrade.
  const token = encodeURIComponent(session.get().token ?? '');
  const ws = new WebSocket(`ws://${location.hostname}:${port}/ws?auth=${token}`);
  ws.binaryType = 'arraybuffer';
  const sock: ConsoleSocket = {
    send: (d) => ws.send(d),
    close: () => ws.close(),
    onopen: null, onclose: null, onerror: null, onmessage: null,
  };
  ws.onopen = () => sock.onopen?.();
  ws.onclose = () => sock.onclose?.();
  ws.onerror = () => sock.onerror?.();
  ws.onmessage = (e) => sock.onmessage?.(e.data);
  return sock;
}
