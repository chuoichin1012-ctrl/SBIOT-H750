import { useCallback, useEffect, useMemo, useRef, useState } from 'preact/hooks';
import type { Val, Values } from './types';
import { coerce, formErrors, type FieldDef } from './validate';

// ---- Tiny global store (toasts, dialogs) ----------------------------------

export function createStore<T>(initial: T) {
  let state = initial;
  const subs = new Set<() => void>();
  return {
    get: () => state,
    set(next: T) {
      state = next;
      subs.forEach((f) => f());
    },
    sub(f: () => void) {
      subs.add(f);
      return () => void subs.delete(f);
    },
  };
}

export function useStore<T>(store: ReturnType<typeof createStore<T>>): T {
  const [, setTick] = useState(0);
  useEffect(() => store.sub(() => setTick((n) => n + 1)), [store]);
  return store.get();
}

// ---- Async data ----------------------------------------------------------

export function useAsync<T>(fn: () => Promise<T>, deps: unknown[] = []) {
  const [data, setData] = useState<T | undefined>(undefined);
  const [error, setError] = useState<string | null>(null);
  const [loading, setLoading] = useState(true);
  const seq = useRef(0);

  const reload = useCallback(() => {
    const id = ++seq.current;
    setLoading(true);
    setError(null);
    fn().then(
      (d) => id === seq.current && (setData(d), setLoading(false)),
      (e: Error) => id === seq.current && (setError(e.message), setLoading(false)),
    );
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, deps);

  useEffect(reload, [reload]);
  return { data, setData, error, loading, reload };
}

export function useInterval(fn: () => void, ms: number, active = true) {
  const ref = useRef(fn);
  ref.current = fn;
  useEffect(() => {
    if (!active) return;
    const t = setInterval(() => ref.current(), ms);
    return () => clearInterval(t);
  }, [ms, active]);
}

// ---- Forms ---------------------------------------------------------------

/** Form state keyed by the field ids that are sent to the firmware. */
export function useForm(load: () => Promise<Values>, defs: FieldDef[], deps: unknown[] = []) {
  const src = useAsync(load, deps);
  const [values, setValues] = useState<Values>({});
  const [touched, setTouched] = useState(false);
  const [ready, setReady] = useState(false);

  useEffect(() => {
    if (src.data) {
      setValues(coerce(src.data, defs));
      setTouched(false);
      setReady(true);
    }
  }, [src.data]);

  const errors = useMemo(() => formErrors(defs, values), [defs, values]);
  const set = useCallback((id: string, v: Val) => {
    setValues((prev) => ({ ...prev, [id]: v }));
    setTouched(true);
  }, []);

  return {
    values,
    set,
    errors,
    valid: Object.keys(errors).length === 0,
    dirty: touched,
    ready,
    error: src.error,
    reload: src.reload,
  };
}
