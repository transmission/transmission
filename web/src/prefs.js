/**
 * @license
 *
 * This file Copyright (C) 2020 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 */

import { debounce } from './utils.js';

export class Prefs extends EventTarget {
  constructor() {
    super();

    this._cache = {};

    this.dispatchPrefsChange = debounce((key, old_value, value) => {
      const event = new Event('change');
      Object.assign(event, { key, old_value, value });
      this.dispatchEvent(event);
    });

    for (const [key, default_value] of Object.entries(Prefs._Defaults)) {
      // populate the cache...
      this._set(key, Prefs._getCookie(key, default_value));

      // add property getter/setters...
      Object.defineProperty(this, key.replaceAll('-', '_'), {
        get: () => this._get(key),
        set: (value) => {
          this._set(key, value);
        },
      });
    }

    Object.seal(this);
  }

  entries() {
    return Object.entries(this._cache);
  }

  keys() {
    return Object.keys(this._cache);
  }

  _get(key) {
    const { _cache } = this;
    if (!Object.prototype.hasOwnProperty.call(_cache, key)) {
      throw new Error(key);
    }
    return _cache[key];
  }

  _set(key, value) {
    const { _cache } = this;
    const old_value = _cache[key];
    if (old_value !== value) {
      _cache[key] = value;
      Prefs._setCookie(key, value);
      this.dispatchPrefsChange(key, old_value, value);
    }
  }

  static _setCookie(key, value) {
    const date = new Date();
    date.setFullYear(date.getFullYear() + 1);
    document.cookie = `${key}=${value}; SameSite=Strict; expires=${date.toGMTString()}; path=/`;
  }

  static _getCookie(key, fallback) {
    const value = Prefs._readCookie(key);
    if (value === null) {
      return fallback;
    }
    if (value === 'true') {
      return true;
    }
    if (value === 'false') {
      return false;
    }
    if (value.match(/^\d+$/)) {
      return Number.parseInt(value, 10);
    }
    return value;
  }

  static _readCookie(key) {
    const value = `; ${document.cookie}`;
    const parts = value.split(`; ${key}=`);
    return parts.length === 2 ? parts.pop().split(';').shift() : null;
  }
}

Prefs.AltSpeedEnabled = 'alt-speed-enabled';
Prefs.DisplayCompact = 'compact';
Prefs.DisplayFull = 'full';
Prefs.DisplayMode = 'display-mode';
Prefs.FilterActive = 'active';
Prefs.FilterAll = 'all';
Prefs.FilterDownloading = 'downloading';
Prefs.FilterFinished = 'finished';
Prefs.FilterMode = 'filter-mode';
Prefs.FilterPaused = 'paused';
Prefs.FilterSeeding = 'seeding';
Prefs.NotificationsEnabled = 'notifications-enabled';
Prefs.RefreshRate = 'refresh-rate-sec';
Prefs.SortAscending = 'ascending';
Prefs.SortByActivity = 'activity';
Prefs.SortByAge = 'age';
Prefs.SortByName = 'name';
Prefs.SortByProgress = 'progress';
Prefs.SortByQueue = 'queue';
Prefs.SortByRatio = 'ratio';
Prefs.SortBySize = 'size';
Prefs.SortByState = 'state';
Prefs.SortDescending = 'descending';
Prefs.SortDirection = 'sort-direction';
Prefs.SortMode = 'sort-mode';

Prefs._Defaults = {
  [Prefs.AltSpeedEnabled]: false,
  [Prefs.DisplayMode]: Prefs.DisplayFull,
  [Prefs.FilterMode]: Prefs.FilterAll,
  [Prefs.NotificationsEnabled]: false,
  [Prefs.RefreshRate]: 5,
  [Prefs.SortDirection]: Prefs.SortAscending,
  [Prefs.SortMode]: Prefs.SortByName,
};
