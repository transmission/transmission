/**
 * Copyright © Mnemosyne LLC
 *
 * This file is licensed under the GPLv2.
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 */

import { Formatter } from './formatter.js';
import { Utils } from './utils.js';

export class FileRow extends EventTarget {
  isDone() {
    return this.fields.have >= this.fields.size;
  }

  isEditable() {
    return this.fields.torrent.getFileCount() > 1 && !this.isDone();
  }

  refreshWantedHTML() {
    const e = this.elements.root;
    e.classList.toggle('skip', !this.fields.isWanted);
    e.classList.toggle('complete', this.isDone());
    e.checkbox.disabled = !this.isEditable();
    e.checkbox.checked = this.fields.isWanted;
  }

  refreshProgressHTML() {
    const { size, have } = this.fields;
    const pct = 100 * (size ? have / size : 1.0);
    const fmt = Formatter;
    const c = `${fmt.size(have)} of ${fmt.size(size)} (${fmt.percentString(
      pct
    )}%)`;
    Utils.setTextContent(this.elements.progress, c);
  }

  refresh() {
    let have = 0;
    let high = false;
    let low = false;
    let normal = false;
    let size = 0;
    let wanted = false;

    // loop through the file_indices that affect this row
    for (const idx of this.fields.indices) {
      const file = this.fields.torrent.getFile(idx);
      have += file.bytesCompleted;
      size += file.length;
      wanted |= file.wanted;
      switch (file.priority) {
        case -1:
          low = true;
          break;
        case 1:
          high = true;
          break;
        default:
          normal = true;
          break;
      }
    }

    if (this.fields.have !== have || this.fields.size !== size) {
      this.fields.have = have;
      this.fields.size = size;
      this.refreshProgressHTML();
    }

    if (this.fields.isWanted !== wanted) {
      this.fields.isWanted = wanted;
      this.refreshWantedHTML();
    }

    if (this.fields.priorityLow !== low) {
      this.fields.priorityLow = low;
      this.elements.priority_low_button.classList.toggle('selected', low);
    }

    if (this.fields.priorityNormal !== normal) {
      this.fields.priorityNormal = normal;
      this.elements.priority_normal_button.classList.toggle('selected', normal);
    }

    if (this.fields.priorityHigh !== high) {
      this.fields.priorityHigh = high;
      this.elements.priority_high_button.classList.toggle('selected', high);
    }
  }

  fireWantedChanged(wanted) {
    const e = new Event('wantedToggled');
    e.indices = [...this.fields.indices];
    e.wanted = wanted;
    this.dispatchEvent(e);
  }

  firePriorityChanged(priority) {
    const e = new Event('priorityToggled');
    e.indices = [...this.fields.indices];
    e.priority = priority;
    this.dispatchEvent(e);
  }

  fireNameClicked() {
    this.dispatchEvent(new Event('nameClicked'));
  }

  createRow(torrent, depth, name, even) {
    const root = document.createElement('li');
    root.className = `inspector-torrent-file-list-entry${
      even ? 'even' : 'odd'
    }`;
    this.elements.root = root;

    let e = document.createElement('input');
    e.type = 'checkbox';
    e.className = 'file-wanted-control';
    e.title = 'Download file';
    e.addEventListener('change', (ev) =>
      this.fireWantedChanged(ev.target.checked)
    );
    root.checkbox = e;
    root.appendChild(e);

    e = document.createElement('div');
    e.className = 'file-priority-radiobox';
    const box = e;

    e = document.createElement('div');
    e.className = 'low';
    e.title = 'Low Priority';
    e.addEventListener('click', () => this.firePriorityChanged(-1));
    this.elements.priority_low_button = e;
    box.appendChild(e);

    e = document.createElement('div');
    e.className = 'normal';
    e.title = 'Normal Priority';
    e.addEventListener('click', () => this.firePriorityChanged(0));
    this.elements.priority_normal_button = e;
    box.appendChild(e);

    e = document.createElement('div');
    e.title = 'High Priority';
    e.className = 'high';
    e.addEventListener('click', () => this.firePriorityChanged(1));
    this.elements.priority_high_button = e;
    box.appendChild(e);

    root.appendChild(box);

    e = document.createElement('div');
    e.className = 'inspector-torrent-file-list-entry-name';
    e.addEventListener('click', () => this.fireNameClicked());
    Utils.setTextContent(e, name);
    root.appendChild(e);

    e = document.createElement('div');
    e.className = 'inspector-torrent-file-list-entry-progress';
    e.addEventListener('click', () => this.fireNameClicked());
    root.appendChild(e);
    this.elements.progress = e;

    root.style.marginLeft = `${depth * 16}px`;

    this.refresh();
  }

  /// PUBLIC

  getElement() {
    return this.elements.root;
  }

  constructor(torrent, depth, name, indices, even) {
    super();

    this.fields = {
      have: 0,
      indices,
      isWanted: true,
      priorityHigh: false,
      priorityLow: false,
      priorityNormal: false,
      size: 0,
      torrent,
    };
    this.elements = {
      priority_high_button: null,
      priority_low_button: null,
      priority_normal_button: null,
      progress: null,
      root: null,
    };
    this.createRow(torrent, depth, name, even);
  }
}
