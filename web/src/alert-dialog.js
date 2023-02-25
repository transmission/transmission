/* @license This file Copyright © 2020-2023 Mnemosyne LLC.
   It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
   or any future license endorsed by Mnemosyne LLC.
   License text can be found in the licenses/ folder. */

import { createDialogContainer } from './utils.js';

export class AlertDialog extends EventTarget {
  constructor(options) {
    super();

    // options: heading, message
    this.elements = AlertDialog._create(options);
    this.elements.dismiss.addEventListener('click', () => this._onDismiss());
    this.options = options;
    document.body.append(this.elements.root);
    this.elements.dismiss.focus();
  }

  close() {
    if (!this.closed) {
      this.elements.root.remove();
      this.dispatchEvent(new Event('close'));
      for (const key of Object.keys(this)) {
        delete this[key];
      }
      this.closed = true;
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
