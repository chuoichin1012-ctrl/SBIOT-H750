// Display and layout primitives shared by every page.
// Form controls live in form.tsx; dialogs and toasts in feedback.tsx.

import type { ButtonHTMLAttributes, ComponentChildren } from 'preact';

// ---- Buttons -----------------------------------------------------------------

const VARIANT = {
  primary: 'border-accent bg-accent text-white hover:bg-accent-strong',
  plain: 'border-line bg-surface text-ink hover:bg-canvas',
  danger: 'border-led-err bg-surface text-led-err hover:bg-led-err hover:text-white',
};
const SIZE = { md: 'px-3.5 py-1.5', sm: 'px-2.5 py-1 text-[13px]' };

export type ButtonProps = Omit<ButtonHTMLAttributes<HTMLButtonElement>, 'size'> & {
  variant?: keyof typeof VARIANT;
  size?: keyof typeof SIZE;
};

export function Button({ variant = 'plain', size = 'md', class: extra, type = 'button', ...rest }: ButtonProps) {
  return (
    <button
      type={type}
      class={`inline-flex items-center justify-center gap-1.5 rounded-control border font-medium disabled:cursor-not-allowed disabled:opacity-50 ${VARIANT[variant]} ${SIZE[size]} ${extra ?? ''}`}
      {...rest}
    />
  );
}

// ---- Page structure ----------------------------------------------------------

export function PageHeader(props: { title: string; intro?: string; children?: ComponentChildren }) {
  return (
    <header class="mb-5 flex flex-wrap items-end justify-between gap-3">
      <div>
        <h1 class="text-[22px] leading-tight font-semibold">{props.title}</h1>
        {props.intro && <p class="mt-1 max-w-[70ch] text-muted">{props.intro}</p>}
      </div>
      {props.children}
    </header>
  );
}

export function Panel(props: { title?: string; aside?: ComponentChildren; children: ComponentChildren; flush?: boolean }) {
  return (
    <section class="mb-4 rounded-panel border border-line bg-surface">
      {props.title && (
        <div class="flex items-center justify-between gap-3 rounded-t-panel border-b border-line bg-tint px-4 py-2.5">
          <h2 class="font-semibold">{props.title}</h2>
          {props.aside}
        </div>
      )}
      <div class={props.flush ? '' : 'px-4 py-2'}>{props.children}</div>
    </section>
  );
}

export function Tabs(props: { base: string; active: string; tabs: [id: string, label: string][] }) {
  return (
    <nav class="mb-4 flex gap-1 overflow-x-auto overflow-y-hidden shadow-[inset_0_-1px_0_var(--color-line)]" aria-label="Sections">
      {props.tabs.map(([id, label]) => {
        const on = id === props.active;
        return (
          <a
            key={id}
            href={`#${props.base}/${id}`}
            aria-current={on ? 'page' : undefined}
            class={`border-b-2 px-3.5 py-2 whitespace-nowrap ${
              on ? 'border-brand-mark font-semibold text-ink' : 'border-transparent text-muted hover:border-line hover:text-ink'
            }`}
          >
            {label}
          </a>
        );
      })}
    </nav>
  );
}

export function Notice(props: { children: ComponentChildren; tone?: 'info' | 'warn' }) {
  return (
    <p class={`mb-4 rounded-panel border-l-4 bg-surface px-4 py-2.5 ${props.tone === 'warn' ? 'border-led-warn' : 'border-accent'}`}>
      {props.children}
    </p>
  );
}

export function Empty(props: { children: ComponentChildren }) {
  return <p class="px-4 py-8 text-center text-muted">{props.children}</p>;
}

/** Placeholder while a page's data is loading, or the load error with a retry. */
export function Pending(props: { error?: string | null; retry: () => void }) {
  if (!props.error) return <p class="py-10 text-center text-muted">Reading from the gateway…</p>;
  return (
    <div class="rounded-panel border border-led-err/40 bg-surface px-4 py-4">
      <p class="font-medium">Could not load this page.</p>
      <p class="mt-1 text-muted">{props.error}</p>
      <Button class="mt-3" onClick={props.retry}>Try again</Button>
    </div>
  );
}

/** Shown above live data when the latest refresh failed. */
export function Stale(props: { error?: string | null }) {
  return props.error ? <Notice tone="warn">Not updating: {props.error} Showing the last values read.</Notice> : null;
}

/**
 * Sticky action bar at the foot of an editable page. `invalid` disables
 * saving and explains why, unless `note` says something more specific.
 */
export function SaveBar(props: {
  onSave: () => void;
  busy?: boolean;
  invalid?: boolean;
  /** The page has tabs; errors may sit on a tab that is not open. */
  tabbed?: boolean;
  disabled?: boolean;
  label?: string;
  busyLabel?: string;
  note?: string;
  children?: ComponentChildren;
}) {
  const note =
    props.note ??
    (props.invalid
      ? props.tabbed
        ? 'Some fields need attention. Tabs marked (check) contain them.'
        : 'Fix the highlighted fields before saving.'
      : undefined);
  return (
    // The bar runs the width of the content column, at its bottom (main is a flex column); its button lines up
    // with the panels' right edge (the same 64rem as main's other children, padding on top).
    <div class="save-bar sticky bottom-0 z-10 -mx-4 mt-auto -mb-10 border-t border-line bg-surface shadow-[0_-4px_12px_-6px_rgb(26_34_51/0.15)] sm:-mx-6 lg:-mx-8">
      <div class="box-content flex max-w-5xl flex-wrap items-center justify-end gap-3 px-4 py-3 sm:px-6 lg:px-8">
        {note && <p class="mr-auto text-[13px] text-led-err">{note}</p>}
        {props.children}
        <Button variant="primary" onClick={props.onSave} disabled={props.busy || props.invalid || props.disabled}>
          {props.busy ? props.busyLabel ?? 'Saving…' : props.label ?? 'Save settings'}
        </Button>
      </div>
    </div>
  );
}

