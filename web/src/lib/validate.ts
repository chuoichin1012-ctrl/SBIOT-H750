// Input checks shared by all forms. Limits come from the surveyed UI
// (docs/phu-luc-truong-giao-dien.html); the firmware must re-check them.

import type { Val, Values } from './types';

export type Option = readonly [value: string | number, label: string];
export type Options = readonly Option[];

export interface FieldDef {
  id: string;
  label: string;
  /** `flags`: bitmask shown as a checkbox group; option value = bit index. */
  kind: 'text' | 'number' | 'select' | 'check' | 'flags' | 'password' | 'textarea' | 'ipv4' | 'time' | 'radio';
  options?: Options | ((v: Values) => Options);
  min?: number;
  max?: number;
  /** Also accept 0 outside [min, max] (e.g. "0 or 60..65535"). */
  zeroOk?: boolean;
  maxLength?: number;
  required?: boolean;
  unit?: string;
  help?: string;
  placeholder?: string;
  /** Monospace input (topics, hex, formulas). */
  mono?: boolean;
  /** Spans two columns in the grid layout. */
  wide?: boolean;
  show?: (v: Values) => boolean;
  disabled?: (v: Values) => boolean;
  check?: (value: Val, v: Values) => string | null;
}

const IPV4 = /^(25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)(\.(25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)){3}$/;

export const isIpv4 = (s: string) => IPV4.test(s);
/** allowedClients: up to 8 IPv4 addresses or CIDR ranges (firmware proto/ipacl.c). */
export function allowListError(s: string): string | null {
  const items = s.split(/[\s,;]+/).filter(Boolean);
  if (items.length > 8) return 'At most 8 entries.';
  for (const it of items) {
    const [ip, bits, extra] = it.split('/');
    const ok = extra === undefined && isIpv4(ip) && (bits === undefined || (/^\d{1,2}$/.test(bits) && Number(bits) <= 32));
    if (!ok) return `"${it}" is not an IPv4 address or range such as 192.168.1.0/24.`;
  }
  return null;
}
export const isHex = (s: string) => /^([0-9a-fA-F]{2})+$/.test(s.replace(/\s+/g, ''));

export function rangeError(n: number, min?: number, max?: number, zeroOk?: boolean): string | null {
  if (!Number.isFinite(n) || !Number.isInteger(n)) return 'Enter a whole number.';
  if (zeroOk && n === 0) return null;
  if ((min !== undefined && n < min) || (max !== undefined && n > max)) {
    return zeroOk ? `Enter 0 or ${min}–${max}.` : `Enter ${min}–${max}.`;
  }
  return null;
}

/** MQTT topic rules: publish topics cannot hold wildcards. */
export function topicError(topic: string, subscribe: boolean): string | null {
  if (!topic) return 'Enter a topic.';
  if (new TextEncoder().encode(topic).length > 128) return 'Use at most 128 bytes (an accented letter takes 2).';
  if (!subscribe) return /[+#]/.test(topic) ? 'Publish topics cannot contain + or #.' : null;
  const levels = topic.split('/');
  for (let i = 0; i < levels.length; i++) {
    const l = levels[i];
    if (l.includes('#') && (l !== '#' || i !== levels.length - 1)) return '# must be the last level on its own.';
    if (l.includes('+') && l !== '+') return '+ must fill a whole level.';
  }
  return null;
}

export function fieldError(def: FieldDef, values: Values): string | null {
  const v = values[def.id];
  if (def.kind === 'check' || def.kind === 'select' || def.kind === 'radio' || def.kind === 'flags') {
    return def.check ? def.check(v, values) : null;
  }
  const s = String(v ?? '').trim();
  if (!s) return def.required || def.kind === 'number' || def.kind === 'ipv4' ? 'Required.' : null;
  // Bytes, as the firmware's buffers count them: "é" takes two (Codex 09b review).
  if (def.maxLength && new TextEncoder().encode(s).length > def.maxLength) return `Use at most ${def.maxLength} bytes (an accented letter takes 2).`;
  if (def.kind === 'number') {
    const err = rangeError(Number(s), def.min, def.max, def.zeroOk);
    if (err) return err;
  }
  if (def.kind === 'ipv4' && !isIpv4(s)) return 'Enter an IPv4 address, e.g. 192.168.0.7.';
  return def.check ? def.check(v, values) : null;
}

/**
 * Coerces loaded values to the type each field expects, so firmware that
 * sends numbers or flags as strings ("0", "1") still behaves: a checkbox gets
 * a boolean, numeric fields and numeric choices get numbers. Text is kept.
 */
export function coerce(values: Values, defs: FieldDef[]): Values {
  const out: Values = { ...values };
  for (const d of defs) {
    const v = out[d.id];
    if (v === undefined) continue;
    if (d.kind === 'check') out[d.id] = v === true || v === 1 || v === '1' || v === 'true';
    else if (d.kind === 'number' || d.kind === 'flags') {
      if (typeof v === 'string' && v.trim() !== '' && !isNaN(Number(v))) out[d.id] = Number(v);
    } else if (d.kind === 'select' || d.kind === 'radio') {
      const opts = typeof d.options === 'function' ? d.options(out) : d.options ?? [];
      if (typeof opts[0]?.[0] === 'number' && typeof v === 'string' && v.trim() !== '' && !isNaN(Number(v))) out[d.id] = Number(v);
    }
  }
  return out;
}

export function formErrors(defs: FieldDef[], values: Values): Record<string, string> {
  const out: Record<string, string> = {};
  for (const d of defs) {
    if (d.show && !d.show(values)) continue;
    if (d.disabled && d.disabled(values)) continue;
    const e = fieldError(d, values);
    if (e) out[d.id] = e;
  }
  return out;
}

export const ON_OFF: Options = [[0, 'Off'], [1, 'On']];
export const EN_DIS: Options = [[0, 'Disabled'], [1, 'Enabled']];
export const QOS: Options = [[0, 'QoS 0'], [1, 'QoS 1'], [2, 'QoS 2']];
export const HEX_ASCII: Options = [[1, 'HEX'], [0, 'ASCII']];
// No TLS 1.0/1.1: the firmware's mbedTLS 3 implements TLS 1.2 only. (Value 2 kept for stored settings.)
export const TLS: Options = [[0, 'Disabled'], [2, 'TLS 1.2']];
export const TLS_VERIFY: Options = [[0, 'None'], [1, 'Verify server certificate'], [2, 'Verify both sides']];

/** Identity sources for heartbeat / registration packets (USR Cloud removed). */
export const IDENTITY: Options = [[0, 'Custom data'], [1, 'IMEI'], [2, 'Serial number'], [3, 'ICCID'], [4, 'MAC address']];
