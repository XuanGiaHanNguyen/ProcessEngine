// Captures the README images and the Play-mode GIF frames from a running app.
//   (pf_server on :8080 and `ng serve` on :4200 must be running)
//   node scripts/capture-docs.mjs ../docs/images && python3 scripts/make_gif.py ../docs/images
import { mkdirSync } from 'node:fs';
import { chromium } from '@playwright/test';

const out = process.argv[2] ?? '../docs/images';
const base = process.env.PF_WEB ?? 'http://localhost:4200';
mkdirSync(`${out}/frames`, { recursive: true });

const browser = await chromium.launch();
const page = await browser.newPage({ viewport: { width: 1280, height: 860 }, deviceScaleFactor: 1 });

// --- Auto mode: medium generated instance, five solvers ------------------------------------
await page.goto(`${base}/auto`);
await page.getByTestId('sample-select').selectOption('factory');
await page.getByTestId('instance-valid').waitFor();
for (const s of ['greedy_potential', 'simulated_annealing', 'lp_relaxation']) await page.getByTestId(`solver-${s}`).check();
await page.getByTestId('time-limit').fill('2000');
await page.getByTestId('run').click();
await page.getByTestId('earnings-lp_relaxation').getByText(/\d+/).waitFor({ timeout: 30000 });
await page.getByTestId('earnings-simulated_annealing').getByText(/\d+/).waitFor({ timeout: 30000 });
await page.waitForTimeout(800);
await page.screenshot({ path: `${out}/auto-mode.png`, fullPage: true });
await page.locator('app-comparison-view section').screenshot({ path: `${out}/comparison.png` });
await page.locator('app-results-view section').screenshot({ path: `${out}/results.png` });

// --- Play mode GIF frames -------------------------------------------------------------------
let frame = 0;
const shot = async (hold = 1) => {
  for (let i = 0; i < hold; i++) {
    await page.screenshot({ path: `${out}/frames/${String(frame++).padStart(3, '0')}.png` });
  }
};
await page.goto(`${base}/play`);
await page.getByTestId('play-sample-iron_chain').waitFor();
await shot(2);
await page.getByTestId('play-sample-iron_chain').click();
await page.getByTestId('start-smelt').waitFor();
await shot(2);
await page.getByTestId('start-smelt').click();
await page.getByTestId('inv-ore').getByText('4').waitFor();
await shot();
await page.getByTestId('start-smelt').click();
await page.getByTestId('inv-ore').getByText('2').waitFor();
await shot(2);
await page.getByTestId('skip').click();
await page.getByTestId('tick').getByText(/^2/).waitFor();
await shot();
await page.getByTestId('start-forge').click();
await page.getByTestId('inv-iron').getByText('0').waitFor();
await shot();
await page.getByTestId('start-smelt').click();
await page.getByTestId('projected').getByText('13').waitFor();
await shot(2);
await page.getByTestId('finish').click();
await page.getByTestId('final-score').waitFor();
await page.getByTestId('result').scrollIntoViewIfNeeded();
await shot(2);
await page.getByTestId('reveal').click();
await page.getByTestId('player-pct').waitFor({ timeout: 20000 });
await page.getByTestId('leaderboard').scrollIntoViewIfNeeded();
await page.waitForTimeout(300);
await shot(4);
await page.screenshot({ path: `${out}/play-mode.png`, fullPage: true });

await browser.close();
console.log(`captured ${frame} frames into ${out}/frames`);
