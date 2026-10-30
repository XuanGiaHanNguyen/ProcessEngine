import { expect, test } from './fixtures';

test('load a sample and compare 3 solvers in under 30 seconds', async ({ page, problems }) => {
  const started = Date.now();
  await page.goto('/auto');
  await page.getByTestId('sample-select').selectOption('factory');
  await expect(page.getByTestId('instance-valid')).toBeVisible();

  // greedy, beam_search and branch_and_bound are preselected.
  await expect(page.getByTestId('run')).toHaveText(/Run 3 solvers/);
  await page.getByTestId('run').click();

  const table = page.getByTestId('comparison-table');
  for (const solver of ['greedy', 'beam_search', 'branch_and_bound']) {
    await expect(table.getByTestId(`earnings-${solver}`)).toHaveText(/\d+/, { timeout: 25_000 });
  }
  await expect(table.getByTestId('earnings-branch_and_bound')).toHaveText('45');
  await expect(table).toContainText('yes'); // B&B proved optimality
  expect(Date.now() - started).toBeLessThan(30_000);

  // The results view shows the best schedule on the Gantt chart.
  await expect(page.getByTestId('earnings')).toHaveText('45');
  await expect(page.locator('app-gantt rect.bar').first()).toBeVisible();
  // Switch to another solver's result.
  await page.getByTestId('result-select').selectOption('greedy');
  await expect(page.getByTestId('earnings')).not.toHaveText('45');
  expect(problems).toEqual([]);
});

test('invalid edits are explained and block running', async ({ page }) => {
  await page.goto('/auto');
  await page.getByTestId('workers').fill('0');
  await expect(page.getByTestId('instance-errors')).toContainText('workers: must be in [1, 10000]');
  await expect(page.getByTestId('run')).toBeDisabled();
  await page.getByTestId('workers').fill('2');
  await expect(page.getByTestId('instance-valid')).toBeVisible();
  await expect(page.getByTestId('run')).toBeEnabled();
});
