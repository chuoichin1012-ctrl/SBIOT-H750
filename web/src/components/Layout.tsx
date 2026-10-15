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
    <nav aria-label="Main" class="flex flex-col gap-5 px-3 py-4">
      {nav.map(([group, items]) => (
        <div key={group}>
          <p class="px-3 pb-1 text-[12px] text-white/50">{group}</p>
          <ul>
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
                        ? 'bg-nav-active font-medium text-white shadow-[inset_3px_0_0_var(--color-nav-accent)]'
                        : 'text-white/75 hover:bg-nav-active/60 hover:text-white'
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
      <button
        type="button"
        onClick={() => signOut()}
        class="mt-2 flex items-center gap-2.5 rounded-control border-t border-white/10 px-3 pt-4 pb-1.5 text-left text-white/75 hover:text-white"
      >
        <svg viewBox="0 0 20 20" class="size-4" fill="none" stroke="currentColor" stroke-width="1.7" aria-hidden="true">
          <path d="M8 4H4.5A1.5 1.5 0 0 0 3 5.5v9A1.5 1.5 0 0 0 4.5 16H8M13 6.5 16.5 10 13 13.5M16.5 10H8" stroke-linecap="round" stroke-linejoin="round" />
        </svg>
        Sign out
      </button>
    </nav>
  );

  return (
    <div class="min-h-screen lg:grid lg:grid-cols-[15rem_1fr]">
      <a href="#main" class="sr-only focus:not-sr-only focus:absolute focus:z-50 focus:bg-surface focus:p-2">
        Skip to content
      </a>
      <aside class="hidden bg-nav lg:block">
        <div class="sticky top-0 max-h-screen overflow-y-auto">
          <Brand />
          {sidebar}
        </div>
      </aside>

      {open && (
        <div class="fixed inset-0 z-40 lg:hidden">
          <button class="absolute inset-0 bg-ink/50" aria-label="Close menu" tabIndex={-1} onClick={() => setOpen(false)} />
          <aside class="absolute inset-y-0 left-0 w-64 overflow-y-auto bg-nav">
            <Brand />
            {sidebar}
          </aside>
        </div>
      )}

      <div class="min-w-0">
        {/* Only small screens need a bar: it opens the hidden sidebar. */}
        <header class="sticky top-0 z-30 flex items-center border-b border-line bg-surface px-2 py-1.5 lg:hidden">
          <button
            type="button"
            class="inline-flex size-10 items-center justify-center rounded-control text-ink hover:bg-canvas"
            onClick={() => setOpen(true)}
            aria-label="Open menu"
            aria-expanded={open}
          >
            <svg viewBox="0 0 20 20" class="size-5" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" aria-hidden="true">
              <path d="M3 5.5h14M3 10h14M3 14.5h14" />
            </svg>
          </button>
        </header>
        <main id="main" class="mx-auto max-w-5xl px-4 pt-6 sm:px-6">
          {props.children}
        </main>
      </div>
    </div>
  );
}

function Brand() {
  return (
    <div class="flex items-center gap-3 border-b border-white/10 px-6 py-4 text-white">
      <img src={mark} alt="" width={30} height={30} class="rounded-xs" />
      <span class="leading-tight">
        <span class="block font-semibold tracking-tight">SBIOT Gateway</span>
        <span class="block text-[12px] text-white/55">Make your work easier</span>
      </span>
    </div>
  );
}
