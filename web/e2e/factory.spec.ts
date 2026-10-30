import { Page } from '@playwright/test';

import { expect, test } from './fixtures';

// No spinning gears or flowing items, so nodes are stable click targets.
test.use({ reducedMotion: 'reduce' });

function node(page: Page, label: string) {
  return page.locator(`.node[aria-label="${label}"]`);
}

/** Drags from one node to another with the mouse, as a player lays a belt. */
async function dragBelt(page: Page, from: string, to: string) {
  const a = await node(page, from).locator('.disc').boundingBox();
  const b = await node(page, to).locator('.disc').boundingBox();
  if (!a || !b) throw new Error('node not visible');
  const [x1, y1, x2, y2] = [
    a.x + a.width / 2,
    a.y + a.height / 2,
    b.x + b.width / 2,
    b.y + b.height / 2,
  ];
  await page.mouse.move(x1, y1);
  await page.mouse.down();
  for (let i = 1; i <= 12; i++)
    await page.mouse.move(x1 + ((x2 - x1) * i) / 12, y1 + ((y2 - y1) * i) / 12);
  await page.mouse.up();
}

async function openLevelOne(page: Page) {
  await page.goto('/factory');
  await expect(page.getByRole('heading', { name: 'First Stitch' })).toBeVisible();
  await expect(page.locator('.test-row').first()).toContainText('Initial');
  await expect(page.locator('.test-row').first()).toContainText('$8,100');
}

test('edit, test and play back a factory', async ({ page, problems }) => {
  await openLevelOne(page);

  // Remove the loom -> sewing belt from the loom's belt list, then lay it again by dragging.
  await node(page, 'Loom: Weave fabric').click();
  await page
    .getByRole('button', { name: /out · .*Fabric: Loom \(s1\) → Sewing station \(s3\)/ })
    .click();
  await page.getByRole('button', { name: 'Remove belt' }).click();
  await expect(page.locator('.issues.warn')).toContainText('s3 gets no Fabric');
  await dragBelt(page, 'Loom: Weave fabric', 'Sewing station: Sew stuffed animals');
  await expect(page.getByRole('heading', { name: 'Belt' })).toBeVisible();
  await expect(page.locator('.issues.warn')).toHaveCount(0);

  // A belt that cannot carry anything is refused with a reason.
  await dragBelt(page, 'Carder: Card stuffing', 'Market');
  await expect(page.locator('.toast')).toContainText('Nothing to carry');

  // Order less cotton. There is nothing to build: empty slots are not shown.
  await expect(page.locator('.node.machine')).toHaveCount(3);
  await expect(page.locator('.node.slot')).toHaveCount(0);
  await node(page, 'Cotton').click();
  await page.getByLabel('Order per hour (number)').fill('7');
  await page.getByLabel('Order per hour (number)').press('Enter');
  await expect(page.locator('.node.supplier .badge-main').first()).toHaveText('7/h');
  await expect(page.locator('.draft .build')).toContainText('$22,500');

  // Test it: the result is scored by the server and played back from hour 1.
  await page.getByRole('button', { name: 'Test factory' }).click();
  await expect(page.locator('.test-row.active')).toContainText('Test 1');
  await expect(page.locator('.test-row.active')).toContainText('$12,900');
  await expect(page.locator('.hour-line')).toContainText('Hour 1');
  for (let i = 0; i < 11; i++) await page.getByRole('button', { name: 'Next hour' }).click();
  await expect(page.locator('.hour-line')).toContainText('Hour 12');
  await expect(page.locator('.sold-line')).toContainText(/\d/);

  // What a machine is doing in this hour.
  await node(page, 'Sewing station: Sew stuffed animals').click();
  await expect(page.locator('.hour-status')).toContainText(/Working|Waiting|Blocked/);

  // Objectives tick off from the tests and stepped hours.
  const met = page.locator('.objectives li.met');
  await expect(met).toContainText(['Test the factory', 'Step through at least 6 hours of a test']);

  // Back to editing from the test.
  await page.getByRole('button', { name: 'Edit from Test 1' }).click();
  await expect(page.getByRole('button', { name: 'Undo' })).toBeEnabled();
  expect(problems).toEqual([]);
});

