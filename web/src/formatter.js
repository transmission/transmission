/**
 * Copyright © Mnemosyne LLC
 *
 * This file is licensed under the GPLv2.
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 */

const speed_K = 1000;
const speed_K_str = 'kB/s';
const speed_M_str = 'MB/s';
const speed_G_str = 'GB/s';

const size_K = 1000;
const size_B_str = 'B';
const size_K_str = 'kB';
const size_M_str = 'MB';
const size_G_str = 'GB';
const size_T_str = 'TB';

const mem_K = 1024;
const mem_B_str = 'B';
const mem_K_str = 'KiB';
const mem_M_str = 'MiB';
const mem_G_str = 'GiB';
const mem_T_str = 'TiB';

export class Formatter {
  static countString(msgid, msgid_plural, n) {
    return `${Formatter.toStringWithCommas(n)} ${Formatter.ngettext(msgid, msgid_plural, n)}`;
  }

  // Formats the a memory size into a human-readable string
  // @param {Number} bytes the filesize in bytes
  // @return {String} human-readable string
  static mem(bytes) {
    const toStr = (size, units) =>
      `${Formatter._toTruncFixed(size, size <= 9.995 ? 2 : 1)} ${units}`;

    if (bytes < mem_K) {
      return toStr(bytes, mem_B_str);
    }
    if (bytes < Math.pow(mem_K, 2)) {
      return toStr(bytes / mem_K, mem_K_str);
    }
    if (bytes < Math.pow(mem_K, 3)) {
      return toStr(bytes / Math.pow(mem_K, 2), mem_M_str);
    }
    if (bytes < Math.pow(mem_K, 4)) {
      return toStr(bytes / Math.pow(mem_K, 3), mem_G_str);
    }
    return toStr(bytes / Math.pow(mem_K, 4), mem_T_str);
  }

  static ngettext(msgid, msgid_plural, n) {
    // TODO(i18n): http://doc.qt.digia.com/4.6/i18n-plural-rules.html
    return n === 1 ? msgid : msgid_plural;
  }

  // format a percentage to a string
  static percentString(x) {
    if (x < 10.0) {
      return Formatter._toTruncFixed(x, 2);
    }
    if (x < 100.0) {
      return Formatter._toTruncFixed(x, 1);
    }
    return Formatter._toTruncFixed(x, 0);
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
    return Formatter.percentString(x);
  }

  /**
   * Formats the a disk capacity or file size into a human-readable string
   * @param {Number} bytes the filesize in bytes
   * @return {String} human-readable string
   */
  static size(bytes) {
    const toStr = (size, units) =>
      `${Formatter._toTruncFixed(size, size <= 9.995 ? 2 : 1)} ${units}`;

    if (bytes < size_K) {
      return toStr(bytes, size_B_str);
    }
    if (bytes < Math.pow(size_K, 2)) {
      return toStr(bytes / size_K, size_K_str);
    }
    if (bytes < Math.pow(size_K, 3)) {
      return toStr(bytes / Math.pow(size_K, 2), size_M_str);
    }
    if (bytes < Math.pow(size_K, 4)) {
      return toStr(bytes / Math.pow(size_K, 3), size_G_str);
    }
    return toStr(bytes / Math.pow(size_K, 4), size_T_str);
  }

  static speed(KBps) {
    let speed = KBps;

    if (speed <= 999.95) {
      // 0 KBps to 999 K
      return [Formatter._toTruncFixed(speed, 0), speed_K_str].join(' ');
    }

    speed /= speed_K;

    if (speed <= 99.995) {
      // 1 M to 99.99 M
      return [Formatter._toTruncFixed(speed, 2), speed_M_str].join(' ');
    }
    if (speed <= 999.95) {
      // 100 M to 999.9 M
      return [Formatter._toTruncFixed(speed, 1), speed_M_str].join(' ');
    }

    // insane speeds
    speed /= speed_K;
    return [Formatter._toTruncFixed(speed, 2), speed_G_str].join(' ');
  }

  static speedBps(Bps) {
    return Formatter.speed(Formatter.toKBps(Bps));
  }

  static timeInterval(seconds) {
    const days = Math.floor(seconds / 86400);
    const hours = Math.floor((seconds % 86400) / 3600);
    const minutes = Math.floor((seconds % 3600) / 60);
    seconds = Math.floor(seconds % 60);
    const d = `${days} ${days > 1 ? 'days' : 'day'}`;
    const h = `${hours} ${hours > 1 ? 'hours' : 'hour'}`;
    const m = `${minutes} ${minutes > 1 ? 'minutes' : 'minute'}`;
    const s = `${seconds} ${seconds > 1 ? 'seconds' : 'second'}`;

    if (days) {
      if (days >= 4 || !hours) {
        return d;
      }
      return `${d}, ${h}`;
    }
    if (hours) {
      if (hours >= 4 || !minutes) {
        return h;
      }
      return `${h}, ${m}`;
    }
    if (minutes) {
      if (minutes >= 4 || !seconds) {
        return m;
      }
      return `${m}, ${s}`;
    }
    return s;
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
    return Math.floor(Bps / speed_K);
  }

  static toStringWithCommas = (number) => number.toString().replace(/\B(?=(?:\d{3})+(?!\d))/g, ',');

  /** Round a string of a number to a specified number of decimal places */
  static _toTruncFixed(number, places) {
    const ret = Math.floor(this * Math.pow(10, places)) / Math.pow(10, places);
    return ret.toFixed(places);
  }
}
