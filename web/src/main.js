/*
 * This file Copyright (C) 2020 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 */

import { ActionManager } from './action-manager.js';
import { Notifications } from './notifications.js';
import { Prefs } from './prefs.js';
import { Transmission } from './transmission.js';
import { debounce } from './utils.js';

import '../style/transmission-app.scss';

function main() {
  const scroll_soon = debounce(() => window.scrollTo(0, 1));
  window.onload = scroll_soon;
  window.onorientationchange = scroll_soon;

  const action_manager = new ActionManager();
  const prefs = new Prefs();
  const notifications = new Notifications(prefs);
  // eslint-disable-next-line no-unused-vars
  const transmission = new Transmission(action_manager, notifications, prefs);
}

document.addEventListener('DOMContentLoaded', main);
