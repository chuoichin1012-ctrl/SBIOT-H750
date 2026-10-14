import { useState } from 'preact/hooks';
import { askRestart, attempt, toast } from '../components/feedback';

/**
 * Runs a save, reports the outcome, then either offers a restart
 * (settings applied on reboot) or calls `after` straight away.
 */
export function useSaver() {
  const [busy, setBusy] = useState(false);
  async function run(fn: () => Promise<void>, opts: { restart?: boolean; ok?: string; after?: () => void } = {}) {
    setBusy(true);
    const done = await attempt(() => fn().then(() => true));
    setBusy(false);
    if (!done) return;
    if (opts.restart) askRestart(opts.after ?? (() => {}));
    else {
      toast(opts.ok ?? 'Saved.');
      opts.after?.();
    }
  }
  return { busy, run };
}
