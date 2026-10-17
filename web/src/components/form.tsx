// Form controls. Pages describe fields with FieldDef and render them through
// <Fields>; the primitives below are also used directly where a form is not
// driven by FieldDef (sign-in page, table filters).

import type { ComponentChildren } from 'preact';
import { useState } from 'preact/hooks';
import type { Val, Values } from '../lib/types';
import { isRequired, optionsOf, type FieldDef, type Options } from '../lib/validate';
import { Button } from './ui';

// ---- Primitives ------------------------------------------------------------

interface Common {
  id?: string;
  disabled?: boolean;
  /** Error text id, for aria-describedby; also marks the control invalid. */
  errorId?: string;
  /** Announced to screen readers; the visible `*` is on the label. */
  required?: boolean;
  'aria-label'?: string;
}

const a11y = (p: Common) => ({
  id: p.id,
  disabled: p.disabled,
  'aria-label': p['aria-label'],
  'aria-required': p.required || undefined,
  'aria-invalid': p.errorId ? true : undefined,
  'aria-describedby': p.errorId,
});

/**
 * Select whose value keeps the option type (number options give numbers). With a `placeholder`, a value
 * that is not one of the options shows the placeholder, which cannot be picked from the list.
 */
export function Select(props: Common & {
  value: Val | undefined;
  options: Options;
  onChange: (v: string | number) => void;
  placeholder?: string;
  class?: string;
}) {
  const numeric = typeof props.options[0]?.[0] === 'number';
  const value = String(props.value ?? '');
  const known = props.options.some(([v]) => String(v) === value);
  return (
    <select {...a11y(props)} class={`input ${props.class ?? ''}`} value={props.placeholder !== undefined && !known ? '' : value}
      onChange={(e) => props.onChange(numeric ? Number(e.currentTarget.value) : e.currentTarget.value)}>
      {props.placeholder !== undefined && <option value="" disabled hidden>{props.placeholder}</option>}
      {props.options.map(([v, label]) => <option key={v} value={String(v)}>{label}</option>)}
    </select>
  );
}

export function Checkbox(props: Common & { checked: boolean; onChange: (on: boolean) => void; label: ComponentChildren; title?: string }) {
  return (
    <label class="inline-flex items-start gap-2" title={props.title}>
      <input {...a11y(props)} type="checkbox" class="mt-0.5 size-4 shrink-0 accent-accent" checked={props.checked}
        onChange={(e) => props.onChange(e.currentTarget.checked)} />
      <span>{props.label}</span>
    </label>
  );
}

export function TextInput(props: Common & {
  value: Val | undefined;
  onInput: (v: string) => void;
  maxLength?: number;
  type?: 'text' | 'password' | 'time';
  /** Seconds granularity for type "time" (1 shows hh:mm:ss). */
  step?: number;
  inputMode?: 'numeric' | 'decimal';
  autocomplete?: string;
  autoFocus?: boolean;
  placeholder?: string;
  class?: string;
}) {
  return (
    <input {...a11y(props)} class={`input ${props.class ?? ''}`} type={(props.type ?? 'text') as 'text'}
      inputMode={props.inputMode} maxLength={props.maxLength} autocomplete={props.autocomplete} step={props.step}
      autoFocus={props.autoFocus} placeholder={props.placeholder} value={String(props.value ?? '')}
      onInput={(e) => props.onInput(e.currentTarget.value)} />
  );
}

export function PasswordInput(props: Common & { value: Val | undefined; onInput: (v: string) => void; maxLength?: number; autocomplete?: string }) {
  const [reveal, setReveal] = useState(false);
  return (
    <div class="flex gap-2">
      <TextInput {...props} type={reveal ? 'text' : 'password'} autocomplete={props.autocomplete ?? 'off'} />
      <Button class="shrink-0" onClick={() => setReveal(!reveal)} aria-pressed={reveal}>
        {reveal ? 'Hide' : 'Show'}
      </Button>
    </div>
  );
}

