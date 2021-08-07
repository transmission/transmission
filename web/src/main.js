/**
 * @license
 *
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
  const action_manager = new ActionManager();
  const prefs = new Prefs();
  const notifications = new Notifications(prefs);
  const transmission = new Transmission(action_manager, notifications, prefs);

  const scroll_soon = debounce(() =>
    transmission.elements.torrent_list.scrollTo(0, 1)
  );
  window.addEventListener('load', scroll_soon);
  window.onorientationchange = scroll_soon;
}

document.addEventListener('DOMContentLoaded', main);
