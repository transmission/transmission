/**
 * @license
 *
 * This file Copyright (C) 2020 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 */

import isEqual from 'lodash.isequal';

export class Utils {
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

  /** Given a numerator and denominator, return a ratio string */
  static ratio(numerator, denominator) {
    let result = Math.floor((100 * numerator) / denominator) / 100;

    // check for special cases
    if (
      result === Number.POSITIVE_INFINITY ||
      result === Number.NEGATIVE_INFINITY
    ) {
      result = -2;
    } else if (Number.isNaN(result)) {
      result = -1;
    }

    return result;
  }
}

export function createTabsContainer(id, tabs, callback) {
  const root = document.createElement('div');
  root.id = id;
  root.classList.add('tabs-container');

  const buttons = document.createElement('div');
  buttons.classList.add('tabs-buttons');
  root.append(buttons);

  const pages = document.createElement('div');
  pages.classList.add('tabs-pages');
  root.append(pages);

  const button_array = [];
  for (const [button_id, page] of tabs) {
    const button = document.createElement('button');
    button.id = button_id;
    button.classList.add('tabs-button');
    button.setAttribute('type', 'button');
    buttons.append(button);
    button_array.push(button);

    page.classList.add('hidden', 'tabs-page');
    pages.append(page);

    button.addEventListener('click', () => {
      for (const element of buttons.children) {
        element.classList.toggle('selected', element === button);
      }
      for (const element of pages.children) {
        element.classList.toggle('hidden', element !== page);
      }
      if (callback) {
        callback(page);
      }
    });
  }

  button_array[0].classList.add('selected');
  pages.children[0].classList.remove('hidden');

  return {
    buttons: button_array,
    root,
  };
}

export function createDialogContainer(id) {
  const root = document.createElement('dialog');
  root.classList.add('dialog-container', 'popup', id);
  root.open = true;
  root.setAttribute('role', 'dialog');

  const win = document.createElement('div');
  win.classList.add('dialog-window');
  root.append(win);

  const logo = document.createElement('div');
  logo.classList.add('dialog-logo');
  win.append(logo);

  const heading = document.createElement('div');
  heading.classList.add('dialog-heading');
  win.append(heading);

  const message = document.createElement('div');
  message.classList.add('dialog-message');
  win.append(message);

  const workarea = document.createElement('div');
  workarea.classList.add('dialog-workarea');
  win.append(workarea);

  const buttons = document.createElement('div');
  buttons.classList.add('dialog-buttons');
  win.append(buttons);

  const bbegin = document.createElement('span');
  bbegin.classList.add('dialog-buttons-begin');
  buttons.append(bbegin);

  const dismiss = document.createElement('button');
  dismiss.classList.add('dialog-dismiss-button');
  dismiss.textContent = 'Cancel';
  buttons.append(dismiss);

  const confirm = document.createElement('button');
  confirm.textContent = 'OK';
  buttons.append(confirm);

  const bend = document.createElement('span');
  bend.classList.add('dialog-buttons-end');
  buttons.append(bend);

  return {
    confirm,
    dismiss,
    heading,
    message,
    root,
    workarea,
  };
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

export function createSection(title) {
  const root = document.createElement('fieldset');
  root.classList.add('section');

  const legend = document.createElement('legend');
  legend.classList.add('title');
  legend.textContent = title;
  root.append(legend);

  const content = document.createElement('div');
  content.classList.add('content');
  root.append(content);

  return { content, root };
}

export function createInfoSection(title, labels) {
  const children = [];
  const { root, content } = createSection(title);

  for (const label_text of labels) {
    const label_element = document.createElement('label');
    label_element.textContent = label_text;
    content.append(label_element);

    const item = document.createElement('div');
    item.id = makeUUID();
    content.append(item);
    label_element.setAttribute('for', item.id);
    children.push(item);
  }

  return { children, root };
}

export function debounce(callback, wait = 100) {
  let timeout = null;
  return (...arguments_) => {
    if (!timeout) {
      timeout = setTimeout(() => {
        timeout = null;
        callback(arguments_);
      }, wait);
    }
  };
}

export function deepEqual(a, b) {
  return isEqual(a, b);
}

function setOrDeleteAttribute(element, attribute, b) {
  if (b) {
    element.setAttribute(attribute, true);
  } else {
    element.removeAttribute(attribute);
  }
}
export function setEnabled(element, b) {
  setOrDeleteAttribute(element, 'disabled', !b);
}
export function setChecked(element, b) {
  setOrDeleteAttribute(element, 'checked', b);
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

export class OutsideClickListener extends EventTarget {
  constructor(element) {
    super();
    this.listener = (event_) => {
      if (!element.contains(event_.target)) {
        this.dispatchEvent(new MouseEvent(event_.type, event_));
        event_.preventDefault();
      }
    };
    Object.seal(this);
    this.start();
  }
  start() {
    setTimeout(() => document.addEventListener('click', this.listener), 0);
  }
  stop() {
    document.removeEventListener('click', this.listener);
  }
}

export function setTextContent(e, text) {
  if (e.textContent !== text) {
    e.textContent = text;
  }
}
