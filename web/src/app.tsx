import { useEffect, useState } from 'preact/hooks';
import Router, { route, type RouterOnChangeArgs } from 'preact-router';
import { createHashHistory } from 'history';
import { DeviceContext } from './lib/device';
import { useAsync, useStore } from './lib/hooks';
import { session } from './lib/auth';
import * as api from './lib/api';
import { Layout } from './components/Layout';
import { Dialogs, Toaster } from './components/feedback';
import { Pending } from './components/ui';
import { StatusPage } from './pages/Status';
import { NetworkPage } from './pages/Network';
import { PortPage } from './pages/Port';
import { WebsocketPage } from './pages/Websocket';
import { MqttPage } from './pages/Mqtt';
import { EdgePage } from './pages/edge/EdgePage';
import { IoPage } from './pages/Io';
import { SystemPage } from './pages/System';
import { ManagementPage } from './pages/Management';
import { LoginPage } from './pages/Login';

// Hash URLs (#/mqtt/publish): the device web server only knows real files,
// so a refresh on a deep link must not reach it as a path.
const history = createHashHistory();

function Redirect(props: { to: string; path?: string; default?: boolean }) {
  useEffect(() => void route(props.to, true), [props.to]);
  return null;
}

export function App() {
  const auth = useStore(session);
  return (
    <>
      {auth.token ? <Console key={auth.token} /> : <LoginPage reason={auth.reason} />}
      <Toaster />
      <Dialogs />
    </>
  );
}

/** Everything behind the sign-in; remounted per session so no data leaks across logins. */
function Console() {
  const device = useAsync(api.getDevice);
  const [url, setUrl] = useState('/status');

  if (!device.data) {
    return (
      <main class="mx-auto max-w-lg px-4 pt-24">
        <Pending error={device.error} retry={device.reload} />
      </main>
    );
  }

  return (
    <DeviceContext.Provider value={device.data}>
      <Layout url={url}>
        <Router history={history as never} onChange={(e: RouterOnChangeArgs) => setUrl(e.url)}>
          <StatusPage path="/status" />
          <NetworkPage path="/network" />
          <PortPage path="/port/:n/:tab?" />
          <WebsocketPage path="/websocket" />
          <MqttPage path="/mqtt/:tab?" />
          <EdgePage path="/edge/:tab?" />
          <IoPage path="/io/:tab?" />
          <SystemPage path="/system" />
          <ManagementPage path="/management" />
          <Redirect default to="/status" />
        </Router>
      </Layout>
    </DeviceContext.Provider>
  );
}
