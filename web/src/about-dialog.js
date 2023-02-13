/* @license This file Copyright © 2020-2023 Mnemosyne LLC.
   It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
   or any future license endorsed by Mnemosyne LLC.
   License text can be found in the licenses/ folder. */

import { createDialogContainer } from './utils.js';

export class AboutDialog extends EventTarget {
  constructor(version_info) {
    super();

    this.elements = AboutDialog._create(version_info);
    this.elements.dismiss.addEventListener('click', () => this._onDismiss());
    document.body.append(this.elements.root);
    this.elements.dismiss.focus();
  }

  close() {
    this.elements.root.remove();
    this.dispatchEvent(new Event('close'));
    delete this.elements;
  }

  _onDismiss() {
    this.close();
  }

  static _create(version_info) {
    const elements = createDialogContainer('about-dialog');
    elements.root.setAttribute('aria-label', 'About transmission');
    elements.heading.textContent = 'Transmission';
    elements.dismiss.textContent = 'Close';

    let e = document.createElement('div');
    e.classList.add('about-dialog-version-number');
    e.textContent = version_info.version;
    elements.heading.append(e);

    e = document.createElement('div');
    e.classList.add('about-dialog-version-checksum');
    e.textContent = version_info.checksum;
    elements.heading.append(e);

    e = document.createElement('div');
    e.textContent = 'A fast and easy bitTorrent client';
    elements.workarea.append(e);
    e = document.createElement('div');
    e.textContent = 'Copyright © The Transmission Project';
    elements.workarea.append(e);

    elements.confirm.remove();
    delete elements.confirm;

    return elements;
  }
}
