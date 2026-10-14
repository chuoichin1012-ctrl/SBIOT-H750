// Regression test: switching UARTs must never show (or save) the previous port's settings.
// Runs the UI in mock mode and drives it with headless Chromium: npm run test:e2e
// CHROME_PATH overrides the browser; by default the newest Playwright-cached Chromium is used.
import { spawn } from 'node:child_process';
import { readdirSync } from 'node:fs';
import { homedir } from 'node:os';
import { chromium } from 'playwright-core';

const PORT = 5240;
const base = `http://localhost:${PORT}/`;
function chromePath() {
  if (process.env.CHROME_PATH) return process.env.CHROME_PATH;
  const dir = `${homedir()}/.cache/ms-playwright`;
  const builds = readdirSync(dir).filter((d) => /^chromium-\d+$/.test(d)).sort();
  if (!builds.length) throw new Error(`no Chromium in ${dir}; set CHROME_PATH`);
  return `${dir}/${builds.at(-1)}/chrome-linux64/chrome`;
}

const server = spawn('npx', ['vite', '--mode', 'mock', '--port', String(PORT), '--strictPort'], { stdio: 'ignore' });
let failures = 0;
const check = (ok, msg) => { if (!ok) { failures++; console.log(`FAIL: ${msg}`); } };
try {
  for (let i = 0; i < 50; i++) {
    try { if ((await fetch(base)).ok) break; } catch { /* not up yet */ }
    await new Promise((r) => setTimeout(r, 200));
  }
  const browser = await chromium.launch({ executablePath: chromePath() });
  const page = await browser.newPage();
  await page.goto(base);
  await page.getByLabel('User name').fill('admin');
  await page.getByLabel('Password').fill('admin');
  await page.getByRole('button', { name: 'Sign in' }).click();
  await page.waitForTimeout(500);

  // I1: an unsaved edit on UART1 must not appear on UART2 while UART2 is loading.
  await page.goto(base + '#/port/1/serial');
  await page.getByLabel('Baud rate').waitFor();
  await page.getByLabel('Baud rate').fill('9600');
  await page.evaluate(() => { location.hash = '#/port/2/serial'; });
  const early = await page.evaluate(() => {
    const b = document.querySelector('[id$="baudRate"]');
    return { shown: !!b, value: b ? b.value : null, title: document.querySelector('h1')?.textContent ?? '' };
  });
  check(!(early.shown && early.value === '9600'), `UART2 shows UART1's unsaved baud rate while loading (${JSON.stringify(early)})`);
  await page.getByLabel('Baud rate').waitFor();
  check((await page.getByLabel('Baud rate').inputValue()) === '115200', 'UART2 shows its own baud rate once loaded');

  // I2: a certificate file chosen on UART1 must not carry over to UART2.
  await page.goto(base + '#/port/1/socket');
  await page.getByLabel('Working mode').waitFor();
  await page.getByLabel('Working mode').selectOption({ label: 'TCP client' });
  await page.getByLabel('TLS', { exact: true }).selectOption({ index: 1 });
  await page.getByLabel('Certificate check').selectOption({ label: 'Verify server certificate' });
  await page.locator('#cert-port1-ca').setInputFiles({ name: 'ca.pem', mimeType: 'application/x-pem-file', buffer: Buffer.from('-----BEGIN CERTIFICATE-----\n') });
  await page.evaluate(() => { location.hash = '#/port/2/socket'; });
  await page.getByLabel('Working mode').waitFor();
  await page.getByLabel('TLS', { exact: true }).selectOption({ index: 1 });
  await page.getByLabel('Certificate check').selectOption({ label: 'Verify server certificate' });
  const files = await page.locator('#cert-port2-ca').evaluate((el) => el.files.length);
  check(files === 0, `UART2's CA row starts with UART1's chosen file (${files} file(s))`);

  await browser.close();
} finally {
  server.kill();
}
console.log(failures ? `${failures} FAILED` : 'port-switch: all checks passed');
process.exit(failures ? 1 : 0);
