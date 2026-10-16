// Bench test (Plan 06b): against the gateway itself, every settings page saves a change and finds it
// again after a reload. GW overrides the address (default http://192.168.0.7/, factory login).
//   node tests/e2e/pages.mjs
// The configs are read over the API first and written back at the end. Saved settings apply after a
// restart (spec §9.1), so the restart prompt is answered with "Continue".
import { readdirSync } from 'node:fs';
import { homedir } from 'node:os';
import { chromium } from 'playwright-core';

const base = process.env.GW ?? 'http://192.168.0.7/';
const auth = 'Basic ' + Buffer.from('admin:admin').toString('base64');
const CONFIGS = ['network', 'port1', 'port2', 'mqtt', 'system', 'io', 'edge', 'edgeReport'];

// [page, field label, new value, steps first: [label or #id, value]...]
const CASES = [
  ['#/network', 'APN', 'ui-apn-1'],
  ['#/port/1/serial', 'Baud rate', '9600'],
  ['#/port/2/serial', 'Baud rate', '19200'],
  ['#/mqtt', 'Client ID', 'ui-client-1', [['MQTT client', '1'], ['Broker address', 'broker.test']]],
  ['#/system', 'Host name', 'ui-host-1'],
  ['#/io/functions', 'Debounce time', '25'],
  ['#/edge/report', 'Send through', '1', [['#edge-enabled', '1']]],  // a select: UART1 socket
];

function chromePath() {
  if (process.env.CHROME_PATH) return process.env.CHROME_PATH;
  const dir = `${homedir()}/.cache/ms-playwright`;
  const builds = readdirSync(dir).filter((d) => /^chromium-\d+$/.test(d)).sort();
  if (!builds.length) throw new Error(`no Chromium in ${dir}; set CHROME_PATH`);
  return `${dir}/${builds.at(-1)}/chrome-linux64/chrome`;
}

const api = (path, body) =>
  fetch(new URL(path, base), {
    method: body ? 'POST' : 'GET',
    headers: { Authorization: auth, 'Content-Type': 'application/json' },
    body: body ? JSON.stringify(body) : undefined,
  });

let failures = 0;
const check = (ok, msg) => {
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${msg}`);
  if (!ok) failures++;
};

const saved = {};
for (const n of CONFIGS) saved[n] = await (await api(`api/config/${n}`)).json();

const browser = await chromium.launch({ executablePath: chromePath() });
const page = await browser.newPage();
try {
  const t0 = Date.now();
  await page.goto(base);
  await page.getByLabel('User name').fill('admin');
  await page.getByLabel('Password').fill('admin');
  await page.getByRole('button', { name: 'Sign in' }).click();
  await page.getByText('Status').first().waitFor();
  console.log(`     the UI loaded and signed in in ${Date.now() - t0} ms`);

  const locate = (what) => (what.startsWith('#') ? page.locator(what) : page.getByLabel(what, { exact: true }).first());
  const set = async (el, value) => {
    await el.waitFor({ timeout: 10000 });
    if ((await el.evaluate((e) => e.tagName)) === 'SELECT') await el.selectOption(value);
    else await el.fill(value);
  };
  for (const [hash, label, value, steps = []] of CASES) {
    try {
      await page.goto(base + hash);
      for (const [what, v] of steps) await set(locate(what), v);
      await set(locate(label), value);
      await page.getByRole('button', { name: /^Save/ }).click();  // 'Save settings', 'Save edge settings'
      await page.getByRole('button', { name: 'Continue' }).click({ timeout: 10000 });
      await page.reload();
      const again = page.getByLabel(label, { exact: true }).first();
      await again.waitFor({ timeout: 10000 });
      await page.waitForTimeout(500);  // the page fills its fields once the GET answers
      const got = await again.inputValue();
      check(got === value, `${hash}: "${label}" saved and found after a reload (${got})`);
    } catch (e) {
      check(false, `${hash}: "${label}": ${e.message.split('\n')[0]}`);
      if (process.env.SHOTS) await page.screenshot({ path: `${process.env.SHOTS}/fail-${hash.replace(/\W/g, '')}.png` });  // what was on screen
    }
  }
} finally {
  await browser.close();
  for (const n of CONFIGS) await api(`api/config/${n}`, saved[n]);
}
console.log(failures ? `pages: FAILED (${failures})` : 'pages: ok');
process.exit(failures ? 1 : 0);