test('auto-build with the optimizers, inside the factory', async ({ page, problems }) => {
  await openLevelOne(page);
  await page.getByRole('button', { name: 'Auto', exact: true }).click();
  await expect(page.getByRole('tab', { name: /Auto/ })).toHaveAttribute('aria-selected', 'true');
  await page.getByLabel('Time each').selectOption('1000');
  await page.getByRole('checkbox', { name: /flow_lp/ }).uncheck();
  await page.getByRole('button', { name: 'Run optimizers' }).click();
  await expect(page.locator('p.bound')).toContainText('No factory can earn more than', {
    timeout: 20_000,
  });
  const rows = page.locator('.test-row');
  await expect(rows.filter({ hasText: 'local_search' })).toHaveCount(1);
  await expect(rows.filter({ hasText: 'greedy' })).toHaveCount(1);
  await expect(rows.filter({ hasText: 'flow_lp' })).toHaveCount(0);
  await expect(page.locator('.test-row.active ng-icon[name="lucideBot"]')).toHaveCount(1);
  await expect(page.locator('.runs tbody tr')).toHaveCount(2);
  await expect(page.locator('.comparison-chart g.bar')).toHaveCount(3);

  // Edit from an optimizer's factory: it becomes the draft.
  await page.locator('.runs').getByRole('button', { name: 'Edit from local_search' }).click();
  await expect(page.getByRole('button', { name: 'Undo' })).toBeEnabled();
  expect(problems).toEqual([]);
});

test('play helpers: hint, score, timeline and share link', async ({ page, problems }) => {
  await openLevelOne(page);
  await page.getByRole('tab', { name: /Play/ }).click();
  await expect(page.getByText('Test your own factory to get a score.')).toBeVisible();

  // The hint is a real improvement; doing it changes the draft (undoably).
  await page.getByRole('button', { name: 'Get a hint' }).click();
  await expect(page.locator('.card .gain')).toContainText('+$');
  await page.getByRole('button', { name: 'Do it' }).click();
  await expect(page.getByRole('button', { name: 'Undo' })).toBeEnabled();

  await page.getByRole('button', { name: 'Test factory' }).click();
  await expect(page.locator('.test-row.active')).toContainText('Test 1');
  await expect(page.locator('.stars')).toHaveAttribute('aria-label', /of 3 stars/);
  await expect(page.locator('.machine-timeline rect.cell').first()).toBeVisible();
  await page.locator('.machine-timeline rect.cell').nth(9).click({ force: true });
  await expect(page.locator('.hour-line')).toContainText('Hour 10');

  // Faster playback.
  await page.getByRole('button', { name: '4×' }).click();
  await expect(page.getByRole('button', { name: '4×' })).toHaveAttribute('aria-pressed', 'true');

  // A share link opens the level with the same draft, even in a fresh browser.
  await page.getByRole('button', { name: 'Continue editing' }).click();
  const link = await page.evaluate(() => {
    const d = localStorage.getItem('pf.factory.draft.first-stitch')!;
    const b64 = btoa(String.fromCharCode(...new TextEncoder().encode(d)))
      .replace(/\+/g, '-')
      .replace(/\//g, '_')
      .replace(/=+$/, '');
    return `/factory?level=first-stitch&design=${b64}`;
  });
  const sold = await page.locator('.node.supplier .badge-main').allTextContents();
  await page.evaluate(() => localStorage.clear());
  await page.goto(link);
  await expect(page.locator('.toast')).toContainText('Loaded a shared factory');
  await expect(page.locator('.node.supplier .badge-main')).toHaveText(sold);
  expect(problems).toEqual([]);
});

test('drafts are remembered and levels can be switched', async ({ page, problems }) => {
  await openLevelOne(page);
  await node(page, 'String').click();
  await page.getByLabel('Order per hour (number)').fill('6');
  await page.getByLabel('Order per hour (number)').press('Enter');
  await expect(node(page, 'String').locator('.badge-main')).toHaveText('6/h');
  await page.reload();
  await expect(node(page, 'String').locator('.badge-main')).toHaveText('6/h');

  await page.getByRole('tab', { name: /Hard/ }).click();
  await expect(page.getByRole('heading', { name: 'Full Line' })).toBeVisible();
  await expect(node(page, 'Rubber')).toBeVisible();
  await page.getByRole('tab', { name: /Easy/ }).click();
  await expect(page.getByRole('heading', { name: 'First Stitch' })).toBeVisible();
  expect(problems).toEqual([]);
});

test('the right panel folds away to a rail and back', async ({ page, problems }) => {
  await openLevelOne(page);
  await page.getByRole('button', { name: 'Hide panel' }).click();
  await expect(page.getByRole('tab', { name: /Build/ })).toBeHidden();
  // A rail button opens its tab.
  await page.getByRole('button', { name: 'Play', exact: true }).click();
  await expect(page.getByRole('tab', { name: /Play/ })).toHaveAttribute('aria-selected', 'true');
  await page.getByRole('button', { name: 'Hide panel' }).click();
  // Selecting a machine on the floor unfolds it on Build, to show the inspector.
  await page.locator('.node.machine').first().click({ force: true });
  await expect(page.getByRole('button', { name: 'Remove machine' })).toBeVisible();
  expect(problems).toEqual([]);
});
