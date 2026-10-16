import type { ComponentChildren } from 'preact';
import { useEffect, useState } from 'preact/hooks';
import { signOut } from '../lib/auth';
import { range, useDevice, wsIsLog } from '../lib/device';
import mark from '../assets/sbiot-logo-square.svg';

interface Item { href: string; label: string }

function useNav(): [group: string, items: Item[]][] {
  const d = useDevice();
  return [
    ['Overview', [{ href: '/status', label: 'Status' }]],
    ['Network', [{ href: '/network', label: 'WAN & LAN' }]],
    [
      'Serial ports',
      [
        ...range(d.portmax).map((n) => ({ href: `/port/${n}`, label: `UART${n}` })),
        { href: '/websocket', label: wsIsLog(d) ? 'Log console' : 'Websocket' },
      ],
    ],
    [
      'Gateway',
      [
        { href: '/mqtt', label: 'MQTT' },
        { href: '/edge', label: 'Edge computing' },
        { href: '/io', label: 'IO' },
      ],
    ],
    [
      'System',
      [
        { href: '/system', label: 'Settings' },
        { href: '/management', label: 'Maintenance' },
      ],
    ],
  ];
}

const isActive = (url: string, href: string) => url === href || url.startsWith(`${href}/`);

export function Layout(props: { url: string; children: ComponentChildren }) {
  const nav = useNav();
  const [open, setOpen] = useState(false);

  useEffect(() => {
    if (!open) return;
    const onKey = (e: KeyboardEvent) => e.key === 'Escape' && setOpen(false);
    addEventListener('keydown', onKey);
    return () => removeEventListener('keydown', onKey);
  }, [open]);

  const sidebar = (
    <nav aria-label="Main" class="flex flex-col gap-5 px-3 py-5">
      {nav.map(([group, items]) => (
        <div key={group}>
          <p class="px-3 pb-1.5 text-[11px] font-semibold tracking-wide text-muted uppercase">{group}</p>
          <ul class="space-y-0.5">
            {items.map((it) => {
              const on = isActive(props.url, it.href);
              return (
                <li key={it.href}>
                  <a
                    href={`#${it.href}`}
                    onClick={() => setOpen(false)}
                    aria-current={on ? 'page' : undefined}
                    class={`block rounded-control px-3 py-1.5 ${
                      on
                        ? 'bg-select font-medium text-brand shadow-[inset_3px_0_0_var(--color-brand-mark)]'
                        : 'text-ink/85 hover:bg-canvas hover:text-ink'
                    }`}
                  >
                    {it.label}
                  </a>
                </li>
              );
            })}
          </ul>
        </div>
      ))}
    </nav>
  );

  // Header across the top; under it the sidebar (a drawer on small screens) and the main content.
  return (
    <div class="min-h-screen">
      <a href="#main" class="sr-only focus:not-sr-only focus:absolute focus:z-50 focus:bg-surface focus:p-2">
        Skip to content
      </a>
      <header class="sticky top-0 z-30 flex h-14 items-center gap-3 bg-brand px-3 text-white shadow-sm sm:px-5">
        <button
          type="button"
          class="inline-flex size-10 items-center justify-center rounded-control hover:bg-white/10 lg:hidden"
          onClick={() => setOpen(true)}
          aria-label="Open menu"
          aria-expanded={open}
        >
          <svg viewBox="0 0 20 20" class="size-5" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" aria-hidden="true">
            <path d="M3 5.5h14M3 10h14M3 14.5h14" />
          </svg>
        </button>
        <Brand />
        <div class="ml-auto">
          <button
            type="button"
            onClick={() => signOut()}
            class="inline-flex items-center gap-2 rounded-control border border-white/30 px-3 py-1.5 text-[13px] hover:bg-white/10"
          >
            <svg viewBox="0 0 20 20" class="size-4" fill="none" stroke="currentColor" stroke-width="1.7" aria-hidden="true">
              <path d="M8 4H4.5A1.5 1.5 0 0 0 3 5.5v9A1.5 1.5 0 0 0 4.5 16H8M13 6.5 16.5 10 13 13.5M16.5 10H8" stroke-linecap="round" stroke-linejoin="round" />
            </svg>
            Sign out
          </button>
        </div>
      </header>

      <div class="lg:grid lg:min-h-[calc(100vh-3.5rem)] lg:grid-cols-[15rem_1fr]">
        <aside class="hidden border-r border-line bg-surface lg:block">
          <div class="sticky top-14 max-h-[calc(100vh-3.5rem)] overflow-y-auto">{sidebar}</div>
        </aside>

        {open && (
          <div class="fixed inset-0 z-40 lg:hidden">
            <button class="absolute inset-0 bg-ink/40" aria-label="Close menu" tabIndex={-1} onClick={() => setOpen(false)} />
            <aside class="absolute inset-y-0 left-0 w-64 overflow-y-auto bg-surface shadow-xl">
              <div class="flex h-14 items-center justify-between bg-brand pr-2 pl-4 text-white">
                <Brand />
                <button
                  type="button"
                  class="inline-flex size-10 items-center justify-center rounded-control hover:bg-white/10"
                  onClick={() => setOpen(false)}
                  aria-label="Close menu"
                >
                  <svg viewBox="0 0 20 20" class="size-5" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" aria-hidden="true">
                    <path d="M5 5l10 10M15 5 5 15" />
                  </svg>
                </button>
              </div>
              {sidebar}
            </aside>
          </div>
        )}

        <main id="main" class="w-full min-w-0 max-w-5xl px-4 pt-6 pb-10 sm:px-6 lg:px-8">
          {props.children}
        </main>
      </div>
    </div>
  );
}

/** The header's logo and brand name. */
function Brand() {
  return (
    <div class="flex min-w-0 items-center gap-3">
      <img src={mark} alt="" width={32} height={32} class="rounded-control bg-white p-0.5" />
      <span class="truncate text-[17px] font-semibold tracking-tight">SBIOT Gateway</span>
    </div>
  );
}
