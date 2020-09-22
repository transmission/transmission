'use strict';

/**
 * Copyright © Dave Perrett, Malcolm Jarvis and Artem Vorotnikov
 *
 * This file is licensed under the GPLv2.
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 */

const isMobileDevice = /(iPhone|iPod|Android)/.test(navigator.userAgent);

// http://forum.jquery.com/topic/combining-ui-dialog-and-tabs
$.fn.tabbedDialog = function (dialog_opts) {
  this.tabs({
    selected: 0,
  });
  this.dialog(dialog_opts);
  this.find('.ui-tab-dialog-close').append(this.parent().find('.ui-dialog-titlebar-close'));
  this.find('.ui-tab-dialog-close').css({
    position: 'absolute',
    right: '0',
    top: '16px',
  });
  this.find('.ui-tab-dialog-close > a').css({
    float: 'none',
    padding: '0',
  });
  const tabul = this.find('ul:first');
  this.parent().addClass('ui-tabs').prepend(tabul).draggable('option', 'handle', tabul);
  this.siblings('.ui-dialog-titlebar').remove();
  tabul.addClass('ui-dialog-titlebar');
};

class Utils {
  static isIterable(o) {
    return o && typeof o[Symbol.iterator] === 'function';
  }

  static isHidden(el) {
    return el.offsetParent === null;
  }
  static isHiddenId(id) {
    return Utils.isHidden(document.getElementById(id));
  }

  static hide(el) {
    el.style.display = 'none';
  }
  static hideId(id) {
    return Utils.hide(document.getElementById(id));
  }

  static show(el) {
    el.style.display = 'none';
  }
  static showId(id) {
    return Utils.show(document.getElementById(id));
  }

  static setVisible(el, visible) {
    if (visible) {
      Utils.show(el);
    } else {
      Utils.hide(el);
    }
  }
  static setVisibleId(id) {
    return Utils.setVisible(document.getElementById(id));
  }
}

/**
 * Checks to see if the content actually changed before poking the DOM.
 */
function setInnerHTML(e, html) {
  if (!e) {
    return;
  }

  /* innerHTML is listed as a string, but the browser seems to change it.
   * For example, "&infin;" gets changed to "∞" somewhere down the line.
   * So, let's use an arbitrary  different field to test our state... */
  if (e.currentHTML !== html) {
    e.currentHTML = html;
    e.innerHTML = html;
  }
}

function sanitizeText(text) {
  return text.replace(/</g, '&lt;').replace(/>/g, '&gt;');
}

/**
 * Many of our text changes are triggered by periodic refreshes
 * on torrents whose state hasn't changed since the last update,
 * so see if the text actually changed before poking the DOM.
 */
function setTextContent(e, text) {
  if (e && e.textContent !== text) {
    e.textContent = text;
  }
}

/*
 *   Given a numerator and denominator, return a ratio string
 */
Math.ratio = function (numerator, denominator) {
  let result = Math.floor((100 * numerator) / denominator) / 100;

  // check for special cases
  if (result === Number.POSITIVE_INFINITY || result === Number.NEGATIVE_INFINITY) {
    result = -2;
  } else if (isNaN(result)) {
    result = -1;
  }

  return result;
};

/**
 * Round a string of a number to a specified number of decimal places
 */
Number.prototype.toTruncFixed = function (place) {
  const ret = Math.floor(this * Math.pow(10, place)) / Math.pow(10, place);
  return ret.toFixed(place);
};

Number.prototype.toStringWithCommas = function () {
  return this.toString().replace(/\B(?=(?:\d{3})+(?!\d))/g, ',');
};

/// PREFERENCES

class Prefs {
  // set a preference option
  static setValue(key, val) {
    Prefs._warnIfUnknownKey(key);

    const date = new Date();
    date.setFullYear(date.getFullYear() + 1);
    document.cookie = `${key}=${val}; expires=${date.toGMTString()}; path=/`;
  }

  static _warnIfUnknownKey(key) {
    if (!Object.prototype.hasOwnProperty.call(Prefs._Defaults, key)) {
      console.warn("unrecognized preference key '%s'", key);
    }
  }

  static _getCookie(name) {
    const value = `; ${document.cookie}`;
    const parts = value.split(`; ${name}=`);
    return parts.length === 2 ? parts.pop().split(';').shift() : null;
  }

  /**
   * Get a preference option
   *
   * @param key the preference's key
   * @param fallback if the option isn't set, return this instead
   */
  static getValue(key, fallback) {
    Prefs._warnIfUnknownKey(key);

    const val = Prefs._getCookie(key);
    if (!val) {
      return fallback;
    }
    if (val === 'true') {
      return true;
    }
    if (val === 'false') {
      return false;
    }
    return val;
  }

  /**
   * Get an object with all the Clutch preferences set
   *
   * @pararm o object to be populated (optional)
   */
  static getClutchPrefs(o = {}) {
    for (const [key, val] of Object.entries(Prefs._Defaults)) {
      o[key] = Prefs.getValue(key, val);
    }
    return o;
  }
}

Prefs._RefreshRate = 'refresh_rate';

Prefs._FilterMode = 'filter';
Prefs._FilterAll = 'all';
Prefs._FilterActive = 'active';
Prefs._FilterSeeding = 'seeding';
Prefs._FilterDownloading = 'downloading';
Prefs._FilterPaused = 'paused';
Prefs._FilterFinished = 'finished';

Prefs._SortDirection = 'sort_direction';
Prefs._SortAscending = 'ascending';
Prefs._SortDescending = 'descending';

Prefs._SortMethod = 'sort_method';
Prefs._SortByAge = 'age';
Prefs._SortByActivity = 'activity';
Prefs._SortByName = 'name';
Prefs._SortByQueue = 'queue-order';
Prefs._SortBySize = 'size';
Prefs._SortByProgress = 'percent-completed';
Prefs._SortByRatio = 'ratio';
Prefs._SortByState = 'state';

Prefs._CompactDisplayState = 'compact_display_state';

Prefs._Defaults = {
  compact_display_state: false,
  filter: 'all',
  refresh_rate: 5,
  sort_direction: 'ascending',
  sort_method: 'name',
  'turtle-state': false,
};
