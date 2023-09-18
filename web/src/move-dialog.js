/* @license This file Copyright © 2020-2023 Mnemosyne LLC.
   It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
   or any future license endorsed by Mnemosyne LLC.
   License text can be found in the licenses/ folder. */

let default_path = '';

import { createDialogContainer } from './utils.js';

export class MoveDialog extends EventTarget {
  constructor(controller, remote, action_input_value) {
    super();

    this.controller = controller;
    this.remote = remote;
    this.action_input_value = action_input_value;
    this.elements = {};
    this.torrents = [];

    this.show();
  }

  show() {
    const torrents = this.controller.getSelectedTorrents();
    if (torrents.length === 0) {
      return;
    }

    default_path = default_path || torrents[0].getDownloadDir();

    this.torrents = torrents;
    this.elements = MoveDialog._create(typeof this.action_input_value == 'string'
        ? 'Confirm'
        : 'Apply'
    );
    this.elements.confirm.addEventListener('click', () => this._onConfirm());
    this.elements.dismiss.addEventListener('click', () => this._onDismiss());
    this.elements.entry.value = typeof this.action_input_value == 'string'
      ? this.action_input_value
      : default_path;
    document.body.append(this.elements.root);

    this.elements.entry.focus();
  }

  close() {
    this.elements.root.remove();

    this.dispatchEvent(new Event('close'));

    delete this.controller;
    delete this.remote;
    delete this.elements;
    delete this.torrents;
  }

  _onDismiss() {
    typeof this.action_input_value == 'string'
      ? this.controller.action_manager.click('show-inspector')
      : this.close();
  }

  _onConfirm() {
    const ids = this.torrents.map((tor) => tor.getId());
    const path = this.elements.entry.value.trim();
    default_path = path;
    this.remote.moveTorrents(ids, path);

    this._onDismiss();
  }

  static _create(confirm_text) {
    const elements = createDialogContainer('move-dialog');
    elements.root.setAttribute('aria-label', 'Move Torrent');
    elements.heading.textContent = 'Set Torrent Location';
    elements.confirm.textContent = confirm_text;

    const label = document.createElement('label');
    label.setAttribute('for', 'torrent-path');
    label.textContent = 'Location:';
    elements.workarea.append(label);

    const entry = document.createElement('input');
    entry.setAttribute('type', 'text');
    entry.id = 'torrent-path';
    elements.entry = entry;
    elements.workarea.append(entry);

    return elements;
  }
}
