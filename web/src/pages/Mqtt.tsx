import { useEffect, useMemo, useState } from 'preact/hooks';
import * as api from '../lib/api';
import { range, useDevice } from '../lib/device';
import { useAsync, useForm } from '../lib/hooks';
import { useSaver } from '../lib/save';
import type { Values } from '../lib/types';
import { EN_DIS, QOS, TLS, TLS_VERIFY, coerce, formErrors, topicError, type FieldDef } from '../lib/validate';
import { CertUpload } from '../components/CertUpload';
import { PageHeader, Panel, Pending, SaveBar, Tabs } from '../components/ui';
import { Checkbox, Fields } from '../components/form';

const MAX_TOPICS = 16;
const on = (v: Values) => Number(v.enabled) === 1;
const when = (f: (v: Values) => boolean) => (v: Values) => on(v) && f(v);

const brokerDefs: FieldDef[] = [
  { id: 'enabled', label: 'MQTT client', kind: 'select', options: [[0, 'Off'], [1, 'On']] },
  { id: 'version', label: 'Protocol version', kind: 'select', options: [[0, '3.1'], [1, '3.1.1']], show: on },
  { id: 'host', label: 'Broker address', kind: 'text', maxLength: 128, required: true, show: on, help: 'IP address or domain name' },
  { id: 'port', label: 'Broker port', kind: 'number', min: 1, max: 65535, show: on },
  { id: 'clientId', label: 'Client ID', kind: 'text', maxLength: 128, required: true, show: on },
  { id: 'keepAlive', label: 'Keep-alive', kind: 'number', min: 0, max: 65535, unit: 's', show: on },
  { id: 'reconnectIfSilent', label: 'Reconnect after silence', kind: 'number', min: 60, max: 65535, zeroOk: true, unit: 's', show: on, help: 'Reconnect when nothing was received for this long; 0 = never' },
  { id: 'retryInterval', label: 'Retry interval', kind: 'number', min: 3, max: 65535, unit: 's', show: on },
  { id: 'cleanSession', label: 'Clean session', kind: 'check', help: 'Start each connection with a clean session', show: on },
  { id: 'auth', label: 'Authentication', kind: 'check', help: 'Log in with a user name and password', show: on },
  { id: 'username', label: 'User name', kind: 'text', maxLength: 128, show: when((v) => !!v.auth) },
  { id: 'password', label: 'Password', kind: 'password', maxLength: 200, show: when((v) => !!v.auth) },
  { id: 'will', label: 'Last will', kind: 'check', help: 'Broker publishes this message if the gateway drops off', show: on },
  { id: 'willTopic', label: 'Will topic', kind: 'text', maxLength: 128, show: when((v) => !!v.will), check: (t) => topicError(String(t), false) },
  { id: 'willMessage', label: 'Will message', kind: 'text', maxLength: 128, show: when((v) => !!v.will) },
  { id: 'willQos', label: 'Will QoS', kind: 'select', options: QOS, show: when((v) => !!v.will) },
  { id: 'willRetain', label: 'Will retained', kind: 'check', help: 'Retain the will message', show: when((v) => !!v.will) },
  { id: 'tls', label: 'TLS', kind: 'select', options: TLS, show: on },
  { id: 'tlsVerify', label: 'Certificate check', kind: 'select', options: TLS_VERIFY, show: when((v) => Number(v.tls) > 0) },
  { id: 'sni', label: 'Server name (SNI)', kind: 'check', help: 'Send a host name during the TLS handshake', show: when((v) => Number(v.tls) > 0) },
  { id: 'sniHost', label: 'Host name', kind: 'text', maxLength: 128, required: true, show: when((v) => Number(v.tls) > 0 && !!v.sni) },
];

/** One publish or subscribe topic. `ports` is a bitmask, bit 0 = UART1. */
interface Topic {
  enabled: boolean;
  mode: number;
  topic: string;
  ports: number;
  qos: number;
  retain: boolean;
  ioControl: boolean;
  identifier: string;
  delimiter: string;
}

// A row mirrors the firmware's ITEM_TOPIC, with its sample topic (cfgmodel.c DEF_PUBLISH / DEF_SUBSCRIBE).
const blank = (sub = false): Topic => ({
  enabled: false, mode: 0, topic: sub ? 'sbiot/gw/down' : 'sbiot/gw/up', ports: 1, qos: 0, retain: false, ioControl: false,
  identifier: 'sbiot', delimiter: ',',
});