/** Checkbox group stored as a bitmask; option value = bit index. */
export function Flags(props: Common & { value: number; options: Options; onChange: (mask: number) => void }) {
  return (
    <div class="flex flex-wrap gap-x-4 gap-y-1 pt-1.5" role="group" id={props.id} aria-label={props['aria-label']}>
      {props.options.map(([bit, label]) => {
        const m = 1 << Number(bit);
        return (
          <Checkbox key={bit} disabled={props.disabled} checked={(props.value & m) !== 0} label={label}
            onChange={(on) => props.onChange(on ? props.value | m : props.value & ~m)} />
        );
      })}
    </div>
  );
}

const Message = (props: { id?: string; error?: string; help?: string; alert?: boolean }) =>
  props.error ? (
    <p class="mt-1 text-[13px] text-led-err" id={props.id} role={props.alert ? 'alert' : undefined}>{props.error}</p>
  ) : props.help ? (
    <p class="mt-1 text-[13px] text-muted">{props.help}</p>
  ) : null;

/** The label text, with a `*` when the field cannot be left empty. */
const LabelText = (props: { label: string; required?: boolean }) => (
  <>
    {props.label}
    {props.required && <span class="ml-0.5 text-led-err" aria-hidden="true">*</span>}
  </>
);

/** Label left, control right; the standard settings row. */
export function Row(props: { label: string; htmlFor?: string; help?: string; error?: string; required?: boolean; children: ComponentChildren }) {
  return (
    <div class="grid gap-x-6 gap-y-1 border-b border-line/70 py-2.5 last:border-b-0 sm:grid-cols-[minmax(0,14rem)_minmax(0,1fr)]">
      <label for={props.htmlFor} class="pt-1.5 text-muted"><LabelText label={props.label} required={props.required} /></label>
      <div class="min-w-0 max-w-md">
        {props.children}
        <Message id={props.htmlFor && `${props.htmlFor}-err`} error={props.error} help={props.help} />
      </div>
    </div>
  );
}

/** Label above the control; for compact grids and the sign-in form. */
export function Stacked(props: {
  label: string;
  htmlFor?: string;
  help?: string;
  error?: string;
  /** Announce the error when it appears (errors shown after a submit). */
  alert?: boolean;
  required?: boolean;
  children: ComponentChildren;
  class?: string;
}) {
  return (
    <div class={`min-w-0 ${props.class ?? ''}`}>
      <label for={props.htmlFor} class="mb-1 block text-[13px] text-muted"><LabelText label={props.label} required={props.required} /></label>
      {props.children}
      <Message id={props.htmlFor && `${props.htmlFor}-err`} error={props.error} help={props.help} alert={props.alert} />
    </div>
  );
}

// ---- FieldDef rendering ------------------------------------------------------

