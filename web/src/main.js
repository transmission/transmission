/* @license This file Copyright © Mnemosyne LLC.
   It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
   or any future license endorsed by Mnemosyne LLC.
   License text can be found in the licenses/ folder. */

import { AuthManager } from './auth-manager.js';
import { ActionManager } from './action-manager.js';
import { Notifications } from './notifications.js';
import { Prefs } from './prefs.js';
import { Transmission } from './transmission.js';
import { debounce } from './utils.js';

import '../assets/css/transmission-app.scss';

async function main() {
  // The user might load up the web app without being logged in. All RPC calls
  // will fail.
  const credentials = AuthManager.loadCredentials();
  if (!(await AuthManager.testCredentials(credentials))) {
    AuthManager.redirectToLogin();
    // Even though we've just initiated a page navigation, JS on this page will
    // continue executing until until the end of the current synchronous code
    // block. As a result, we'll get a wave of unauthenticated RPC calls, which
    // can artificially inflate the brute force detector's measurements. So we
    // explicitly early return to prevent that from happening.
    return;
  }

  const action_manager = new ActionManager();
  const prefs = new Prefs();
  const notifications = new Notifications(prefs);
  const transmission = new Transmission(action_manager, notifications, prefs);

  const scroll_soon = debounce(() =>
    transmission.elements.torrent_list.scrollTo(0, 1),
  );
  window.addEventListener('load', scroll_soon);
  window.addEventListener('orientationchange', scroll_soon);
}

document.addEventListener('DOMContentLoaded', main);
