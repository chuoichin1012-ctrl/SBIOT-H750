import { useState } from 'preact/hooks';
import * as api from '../lib/api';
import logo from '../assets/sbiot-logo.svg';
import { PasswordInput, Stacked, TextInput } from '../components/form';
import { Button } from '../components/ui';

export function LoginPage(props: { reason?: string }) {
  const [user, setUser] = useState('');
  const [password, setPassword] = useState('');
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const submit = async (e: Event) => {
    e.preventDefault();
    if (!user || !password) return setError('Enter the user name and password.');
    setBusy(true);
    setError(null);
    try {
      await api.login(user, password);
    } catch (err) {
      setError((err as Error).message);
      setBusy(false);
    }
  };

  return (
    <main class="flex min-h-screen items-center justify-center px-4 py-10">
      <div class="w-full max-w-sm overflow-hidden rounded-panel border border-line bg-surface shadow-sm">
        <form class="px-7 pt-7 pb-8" onSubmit={submit} noValidate>
          <img src={logo} alt="SBIOT" width={192} height={48} class="mb-7" />
          <h1 class="text-lg font-semibold">Sign in to the gateway</h1>

          {props.reason && !error && (
            <p class="mt-3 rounded-control border-l-4 border-led-warn bg-canvas px-3 py-2 text-[13px]">{props.reason}</p>
          )}

          <div class="mt-5 space-y-4">
            <Stacked label="User name" htmlFor="login-user">
              <TextInput id="login-user" autocomplete="username" autoFocus maxLength={16} value={user} onInput={setUser} />
            </Stacked>
            <Stacked label="Password" htmlFor="login-pass" error={error ?? undefined} alert>
              <PasswordInput id="login-pass" autocomplete="current-password" maxLength={16} value={password}
                onInput={setPassword} errorId={error ? 'login-pass-err' : undefined} />
            </Stacked>
          </div>

          <Button type="submit" variant="primary" class="mt-6 w-full py-2" disabled={busy}>
            {busy ? 'Signing in…' : 'Sign in'}
          </Button>
          <p class="mt-4 text-[13px] text-muted">
            {import.meta.env.MODE === 'mock'
              ? 'Mock gateway: sign in with admin / admin.'
              : 'Forgot the password? A factory reset restores the login printed on the device label.'}
          </p>
        </form>
      </div>
    </main>
  );
}