export function Control(props: { def: FieldDef; values: Values; error?: string; idPrefix?: string; onChange: (id: string, v: Val) => void }) {
  const { def, values, onChange } = props;
  const v = values[def.id];
  const common: Common = {
    id: `${props.idPrefix ?? 'f-'}${def.id}`,
    disabled: def.disabled?.(values) ?? false,
  };
  common.required = isRequired(def) && !common.disabled;
  if (props.error) common.errorId = `${common.id}-err`;
  const set = (x: Val) => onChange(def.id, x);

  switch (def.kind) {
    case 'select':
      return <Select {...common} value={v} options={optionsOf(def, values)} placeholder={def.placeholder} onChange={set} />;
    case 'check':
      return (
        <div class="pt-1.5">
          <Checkbox {...common} checked={!!v && v !== '0'} onChange={set} label={def.help ?? 'Enabled'} />
        </div>
      );
    case 'flags':
      return <Flags {...common} value={Number(v) || 0} options={optionsOf(def, values)} onChange={set} />;
    case 'radio':
      return (
        <div class="flex flex-wrap gap-4 pt-1.5" role="radiogroup" id={common.id}>
          {optionsOf(def, values).map(([val, label]) => (
            <label key={val} class="inline-flex items-center gap-1.5">
              <input type="radio" name={common.id} disabled={common.disabled} checked={String(v) === String(val)} onChange={() => set(val)} />
              {label}
            </label>
          ))}
        </div>
      );
    case 'textarea':
      return (
        <div>
          <textarea {...a11y(common)} class="input min-h-28 font-mono text-[13px]" maxLength={def.maxLength}
            value={String(v ?? '')} onInput={(e) => set(e.currentTarget.value)} />
          {def.maxLength && <p class="num text-right text-[12px] text-muted">{String(v ?? '').length} / {def.maxLength}</p>}
        </div>
      );
    case 'password':
      return <PasswordInput {...common} value={v} maxLength={def.maxLength} onInput={set} />;
    default: {
      const numeric = def.kind === 'number';
      return (
        <div class="flex items-center gap-2">
          <TextInput {...common} value={v} class={`${numeric ? 'num max-w-40' : ''} ${def.mono ? 'font-mono text-[13px]' : ''}`}
            type={def.kind === 'time' ? 'time' : 'text'} step={def.kind === 'time' ? 1 : undefined} placeholder={def.placeholder}
            inputMode={numeric ? 'numeric' : def.kind === 'ipv4' ? 'decimal' : undefined}
            maxLength={def.maxLength ?? (def.kind === 'ipv4' ? 15 : undefined)}
            onInput={(raw) => set(numeric && raw.trim() !== '' && !isNaN(Number(raw)) ? Number(raw) : raw)} />
          {def.unit && <span class="text-muted">{def.unit}</span>}
        </div>
      );
    }
  }
}

function hint(def: FieldDef): string | undefined {
  if (def.kind === 'check') return undefined; // its help is the checkbox label
  if (def.kind !== 'number' || def.min === undefined) return def.help;
  const range = def.zeroOk ? `0 or ${def.min}–${def.max}` : `${def.min}–${def.max}`;
  return def.help ? `${def.help} (${range})` : `Range ${range}.`;
}

/**
 * Renders visible fields. `rows`: label left (settings pages);
 * `grid`: labels above, several fields per line (repeated items).
 */
export function Fields(props: {
  defs: FieldDef[];
  values: Values;
  errors: Record<string, string>;
  onChange: (id: string, v: Val) => void;
  layout?: 'rows' | 'grid';
  idPrefix?: string;
}) {
  const prefix = props.idPrefix ?? 'f-';
  const visible = props.defs.filter((d) => !d.show || d.show(props.values));
  const required = (def: FieldDef) => isRequired(def) && !def.disabled?.(props.values);
  const control = (def: FieldDef) => (
    <Control def={def} values={props.values} error={props.errors[def.id]} idPrefix={prefix} onChange={props.onChange} />
  );

  if (props.layout === 'grid') {
    return (
      <div class="grid gap-3 sm:grid-cols-2 lg:grid-cols-4">
        {visible.map((def) =>
          def.kind === 'check' ? (
            // A checkbox carries its own label; the help text becomes its tooltip.
            <div key={def.id} class="pt-1 sm:pt-7.5">
              <Checkbox id={`${prefix}${def.id}`} checked={!!props.values[def.id]} title={def.help}
                label={def.label} onChange={(on) => props.onChange(def.id, on)} />
            </div>
          ) : (
            <Stacked key={def.id} label={def.label} htmlFor={`${prefix}${def.id}`} error={props.errors[def.id]} required={required(def)}
              class={def.wide ? 'sm:col-span-2' : ''}>
              {control(def)}
            </Stacked>
          ),
        )}
      </div>
    );
  }
  return (
    <>
      {visible.map((def) => (
        <Row key={def.id} label={def.label} htmlFor={`${prefix}${def.id}`} error={props.errors[def.id]} help={hint(def)} required={required(def)}>
          {control(def)}
        </Row>
      ))}
    </>
  );
}
