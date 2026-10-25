// Captures the Factory-mode README images and GIF frames from a running app.
//   (pf_server on :8080 and `ng serve` on :4200 must be running)
//   node scripts/capture-factory.mjs ../docs/images && python3 scripts/make_gif.py ../docs/images factory-mode.gif
import { mkdirSync } from 'node:fs';
import { chromium } from '@playwright/test';

const out = process.argv[2] ?? '../docs/images';
const base = process.env.PF_WEB ?? 'http://localhost:4200';
mkdirSync(`${out}/frames`, { recursive: true });

const browser = await chromium.launch();
const page = await browser.newPage({ viewport: { width: 1280, height: 800 }, deviceScaleFactor: 1 });
const node = (label) => page.locator(`g.node[aria-label="${label}"] circle.disc`).first();

let frame = 0;
const shot = async (hold = 1) => {
  for (let i = 0; i < hold; i++) {
    await page.screenshot({ path: `${out}/frames/${String(frame++).padStart(3, '0')}.png` });
  }
};

await page.goto(`${base}/factory`);
await page.locator('.test-row').first().waitFor();
await page.waitForTimeout(1200);
await shot(2);

// Build a second sewing station and trim the cotton order.
await node('Empty slot s4').click({ force: true });
await page.waitForTimeout(300);
await shot(2);
await page.getByRole('button', { name: 'Build Sewing station: Sew stuffed animals' }).click();
await page.waitForTimeout(700);
await shot(2);
await node('Cotton').click({ force: true });
await page.getByLabel('Order per hour (number)').fill('7');
await page.getByLabel('Order per hour (number)').press('Enter');
await page.waitForTimeout(500);
await shot(2);

// Test it and step through the day.
await page.getByRole('button', { name: '▶ Test factory' }).click();
await page.locator('.playback').waitFor();
await page.waitForTimeout(400);
await shot(2);
for (let h = 0; h < 10; h++) {
  await page.getByRole('button', { name: 'Next hour' }).click();
  await page.waitForTimeout(250);
  await shot(1);
}
await node('Sewing station: Sew stuffed animals').click({ force: true });
await page.waitForTimeout(300);
await shot(2);
await page.screenshot({ path: `${out}/factory-mode.png` });
for (let h = 0; h < 13; h++) {
  await page.getByRole('button', { name: 'Next hour' }).click();
  await page.waitForTimeout(150);
  if (h % 3 === 2) await shot(1);
}
await shot(2);

// Compare with the optimizers.
await page.keyboard.press('Escape');
await page.getByRole('button', { name: '🤖 Compare' }).click();
await page.locator('.bound').waitFor({ timeout: 30000 });
await page.waitForTimeout(800);
await shot(3);

await browser.close();
console.log(`captured ${frame} frames into ${out}/frames`);
