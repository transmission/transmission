// @ts-check
/// <reference types="node" />
/// <reference lib="es2024" />

import { test, expect } from '@playwright/test';

import {launchTransmissionDaemon} from './utils.js';

test.describe('Authentication', () => {
  const credentials = { password: 'pass', username: 'user' };

  /**
   * Given a page that's currently on the login form, fills in the form and
   * submits it
   *
   * @param {import('@playwright/test').Page} page
   * @param {Partial<{username: string; password: string}>?}
   * credentialsOverrides Optional values to override the default credentials
   */
  const fillLoginForm = async (page, credentialsOverrides = {}) => {
    const overridenCredentials = { ...credentials, ...credentialsOverrides };
    console.log({ overridenCredentials });
    await page.getByTestId('username').fill(overridenCredentials.username);
    await page.getByTestId('password').fill(overridenCredentials.password);
    await page.getByTestId('log in').click();
  };

  {
    let cleanup = null;

    // eslint-disable-next-line no-empty-pattern
    test.beforeEach(async ({}, testInfo) => {
      if (testInfo.tags.includes('@skip-server')) {
        return;
      }

      cleanup = await launchTransmissionDaemon(credentials);
    });

    test.afterEach(async () => {
      if (cleanup) {
        await cleanup();
      }
    });
  }

  test('Redirects to login page when not authenticated', async ({ page }) => {
    await page.goto('/');

    await expect(page).toHaveURL(/\/transmission\/web\/login\/index.html$/);
  });

  test('Login page shows error message when credentials are incorrect', async ({
    page,
  }) => {
    await page.goto('/');

    await expect(page).toHaveURL(/\/transmission\/web\/login\/index.html$/);
  });

  test('Login form successfully authenticates', async ({ page }) => {
    await page.goto('/transmission/web/login/index.html');

    await fillLoginForm(page);

    await expect(page).toHaveURL(/\/transmission\/web\/$/);
  });

  test('Login form shows an error message on failed login', async ({
    page,
  }) => {
    await page.goto('/transmission/web/login/index.html');

    await expect(page.getByTestId('login-error')).not.toBeVisible();

    await fillLoginForm(page, { password: 'hunter2' });

    await expect(page.getByTestId('login-error')).toHaveText(
      'The username or password you entered is incorrect.',
    );

    // Now try the correct credentials, to confirm the form doesn't lock up
    await fillLoginForm(page);

    await expect(page).toHaveURL(/\/transmission\/web\/$/);
  });

  test(
    'Login page redirects to the web UI if the server is unauthenticated',
    { tag: '@skip-server' },
    async ({ page }) => {
      const cleanup = await launchTransmissionDaemon(null);
      try {
        await page.goto('/transmission/web/login/index.html');
        await expect(page).toHaveURL(/\/transmission\/web\/$/);
      } finally {
        await cleanup();
      }
    },
  );

  test(
    'Web UI redirects to the login page if the server credentials are reconfigured',
    { tag: '@skip-server' },
    async ({ page }) => {
      let cleanup = await launchTransmissionDaemon(credentials);

      try {
        await page.goto('/transmission/web/login/index.html');

        await fillLoginForm(page);

        await expect(page).toHaveURL(/\/transmission\/web\/$/);

        await cleanup();

        cleanup = await launchTransmissionDaemon({
          ...credentials,
          password: 'hunter2',
        });
        await expect(page).toHaveURL(/\/transmission\/web\/login\/index.html$/);
      } finally {
        await cleanup();
      }
    },
  );

  test('Logout button works', async ({ page }) => {
    await page.goto('/transmission/web/login/index.html');

    await fillLoginForm(page);

    await expect(page).toHaveURL(/\/transmission\/web\/$/);

    await page.getByTestId('overflow-menu').click();
    await page.getByTestId('logout').click();
    await expect(page).toHaveURL(/\/transmission\/web\/login\/index.html$/);

    // Make sure we didn't just get sent to the login page without actually
    // clearing the session
    await page.goto('/transmission/web/');
    await expect(page).toHaveURL(/\/transmission\/web\/login\/index.html$/);
  });
});
