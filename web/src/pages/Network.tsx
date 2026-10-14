import * as api from '../lib/api';
import { useMemo } from 'preact/hooks';
import { useAsync, useForm } from '../lib/hooks';
import { useSaver } from '../lib/save';
import type { StatusInfo, Values } from '../lib/types';
import type { FieldDef } from '../lib/validate';
import { Notice, PageHeader, Panel, Pending, SaveBar } from '../components/ui';
import { Fields } from '../components/form';

const isStatic = (v: Values) => Number(v.ipMode) === 1;
const ethDnsManual = (v: Values) => isStatic(v) || Number(v.dnsMode) === 1;
const lteUsed = (v: Values) => Number(v.priority) !== 0;

const priority: FieldDef[] = [
  {
    id: 'priority', label: 'Network priority', kind: 'select',
    options: [[0, 'Ethernet only'], [1, 'Ethernet first, cellular as backup'], [2, 'Cellular first, Ethernet as backup']],
  },
];

// Address fields stay visible in automatic modes, locked and showing what the
// gateway is using right now (from the live status), as on the reference unit.
const ethDnsAuto = (v: Values) => !ethDnsManual(v);
const eth: FieldDef[] = [
  { id: 'ipMode', label: 'IP address mode', kind: 'select', options: [[0, 'DHCP'], [1, 'Static']] },
  { id: 'ip', placeholder: 'Assigned by DHCP', label: 'IP address', kind: 'ipv4', disabled: (v) => !isStatic(v) },
  { id: 'netmask', placeholder: 'Assigned by DHCP', label: 'Subnet mask', kind: 'ipv4', disabled: (v) => !isStatic(v) },
  { id: 'gateway', placeholder: 'Assigned by DHCP', label: 'Gateway', kind: 'ipv4', disabled: (v) => !isStatic(v) },
  {
    id: 'dnsMode', label: 'DNS', kind: 'select', options: [[0, 'Automatic'], [1, 'Manual']],
    disabled: isStatic, help: 'Locked to manual while the IP address is static.',
  },
  { id: 'dns1', placeholder: 'Assigned automatically', label: 'Preferred DNS server', kind: 'ipv4', disabled: ethDnsAuto },
  { id: 'dns2', placeholder: 'Assigned automatically', label: 'Alternate DNS server', kind: 'ipv4', disabled: ethDnsAuto },
];

const lteDnsAuto = (v: Values) => Number(v.cellDnsMode) !== 1;
/** Quectel EC600U (LTE Cat 1, single SIM) over PPP. */
const lte: FieldDef[] = ([
  { id: 'apn', label: 'APN', kind: 'text', maxLength: 30, help: 'From your carrier. Leave empty to use the carrier default.' },
  { id: 'apnAuth', label: 'Authentication', kind: 'select', options: [[0, 'None'], [1, 'PAP'], [2, 'CHAP'], [3, 'PAP or CHAP (automatic)']] },
  { id: 'apnUser', label: 'User name', kind: 'text', maxLength: 30, show: (v) => Number(v.apnAuth) !== 0 },
  { id: 'apnPassword', label: 'Password', kind: 'password', maxLength: 30, show: (v) => Number(v.apnAuth) !== 0 },
  { id: 'cellDnsMode', label: 'DNS', kind: 'select', options: [[0, 'Automatic'], [1, 'Manual']] },
  { id: 'cellDns1', placeholder: 'Assigned automatically', label: 'Preferred DNS server', kind: 'ipv4', disabled: lteDnsAuto },
  { id: 'cellDns2', placeholder: 'Assigned automatically', label: 'Alternate DNS server', kind: 'ipv4', disabled: lteDnsAuto },
] as FieldDef[]).map((d) => ({ ...d, show: (v: Values) => lteUsed(v) && (d.show ? d.show(v) : true) }));

/** Values to display: saved settings, with live addresses in the locked automatic fields. */
function displayed(v: Values, st: StatusInfo | undefined): Values {
  if (!st) return v;
  const out = { ...v };
  if (!isStatic(v)) Object.assign(out, { ip: st.eth.ip, netmask: st.eth.mask, gateway: st.eth.gateway });
  if (ethDnsAuto(v)) Object.assign(out, { dns1: st.eth.dns1, dns2: st.eth.dns2 });
  if (lteDnsAuto(v)) Object.assign(out, { cellDns1: st.cell.dns1, cellDns2: st.cell.dns2 });
  return out;
}

export function NetworkPage(_: { path?: string }) {
  const all = useMemo(() => [...priority, ...eth, ...lte], []);
  const f = useForm(() => api.getValues('network'), all);
  const live = useAsync(api.getStatus);
  const save = useSaver();

  if (!f.ready) return <Pending error={f.error} retry={f.reload} />;

  const props = { values: displayed(f.values, live.data), errors: f.errors, onChange: f.set };
  return (
    <>
      <PageHeader title="WAN & LAN" intro="How the gateway reaches the network, and which link it prefers." />
      <Notice tone="warn">
        Changing the address, gateway or priority can move the gateway to a new address. Note it before you save.
      </Notice>
      <Panel><Fields defs={priority} {...props} /></Panel>
      <Panel title="Ethernet"><Fields defs={eth} {...props} /></Panel>
      <Panel title="Cellular (EC600U LTE)">
        {lteUsed(f.values) ? <Fields defs={lte} {...props} /> : <p class="py-2 text-muted">Cellular is off while the priority is Ethernet only.</p>}
      </Panel>
      <SaveBar
        busy={save.busy}
        invalid={!f.valid}
        onSave={() => save.run(() => api.saveValues('network', f.values), { restart: true, after: f.reload })}
      />
    </>
  );
}
