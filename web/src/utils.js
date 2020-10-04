/**
 * Copyright © Dave Perrett, Malcolm Jarvis and Artem Vorotnikov
 *
 * This file is licensed under the GPLv2.
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 */

// TODO: remove this and set layout based on window size
export const isMobileDevice = /(iPhone|iPod|Android)/.test(navigator.userAgent);

export class Utils {
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
    el.classList.add('hidden');
  }
  static hideId(id) {
    return Utils.hide(document.getElementById(id));
  }

  static show(el) {
    el.classList.remove('hidden');
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
  static setVisibleId(id, visible) {
    return Utils.setVisible(document.getElementById(id), visible);
  }

  /**
   * Checks to see if the content actually changed before poking the DOM.
   */
  static setInnerHTML(e, html) {
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

  static sanitizeText(text) {
    return text.replace(/</g, '&lt;').replace(/>/g, '&gt;');
  }

  /**
   * Avoid poking properties that haven't changed.
   * This is a (possibly unnecessary?) precaution to avoid unnecessary DOM changes
   */
  static setProperty(o, key, val) {
    if (o && o[key] !== val) {
      o[key] = val;
    }
  }
  static setTextContent(e, text) {
    Utils.setProperty(e, 'textContent', text);
  }

  static debounce(callback, wait = 100) {
    let timeout = null;
    return (...args) => {
      const context = this;
      if (!timeout) {
        timeout = setTimeout(() => {
          timeout = null;
          callback.apply(context, args);
        }, wait);
      }
    };
  }

  /** Given a numerator and denominator, return a ratio string */
  static ratio(numerator, denominator) {
    let result = Math.floor((100 * numerator) / denominator) / 100;

    // check for special cases
    if (result === Number.POSITIVE_INFINITY || result === Number.NEGATIVE_INFINITY) {
      result = -2;
    } else if (isNaN(result)) {
      result = -1;
    }

    return result;
  }
}
