import { Page } from '@playwright/test';

import { expect, test } from './fixtures';

// No spinning gears or flowing items, so nodes are stable click targets.
test.use({ reducedMotion: 'reduce' });

function node(page: Page, label: string) {
  return page.locator(`g.node[aria-label="${label}"]`);
}

/** Drags from one node to another with the mouse, as a player lays a belt. */
async function dragBelt(page: Page, from: string, to: string) {
  const a = await node(page, from).locator('circle.disc').boundingBox();
  const b = await node(page, to).locator('circle.disc').boundingBox();
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
  await expect(page.locator('.test-row').first()).toContainText('$16.20');
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

  // Build a second sewing station and order less cotton.
  await node(page, 'Empty slot s4').click();
  await page.getByRole('button', { name: 'Build Sewing station: Sew stuffed animals' }).click();
  await expect(page.locator('g.node.machine')).toHaveCount(4);
  await node(page, 'Cotton').click();
  await page.getByLabel('Order per hour (number)').fill('7');
  await page.getByLabel('Order per hour (number)').press('Enter');
  await expect(page.locator('g.node.supplier .badge-main').first()).toHaveText('7/h');
  await expect(page.locator('.draft .build')).toContainText('$65.00');

  // Test it: the result is scored by the server and played back from hour 1.
  await page.getByRole('button', { name: '▶ Test factory' }).click();
  await expect(page.locator('.test-row.active')).toContainText('Test 1');
  await expect(page.locator('.test-row.active')).toContainText('$99.80');
  await expect(page.locator('.hour-line')).toContainText('Hour 1');
  for (let i = 0; i < 11; i++) await page.getByRole('button', { name: 'Next hour' }).click();
  await expect(page.locator('.hour-line')).toContainText('Hour 12');
  await expect(page.locator('.sold-line')).toContainText('🧸');

  // What a machine is doing in this hour.
  await node(page, 'Sewing station: Sew stuffed animals').first().click();
  await expect(page.locator('.hour-status')).toContainText(/Working|Waiting|Blocked/);

  // Objectives tick off from the tests and stepped hours.
  const met = page.locator('.objectives li.met');
  await expect(met).toContainText([
    'Test the factory',
    'Step through at least 6 hours of a test',
    'Earn $90',
  ]);

  // Back to editing from the test.
  await page.getByRole('button', { name: 'Edit from Test 1' }).click();
  await expect(page.getByRole('button', { name: 'Undo' })).toBeEnabled();
  expect(problems).toEqual([]);
});

test('compare with the optimizers', async ({ page, problems }) => {
  await openLevelOne(page);
  await page.getByRole('button', { name: '🤖 Compare' }).click();
  await expect(page.locator('.bound')).toContainText('No factory can earn more than', {
    timeout: 20_000,
  });
  const rows = page.locator('.test-row');
  await expect(rows.filter({ hasText: '🤖 local_search' })).toHaveCount(1);
  await expect(rows.filter({ hasText: '🤖 greedy' })).toHaveCount(1);
  await expect(rows.filter({ hasText: '🤖 flow_lp' })).toHaveCount(1);
  await expect(page.locator('.test-row.active')).toContainText('🤖');
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

  await page.getByRole('button', { name: 'Next level' }).click();
  await expect(page.getByRole('heading', { name: 'Up and Away' })).toBeVisible();
  await expect(node(page, 'Wood')).toBeVisible();
  await page.getByRole('button', { name: 'Previous level' }).click();
  await expect(page.getByRole('heading', { name: 'First Stitch' })).toBeVisible();
  expect(problems).toEqual([]);
});
