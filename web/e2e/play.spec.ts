import { expect, test } from './fixtures';

test('full manual playthrough with undo, finish, and solver reveal', async ({ page, problems }) => {
  await page.goto('/play');
  await page.getByTestId('play-sample-iron_chain').click();
  const tick = page.getByTestId('tick');
  await expect(tick).toHaveText(/^0/);

  // Start two smelts at tick 0.
  await page.getByTestId('start-smelt').click();
  await expect(page.getByTestId('inv-ore')).toHaveText('4');
  await page.getByTestId('start-smelt').click();
  await expect(page.getByTestId('inv-ore')).toHaveText('2');

  // Both workers are busy: Start is disabled and says why.
  await expect(page.getByTestId('start-forge')).toBeDisabled();
  await expect(page.locator('#why-forge')).toHaveText(/no free worker/);

  // Undo the second smelt, then redo it with the keyboard.
  await page.getByTestId('undo').click();
  await expect(page.getByTestId('inv-ore')).toHaveText('4');
  await page.keyboard.press('y');
  await expect(page.getByTestId('inv-ore')).toHaveText('2');

  // Skip to the next event (tick 2), forge + one more smelt: the optimum.
  await page.getByTestId('skip').click();
  await expect(tick).toHaveText(/^2/);
  await page.getByTestId('start-forge').click();
  await expect(page.getByTestId('inv-iron')).toHaveText('0');
  await page.getByTestId('start-smelt').click();
  await expect(page.getByTestId('projected')).toHaveText('13');

  await page.getByTestId('finish').click();
  await expect(page.getByTestId('final-score')).toHaveText('13');
  await expect(page.getByTestId('result')).toContainText('Validated by the engine: yes');

  // Reveal: compare against the solvers.
  await page.getByTestId('reveal').click();
  await expect(page.getByTestId('player-pct')).toContainText('100.0%', { timeout: 20_000 });
  const board = page.getByTestId('leaderboard');
  await expect(board).toContainText('You');
  await expect(board).toContainText('branch_and_bound');
  expect(problems).toEqual([]);
});

test('assist mode hint and replaying a solver schedule', async ({ page }) => {
  await page.goto('/play');
  await page.getByTestId('play-sample-pottery').click();
  await page.getByLabel('Assist').check();
  await page.getByTestId('hint').click();
  await expect(page.getByTestId('hint-text')).toContainText('earns 27');

  // Space waits one tick.
  await page.keyboard.press(' ');
  await expect(page.getByTestId('tick')).toHaveText(/^1/);
  await page.getByTestId('finish').click();
  await expect(page.getByTestId('final-score')).toHaveText('15');

  await page.getByTestId('reveal').click();
  await expect(page.getByTestId('leaderboard')).toContainText('branch_and_bound', {
    timeout: 20_000,
  });
  await page
    .getByTestId('leaderboard')
    .getByRole('listitem')
    .filter({ hasText: 'branch_and_bound' })
    .getByRole('button', { name: 'Replay' })
    .click();
  await expect(page.getByText(/Replaying/)).toBeVisible();
  // Step once by hand, then let autoplay run the rest.
  await page.getByRole('button', { name: 'Step' }).click();
  await page.getByRole('button', { name: 'Play', exact: true }).click();
  await expect(page.getByText(/\s0 steps left/)).toBeVisible({ timeout: 20_000 });
  await page.getByTestId('finish').click();
  await expect(page.getByTestId('final-score')).toHaveText('27');
});

test('share links restore a game', async ({ page }) => {
  await page.goto('/play?sample=sawmill&actions=s%3Asaw,a%3A2,s%3Asaw');
  await expect(page.getByTestId('tick')).toHaveText(/^2/);
  await expect(page.getByTestId('projected')).toHaveText('8');
});
