// Screenshots of every page for a look at the layout (not a test): node tests/e2e/shots.mjs <base> <out dir> [pages...]
import { readdirSync } from 'node:fs';
import { homedir } from 'node:os';
import { chromium } from 'playwright-core';

const [base = 'http://localhost:5173/', out = '.', ...only] = process.argv.slice(2);
const PAGES = [
  ['status', '#/status'], ['network', '#/network'], ['port-serial', '#/port/1/serial'], ['port-socket', '#/port/1/socket'],
  ['websocket', '#/websocket'], ['mqtt-broker', '#/mqtt/broker'], ['mqtt-publish', '#/mqtt/publish'],
  ['edge-acq', '#/edge/acquisition'], ['edge-report', '#/edge/report'], ['edge-linkage', '#/edge/linkage'],
  ['io-control', '#/io/control'], ['io-functions', '#/io/functions'], ['system', '#/system'], ['management', '#/management'],
];
const dir = `${homedir()}/.cache/ms-playwright`;
const chrome = process.env.CHROME_PATH ?? `${dir}/${readdirSync(dir).filter((d) => /^chromium-\d+$/.test(d)).sort().at(-1)}/chrome-linux64/chrome`;
const browser = await chromium.launch({ executablePath: chrome });
const signedIn = async (width, height) => {
  const page = await browser.newPage({ viewport: { width, height } });
  await page.goto(base);
  await page.getByLabel('User name').fill('admin');
  await page.getByLabel('Password').fill('admin');
  await page.getByRole('button', { name: 'Sign in' }).click();
  await page.getByRole('heading', { name: 'Status' }).waitFor();
  return page;
};
const page = await signedIn(1366, 900);
for (const [name, hash] of PAGES.filter(([n]) => !only.length || only.includes(n))) {
  await page.goto(new URL(hash, base).href);
  await page.waitForTimeout(700);
  await page.screenshot({ path: `${out}/${name}.png`, fullPage: true });
}
if (!only.length) {
  const login = await browser.newPage({ viewport: { width: 1366, height: 768 } });
  await login.goto(base);
  await login.waitForTimeout(400);
  await login.screenshot({ path: `${out}/login.png` });
  const phone = await signedIn(390, 780);
  await phone.waitForTimeout(500);
  await phone.screenshot({ path: `${out}/mobile.png` });
  await phone.getByRole('button', { name: 'Open menu' }).click();
  await phone.screenshot({ path: `${out}/mobile-menu.png` });
}
await browser.close();
