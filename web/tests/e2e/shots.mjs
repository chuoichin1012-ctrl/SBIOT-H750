// Screenshots of the console for a look at the layout (not a test): node tests/e2e/shots.mjs <base> <out dir>
import { readdirSync } from 'node:fs';
import { homedir } from 'node:os';
import { chromium } from 'playwright-core';

const [base = 'http://localhost:5173/', out = '.'] = process.argv.slice(2);
const dir = `${homedir()}/.cache/ms-playwright`;
const chrome = process.env.CHROME_PATH ?? `${dir}/${readdirSync(dir).filter((d) => /^chromium-\d+$/.test(d)).sort().at(-1)}/chrome-linux64/chrome`;
const browser = await chromium.launch({ executablePath: chrome });
const shot = async (name, width, height, hash, menu = false) => {
  const page = await browser.newPage({ viewport: { width, height } });
  await page.goto(base);
  await page.getByLabel('User name').fill('admin');
  await page.getByLabel('Password').fill('admin');
  await page.getByRole('button', { name: 'Sign in' }).click();
  await page.getByRole('heading', { name: 'Status' }).waitFor();
  if (hash) await page.goto(new URL(hash, base).href);
  await page.waitForTimeout(600);
  if (menu) await page.getByRole('button', { name: 'Open menu' }).click();
  await page.screenshot({ path: `${out}/${name}.png` });
  await page.close();
};
const login = await browser.newPage({ viewport: { width: 1366, height: 768 } });
await login.goto(base);
await login.waitForTimeout(400);
await login.screenshot({ path: `${out}/login.png` });
await shot('status', 1366, 820, '#/status');
await shot('mqtt', 1366, 820, '#/mqtt');
await shot('mobile', 390, 780, '#/status');
await shot('mobile-menu', 390, 780, '#/status', true);
await browser.close();
