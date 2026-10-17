import { useState } from 'preact/hooks';
import { askSave, attempt, toast } from '../components/feedback';

/**
 * Runs a save and reports the outcome. A save whose settings apply on reboot (`restart`) first asks: nothing is
 * sent until "Save and continue editing" or "Save and restart" is chosen (the user, 2026-10-17). Others run at once.
 */
export function useSaver() {
  const [busy, setBusy] = useState(false);
  async function run(fn: () => Promise<void>, opts: { restart?: boolean; ok?: string; after?: () => void } = {}) {
    if (opts.restart) {
      askSave(async () => {
        setBusy(true);
        const done = await attempt(() => fn().then(() => true));
        setBusy(false);
        return !!done;
      }, opts.after ?? (() => {}));
      return;
    }
    setBusy(true);
    const done = await attempt(() => fn().then(() => true));
    setBusy(false);
    if (!done) return;
    toast(opts.ok ?? 'Saved.');
    opts.after?.();
  }
  return { busy, run };
}
