import { useState } from 'preact/hooks';
import * as api from '../lib/api';
import { useAction } from './feedback';
import { Row } from './form';
import { Button } from './ui';

const KINDS = [
  ['ca', 'Server CA certificate'],
  ['cert', 'Client certificate'],
  ['key', 'Client private key'],
] as const;

/** Certificate files for a TLS connection; each file uploads on its own. */
export function CertUpload(props: { scope: string; verify: number }) {
  // "Verify server" needs the CA only; "verify both" also needs the client pair.
  const kinds = props.verify >= 2 ? KINDS : props.verify === 1 ? KINDS.slice(0, 1) : [];
  return (
    <>
      {kinds.map(([kind, label]) => (
        <CertRow key={kind} scope={props.scope} kind={kind} label={label} />
      ))}
    </>
  );
}

/** One line on what was stored, with what still needs attention. */
function describe(i: api.CertInfo | undefined): { text: string; warn: boolean } | null {
  if (!i) return null;
  const parts: string[] = [];
  let warn = false;
  if (i.subject) parts.push(i.subject);
  if (i.keyType) parts.push(`${i.keyType} key`);
  if (i.validTo) parts.push(i.expired ? `expired on ${i.validTo}` : `valid until ${i.validTo}`);
  if (i.expired) warn = true;
  if (i.pairMatches === false) {
    parts.push('does not match the stored client certificate/key: upload the other half too');
    warn = true;
  }
  if (i.restartNeeded) parts.push('MQTT uses it after a restart');
  return { text: parts.join(' · '), warn };
}

function CertRow(props: { scope: string; kind: 'ca' | 'cert' | 'key'; label: string }) {
  const [file, setFile] = useState<File | null>(null);
  const [info, setInfo] = useState<api.CertInfo | undefined>();
  const up = useAction();
  const note = describe(info);
  const id = `cert-${props.scope}-${props.kind}`;
  return (
    <Row label={props.label} htmlFor={id} help="PEM file. Uploads immediately, separate from Save settings.">
      <div class="flex flex-wrap items-center gap-2">
        <input id={id} type="file" accept=".pem,.crt,.cer,.key" class="min-w-0 flex-1 text-[13px]"
          onChange={(e) => setFile(e.currentTarget.files?.[0] ?? null)} />
        <Button disabled={!file || up.busy} onClick={() => file && up.run(async () => setInfo(await api.uploadCert(props.scope, props.kind, file)), `${props.label} uploaded.`)}>
          {up.busy ? 'Uploading…' : 'Upload'}
        </Button>
      </div>
      {note && <p class={`mt-1 text-[12px] ${note.warn ? 'text-led-warn' : 'text-muted'}`}>{note.text}</p>}
    </Row>
  );
}
