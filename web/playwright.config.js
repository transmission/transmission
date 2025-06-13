/* eslint-disable sort-keys */
// @ts-check
import { defineConfig, devices } from '@playwright/test';

/**
 * @see https://playwright.dev/docs/test-configuration
 */
export default defineConfig({
  testDir: './tests',
  // The tests launch Transmission on its default port, which means we can only
  // have one test running at a time. To go parallel we'd need to select a
  // random port and communicated that to each test's baseUrl. Probably also
  // configure each daemon instance with its own data directories.
  workers: 1,
  use: {
    baseURL: 'http://127.0.0.1:9091',
    trace: 'on',

    // Makes it easier to understand why a test is failing
    headless: false,
  },

  projects: [
    {
      name: 'chromium',
      use: { ...devices['Desktop Chrome'] },
    },
  ],
  
    // Playwright writes test results to disk and you can't stop it. So at least
    // put them out of the way.
    preserveOutput: "never",
    outputDir: "/tmp/playwright-garbage-I-cant-suppress"
});
