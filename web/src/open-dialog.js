/*
 * This file Copyright (C) 2020 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 */

import { AlertDialog } from './alert-dialog.js';
import { Formatter } from './formatter.js';
import { createDialogContainer, makeUUID } from './utils.js';

export class OpenDialog extends EventTarget {
  constructor(controller, remote) {
    super();

    this.controller = controller;
    this.remote = remote;

    this.elements = this._create();
    this.elements.dismiss.addEventListener('click', () => this._onDismiss());
    this.elements.confirm.addEventListener('click', () => this._onConfirm());
    this._updateFreeSpaceInAddDialog();
    document.body.appendChild(this.elements.root);
    this.elements.url_input.focus();
  }

  close() {
    if (!this.closed) {
      clearInterval(this.interval);

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
      const str = bytes > 0 ? `${Formatter.size(bytes)} Free` : '';
      this.elements.freespace.textContent = str;
    });
  }

  _onConfirm() {
    const { remote } = this;
    const { file_input, folder_input, start_input, url_input } = this.elements;
    const paused = !start_input.checked;
    const destination = folder_input.value.trim();

    for (const file of file_input.files) {
      const reader = new FileReader();
      reader.onload = (e) => {
        const contents = e.target.result;
        const key = 'base64,';
        const index = contents.indexOf(key);
        if (index === -1) {
          return;
        }
        const o = {
          arguments: {
            'download-dir': destination,
            metainfo: contents.substring(index + key.length),
            paused,
          },
          method: 'torrent-add',
        };
        console.log(o);
        remote.sendRequest(o, (response) => {
          if (response.result !== 'success') {
            alert(`Error adding "${file.name}": ${response.result}`);
            this.controller.setCurrentPopup(
              new AlertDialog({
                heading: `Error adding "${file.name}"`,
                message: response.result,
              })
            );
          }
        });
      };
      reader.readAsDataURL(file);
    }

    let url = url_input.value.trim();
    if (url.length > 0) {
      if (url.match(/^[0-9a-f]{40}$/i)) {
        url = `magnet:?xt=urn:btih:${url}`;
      }
      const o = {
        arguments: {
          'download-dir': destination,
          filename: url,
          paused,
        },
        method: 'torrent-add',
      };
      console.log(o);
      remote.sendRequest(o, (payload, response) => {
        if (response.result !== 'success') {
          this.controller.setCurrentPopup(
            new AlertDialog({
              heading: `Error adding "${url}"`,
              message: response.result,
            })
          );
        }
      });
    }

    this._onDismiss();
  }

  _create() {
    const elements = createDialogContainer();
    const { confirm, root, heading, workarea } = elements;

    root.classList.add('open-torrent');
    heading.textContent = 'Add Torrents';
    confirm.textContent = 'Add';

    let input_id = makeUUID();
    let label = document.createElement('label');
    label.setAttribute('for', input_id);
    label.textContent = 'Please select torrent files to add:';
    workarea.appendChild(label);

    let input = document.createElement('input');
    input.type = 'file';
    input.name = 'torrent-files[]';
    input.id = input_id;
    input.multiple = 'multiple';
    workarea.appendChild(input);
    elements.file_input = input;

    input_id = makeUUID();
    label = document.createElement('label');
    label.setAttribute('for', input_id);
    label.textContent = 'Or enter a URL:';
    workarea.appendChild(label);

    input = document.createElement('input');
    input.type = 'url';
    input.id = input_id;
    workarea.appendChild(input);
    elements.url_input = input;

    input_id = makeUUID();
    label = document.createElement('label');
    label.id = 'add-dialog-folder-label';
    label.for = input_id;
    label.textContent = 'Destination folder:';
    workarea.appendChild(label);

    const freespace = document.createElement('span');
    freespace.id = 'free-space-text';
    label.appendChild(freespace);
    workarea.appendChild(label);
    elements.freespace = freespace;

    input = document.createElement('input');
    input.type = 'text';
    input.id = 'add-dialog-folder-input';
    input.addEventListener('change', () => this._updateFreeSpaceInAddDialog());
    input.value = this.controller.session_properties['download-dir'];
    workarea.appendChild(input);
    elements.folder_input = input;

    const checkarea = document.createElement('div');
    workarea.appendChild(checkarea);

    const check = document.createElement('input');
    check.type = 'checkbox';
    check.id = 'auto-start-check';
    check.checked = this.controller.shouldAddedTorrentsStart();
    checkarea.appendChild(check);
    elements.start_input = check;

    label = document.createElement('label');
    label.id = 'auto-start-label';
    label.setAttribute('for', check.id);
    label.textContent = 'Start when added';
    checkarea.appendChild(label);

    return elements;
  }
}
