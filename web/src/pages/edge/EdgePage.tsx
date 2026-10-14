import { useEffect, useMemo, useState } from 'preact/hooks';
import * as api from '../../lib/api';
import { useDevice } from '../../lib/device';
import { session } from '../../lib/auth';
import { allNodeNames } from '../../lib/edge';
import { useAsync } from '../../lib/hooks';
import { useSaver } from '../../lib/save';
import { ON_OFF, coerce, formErrors } from '../../lib/validate';
import { Button, PageHeader, Panel, Pending, SaveBar, Tabs } from '../../components/ui';
import { Fields, Select } from '../../components/form';
import { Acquisition } from './Acquisition';
import { Linkage } from './Events';
import { reportDefs } from './Report';

const TABS = ['acquisition', 'report', 'linkage'] as const;

// Unsaved edits outlive the page component, so visiting another page and
// coming back does not lose them. Cleared on save and on discard.
// Tied to the session token so it never carries over to the next sign-in.
let draft: api.EdgeConfig | null = null;
let draftToken: string | null = null;

export function EdgePage(props: { path?: string; tab?: string }) {
  const dev = useDevice();
  const tab = (TABS as readonly string[]).includes(props.tab ?? '') ? props.tab! : 'acquisition';
  const src = useAsync(api.getEdge);
  const [cfg, setCfg] = useState<api.EdgeConfig | null>(null);
  const [dirty, setDirty] = useState(false);
  const [step, setStep] = useState('');
  const save = useSaver();

  useEffect(() => {
    if (!src.data) return;
    if (draftToken !== session.get().token) draft = null;
    const loaded = { ...src.data, report: coerce(src.data.report, reportDefs(dev, allNodeNames(src.data.slaves))) };
    setCfg(draft ?? loaded);
    setDirty(!!draft);
  }, [src.data]);

  const discard = () => {
    draft = null;
    src.reload();
  };

  const names = useMemo(() => (cfg ? allNodeNames(cfg.slaves) : []), [cfg]);
  const rDefs = useMemo(() => reportDefs(dev, names), [dev, names]);

  // Closing or reloading the tab would lose the draft; the browser asks first.
  useEffect(() => {
    if (!dirty) return;
    const warn = (e: BeforeUnloadEvent) => e.preventDefault();
    addEventListener('beforeunload', warn);
    return () => removeEventListener('beforeunload', warn);
  }, [dirty]);

  if (!cfg) return <Pending error={src.error} retry={src.reload} />;

  const change = (next: api.EdgeConfig) => {
    draft = next;
    draftToken = session.get().token;
    setCfg(next);
    setDirty(true);
  };
  const rErrors = cfg.enabled ? formErrors(rDefs, cfg.report) : {};
  const rBad = Object.keys(rErrors).length > 0;

  return (
    <>
      <PageHeader title="Edge computing" intro="Poll field devices, compute values, report them and act on them locally.">
        <label class="flex items-center gap-2" for="edge-enabled">
          <span class="text-muted">Edge computing</span>
          <Select id="edge-enabled" class="w-auto" value={cfg.enabled ? 1 : 0} options={ON_OFF}
            onChange={(v) => change({ ...cfg, enabled: v === 1 })} />
        </label>
      </PageHeader>

      {!cfg.enabled ? (
        <Panel>
          <p class="py-3 text-muted">Edge computing is off. Turn it on to poll devices, report data and run local events. Your points are kept.</p>
        </Panel>
      ) : (
        <>
          <Tabs base="/edge" active={tab} tabs={[
            ['acquisition', 'Data acquisition'],
            ['report', `Query & report${rBad ? ' (check)' : ''}`],
            ['linkage', 'Linkage'],
          ]} />
          {tab === 'acquisition' && <Acquisition cfg={cfg} onChange={change} />}
          {tab === 'report' && (
            <Panel>
              <Fields defs={rDefs} values={cfg.report} errors={rErrors}
                onChange={(id, v) => change({ ...cfg, report: { ...cfg.report, [id]: v } })} />
            </Panel>
          )}
          {tab === 'linkage' && <Linkage cfg={cfg} onChange={change} />}
        </>
      )}

      <SaveBar
        busy={save.busy}
        disabled={rBad || !dirty}
        label="Save edge settings"
        busyLabel={`Saving ${step}…`}
        note={rBad ? 'Query & report has fields that need attention.' : dirty ? 'Unsaved changes. They are kept while you visit other pages, until you save or discard.' : undefined}
        onSave={() => save.run(async () => {
          await api.saveEdge(cfg, setStep);
          draft = null;
        }, { restart: true, after: discard })}
      >
        {dirty && <Button onClick={discard}>Discard changes</Button>}
      </SaveBar>
    </>
  );
}
