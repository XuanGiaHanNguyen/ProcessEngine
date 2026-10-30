import { defineConfig, devices } from '@playwright/test';

// Starts the real C++ server (build it first: cmake --build ../build --target pf_server) and
// the Angular dev server proxying to it, on ports that do not clash with a local dev setup.
const API_PORT = 18080;
const WEB_PORT = 4300;
const serverBinary = process.env.PF_SERVER_BIN ?? '../build/server/pf_server';

export default defineConfig({
  testDir: './e2e',
  timeout: 60_000,
  fullyParallel: false,
  retries: process.env.CI ? 1 : 0,
  reporter: process.env.CI ? [['list'], ['html', { open: 'never' }]] : 'list',
  use: {
    baseURL: `http://localhost:${WEB_PORT}`,
    trace: 'retain-on-failure',
  },
  projects: [
    {
      name: 'chromium',
      use: { ...devices['Desktop Chrome'], viewport: { width: 1440, height: 1000 } },
    },
  ],
  webServer: [
    {
      command: `${serverBinary} --port ${API_PORT} --instances ../instances --log-level warn`,
      url: `http://localhost:${API_PORT}/api/health`,
      reuseExistingServer: !process.env.CI,
    },
    {
      command: `npx ng serve --port ${WEB_PORT}`,
      url: `http://localhost:${WEB_PORT}`,
      env: { PF_API: `http://localhost:${API_PORT}` },
      reuseExistingServer: !process.env.CI,
      timeout: 120_000,
    },
  ],
});
