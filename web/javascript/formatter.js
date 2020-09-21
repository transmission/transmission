'use strict';

/**
 * Copyright © Mnemosyne LLC
 *
 * This file is licensed under the GPLv2.
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 */

Transmission.fmt = (function () {
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

  return {
    countString(msgid, msgid_plural, n) {
      return [n.toStringWithCommas(), this.ngettext(msgid, msgid_plural, n)].join(' ');
    },

    /**
     * Formats the a memory size into a human-readable string
     * @param {Number} bytes the filesize in bytes
     * @return {String} human-readable string
     */
    mem(bytes) {
      const toStr = (size, units) => `${size.toTruncFixed(size <= 9.995 ? 2 : 1)} ${units}`;

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
      return bytes / Math.pow(mem_K, 4), mem_T_str;
    },

    ngettext(msgid, msgid_plural, n) {
      // TODO(i18n): http://doc.qt.digia.com/4.6/i18n-plural-rules.html
      return n === 1 ? msgid : msgid_plural;
    },

    peerStatus(flagStr) {
      const formattedFlags = [];
      for (const flag of flagStr) {
        let explanation = null;
        switch (flag) {
          case 'O':
            explanation = 'Optimistic unchoke';
            break;
          case 'D':
            explanation = 'Downloading from this peer';
            break;
          case 'd':
            explanation = "We would download from this peer if they'd let us";
            break;
          case 'U':
            explanation = 'Uploading to peer';
            break;
          case 'u':
            explanation = "We would upload to this peer if they'd ask";
            break;
          case 'K':
            explanation = "Peer has unchoked us, but we're not interested";
            break;
          case '?':
            explanation = "We unchoked this peer, but they're not interested";
            break;
          case 'E':
            explanation = 'Encrypted Connection';
            break;
          case 'H':
            explanation = 'Peer was discovered through Distributed Hash Table (DHT)';
            break;
          case 'X':
            explanation = 'Peer was discovered through Peer Exchange (PEX)';
            break;
          case 'I':
            explanation = 'Peer is an incoming connection';
            break;
          case 'T':
            explanation = 'Peer is connected via uTP';
            break;
        }

        if (!explanation) {
          formattedFlags.push(flag);
        } else {
          formattedFlags.push(`<span title="${flag}: ${explanation}">${flag}</span>`);
        }
      }

      return formattedFlags.join('');
    },

    /*
     *   Format a percentage to a string
     */
    percentString(x) {
      if (x < 10.0) {
        return x.toTruncFixed(2);
      } else if (x < 100.0) {
        return x.toTruncFixed(1);
      } else {
        return x.toTruncFixed(0);
      }
    },

    /*
     *   Format a ratio to a string
     */
    ratioString(x) {
      if (x === -1) {
        return 'None';
      }
      if (x === -2) {
        return '&infin;';
      }
      return this.percentString(x);
    },

    /**
     * Formats the a disk capacity or file size into a human-readable string
     * @param {Number} bytes the filesize in bytes
     * @return {String} human-readable string
     */
    size(bytes) {
      const toStr = (size, units) => `${size.toTruncFixed(size <= 9.995 ? 2 : 1)} ${units}`;

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
      return bytes / Math.pow(size_K, 4), size_T_str;
    },

    speed(KBps) {
      let speed = KBps;

      if (speed <= 999.95) {
        // 0 KBps to 999 K
        return [speed.toTruncFixed(0), speed_K_str].join(' ');
      }

      speed /= speed_K;

      if (speed <= 99.995) {
        // 1 M to 99.99 M
        return [speed.toTruncFixed(2), speed_M_str].join(' ');
      }
      if (speed <= 999.95) {
        // 100 M to 999.9 M
        return [speed.toTruncFixed(1), speed_M_str].join(' ');
      }

      // insane speeds
      speed /= speed_K;
      return [speed.toTruncFixed(2), speed_G_str].join(' ');
    },

    speedBps(Bps) {
      return this.speed(this.toKBps(Bps));
    },

    timeInterval(seconds) {
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
    },

    timestamp(seconds) {
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
    },

    toKBps(Bps) {
      return Math.floor(Bps / speed_K);
    },
  };
})();
