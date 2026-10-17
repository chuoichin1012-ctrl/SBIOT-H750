import { useMemo, useState } from 'preact/hooks';
import type { Val, Values } from '../../lib/types';
import { formErrors, type FieldDef } from '../../lib/validate';
import { Modal } from '../../components/feedback';
import { Fields } from '../../components/form';
import { Button } from '../../components/ui';

/**
 * Modal form over a plain object (slave, node, event). A field left empty is not flagged until it is
 * changed or Done is pressed, so a new item does not open covered in errors; its `*` says it is needed.
 */
export function EditDialog<T extends object>(props: {
  title: string;
  initial: T;
  defs: (draft: T) => FieldDef[];
  onSave: (value: T) => void;
  onCancel: () => void;
  onChange?: (id: string, v: Val, draft: T) => Partial<T>;
}) {
  const [draft, setDraft] = useState<T>(props.initial);
  const [touched, setTouched] = useState<Set<string>>(new Set());
  const [submitted, setSubmitted] = useState(false);
  const defs = useMemo(() => props.defs(draft), [draft]);
  const values = draft as unknown as Values;
  const errors = formErrors(defs, values);
  const shown = Object.fromEntries(Object.entries(errors).filter(([id]) =>
    submitted || touched.has(id) || String(values[id] ?? '') !== ''));

  function done() {
    const first = Object.keys(errors)[0];
    if (first === undefined) return props.onSave(draft);
    setSubmitted(true);
    document.querySelector<HTMLElement>(`dialog[open] #f-${first}`)?.focus();
  }

  return (
    <Modal
      wide
      title={props.title}
      onClose={props.onCancel}
      actions={
        <>
          <Button onClick={props.onCancel}>Cancel</Button>
          <Button variant="primary" onClick={done}>Done</Button>
        </>
      }
    >
      <Fields
        defs={defs}
        values={values}
        errors={shown}
        onChange={(id, v) => {
          setTouched((t) => (t.has(id) ? t : new Set(t).add(id)));
          setDraft((d) => ({ ...d, [id]: v, ...(props.onChange?.(id, v, { ...d, [id]: v }) ?? {}) }));
        }}
      />
    </Modal>
  );
}
