/**
 * Copyright © Mnemosyne LLC
 *
 * This file is licensed under the GPLv2.
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 */

import { Utils } from './utils.js';

export class Notifications {
  constructor(prefs) {
    this._prefs = prefs;
    this._elements = {
      toggle: document.getElementById('toggle-notifications'),
    };
  }

  _setEnabled(enabled) {
    this.prefs.notifications_enabled = enabled;
    Utils.setTextContent(this._toggle, `${enabled ? 'Disable' : 'Enable'} Notifications`);
  }

  _requestPermission() {
    Notification.requestPermission().then((s) => this._setEnabled(s === 'granted'));
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
  // FIXME:
  // $(transmission).bind('downloadComplete seedingComplete', (event, torrent) => {
  //  if (notificationsEnabled) {
      const title = `${event.type === 'downloadComplete' ? 'Download' : 'Seeding'} complete`;
      const content = torrent.getName();
      const notification = window.webkitNotifications.createNotification(
        'style/transmission/images/logo.png',
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
