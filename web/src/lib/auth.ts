// HTTP Basic authentication, sent by the app on every request.
//
// The app builds the Authorization header itself instead of relying on the
// browser's login prompt, so it can show its own sign-in page and sign out.
// For that to work the firmware must answer API requests that carry
// `X-Requested-With: XMLHttpRequest` with a bare 401 (no
// `WWW-Authenticate: Basic`), and serve index.html and assets/ without auth.

import { createStore } from './hooks';

const KEY = 'sbiot.auth';

function stored(): string | null {
  try {
    return sessionStorage.getItem(KEY);
  } catch {
    return null;
  }
}

/** `token` is base64("user:password"); `reason` explains a forced sign-out. */
export const session = createStore<{ token: string | null; reason?: string }>({ token: stored() });

/** UTF-8 safe base64 of `user:password`. */
export function encodeCredentials(user: string, password: string): string {
  const bytes = new TextEncoder().encode(`${user}:${password}`);
  let bin = '';
  bytes.forEach((b) => (bin += String.fromCharCode(b)));
  return btoa(bin);
}

export function signIn(token: string) {
  // Session storage: survives a reload or a gateway restart, ends with the tab.
  try {
    sessionStorage.setItem(KEY, token);
  } catch {
    /* private mode: the session lasts until the page reloads */
  }
  session.set({ token });
}

export function signOut(reason?: string) {
  try {
    sessionStorage.removeItem(KEY);
  } catch {
    /* nothing stored */
  }
  session.set({ token: null, reason });
}

export function authHeaders(token = session.get().token): Record<string, string> {
  const h: Record<string, string> = { 'X-Requested-With': 'XMLHttpRequest' };
  if (token) h.Authorization = `Basic ${token}`;
  return h;
}
