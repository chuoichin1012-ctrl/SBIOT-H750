import { useMemo, useState } from 'preact/hooks';
import type { Val, Values } from '../../lib/types';
import { formErrors, type FieldDef } from '../../lib/validate';
import { Modal } from '../../components/feedback';
import { Fields } from '../../components/form';
import { Button } from '../../components/ui';

/** Modal form over a plain object (slave, node, event). */
export function EditDialog<T extends object>(props: {
  title: string;
  initial: T;
  defs: (draft: T) => FieldDef[];
  onSave: (value: T) => void;
  onCancel: () => void;
  onChange?: (id: string, v: Val, draft: T) => Partial<T>;
}) {
  const [draft, setDraft] = useState<T>(props.initial);
  const defs = useMemo(() => props.defs(draft), [draft]);
  const values = draft as unknown as Values;
  const errors = formErrors(defs, values);
  const ok = Object.keys(errors).length === 0;

  return (
    <Modal
      wide
      title={props.title}
      onClose={props.onCancel}
      actions={
        <>
          <Button onClick={props.onCancel}>Cancel</Button>
          <Button variant="primary" disabled={!ok} onClick={() => props.onSave(draft)}>Done</Button>
        </>
      }
    >
      <Fields
        defs={defs}
        values={values}
        errors={errors}
        onChange={(id, v) =>
          setDraft((d) => ({ ...d, [id]: v, ...(props.onChange?.(id, v, { ...d, [id]: v }) ?? {}) }))
        }
      />
    </Modal>
  );
}
