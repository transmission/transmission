/**
 * Copyright © Mnemosyne LLC
 *
 * This file is licensed under the GPLv2.
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 */

class FileRow {
  isDone() {
    return this.fields.have >= this.fields.size;
  }

  isEditable() {
    return this.fields.torrent.getFileCount() > 1 && !this.isDone();
  }

  refreshWantedHTML() {
    const e = $(this.elements.root);
    e.toggleClass('skip', !this.fields.isWanted);
    e.toggleClass('complete', this.isDone());
    $(e[0].checkbox).prop('disabled', !this.isEditable());
    $(e[0].checkbox).prop('checked', this.fields.isWanted);
  }

  refreshProgressHTML() {
    const pct = 100 * (this.fields.size ? this.fields.have / this.fields.size : 1.0);
    const c = [
      Transmission.fmt.size(this.fields.have),
      ' of ',
      Transmission.fmt.size(this.fields.size),
      ' (',
      Transmission.fmt.percentString(pct),
      '%)',
    ].join('');
    setTextContent(this.elements.progress, c);
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
        case 0:
          normal = true;
          break;
        case 1:
          high = true;
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
      $(this.elements.priority_low_button).toggleClass('selected', low);
    }

    if (this.fields.priorityNormal !== normal) {
      this.fields.priorityNormal = normal;
      $(this.elements.priority_normal_button).toggleClass('selected', normal);
    }

    if (this.fields.priorityHigh !== high) {
      this.fields.priorityHigh = high;
      $(this.elements.priority_high_button).toggleClass('selected', high);
    }
  }

  fireWantedChanged(do_want) {
    $(this.fields.me).trigger('wantedToggled', [this.fields.indices, do_want]);
  }

  firePriorityChanged(priority) {
    $(this.fields.me).trigger('priorityToggled', [this.fields.indices, priority]);
  }

  fireNameClicked() {
    $(this.fields.me).trigger('nameClicked', [this.fields.me, this.fields.indices]);
  }

  createRow(torrent, depth, name, even) {
    const root = document.createElement('li');
    root.className = `inspector_torrent_file_list_entry${even ? 'even' : 'odd'}`;
    this.elements.root = root;

    let e = document.createElement('input');
    e.type = 'checkbox';
    e.className = 'file_wanted_control';
    e.title = 'Download file';
    $(e).change(function (ev) {
      this.fireWantedChanged($(ev.currentTarget).prop('checked'));
    });
    root.checkbox = e;
    root.appendChild(e);

    e = document.createElement('div');
    e.className = 'file-priority-radiobox';
    const box = e;

    e = document.createElement('div');
    e.className = 'low';
    e.title = 'Low Priority';
    $(e).click(function () {
      this.firePriorityChanged(-1);
    });
    this.elements.priority_low_button = e;
    box.appendChild(e);

    e = document.createElement('div');
    e.className = 'normal';
    e.title = 'Normal Priority';
    $(e).click(function () {
      this.firePriorityChanged(0);
    });
    this.elements.priority_normal_button = e;
    box.appendChild(e);

    e = document.createElement('div');
    e.title = 'High Priority';
    e.className = 'high';
    $(e).click(function () {
      this.firePriorityChanged(1);
    });
    this.elements.priority_high_button = e;
    box.appendChild(e);

    root.appendChild(box);

    e = document.createElement('div');
    e.className = 'inspector_torrent_file_list_entry_name';
    setTextContent(e, name);
    $(e).click(this.fireNameClicked);
    root.appendChild(e);

    e = document.createElement('div');
    e.className = 'inspector_torrent_file_list_entry_progress';
    root.appendChild(e);
    $(e).click(this.fireNameClicked);
    this.elements.progress = e;

    $(root).css('margin-left', `${depth * 16}px`);

    this.refresh();
  }

  /// PUBLIC

  getElement() {
    return this.elements.root;
  }

  constructor(torrent, depth, name, indices, even) {
    this.fields = {
      have: 0,
      indices,
      isWanted: true,
      me: this,
      priorityHigh: false,
      priorityLow: false,
      priorityNormal: false,
      size: 0,
      torrent,
    };
    this.elements = {
      priority_low_button: null,
      priority_normal_button: null,
      priority_high_button: null,
      progress: null,
      root: null,
    };
    this.createRow(torrent, depth, name, even);
  }
}
