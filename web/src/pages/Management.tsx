import { useState } from 'preact/hooks';
import * as api from '../lib/api';
import { useDevice } from '../lib/device';
import { attempt, confirmAction, restartDevice } from '../components/feedback';
import { Button, PageHeader, Panel } from '../components/ui';

function Firmware() {
  const d = useDevice();
  const [file, setFile] = useState<File | null>(null);
  const [pct, setPct] = useState<number | null>(null);
  const [done, setDone] = useState(false);

  const start = async () => {
    if (!file) return;
    const ok = await confirmAction({
      title: 'Install firmware?',
      body: `Install ${file.name} over firmware ${d.firmware}. Keep the gateway powered until it restarts.`,
      confirm: 'Install firmware',
      danger: true,
    });
    if (!ok) return;
    setPct(0);
    const sent = await attempt(
      () => api.uploadFirmware(file, setPct).then(() => true),
      'Firmware uploaded. The gateway is installing it and will restart.',
    );
    if (!sent) return setPct(null);
    setDone(true);
    setTimeout(() => location.reload(), 60000);
  };

  return (
    <Panel title="Firmware">
      <div class="py-2">
        <p class="text-muted">Installed: <span class="num text-ink">{d.firmware}</span></p>
        <div class="mt-3 flex flex-wrap items-center gap-3">
          <input type="file" aria-label="Firmware file" accept=".bin,.img" disabled={pct !== null}
            onChange={(e) => setFile(e.currentTarget.files?.[0] ?? null)} class="min-w-0 text-[13px]" />
          <Button variant="primary" disabled={!file || pct !== null} onClick={start}>Install firmware</Button>
        </div>
        {pct !== null && (
          <div class="mt-4 max-w-md">
            <div class="h-2 overflow-hidden rounded-full bg-canvas" role="progressbar" aria-valuenow={pct} aria-valuemin={0} aria-valuemax={100}>
              <div class="h-full bg-accent transition-[width]" style={{ width: `${pct}%` }} />
            </div>
            <p class="num mt-1 text-[13px] text-muted">
              {done ? 'Installing. Do not power off. This page reloads in a minute.' : `Uploading ${pct}%`}
            </p>
          </div>
        )}
      </div>
    </Panel>
  );
}

/** A panel whose single action asks for confirmation first. */
function ConfirmPanel(props: {
  title: string;
  text: string;
  button: string;
  danger?: boolean;
  confirm: { title: string; body: string; confirm: string };
  action: () => void;
}) {
  return (
    <Panel title={props.title}>
      <div class="flex flex-wrap items-center justify-between gap-3 py-2">
        <p class="text-muted">{props.text}</p>
        <Button
          variant={props.danger ? 'danger' : 'plain'}
          onClick={async () => (await confirmAction({ ...props.confirm, danger: props.danger })) && props.action()}
        >
          {props.button}
        </Button>
      </div>
    </Panel>
  );
}

export function ManagementPage(_: { path?: string }) {
  return (
    <>
      <PageHeader title="Maintenance" />
      <Firmware />
      <ConfirmPanel
        title="Restart"
        text="Applies saved settings. Connections drop for about 30 seconds."
        button="Restart"
        confirm={{ title: 'Restart the gateway?', body: 'All connections drop until it is back up, in about 30 seconds.', confirm: 'Restart' }}
        action={restartDevice}
      />
      <ConfirmPanel
        title="Factory reset"
        text="Erases every setting, including the network address and login."
        button="Reset to factory settings"
        danger
        confirm={{
          title: 'Erase all settings?',
          body: 'The gateway returns to factory settings and restarts. Its address and login go back to the defaults printed on the label.',
          confirm: 'Erase and restart',
        }}
        action={() => attempt(api.factoryReset, 'Factory settings restored. The gateway is restarting.')}
      />
    </>
  );
}
