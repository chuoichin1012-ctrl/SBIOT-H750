import { createContext } from 'preact';
import { useContext } from 'preact/hooks';
import type { DeviceInfo } from './types';
import type { Options } from './validate';

export const DeviceContext = createContext<DeviceInfo | null>(null);

export function useDevice(): DeviceInfo {
  const d = useContext(DeviceContext);
  if (!d) throw new Error('DeviceContext missing');
  return d;
}

export const range = (n: number) => Array.from({ length: n }, (_, i) => i + 1);

/** UART1..portmax as select options (value = 1-based port index). */
export const uartOptions = (d: DeviceInfo): Options => range(d.portmax).map((n) => [n, `UART${n}`] as const);

/** IO point ids that exist on this unit, e.g. ['DO1', 'DO2']. */
export const ioPoints = (d: DeviceInfo, kind: 'do' | 'di' | 'ai' | 'ao') =>
  range(d.io[kind]).map((n) => `${kind.toUpperCase()}${n}`);

export const wsIsLog = (d: DeviceInfo) => d.wsDirection === d.portmax + 1;