// ---- State display -------------------------------------------------------------

export type LedState = 'ok' | 'warn' | 'err' | 'off';
const LED_CLASS: Record<LedState, string> = {
  // A soft halo, like a lit indicator; derived from the token, not hard-coded.
  ok: 'bg-led-ok ring-3 ring-led-ok/20',
  warn: 'bg-led-warn ring-3 ring-led-warn/20',
  err: 'bg-led-err ring-3 ring-led-err/20',
  off: 'bg-led-off',
};

export function Led(props: { state: LedState; label?: ComponentChildren }) {
  return (
    <span class="inline-flex items-center gap-2">
      <span class={`inline-block size-2.5 shrink-0 rounded-full ${LED_CLASS[props.state]}`} aria-hidden="true" />
      {props.label}
    </span>
  );
}

/** Read-only label/value pair; wrap a group of them in <dl>. */
export function Info(props: { label: string; children: ComponentChildren }) {
  return (
    <div class="flex justify-between gap-4 border-b border-line/70 py-2 last:border-b-0">
      <dt class="text-muted">{props.label}</dt>
      <dd class="num min-w-0 text-right break-all">{props.children}</dd>
    </div>
  );
}

// ---- Collections ----------------------------------------------------------------

/**
 * A list of items inside a flush Panel. Each row shows a title, an optional
 * line of detail and its actions; Edit/Delete buttons appear when given.
 */
export function ItemList<T>(props: {
  items: T[];
  keyOf: (item: T, i: number) => string;
  empty: ComponentChildren;
  render: (item: T, i: number) => ItemRowProps;
}) {
  if (!props.items.length) return <Empty>{props.empty}</Empty>;
  return (
    <ul class="divide-y divide-line">
      {props.items.map((it, i) => <ItemRow key={props.keyOf(it, i)} {...props.render(it, i)} />)}
    </ul>
  );
}

export interface ItemRowProps {
  title: ComponentChildren;
  detail?: ComponentChildren;
  led?: LedState;
  selected?: boolean;
  onSelect?: () => void;
  onEdit?: () => void;
  onDelete?: () => void;
  /** Extra content on the right (values, inputs, action buttons). */
  aside?: ComponentChildren;
}

function ItemRow(p: ItemRowProps) {
  const body = (
    <>
      <span class="block truncate font-medium">{p.title}</span>
      {p.detail && <span class="num block text-[13px] text-muted">{p.detail}</span>}
    </>
  );
  return (
    <li class={`flex flex-wrap items-center gap-x-3 gap-y-2 px-4 py-2.5 ${p.selected ? 'bg-canvas' : ''}`}>
      {p.led && <Led state={p.led} />}
      {p.onSelect ? (
        <button type="button" class="min-w-0 flex-1 text-left" onClick={p.onSelect} aria-pressed={p.selected}>{body}</button>
      ) : (
        <div class="min-w-0 flex-1">{body}</div>
      )}
      {p.aside}
      {p.onEdit && <Button size="sm" onClick={p.onEdit}>Edit</Button>}
      {p.onDelete && <Button size="sm" onClick={p.onDelete}>Delete</Button>}
    </li>
  );
}

export interface Column<T> {
  head: string;
  cell: (row: T, i: number) => ComponentChildren;
  align?: 'right';
}

export function DataTable<T>(props: {
  columns: Column<T>[];
  rows: T[];
  keyOf: (row: T, i: number) => string;
  empty?: ComponentChildren;
  onEdit?: (i: number) => void;
  onDelete?: (i: number) => void;
  /** Rows to pick from: the first cell selects its row, the selected one is shaded. */
  selected?: number;
  onSelect?: (i: number) => void;
}) {
  if (!props.rows.length && props.empty) return <Empty>{props.empty}</Empty>;
  const actions = props.onEdit || props.onDelete;
  const align = (c: Column<T>) => (c.align === 'right' ? 'text-right' : '');
  return (
    <div class="overflow-x-auto">
      <table class="w-full text-left">
        <thead class="text-muted">
          <tr class="border-b border-line">
            {props.columns.map((c) => <th key={c.head} class={`px-4 py-2 font-normal ${align(c)}`}>{c.head}</th>)}
            {actions && <th class="px-4 py-2"><span class="sr-only">Actions</span></th>}
          </tr>
        </thead>
        <tbody class="num">
          {props.rows.map((r, i) => (
            <tr key={props.keyOf(r, i)} class={`border-b border-line/70 last:border-b-0 ${props.selected === i ? 'bg-canvas' : ''}`}>
              {props.columns.map((c, k) => (
                <td key={c.head} class={`px-4 py-2 ${align(c)}`}>
                  {k === 0 && props.onSelect ? (
                    <button type="button" class="text-left" onClick={() => props.onSelect!(i)} aria-pressed={props.selected === i}>
                      {c.cell(r, i)}
                    </button>
                  ) : c.cell(r, i)}
                </td>
              ))}
              {actions && (
                <td class="px-4 py-2 text-right whitespace-nowrap">
                  <span class="inline-flex gap-1.5">
                    {props.onEdit && <Button size="sm" onClick={() => props.onEdit!(i)}>Edit</Button>}
                    {props.onDelete && <Button size="sm" onClick={() => props.onDelete!(i)}>Delete</Button>}
                  </span>
                </td>
              )}
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
}
