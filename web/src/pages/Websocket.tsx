import { useEffect, useRef, useState } from 'preact/hooks';
import * as api from '../lib/api';
import { useDevice, wsIsLog } from '../lib/device';
import { isHex } from '../lib/validate';
import { appendLines, lineKind, type Lines } from '../lib/console';
import { Button, Led, Notice, PageHeader, Panel, type LedState } from '../components/ui';

const MAX_LINES = 2000;
const EMPTY: Lines = { lines: [], partial: '' };
const KIND_CLASS = { tx: 'text-brand', rx: 'text-led-ok', text: 'text-ink' } as const;

const toHex = (buf: ArrayBuffer) =>
  Array.from(new Uint8Array(buf), (b) => b.toString(16).padStart(2, '0').toUpperCase()).join(' ');

export function WebsocketPage(_: { path?: string }) {
  const d = useDevice();
  const log = wsIsLog(d);
  const target = d.wsDirection === 0 ? null : log ? 'the gateway log and command shell' : `UART${d.wsDirection}`;

  const sock = useRef<api.ConsoleSocket | null>(null);
  const [state, setState] = useState<[LedState, string]>(['off', 'Not connected']);
  const [rx, setRx] = useState<Lines>(EMPTY);
  const [tx, setTx] = useState('');
  const [txError, setTxError] = useState<string | null>(null);
  const out = useRef<HTMLDivElement>(null);

  const connect = async () => {
    sock.current?.close();
    setState(['warn', 'Connecting…']);
    const s = await api.openConsole(d.wsPort);
    s.onopen = () => setState(['ok', `Connected to ws://${location.hostname}:${d.wsPort}`]);
    s.onerror = () => setState(['err', 'Connection failed. Check the Websocket port in Settings.']);
    s.onclose = () => setState((p) => (p[0] === 'err' ? p : ['off', 'Disconnected']));
    s.onmessage = (data) => setRx((prev) => appendLines(prev, typeof data === 'string' ? data : `${toHex(data)}\n`, MAX_LINES));
    sock.current = s;
  };

  useEffect(() => {
    if (target) connect();
    return () => sock.current?.close();
  }, []);

  useEffect(() => {
    if (out.current) out.current.scrollTop = out.current.scrollHeight;
  }, [rx]);

  const send = (hex: boolean) => {
    if (!sock.current || state[0] !== 'ok') return setTxError('Connect first.');
    if (!tx) return setTxError('Type something to send.');
    if (hex) {
      if (!isHex(tx)) return setTxError('Enter pairs of hex digits, e.g. 01 03 00 00 00 02.');
      const clean = tx.replace(/\s+/g, '');
      sock.current.send(new Uint8Array(clean.match(/../g)!.map((h) => parseInt(h, 16))).buffer);
    } else {
      sock.current.send(tx);
    }
    setTxError(null);
  };

  return (
    <>
      <PageHeader
        title={log ? 'Log console' : 'Websocket'}
        intro={target ? `Talk to ${target} from the browser.` : undefined}
      >
        {target && (
          <div class="flex items-center gap-3">
            <Led state={state[0]} label={state[1]} />
            <Button onClick={connect}>Reconnect</Button>
          </div>
        )}
      </PageHeader>

      {!target ? (
        <Notice>The Websocket bridge is off. Choose a serial port or the log in System settings to use it.</Notice>
      ) : (
        <>
          {log && (
            <Notice tone="warn">Text you send here runs as a command on the gateway.</Notice>
          )}
          <Panel title={log ? 'Received' : `Traffic on UART${d.wsDirection}`} aside={<Button size="sm" onClick={() => setRx(EMPTY)}>Clear</Button>}>
            {!log && (
              <p class="mt-2 text-[13px] text-muted">
                Every frame on the port: <span class={KIND_CLASS.tx}>TX</span> sent to the device (by any service or from here),{' '}
                <span class={KIND_CLASS.rx}>RX</span> received from it.
              </p>
            )}
            <div ref={out} role="log" aria-label="Received data"
              class="my-2 h-80 resize-y overflow-auto whitespace-pre-wrap break-all rounded-control border border-line bg-tint p-3 font-mono text-[13px]">
              {[...rx.lines, ...(rx.partial ? [rx.partial] : [])].map((l, i) => (
                <div key={i} class={KIND_CLASS[lineKind(l)]}>{l}</div>
              ))}
            </div>
          </Panel>
          <Panel title="Send">
            <textarea aria-label="Data to send" maxLength={4000} value={tx} onInput={(e) => setTx(e.currentTarget.value)}
              class="input mt-2 h-24 font-mono text-[13px]" />
            {txError && <p class="mt-1 text-[13px] text-led-err">{txError}</p>}
            <div class="flex gap-2 py-2">
              <Button variant="primary" onClick={() => send(false)}>Send as text</Button>
              <Button onClick={() => send(true)}>Send as hex</Button>
            </div>
          </Panel>
        </>
      )}
    </>
  );
}
