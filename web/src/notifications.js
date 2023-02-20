/* @license This file Copyright © 2020-2023 Mnemosyne LLC.
   It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
   or any future license endorsed by Mnemosyne LLC.
   License text can be found in the licenses/ folder. */

import { setTextContent } from './utils.js';

export class Notifications {
  constructor(prefs) {
    this._prefs = prefs;
    this._elements = {
      toggle: document.querySelector('#toggle-notifications'),
    };
  }

  _setEnabled(enabled) {
    this.prefs.notifications_enabled = enabled;
    setTextContent(
      this._toggle,
      `${enabled ? 'Disable' : 'Enable'} Notifications`
    );
  }

  _requestPermission() {
    Notification.requestPermission().then((s) =>
      this._setEnabled(s === 'granted')
    );
  }

  toggle() {
    if (this._enabled) {
      this._setEnabled(false);
    } else if (Notification.permission === 'granted') {
      this._setEnabled(true);
    } else if (Notification.permission !== 'denied') {
      this._requestPermission();
    }
  }

  /*
  // TODO:
  // $(transmission).bind('downloadComplete seedingComplete', (event, torrent) => {
  //  if (notificationsEnabled) {
      const title = `${event.type === 'downloadComplete' ? 'Download' : 'Seeding'} complete`;
      const content = torrent.getName();
      const notification = window.webkitNotifications.createNotification(
        'assets/img/logo.png',
        title,
        content
      );
      notification.show();
      setTimeout(() => {
        notification.cancel();
      }, 5000);
    }
  });
*/
}
