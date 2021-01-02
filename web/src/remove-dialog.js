/**
 * @license
 *
 * This file Copyright (C) 2020 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 */

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
    const { torrents } = this.options;
    if (torrents.length > 0) {
      if (this.options.trash) {
        this.options.remote.removeTorrentsAndData(torrents);
      } else {
        this.options.remote.removeTorrents(torrents);
      }
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
    const { torrents } = options;
    const [torrent] = torrents;
    if (options.trash && torrents.length === 1) {
      heading = `Remove ${torrent.getName()} and delete data?`;
      message =
        'All data downloaded for this torrent will be deleted. Are you sure you want to remove it?';
    } else if (options.trash) {
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
