/* @license This file Copyright © Mnemosyne LLC.
   It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
   or any future license endorsed by Mnemosyne LLC.
   License text can be found in the licenses/ folder. */

import { AlertDialog } from './alert-dialog.js';
import { Formatter } from './formatter.js';
import { RPC } from './remote.js';
import { createDialogContainer, makeUUID } from './utils.js';

const is_ios =
  /iPad|iPhone|iPod/.test(navigator.userAgent) && !globalThis.MSStream;
const is_safari = /^((?!chrome|android).)*safari/i.test(navigator.userAgent);
// https://github.com/transmission/transmission/pull/6320#issuecomment-1896968904
// https://caniuse.com/input-file-accept
const can_use_input_accept = !(is_ios && is_safari);

export class OpenDialog extends EventTarget {
  constructor(controller, remote, url = '', files = null) {
    super();

    this.controller = controller;
    this.remote = remote;

    this.elements = this._create(url);
    this.elements.dismiss.addEventListener('click', () => this._onDismiss());
    this.elements.confirm.addEventListener('click', () => this._onConfirm());
    document.body.append(this.elements.root);
    if (files) {
      this.elements.file_input.files = files;
    }
    this._updateFreeSpaceInAddDialog();
    this.elements.url_input.focus();

    // Bind paste event listener
    this._boundPasteHandler = (e) => this._onPaste(e);
    document.addEventListener('paste', this._boundPasteHandler);

    // Bind drag-and-drop listeners
    this._boundDragOverHandler = (e) => this._onDragOver(e);
    this._boundDragLeaveHandler = (e) => this._onDragLeave(e);
    this._boundDropHandler = (e) => this._onDrop(e);

    const { root } = this.elements;
    root.addEventListener('dragover', this._boundDragOverHandler);
    root.addEventListener('dragleave', this._boundDragLeaveHandler);
    root.addEventListener('drop', this._boundDropHandler);
  }