const EN: FieldDef = { id: 'enabled', label: '', kind: 'check' };

function normalize(rows: Values[] | undefined, defs: FieldDef[], sub: boolean): Topic[] {
  const out = (rows ?? []).slice(0, MAX_TOPICS).map((r) => ({ ...blank(sub), ...(coerce(r, [EN, ...defs]) as unknown as Partial<Topic>) }));
  while (out.length < MAX_TOPICS) out.push(blank(sub));
  return out;
}

const distribute = (v: Values) => Number(v.mode) === 1;

function topicDefs(sub: boolean, portmax: number): FieldDef[] {
  return [
    {
      id: 'topic', label: 'Topic', kind: 'text', maxLength: 128, required: true, mono: true, wide: true,
      placeholder: sub ? 'site/+/cmd' : 'site/gw01/data', check: (t) => topicError(String(t), sub),
    },
    {
      id: 'mode', label: 'Mode', kind: 'select',
      options: sub ? [[0, 'Payload only'], [1, 'Topic + payload']] : [[0, 'Transparent'], [1, 'Distribute by identifier']],
    },
    { id: 'qos', label: 'QoS', kind: 'select', options: QOS },
    // Publish "distribute": serial data "identifier,payload" goes to this topic as "payload";
    // subscribe "topic + payload" joins them with a delimiter.
    {
      id: 'identifier', label: 'Identifier', kind: 'text', maxLength: 64, required: true, mono: true, show: (v) => !sub && distribute(v),
      help: 'Serial data "identifier,payload" is published here as "payload"',
    },
    { id: 'delimiter', label: 'Delimiter', kind: 'text', maxLength: 16, required: true, mono: true, show: (v) => sub && distribute(v) },
    {
      id: 'ports', label: sub ? 'Deliver to' : 'Data from', kind: 'flags', wide: true,
      options: range(portmax).map((n) => [n - 1, `UART${n}`] as const),
      check: (m, v) => (!Number(m) && !v.ioControl ? 'Bind at least one port, or enable IO control.' : null),
    },
    { id: 'ioControl', label: 'IO control', kind: 'check', help: 'Carries IO read and write commands' },
    { id: 'retain', label: 'Retained', kind: 'check', help: 'Retain messages', show: () => !sub },
  ];
}

/**
 * Custom mode: the serial data names its own topic, so no publish topics are
 * configured. Stored with the broker settings (`customMode`, `customModePorts` bitmask).
 */
function customDefs(portmax: number): FieldDef[] {
  return [
    { id: 'customMode', label: 'Custom mode', kind: 'select', options: EN_DIS, show: on },
    {
      id: 'customModePorts', label: 'Read from', kind: 'flags', show: (v) => on(v) && Number(v.customMode) === 1,
      options: range(portmax).map((n) => [n - 1, `UART${n}`] as const),
      check: (m) => (Number(m) ? null : 'Choose at least one port.'),
    },
  ];
}

function TopicEditor(props: { sub: boolean; rows: Topic[]; defs: FieldDef[]; onChange: (rows: Topic[]) => void }) {
  const { rows, sub } = props;
  // Topic n+1 appears once topic n is enabled, as on the original firmware.
  const lastOn = rows.reduce((acc, t, i) => (t.enabled ? i : acc), -1);
  const visible = Math.min(lastOn + 2, MAX_TOPICS);
  const set = (i: number, patch: Partial<Topic>) => props.onChange(rows.map((t, j) => (j === i ? { ...t, ...patch } : t)));

  return (
    <div class="divide-y divide-line">
      {rows.slice(0, visible).map((t, i) => {
        const values = t as unknown as Values;
        return (
          <fieldset key={i} class="px-4 py-3">
            <legend class="sr-only">Topic {i + 1}</legend>
            <Checkbox checked={t.enabled} onChange={(enabled) => set(i, { enabled })} label={<span class="font-medium">Topic {i + 1}</span>} />
            {t.enabled && (
              <div class="mt-3">
                <Fields layout="grid" idPrefix={`${sub ? 's' : 'p'}${i}-`} defs={props.defs} values={values}
                  errors={formErrors(props.defs, values)} onChange={(id, v) => set(i, { [id]: v } as Partial<Topic>)} />
              </div>
            )}
          </fieldset>
        );
      })}
      {visible < MAX_TOPICS && (
        <p class="px-4 py-2.5 text-[13px] text-muted">Enable the last topic to add another. Up to {MAX_TOPICS} topics.</p>
      )}
    </div>
  );
}

