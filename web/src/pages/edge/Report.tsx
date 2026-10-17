import { range } from '../../lib/device';
import { reportModeError, templateError } from '../../lib/edge';
import { templateError26k } from '../../lib/rules';
import type { DeviceInfo, Values } from '../../lib/types';
import { EN_DIS, QOS, topicError, type FieldDef } from '../../lib/validate';

const mqtt = (v: Values) => Number(v.channel) === 0;
const query = (v: Values) => Number(v.remoteAccess) === 1;
const report = (v: Values) => Number(v.reporting) === 1;

/** Fields of the "Query & report" tab. Alibaba/AWS/Azure channels were removed. */
export function reportDefs(dev: DeviceInfo, nodeNames: string[], anyOnChange: boolean): FieldDef[] {
  return [
    {
      id: 'channel', label: 'Send through', kind: 'select',
      options: [[0, 'MQTT'], ...range(dev.portmax).map((n) => [n, `UART${n} socket`] as const)],
      help: 'The channel must be set up on its own page first.',
    },

    { id: 'remoteAccess', label: 'Remote query', kind: 'select', options: EN_DIS, help: 'Let the server read points on request' },
    { id: 'remoteWrite', label: 'Remote set', kind: 'check', help: 'Also let it write points (Modbus coils/holding registers, DO)', show: query },
    { id: 'remoteFormat', label: 'Request format', kind: 'select', options: [[0, 'Modbus RTU'], [1, 'Modbus TCP'], [2, 'JSON (rw_prot)']], show: query },
    { id: 'requestTopic', label: 'Request topic', kind: 'text', maxLength: 72, required: true, show: (v) => query(v) && mqtt(v), check: (t) => topicError(String(t), true) },
    { id: 'requestQos', label: 'Request QoS', kind: 'select', options: QOS, show: (v) => query(v) && mqtt(v) },
    { id: 'replyTopic', label: 'Reply topic', kind: 'text', maxLength: 72, required: true, show: (v) => query(v) && mqtt(v), check: (t) => topicError(String(t), false) },
    { id: 'replyQos', label: 'Reply QoS', kind: 'select', options: QOS, show: (v) => query(v) && mqtt(v) },
    { id: 'replyRetain', label: 'Reply retained', kind: 'check', help: 'Retain reply messages', show: (v) => query(v) && mqtt(v) },

    {
      id: 'reporting', label: 'Data reporting', kind: 'select', options: EN_DIS,
      check: (_, v) => (report(v) ? reportModeError(v, anyOnChange) : null),
    },
    { id: 'reportTopic', label: 'Report topic', kind: 'text', maxLength: 72, required: true, show: (v) => report(v) && mqtt(v), check: (t) => topicError(String(t), false) },
    { id: 'reportQos', label: 'Report QoS', kind: 'select', options: QOS, show: (v) => report(v) && mqtt(v) },
    { id: 'reportRetain', label: 'Report retained', kind: 'check', help: 'Retain report messages', show: (v) => report(v) && mqtt(v) },
    { id: 'periodic', label: 'Periodic', kind: 'check', help: 'Report at a fixed interval', show: report },
    { id: 'interval', label: 'Interval', kind: 'number', min: 1, max: 36000, unit: 's', show: (v) => report(v) && !!v.periodic },
    { id: 'scheduled', label: 'Scheduled', kind: 'check', help: "Report on the gateway's clock (network time once set, else the time since power-on)", show: report },
    {
      id: 'schedule', label: 'Schedule', kind: 'select', show: (v) => report(v) && !!v.scheduled,
      options: [[0, 'Every hour, on the hour'], [1, 'Every 15 minutes'], [2, 'Every minute'], [3, 'Once a day at a set time']],
    },
    { id: 'scheduleHour', label: 'Hour', kind: 'number', min: 0, max: 23, show: (v) => report(v) && !!v.scheduled && Number(v.schedule) === 3 },
    { id: 'scheduleMinute', label: 'Minute', kind: 'number', min: 0, max: 59, show: (v) => report(v) && !!v.scheduled && Number(v.schedule) === 3 },
    { id: 'failurePlaceholder', label: 'Failed reads', kind: 'check', help: 'Send a placeholder when a point cannot be read', show: report },
    { id: 'placeholder', label: 'Placeholder', kind: 'text', maxLength: 16, show: (v) => report(v) && !!v.failurePlaceholder },
    { id: 'quoteValues', label: 'Quote values', kind: 'check', help: 'Send values as JSON strings ("12.5")', show: report },
    {
      id: 'template', label: 'JSON template', kind: 'textarea', maxLength: 2048, required: true, show: report,
      help: 'Keys are your field names. A value naming a point, or sys_sn, sys_mac, sys_ver, sys_time (GMT), sys_unix_time, is replaced; any other text is sent as written.',
      check: (t) => templateError(String(t), nodeNames) ?? templateError26k(String(t), nodeNames),
    },
  ];
}
