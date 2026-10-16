// Data contract between this UI and the gateway firmware.
//
// The endpoints are listed in api.ts; the JSON shapes are below and in each
// page's FieldDefs. This project's own data model (not the USR-M100's): treat
// it as the spec the firmware (firmware/) implements.

export type Val = string | number | boolean;
export type Values = Record<string, Val>;

/**
 * GET /api/device — hardware capabilities plus the *running* identity, read once
 * at start-up. hostname / wsDirection / wsPort reflect what is active now;
 * /api/config/misc holds the saved values, which apply after the next restart.
 */
export interface DeviceInfo {
  model: string;
  firmware: string;
  hardware: string;
  /** Number of serial ports exposed (UART1..portmax). */
  portmax: number;
  baudMin: number;
  baudMax: number;
  io: { do: number; di: number; ai: number; ao: number };
  hostname: string;
  /** Running websocketTarget: 0 = OFF, 1..portmax = UARTn, portmax+1 = LOG. */
  wsDirection: number;
  wsPort: number;
}

/** Live state of a UART's network socket (one socket per port). */
export interface PortRuntime {
  state: string;
  tx: number;
  rx: number;
}

/** GET /api/status — live status, polled by the Status page. */
export interface StatusInfo {
  system: {
    uptime: number;
    os: string;
    mac: string;
    imei: string;
    sn: string;
    activeNet: 'ETH' | 'LTE' | 'NONE';
    time: string;
    /** False while the clock is not set: `time` then reads 2020-01-01 00:00:00 plus the uptime. */
    clockSet?: boolean;
    /** Bytes: free internal RAM now, its low-water mark since boot, free PSRAM. */
    heapFree: number;
    heapMin: number;
    psramFree: number;
  };
  eth: { ip: string; mask: string; gateway: string; dns1: string; dns2: string };
  cell: {
    iccid: string;
    ip: string;
    dns1: string;
    dns2: string;
    /** CSQ 0..31, 99 = unknown/not detectable. */
    signal: number;
    netType: string;
    state: 'connected' | 'disconnected' | 'connecting';
  };
  ports: PortRuntime[];
  mqtt: { enabled: boolean; state: string };
  edge: { enabled: boolean; state: string };
}

/** GET /api/io — live IO values. AI in µA; AO in µA or mV per its mode. Indexes are 0-based. */
export interface IoState {
  do: boolean[];
  di: boolean[];
  ai: number[];
  ao: number[];
}

// ---- Edge computing model ----------------------------------------------

export type SlaveSource = 'uart' | 'eth' | 'io' | 'virtual';
export type Protocol = 'modbus' | 'dlt645';
export type DataType =
  | 'bool' | 'uint8' | 'int8' | 'uint16' | 'int16'
  | 'uint32_abcd' | 'uint32_cdab' | 'int32_abcd' | 'int32_cdab'
  | 'float32_abcd' | 'float32_cdab' | 'float64';

export interface EdgeNode {
  name: string;
  /** Modbus function code 1..4 (ignored for io/virtual). */
  fc: number;
  /** Register offset 0..65534; DL/T645 uses `di` instead. */
  reg: number;
  /** DL/T645 data identifier, 8 hex digits. */
  di: string;
  dtype: DataType;
  timeout: number;
  onChange: boolean;
  range: number;
  mapEn: boolean;
  mapAddr: number;
  /** `%s` stands for the raw value, e.g. `=%s*0.1`. */
  formula: string;
  /** IO source only: point id such as `DI1`, `AI2`. */
  ioPoint: string;
  /** IO source only: 0 off, 1 within [lower, upper], 2 outside it. */
  ioReport: number;
  upper: number;
  lower: number;
}

export interface EdgeSlave {
  name: string;
  source: SlaveSource;
  /** UART index (1-based) when source = uart. */
  uart: number;
  ip: string;
  ethPort: number;
  protocol: Protocol;
  /** Modbus 1..255, or DL/T645 12-digit address as string. */
  addr: string;
  poll: number;
  mapEn: boolean;
  mapAddr: number;
  merge: boolean;
  nodes: EdgeNode[];
}

export interface LinkageEvent {
  name: string;
  enable: boolean;
  /** Index into LINKAGE_CONDITIONS. */
  cond: number;
  point: string;
  cycle: number;
  minInterval: number;
  upper: number;
  lower: number;
  /** Output id, e.g. `DO1`. */
  output: string;
  /** 0 No action, 1 NO, 2 NC, 3 Flip. */
  action: number;
  desc: string;
}

