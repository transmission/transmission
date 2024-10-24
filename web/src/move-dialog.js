/* @license This file Copyright © Mnemosyne LLC.
   It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
   or any future license endorsed by Mnemosyne LLC.
   License text can be found in the licenses/ folder. */

import { createDialogContainer } from './utils.js';

export class MoveDialog extends EventTarget {
  constructor(controller, remote) {
    super();

    this.controller = controller;
    this.remote = remote;
    this.elements = {};
    this.torrents = [];

    this.show();
  }

  show() {
    const torrents = this.controller.getSelectedTorrents();
    if (torrents.length === 0) {
      return;
    }

    const paths = this.controller.getAllTorrentsPaths();

    this.torrents = torrents;
    this.elements = MoveDialog._create();
    this.elements.confirm.addEventListener('click', () => this._onConfirm());
    this.elements.dismiss.addEventListener('click', () => this._onDismiss());
    this.elements.entry.value = torrents[0].getDownloadDir();

    while (this.elements.datalist.firstChild) {
      this.elements.datalist.firstChild.remove();
    }
    for (const path of paths) {
      const option = document.createElement('option');
      option.value = path;
      this.elements.datalist.append(option);
    }

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
    this.close();
  }

  _onConfirm() {
    const ids = this.torrents.map((tor) => tor.getId());
    const path = this.elements.entry.value.trim();
    this.remote.moveTorrents(ids, path);
    this.close();
  }

  static _create() {
    const elements = createDialogContainer('move-dialog');
    elements.root.setAttribute('aria-label', 'Move Torrent');
    elements.heading.textContent = 'Set Torrent Location';
    confirm.textContent = 'Apply';

    const label = document.createElement('label');
    label.setAttribute('for', 'torrent-path');
    label.textContent = 'Location:';
    elements.workarea.append(label);

    const entry = document.createElement('input');
    entry.setAttribute('type', 'text');
    entry.setAttribute('list', 'path-list');
    entry.id = 'torrent-path';
    elements.entry = entry;
    elements.workarea.append(entry);

    const datalist = document.createElement('datalist');
    datalist.id = 'path-list';
    elements.datalist = datalist;
    elements.workarea.append(datalist);

    return elements;
  }
}
