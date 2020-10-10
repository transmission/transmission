/*
 * This file Copyright (C) 2020 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 */

import { createDialogContainer } from './utils.js';

export class AlertDialog extends EventTarget {
  constructor(options) {
    super();

    // options: heading, message
    this.elements = AlertDialog._create(options);
    this.elements.dismiss.addEventListener('click', () => this._onDismiss());
    this.options = options;
    document.body.appendChild(this.elements.root);
    this.elements.dismiss.focus();
  }

  close() {
    if (!this.closed) {
      this.closed = true;
      this.elements.root.remove();
      this.dispatchEvent(new Event('close'));
      delete this.elements;
    }
  }

  _onDismiss() {
    this.close();
  }

  static _create(options) {
    const { heading, message } = options;
    const elements = createDialogContainer('confirm-dialog');
    elements.confirm.remove();
    delete elements.confirm;
    elements.heading.textContent = heading;
    elements.workarea.textContent = message;
    return elements;
  }
}
