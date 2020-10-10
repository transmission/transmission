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
    return el.classList.contains('hidden');
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

  static toggle(el) {
    el.classList.toggle('hidden');
  }
  static toggleId(id) {
    return Utils.toggle(document.getElementById(id));
  }

  static isChecked(el) {
    return el.getAttribute('aria-checked') === 'true';
  }
  static setChecked(el, b) {
    el.setAttribute('aria-checked', b ? 'true' : 'false');
  }
  static toggleChecked(el) {
    Utils.setChecked(el, !Utils.isChecked(el));
  }
  static isCheckedId(id) {
    return Utils.isChecked(document.getElementById(id));
  }
  static setCheckedId(id, b) {
    Utils.setChecked(document.getElementById(id), b);
  }
  static toggleCheckedId(id) {
    Utils.toggleChecked(document.getElementById(id));
  }
  static setCheckedCmd(cmd, b) {
    for (const el of document.querySelectorAll(`[data-command="${cmd}"]`)) {
      Utils.setChecked(el, b);
    }
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
    if (
      result === Number.POSITIVE_INFINITY ||
      result === Number.NEGATIVE_INFINITY
    ) {
      result = -2;
    } else if (isNaN(result)) {
      result = -1;
    }

    return result;
  }
}

export function createDialogContainer(id) {
  const root = document.createElement('div');
  root.classList.add('dialog-container');
  root.classList.add('popup');
  root.classList.add(id);
  root.setAttribute('role', 'dialog');

  const win = document.createElement('div');
  win.classList.add('dialog-window');
  root.appendChild(win);

  const logo = document.createElement('div');
  logo.classList.add('dialog-logo');
  win.appendChild(logo);

  const heading = document.createElement('div');
  heading.classList.add('dialog-heading');
  win.appendChild(heading);

  const message = document.createElement('div');
  message.classList.add('dialog-message');
  win.appendChild(message);

  const workarea = document.createElement('div');
  workarea.classList.add('dialog-workarea');
  win.appendChild(workarea);

  const buttons = document.createElement('div');
  buttons.classList.add('dialog-buttons');
  win.appendChild(buttons);

  const bbegin = document.createElement('span');
  bbegin.classList.add('dialog-buttons-begin');
  buttons.appendChild(bbegin);

  const dismiss = document.createElement('button');
  dismiss.classList.add('dialog-dismiss-button');
  dismiss.textContent = 'Cancel';
  buttons.appendChild(dismiss);

  const confirm = document.createElement('button');
  confirm.textContent = 'OK';
  buttons.appendChild(confirm);

  const bend = document.createElement('span');
  bend.classList.add('dialog-buttons-end');
  buttons.appendChild(bend);

  return {
    confirm,
    dismiss,
    heading,
    message,
    root,
    workarea,
  };
}
