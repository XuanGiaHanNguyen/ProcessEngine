import { test as base, expect } from '@playwright/test';

/** Fails the test on any console error or warning (but the GPU driver's), page error, or 409
 * (illegal action). */
export const test = base.extend<{ problems: string[] }>({
  problems: async ({ page }, use) => {
    const problems: string[] = [];
    page.on('console', (m) => {
      // Chrome's software WebGL in headless runs reports its own performance notes.
      if (m.text().includes('GL Driver Message')) return;
      if (m.type() === 'error' || m.type() === 'warning')
        problems.push(`console ${m.type()}: ${m.text()}`);
    });
    page.on('pageerror', (e) => problems.push(`page error: ${e.message}`));
    page.on('response', (r) => {
      if (r.status() === 409) problems.push(`illegal action reached the server: ${r.url()}`);
    });
    await use(problems);
    expect(problems).toEqual([]);
  },
});

export { expect };
