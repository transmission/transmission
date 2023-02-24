/* @license This file Copyright © 2020-2023 Mnemosyne LLC.
   It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
   or any future license endorsed by Mnemosyne LLC.
   License text can be found in the licenses/ folder. */

import { createDialogContainer } from './utils.js';

export class RemoveDialog extends EventTarget {
  constructor(options) {
    super();

    // options: remote, torrents, trash
    this.options = options;
    this.elements = RemoveDialog._create(options);
    this.elements.dismiss.addEventListener('click', () => this._onDismiss());
    this.elements.confirm.addEventListener('click', () => this._onConfirm());
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

  _onConfirm() {
    const { remote, torrents, trash } = this.options;
    if (torrents.length > 0) {
      remote.removeTorrents(torrents, trash);
    }

    this.close();
  }

  static _create(options) {
    const { trash } = options;
    const { heading, message } = RemoveDialog._createMessage(options);

    const elements = createDialogContainer('remove-dialog');
    elements.heading.textContent = heading;
    elements.message.textContent = message;
    elements.confirm.textContent = trash ? 'Trash' : 'Remove';
    return elements;
  }

  static _createMessage(options) {
    let heading = null;
    let message = null;
    const { torrents, trash } = options;
    const [torrent] = torrents;
    if (trash && torrents.length === 1) {
      heading = `Remove ${torrent.getName()} and delete data?`;
      message =
        'All data downloaded for this torrent will be deleted. Are you sure you want to remove it?';
    } else if (trash) {
      heading = `Remove ${torrents.length} transfers and delete data?`;
      message =
        'All data downloaded for these torrents will be deleted. Are you sure you want to remove them?';
    } else if (torrents.length === 1) {
      heading = `Remove ${torrent.getName()}?`;
      message =
        'Once removed, continuing the transfer will require the torrent file. Are you sure you want to remove it?';
    } else {
      heading = `Remove ${torrents.length} transfers?`;
      message =
        'Once removed, continuing the transfers will require the torrent files. Are you sure you want to remove them?';
    }
    return { heading, message };
  }
}