  close() {
    if (!this.closed) {
      clearInterval(this.interval);

      // Clean up paste event listener
      if (this._boundPasteHandler) {
        document.removeEventListener('paste', this._boundPasteHandler);
      }

      // Clean up drag-and-drop listeners
      if (this._boundDragOverHandler) {
        const { root } = this.elements;
        root.removeEventListener('dragover', this._boundDragOverHandler);
        root.removeEventListener('dragleave', this._boundDragLeaveHandler);
        root.removeEventListener('drop', this._boundDropHandler);
      }

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

  _updateFreeSpaceInAddDialog() {
    const path = this.elements.folder_input.value;
    this.remote.getFreeSpace(path, (dir, bytes) => {
      if (!this.closed) {
        this.elements.freespace.textContent =
          bytes > 0 ? `${Formatter.size(bytes)} Free` : '';
      }
    });
  }

  _onConfirm() {
    const { controller, elements, remote } = this;
    const { file_input, folder_input, start_input, url_input } = elements;
    const paused = !start_input.checked;
    const destination = folder_input.value.trim();

    for (const file of file_input.files) {
      const reader = new FileReader();
      reader.addEventListener('load', (e) => {
        const contents = e.target.result;
        const key = 'base64,';
        const index = contents.indexOf(key);
        if (index === -1) {
          return;
        }
        const o = {
          id: 'webui',
          jsonrpc: RPC._JsonRpcVersion,
          method: 'torrent_add',
          params: {
            download_dir: destination,
            metainfo: contents.slice(Math.max(0, index + key.length)),
            paused,
          },
        };
        remote.sendRequest(o, (response) => {
          if ('error' in response) {
            const message =
              response.error.data?.errorString ?? response.error.message;
            alert(`Error adding "${file.name}": ${message}`);
            controller.setCurrentPopup(
              new AlertDialog({
                heading: `Error adding "${file.name}"`,
                message,
              }),
            );
          }
        });
      });
      reader.readAsDataURL(file);
    }

    let url = url_input.value.trim();
    if (url.length > 0) {
      if (/^[\da-f]{40}$/i.test(url)) {
        url = `magnet:?xt=urn:btih:${url}`;
      }
      const o = {
        id: 'webui',
        jsonrpc: RPC._JsonRpcVersion,
        method: 'torrent_add',
        params: {
          download_dir: destination,
          filename: url,
          paused,
        },
      };
      remote.sendRequest(o, (payload) => {
        if ('error' in payload) {
          controller.setCurrentPopup(
            new AlertDialog({
              heading: `Error adding "${url}"`,
              message: payload.error.data?.errorString ?? payload.error.message,
            }),
          );
        }
      });
    }

    this._onDismiss();
  }

  _onPaste(event) {
    const files = [...event.clipboardData.files];

    // Allow text paste in url_input
    if (event.target === this.elements.url_input && files.length === 0) {
      return; // Allow default text paste behavior
    }

    // Check for .torrent files
    const torrentFiles = files.filter(
      (f) =>
        f.name.endsWith('.torrent') || f.type === 'application/x-bittorrent',
    );

    if (torrentFiles.length > 0) {
      event.preventDefault();
      this._addFilesToInput(torrentFiles);
    }
  }

  _addFilesToInput(filesArray) {
    const dt = new DataTransfer();

    // Add existing files first (preserve them)
    for (const file of this.elements.file_input.files) {
      dt.items.add(file);
    }

    // Add new files
    for (const file of filesArray) {
      dt.items.add(file);
    }

    this.elements.file_input.files = dt.files;
  }

  _onDragOver(event) {
    event.preventDefault();
    event.stopPropagation();
    this.elements.root.classList.add('drag-over');
  }

  _onDragLeave(event) {
    event.preventDefault();
    event.stopPropagation();
    // Only remove if leaving the root element entirely
    if (event.target === this.elements.root) {
      this.elements.root.classList.remove('drag-over');
    }
  }

  _onDrop(event) {
    event.preventDefault();
    event.stopPropagation();
    this.elements.root.classList.remove('drag-over');

    const files = [...event.dataTransfer.files];
    const torrentFiles = files.filter(
      (f) =>
        f.name.endsWith('.torrent') || f.type === 'application/x-bittorrent',
    );

    if (torrentFiles.length > 0) {
      this._addFilesToInput(torrentFiles);
    }
  }

  _create(url) {
    const elements = createDialogContainer();
    const { confirm, root, heading, workarea } = elements;

    root.classList.add('open-torrent');
    heading.textContent = 'Add Torrents';
    confirm.textContent = 'Add';

    let input_id = makeUUID();
    let label = document.createElement('label');
    label.setAttribute('for', input_id);
    label.textContent = 'Please select torrent files to add:';
    workarea.append(label);

    let input = document.createElement('input');
    input.type = 'file';
    input.name = 'torrent-files[]';
    input.id = input_id;
    input.multiple = true;
    if (can_use_input_accept) {
      input.accept = '.torrent,application/x-bittorrent';
    }
    workarea.append(input);
    elements.file_input = input;

    input_id = makeUUID();
    label = document.createElement('label');
    label.setAttribute('for', input_id);
    label.textContent = 'Or enter a URL:';
    workarea.append(label);

    input = document.createElement('input');
    input.type = 'url';
    input.id = input_id;
    input.value = url;
    workarea.append(input);
    elements.url_input = input;

    input_id = makeUUID();
    label = document.createElement('label');
    label.id = 'add-dialog-folder-label';
    label.for = input_id;
    label.textContent = 'Destination folder: ';
    workarea.append(label);

    const freespace = document.createElement('span');
    freespace.id = 'free-space-text';
    label.append(freespace);
    workarea.append(label);
    elements.freespace = freespace;

    input = document.createElement('input');
    input.type = 'text';
    input.id = 'add-dialog-folder-input';
    input.addEventListener('change', () => this._updateFreeSpaceInAddDialog());
    input.value = this.controller.session_properties.download_dir;
    workarea.append(input);
    elements.folder_input = input;

    const checkarea = document.createElement('div');
    workarea.append(checkarea);

    const check = document.createElement('input');
    check.type = 'checkbox';
    check.id = 'auto-start-check';
    check.checked = this.controller.shouldAddedTorrentsStart();
    checkarea.append(check);
    elements.start_input = check;

    label = document.createElement('label');
    label.id = 'auto-start-label';
    label.setAttribute('for', check.id);
    label.textContent = 'Start when added';
    checkarea.append(label);

    return elements;
  }
}
