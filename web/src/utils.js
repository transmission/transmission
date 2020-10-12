/**
 * Copyright © Mnemosyne LLC
 *
 * This file is licensed under the GPLv2.
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 */

export class Utils {
  static setVisible(el, visible) {
    el.classList.toggle('hidden', !visible);
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
  const root = document.createElement('dialog');
  root.classList.add('dialog-container', 'popup', id);
  root.open = true;
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

export function setEnabled(el, enabled) {
  if (enabled) {
    delete el.disabled;
  } else {
    el.setAttribute('disabled', true);
  }
}

export function makeUUID() {
  // source: https://stackoverflow.com/a/2117523/6568470
  return ([1e7] + -1e3 + -4e3 + -8e3 + -1e11).replace(/[018]/g, (c) =>
    (
      c ^
      (crypto.getRandomValues(new Uint8Array(1))[0] & (15 >> (c / 4)))
    ).toString(16)
  );
}

function getBestMenuPos(r, bounds) {
  let { x, y } = r;
  const { width, height } = r;

  if (x > bounds.x + bounds.width - width && x - width >= bounds.x) {
    x -= width;
  } else {
    x = Math.min(x, bounds.x + bounds.width - width);
  }

  if (y > bounds.y + bounds.height - height && y - height >= bounds.y) {
    y -= height;
  } else {
    y = Math.min(y, bounds.y + bounds.height - height);
  }

  return new DOMRect(x, y, width, height);
}

export function movePopup(popup, x, y, boundingElement) {
  const initial_pos = new DOMRect(x, y, popup.clientWidth, popup.clientHeight);
  const clamped_pos = getBestMenuPos(
    initial_pos,
    boundingElement.getBoundingClientRect()
  );
  popup.style.left = `${clamped_pos.left}px`;
  popup.style.top = `${clamped_pos.top}px`;
}

export function sanitizeText(text) {
  return text.replace(/</g, '&lt;').replace(/>/g, '&gt;');
}
