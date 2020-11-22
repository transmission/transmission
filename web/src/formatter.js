/**
 * @license
 *
 * This file Copyright (C) 2020 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 */

const plural_rules = new Intl.PluralRules();
const current_locale = plural_rules.resolvedOptions().locale;
const number_format = new Intl.NumberFormat(current_locale);

const kilo = 1000;
const mem_formatters = [
  new Intl.NumberFormat(current_locale, { style: 'unit', unit: 'byte' }),
  new Intl.NumberFormat(current_locale, { style: 'unit', unit: 'kilobyte' }),
  new Intl.NumberFormat(current_locale, { style: 'unit', unit: 'megabyte' }),
  new Intl.NumberFormat(current_locale, { style: 'unit', unit: 'gigabyte' }),
  new Intl.NumberFormat(current_locale, { style: 'unit', unit: 'terabyte' }),
  new Intl.NumberFormat(current_locale, { style: 'unit', unit: 'petabyte' }),
];

const fmt_kBps = new Intl.NumberFormat(current_locale, {
  style: 'unit',
  unit: 'kilobyte-per-second',
});
const fmt_MBps = new Intl.NumberFormat(current_locale, {
  style: 'unit',
  unit: 'megabyte-per-second',
});

export class Formatter {
  static countString(msgid, msgid_plural, n) {
    return `${this.number(n)} ${this.ngettext(msgid, msgid_plural, n)}`;
  }

  // Formats the a memory size into a human-readable string
  // @param {Number} bytes the filesize in bytes
  // @return {String} human-readable string
  static mem(bytes) {
    if (bytes < 0) {
      return 'Unknown';
    }
    if (bytes === 0) {
      return 'None';
    }

    let size = bytes;
    for (const nf of mem_formatters) {
      if (size < kilo) {
        return nf.format(Math.floor(size));
      }
      size /= kilo;
    }

    return 'E2BIG';
  }

  static ngettext(msgid, msgid_plural, n) {
    return plural_rules.select(n) === 'one' ? msgid : msgid_plural;
  }

  // format a percentage to a string
  static percentString(x) {
    const decimal_places = x < 100 ? 1 : 0;
    return this._toTruncFixed(x, decimal_places);
  }

  /*
   *   Format a ratio to a string
   */
  static ratioString(x) {
    if (x === -1) {
      return 'None';
    }
    if (x === -2) {
      return '&infin;';
    }
    return this.percentString(x);
  }

  /**
   * Formats the a disk capacity or file size into a human-readable string
   * @param {Number} bytes the filesize in bytes
   * @return {String} human-readable string
   */
  static size(bytes) {
    return this.mem(bytes);
  }

  static speed(KBps) {
    return KBps < 999.95 ? fmt_kBps.format(KBps) : fmt_MBps.format(KBps / 1000);
  }

  static speedBps(Bps) {
    return this.speed(this.toKBps(Bps));
  }

  static timeInterval(seconds) {
    const days = Math.floor(seconds / 86400);
    if (days) {
      return this.countString('day', 'days', days);
    }

    const hours = Math.floor((seconds % 86400) / 3600);
    if (hours) {
      return this.countString('hour', 'hours', hours);
    }

    const minutes = Math.floor((seconds % 3600) / 60);
    if (minutes) {
      return this.countString('minute', 'minutes', minutes);
    }

    seconds = Math.floor(seconds % 60);
    return this.countString('second', 'seconds', seconds);
  }

  static timestamp(seconds) {
    if (!seconds) {
      return 'N/A';
    }

    const myDate = new Date(seconds * 1000);
    const now = new Date();

    let date = '';
    let time = '';

    const sameYear = now.getFullYear() === myDate.getFullYear();
    const sameMonth = now.getMonth() === myDate.getMonth();

    const dateDiff = now.getDate() - myDate.getDate();
    if (sameYear && sameMonth && Math.abs(dateDiff) <= 1) {
      if (dateDiff === 0) {
        date = 'Today';
      } else if (dateDiff === 1) {
        date = 'Yesterday';
      } else {
        date = 'Tomorrow';
      }
    } else {
      date = myDate.toDateString();
    }

    let hours = myDate.getHours();
    let period = 'AM';
    if (hours > 12) {
      hours = hours - 12;
      period = 'PM';
    }
    if (hours === 0) {
      hours = 12;
    }
    if (hours < 10) {
      hours = `0${hours}`;
    }
    let minutes = myDate.getMinutes();
    if (minutes < 10) {
      minutes = `0${minutes}`;
    }
    seconds = myDate.getSeconds();
    if (seconds < 10) {
      seconds = `0${seconds}`;
    }

    time = [hours, minutes, seconds].join(':');

    return [date, time, period].join(' ');
  }

  static toKBps(Bps) {
    return Math.floor(Bps / kilo);
  }

  static number(number) {
    return number_format.format(number);
  }

  /** Round a string of a number to a specified number of decimal places */
  static _toTruncFixed(number, places) {
    const returnValue = Math.floor(number * 10 ** places) / 10 ** places;
    return returnValue.toFixed(places);
  }
}