export function MqttPage(props: { path?: string; tab?: string }) {
  const d = useDevice();
  const pubDefs = useMemo(() => topicDefs(false, d.portmax), [d]);
  const subDefs = useMemo(() => topicDefs(true, d.portmax), [d]);
  const tab = props.tab === 'publish' || props.tab === 'subscribe' ? props.tab : 'broker';
  const custom = useMemo(() => customDefs(d.portmax), [d]);
  const baseDefs = useMemo(() => [...brokerDefs, ...custom], [custom]);
  const base = useForm(() => api.getValues('mqtt'), baseDefs);
  const lists = useAsync(() => Promise.all([api.getList('mqttPublish'), api.getList('mqttSubscribe')]));
  const [pub, setPub] = useState<Topic[]>([]);
  const [sub, setSub] = useState<Topic[]>([]);
  const save = useSaver();

  useEffect(() => {
    if (lists.data) {
      setPub(normalize(lists.data[0], pubDefs, false));
      setSub(normalize(lists.data[1], subDefs, true));
    }
  }, [lists.data]);

  if (!base.ready || !pub.length) {
    return <Pending error={base.error ?? lists.error} retry={() => (base.reload(), lists.reload())} />;
  }

  const enabled = on(base.values);
  const bad = (list: Topic[], defs: FieldDef[]) =>
    list.some((t) => t.enabled && Object.keys(formErrors(defs, t as unknown as Values)).length > 0);
  const customOn = enabled && Number(base.values.customMode) === 1;
  const has = (defs: FieldDef[]) => defs.some((x) => base.errors[x.id]);
  const brokerBad = has(brokerDefs);
  // In custom mode the publish topics are unused, so they do not block saving.
  const pubBad = has(custom) || (!customOn && bad(pub, pubDefs));
  const subBad = bad(sub, subDefs);
  const tls = enabled && Number(base.values.tls) > 0;

  // Only enabled topics are sent; disabled ones carry no settings worth keeping.
  const rows = (list: Topic[]) => list.filter((t) => t.enabled) as unknown as Values[];

  return (
    <>
      <PageHeader title="MQTT" intro="Connect to a broker and map serial ports to topics." />
      <Tabs base="/mqtt" active={tab} tabs={[
        ['broker', `Broker${brokerBad ? ' (check)' : ''}`],
        ['publish', `Publish${pubBad ? ' (check)' : ''}`],
        ['subscribe', `Subscribe${subBad ? ' (check)' : ''}`],
      ]} />

      {tab === 'broker' && (
        <Panel>
          <Fields defs={brokerDefs} values={base.values} errors={base.errors} onChange={base.set} />
          {tls && <CertUpload scope="mqtt" verify={Number(base.values.tlsVerify)} />}
        </Panel>
      )}
      {tab !== 'broker' && !enabled && (
        <p class="mb-4 text-muted">Topics take effect once the MQTT client is on (Broker tab).</p>
      )}
      {tab === 'publish' && (
        <>
          {enabled && <Panel>
            <Fields defs={custom} values={base.values} errors={base.errors} onChange={base.set} />
            {customOn && (
              <p class="pb-2 text-[13px] text-muted">
                Each serial message names its own topic: <code class="font-mono">topic,qos,retain,payload</code>.
                For example <code class="font-mono">/site/temp,0,off,21.5</code> publishes <code class="font-mono">21.5</code> to{' '}
                <code class="font-mono">/site/temp</code> with QoS 0, not retained.
              </p>
            )}
          </Panel>}
          {!customOn && (
            <Panel title="Publish: serial data to the broker" flush>
              <TopicEditor sub={false} rows={pub} defs={pubDefs} onChange={setPub} />
            </Panel>
          )}
        </>
      )}
      {tab === 'subscribe' && (
        <Panel title="Subscribe: broker messages to serial ports" flush>
          <TopicEditor sub rows={sub} defs={subDefs} onChange={setSub} />
        </Panel>
      )}

      <SaveBar
        busy={save.busy}
        invalid={brokerBad || pubBad || subBad}
        tabbed
        onSave={() =>
          save.run(async () => {
            await api.saveValues('mqtt', base.values);
            await api.saveList('mqttPublish', rows(pub));
            await api.saveList('mqttSubscribe', rows(sub));
          }, { restart: true, after: () => (base.reload(), lists.reload()) })
        }
      />
    </>
  );
}
