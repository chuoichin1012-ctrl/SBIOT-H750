import type { ComponentChildren } from 'preact';
import { useEffect, useRef, useState } from 'preact/hooks';
import { createStore, useStore } from '../lib/hooks';
import * as api from '../lib/api';
import { Button } from './ui';

// ---- Toasts --------------------------------------------------------------

interface Toast { id: number; text: string; kind: 'ok' | 'err' }
const toasts = createStore<Toast[]>([]);
let nextId = 1;

export function toast(text: string, kind: Toast['kind'] = 'ok') {
  const t = { id: nextId++, text, kind };
  toasts.set([...toasts.get(), t]);
  setTimeout(() => toasts.set(toasts.get().filter((x) => x !== t)), kind === 'err' ? 8000 : 3500);
}

export function Toaster() {
  const list = useStore(toasts);
  return (
    <div class="pointer-events-none fixed right-4 bottom-4 z-50 flex max-w-sm flex-col gap-2" role="status" aria-live="polite">
      {list.map((t) => (
        <div
          key={t.id}
          class={`pointer-events-auto rounded-control border-l-4 bg-ink px-4 py-2.5 text-white shadow-lg ${
            t.kind === 'err' ? 'border-led-err' : 'border-led-ok'
          }`}
        >
          {t.text}
        </div>
      ))}
    </div>
  );
}

/**
 * Runs a one-off device action and reports the outcome as a toast.
 * Returns the result, or undefined when it failed (the error is shown).
 */
export async function attempt<T>(fn: () => Promise<T>, ok?: string): Promise<T | undefined> {
  try {
    const r = await fn();
    if (ok) toast(ok);
    return r;
  } catch (e) {
    toast((e as Error).message, 'err');
    return undefined;
  }
}

/** `attempt` with a busy flag, for buttons that must not be pressed twice. */
export function useAction() {
  const [busy, setBusy] = useState(false);
  const run = async <T,>(fn: () => Promise<T>, ok?: string) => {
    setBusy(true);
    const r = await attempt(fn, ok);
    setBusy(false);
    return r;
  };
  return { busy, run };
}

// ---- Modal ---------------------------------------------------------------

export function Modal(props: {
  title: string;
  onClose: () => void;
  children: ComponentChildren;
  actions: ComponentChildren;
  wide?: boolean;
}) {
  const ref = useRef<HTMLDialogElement>(null);
  useEffect(() => {
    const d = ref.current;
    if (d && !d.open) d.showModal();
  }, []);
  return (
    <dialog
      ref={ref}
      onCancel={(e) => (e.preventDefault(), props.onClose())}
      class={`m-auto w-[calc(100%-2rem)] rounded-panel border border-line bg-surface p-0 text-ink shadow-2xl backdrop:bg-ink/50 ${
        props.wide ? 'max-w-3xl' : 'max-w-md'
      }`}
    >
      <div class="border-b border-line px-5 py-3 text-base font-semibold">{props.title}</div>
      <div class="max-h-[70vh] overflow-y-auto px-5 py-4">{props.children}</div>
      <div class="flex justify-end gap-2 border-t border-line bg-canvas/60 px-5 py-3">{props.actions}</div>
    </dialog>
  );
}

// ---- Confirm + restart prompt ---------------------------------------------

interface Ask {
  title: string;
  body: string;
  confirm: string;
  danger?: boolean;
  resolve: (ok: boolean) => void;
}
const asks = createStore<Ask | null>(null);

export function confirmAction(opts: Omit<Ask, 'resolve'>): Promise<boolean> {
  return new Promise((resolve) => asks.set({ ...opts, resolve }));
}

interface SavePrompt {
  /** The POST; true when it went through. */
  save: () => Promise<boolean>;
  after: () => void;
}
const savePrompt = createStore<SavePrompt | null>(null);

/**
 * Shown when Save is pressed, before anything is sent (the user, 2026-10-17): Cancel sends nothing; "Save and
 * continue editing" saves and stays (the settings apply at the next restart); "Save and restart" saves, then
 * restarts so they take effect.
 */
export function askSave(save: () => Promise<boolean>, after: () => void) {
  savePrompt.set({ save, after });
}

export async function restartDevice() {
  const ok = await attempt(() => api.restart().then(() => true), 'Restarting. The page will reconnect in about 30 seconds.');
  if (ok) setTimeout(() => location.reload(), 30000);
}

export function Dialogs() {
  const ask = useStore(asks);
  const prompt = useStore(savePrompt);
  const [busy, setBusy] = useState(false);

  const close = (ok: boolean) => {
    ask?.resolve(ok);
    asks.set(null);
  };

  return (
    <>
      {ask && (
        <Modal
          title={ask.title}
          onClose={() => close(false)}
          actions={
            <>
              <Button onClick={() => close(false)}>Cancel</Button>
              <Button variant={ask.danger ? 'danger' : 'primary'} onClick={() => close(true)}>{ask.confirm}</Button>
            </>
          }
        >
          <p>{ask.body}</p>
        </Modal>
      )}
      {prompt && (
        <Modal
          title="Save settings?"
          onClose={() => !busy && savePrompt.set(null)}
          actions={
            <>
              <Button disabled={busy} onClick={() => savePrompt.set(null)}>Cancel</Button>
              <Button
                disabled={busy}
                onClick={async () => {
                  setBusy(true);
                  const ok = await prompt.save();
                  setBusy(false);
                  savePrompt.set(null);
                  if (ok) {
                    toast('Saved. The gateway applies the new settings after a restart.');
                    prompt.after();
                  }
                }}
              >
                Save and continue editing
              </Button>
              <Button
                variant="primary"
                disabled={busy}
                onClick={async () => {
                  setBusy(true);
                  const ok = await prompt.save();
                  if (ok) await restartDevice();
                  setBusy(false);
                  savePrompt.set(null);
                }}
              >
                Save and restart
              </Button>
            </>
          }
        >
          <p>The gateway applies the new settings after a restart.</p>
        </Modal>
      )}
    </>
  );
}
