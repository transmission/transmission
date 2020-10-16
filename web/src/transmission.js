/**
 * Copyright © Charles Kerr, Dave Perrett, Malcolm Jarvis and Bruno Bierbaumer
 *
 * This file is licensed under the GPLv2.
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 */

import { AboutDialog } from './about-dialog.js';
import { ContextMenu } from './context-menu.js';
import { Formatter } from './formatter.js';
import { Inspector } from './inspector.js';
import { MoveDialog } from './move-dialog.js';
import { OpenDialog } from './open-dialog.js';
import { OverflowMenu } from './overflow-menu.js';
import { Prefs } from './prefs.js';
import { PrefsDialog } from './prefs-dialog.js';
import { Remote, RPC } from './remote.js';
import { RemoveDialog } from './remove-dialog.js';
import { RenameDialog } from './rename-dialog.js';
import { ShortcutsDialog } from './shortcuts-dialog.js';
import { StatisticsDialog } from './statistics-dialog.js';
import { Torrent } from './torrent.js';
import {
  TorrentRow,
  TorrentRendererCompact,
  TorrentRendererFull,
} from './torrent-row.js';
import { setEnabled, movePopup, Utils } from './utils.js';

export class Transmission extends EventTarget {
  constructor(action_manager, notifications, prefs) {
    super();

    // Initialize the helper classes
    this.action_manager = action_manager;
    this.notifications = notifications;
    this.prefs = prefs;
    this.remote = new Remote(this);

    this.addEventListener('torrent-selection-changed', (ev) =>
      this.action_manager.update(ev)
    );

    // Initialize the implementation fields
    this.filterText = '';
    this._torrents = {};
    this._rows = [];
    this.dirtyTorrents = new Set();
    this.uriCache = {};

    this.refilterSoon = Utils.debounce(() => this._refilter(false));
    this.refilterAllSoon = Utils.debounce(() => this._refilter(true));

    this.boundPopupCloseListener = this.popupCloseListener.bind(this);
    this.dispatchSelectionChangedSoon = Utils.debounce(
      () => this._dispatchSelectionChanged(),
      200
    );

    // listen to actions
    // TODO: consider adding a mutator listener here to pick up dynamic additions
    for (const el of document.querySelectorAll(`button[data-action]`)) {
      const { action } = el.dataset;
      setEnabled(el, this.action_manager.isEnabled(action));
      el.addEventListener('click', () => {
        this.action_manager.click(action);
      });
    }
    for (const el of document.querySelectorAll(`input[data-action]`)) {
      const { action } = el.dataset;
      setEnabled(el, this.action_manager.isEnabled(action));
      el.addEventListener('change', () => {
        this.action_manager.click(action);
      });
    }

    document
      .getElementById('filter-tracker')
      .addEventListener('change', (ev) => {
        this.setFilterTracker(
          ev.target.value === 'all' ? null : ev.target.value
        );
      });

    this.action_manager.addEventListener('change', (ev) => {
      for (const el of document.querySelectorAll(
        `[data-action="${ev.action}"]`
      )) {
        setEnabled(el, ev.enabled);
      }
    });

    this.action_manager.addEventListener('click', (ev) => {
      switch (ev.action) {
        case 'deselect-all':
          this._deselectAll();
          break;
        case 'move-bottom':
          this._moveBottom();
          break;
        case 'move-down':
          this._moveDown();
          break;
        case 'move-top':
          this._moveTop();
          break;
        case 'move-up':
          this._moveUp();
          break;
        case 'open-torrent':
          this.setCurrentPopup(new OpenDialog(this, this.remote));
          break;
        case 'pause-all-torrents':
          this._stopTorrents(this._getAllTorrents());
          break;
        case 'pause-selected-torrents':
          this._stopTorrents(this.getSelectedTorrents());
          break;
        case 'reannounce-selected-torrents':
          this._reannounceTorrents(this.getSelectedTorrents());
          break;
        case 'remove-selected-torrents':
          this._removeSelectedTorrents(false);
          break;
        case 'resume-selected-torrents':
          this._startSelectedTorrents(false);
          break;
        case 'resume-selected-torrents-now':
          this._startSelectedTorrents(true);
          break;
        case 'select-all':
          this._selectAll();
          break;
        case 'show-about-dialog':
          this.setCurrentPopup(new AboutDialog(this.version_info));
          break;
        case 'show-inspector':
          this.setCurrentPopup(new Inspector(this));
          break;
        case 'show-move-dialog':
          this.setCurrentPopup(new MoveDialog(this, this.remote));
          break;
        case 'show-overflow-menu':
          {
            this.setCurrentPopup(
              new OverflowMenu(
                this,
                this.prefs,
                this.remote,
                this.action_manager
              )
            );
            const btnbox = document
              .getElementById('toolbar-overflow')
              .getBoundingClientRect();
            movePopup(
              this.popup.root,
              btnbox.left + btnbox.width,
              btnbox.top + btnbox.height,
              document.body
            );
          }
          break;
        case 'show-preferences-dialog':
          this.setCurrentPopup(new PrefsDialog(this, this.remote));
          break;
        case 'show-shortcuts-dialog':
          this.setCurrentPopup(new ShortcutsDialog(this.action_manager));
          break;
        case 'show-statistics-dialog':
          this.setCurrentPopup(new StatisticsDialog(this.remote));
          break;
        case 'show-rename-dialog':
          this.setCurrentPopup(new RenameDialog(this, this.remote));
          break;
        case 'start-all-torrents':
          this._startTorrents(this._getAllTorrents());
          break;
        case 'toggle-compact-rows':
          this.prefs.display_mode =
            this.prefs.display_mode !== Prefs.DisplayCompact
              ? Prefs.DisplayCompact
              : Prefs.DisplayFull;
          break;
        case 'trash-selected-torrents':
          this._removeSelectedTorrents(true);
          break;
        case 'verify-selected-torrents':
          this._verifyTorrents(this.getSelectedTorrents());
          break;
        default:
          console.warn(`unhandled action: ${ev.action}`);
      }
    });

    // listen to filter changes
    let e = document.getElementById('filter-mode');
    e.value = this.prefs.filter_mode;
    e.addEventListener('change', (ev) => {
      this.prefs.filter_mode = ev.target.value;
    });

    //if (!isMobileDevice) {
    document.addEventListener('keydown', this._keyDown.bind(this));
    document.addEventListener('keyup', this._keyUp.bind(this));
    e = document.getElementById('torrent-container');
    e.addEventListener('click', () => {
      // FIXME: :thinking:
      if (this.popup && this.popup.name !== 'inspector') {
        this.setCurrentPopup(null);
      } else {
        this._deselectAll();
      }
    });
    e.addEventListener('dragenter', Transmission._dragenter);
    e.addEventListener('dragover', Transmission._dragenter);
    e.addEventListener('drop', this._drop.bind(this));
    this._setupSearchBox();
    //}

    this.elements = {
      torrent_list: document.getElementById('torrent-list'),
    };

    this.elements.torrent_list.addEventListener('contextmenu', (ev) => {
      // ensure the clicked row is selected
      let row_element = event.target;
      while (row_element && !row_element.classList.contains('torrent')) {
        row_element = row_element.parentNode;
      }
      const row = this._rows.find((r) => r.getElement() === row_element);
      if (row && !row.isSelected()) {
        this._setSelectedRow(row);
      }

      const popup = new ContextMenu(this.action_manager);
      this.setCurrentPopup(popup);
      movePopup(
        popup.root,
        ev.x,
        ev.y,
        document.getElementById('torrent-container')
      );
      ev.preventDefault();
    });

    // Get preferences & torrents from the daemon
    this.loadDaemonPrefs();
    this._initializeTorrents();
    this.refreshTorrents();
    this.togglePeriodicSessionRefresh(true);

    // this.updateButtonsSoon();

    this.prefs.addEventListener('change', ({ key, value }) =>
      this._onPrefChanged(key, value)
    );
    this.prefs
      .entries()
      .forEach(([key, value]) => this._onPrefChanged(key, value));
  }

  loadDaemonPrefs() {
    this.remote.loadDaemonPrefs((data) => {
      this.session_properties = data.arguments;
    });
  }

  get session_properties() {
    return this._session_properties;
  }
  set session_properties(o) {
    if (JSON.stringify(this._session_properties) !== JSON.stringify(o)) {
      this._session_properties = Object.seal(o);
      const event = new Event('session-change');
      event.session_properties = o;
      this.dispatchEvent(event);

      // TODO: maybe have this in a listener handler?
      this._updateGuiFromSession(o);
    }
  }

  _setupSearchBox() {
    const e = document.getElementById('torrent-search');
    const blur_token = 'blur';
    e.classList.add(blur_token);
    e.addEventListener('blur', () => e.classList.add(blur_token));
    e.addEventListener('focus', () => e.classList.remove(blur_token));
    e.addEventListener('keyup', () => this._setFilterText(e.value));
  }

  _onPrefChanged(key, value) {
    switch (key) {
      case Prefs.DisplayMode: {
        this.torrentRenderer =
          value === 'compact'
            ? new TorrentRendererCompact()
            : new TorrentRendererFull();
        this.refilterAllSoon();
        break;
      }

      case Prefs.FilterMode:
      case Prefs.SortDirection:
      case Prefs.SortMode:
        this.refilterAllSoon();
        break;

      case Prefs.RefreshRate: {
        clearInterval(this.refreshTorrentsInterval);
        const callback = this.refreshTorrents.bind(this);
        const msec = Math.max(2, this.prefs.refresh_rate_sec) * 1000;
        this.refreshTorrentsInterval = setInterval(callback, msec);
        break;
      }

      case Prefs.AltSpeedEnabled:
      case Prefs.NotificationsEnabled:
      default:
        /*noop*/
        break;
    }
  }

  /// UTILITIES

  _getAllTorrents() {
    return Object.values(this._torrents);
  }

  static _getTorrentIds(torrents) {
    return torrents.map((t) => t.getId());
  }

  seedRatioLimit() {
    const p = this.session_properties;
    if (p && p.seedRatioLimited) {
      return p.seedRatioLimit;
    }
    return -1;
  }

  /// SELECTION

  _getSelectedRows() {
    return this._rows.filter((r) => r.isSelected());
  }

  getSelectedTorrents() {
    return this._getSelectedRows().map((r) => r.getTorrent());
  }

  _getSelectedTorrentIds() {
    return Transmission._getTorrentIds(this.getSelectedTorrents());
  }

  _setSelectedRow(row) {
    const e_sel = row ? row.getElement() : null;
    for (const e of this.elements.torrent_list.children) {
      e.classList.toggle('selected', e === e_sel);
    }
    this.dispatchSelectionChangedSoon();
  }

  _selectRow(row) {
    row.getElement().classList.add('selected');
    this.dispatchSelectionChangedSoon();
  }

  _deselectRow(row) {
    row.getElement().classList.remove('selected');
    this.dispatchSelectionChangedSoon();
  }

  _selectAll() {
    for (const e of this.elements.torrent_list.children) {
      e.classList.add('selected');
    }
    this.dispatchSelectionChangedSoon();
  }

  _deselectAll() {
    for (const e of this.elements.torrent_list.children) {
      e.classList.remove('selected');
    }
    this.dispatchSelectionChangedSoon();
    delete this._last_torrent_clicked;
  }

  _indexOfLastTorrent() {
    return this._rows.findIndex(
      (row) => row.getTorrentId() === this._last_torrent_clicked
    );
  }

  // Select a range from this row to the last clicked torrent
  _selectRange(row) {
    const last = this._indexOfLastTorrent();

    if (last === -1) {
      this._selectRow(row);
    } else {
      // select the range between the prevous & current
      const next = this._rows.indexOf(row);
      const min = Math.min(last, next);
      const max = Math.max(last, next);
      for (let i = min; i <= max; ++i) {
        this._selectRow(this._rows[i]);
      }
    }

    this.dispatchSelectionChangedSoon();
  }

  _dispatchSelectionChanged() {
    const nonselected = [];
    const selected = [];
    this._rows.forEach((r) =>
      (r.isSelected() ? selected : nonselected).push(r.getTorrent())
    );

    const event = new Event('torrent-selection-changed');
    event.nonselected = nonselected;
    event.selected = selected;
    this.dispatchEvent(event);
  }

  /*--------------------------------------------
   *
   *  E V E N T   F U N C T I O N S
   *
   *--------------------------------------------*/

  static _createKeyShortcutFromKeyboardEvent(ev) {
    const a = [];
    if (ev.ctrlKey) {
      a.push('Control');
    }
    if (ev.altKey) {
      a.push('Alt');
    }
    if (ev.metaKey) {
      a.push('Meta');
    }
    if (ev.shitKey) {
      a.push('Shift');
    }
    a.push(ev.key.length === 1 ? ev.key.toUpperCase() : ev.key);
    return a.join('+');
  }

  // Process key events
  _keyDown(ev) {
    const { keyCode } = ev;

    // look for a shortcut
    const aria_keys = Transmission._createKeyShortcutFromKeyboardEvent(ev);
    const action = this.action_manager.getActionForShortcut(aria_keys);
    if (action) {
      ev.preventDefault();
      this.action_manager.click(action);
      return;
    }

    const esc_key = keyCode === 27; // esc key pressed
    if (esc_key && this.popup) {
      this.setCurrentPopup(null);
      ev.preventDefault();
      return;
    }

    const any_popup_active = document.querySelector('.popup:not(.hidden)');
    const is_input_focused = ev.target.matches('input');
    const rows = this._rows;

    // Some shortcuts can only be used if the following conditions are met:
    // 1. when no input fields are focused
    // 2. when no other dialogs are visible
    // 3. when the meta or ctrl key isn't pressed (i.e. opening dev tools shouldn't trigger the info panel)
    if (!is_input_focused && !any_popup_active && !ev.metaKey && !ev.ctrlKey) {
      const shift_key = keyCode === 16; // shift key pressed
      const up_key = keyCode === 38; // up key pressed
      const dn_key = keyCode === 40; // down key pressed
      if ((up_key || dn_key) && rows.length) {
        const last = this._indexOfLastTorrent();
        const anchor = this._shift_index;
        const min = 0;
        const max = rows.length - 1;
        let i = last;

        if (dn_key && i + 1 <= max) {
          ++i;
        } else if (up_key && i - 1 >= min) {
          --i;
        }

        const r = rows[i];

        if (anchor >= 0) {
          // user is extending the selection
          // with the shift + arrow keys...
          if ((anchor <= last && last < i) || (anchor >= last && last > i)) {
            this._selectRow(r);
          } else if (
            (anchor >= last && i > last) ||
            (anchor <= last && last > i)
          ) {
            this._deselectRow(rows[last]);
          }
        } else {
          if (ev.shiftKey) {
            this._selectRange(r);
          } else {
            this._setSelectedRow(r);
          }
        }
        if (r) {
          this._last_torrent_clicked = r.getTorrentId();
          r.getElement().scrollIntoView();
          ev.preventDefault();
        }
      } else if (shift_key) {
        this._shift_index = this._indexOfLastTorrent();
      }
    }
  }

  _keyUp(ev) {
    if (ev.keyCode === 16) {
      // shift key pressed
      delete this._shift_index;
    }
  }

  static _dragenter(ev) {
    if (ev.dataTransfer && ev.dataTransfer.types) {
      const copy_types = ['text/uri-list', 'text/plain'];
      if (ev.dataTransfer.types.some((type) => copy_types.includes(type))) {
        ev.stopPropagation();
        ev.preventDefault();
        ev.dataTransfer.dropEffect = 'copy';
        return false;
      }
    } else if (ev.dataTransfer) {
      ev.dataTransfer.dropEffect = 'none';
    }
    return true;
  }

  static _isValidURL(str) {
    try {
      const url = new URL(str);
      return url !== null;
    } catch (e) {
      return false;
    }
  }

  _drop(ev) {
    const paused = true; // FIXME this.shouldAddedTorrentsStart();

    if (!ev.dataTransfer || !ev.dataTransfer.types) {
      return true;
    }

    const type = ev.data.Transfer.types
      .filter((t) => ['text/uri-list', 'text/plain'].contains(t))
      .pop();
    ev.dataTransfer
      .getData(type)
      .split('\n')
      .map((str) => str.trim())
      .filter((str) => Transmission._isValidURL(str))
      .forEach((uri) => this.remote.addTorrentByUrl(uri, paused));

    ev.preventDefault();
    return false;
  }

  // turn the periodic ajax session refresh on & off
  togglePeriodicSessionRefresh(enabled) {
    if (!enabled && this.sessionInterval) {
      clearInterval(this.sessionInterval);
      delete this.sessionInterval;
    }
    if (enabled) {
      this.loadDaemonPrefs();
      if (!this.sessionInterval) {
        const msec = 8000;
        this.sessionInterval = setInterval(
          this.loadDaemonPrefs.bind(this),
          msec
        );
      }
    }
  }

  _setFilterText(search) {
    this.filterText = search ? search.trim() : null;
    this.refilterAllSoon();
  }

  _onTorrentChanged(ev) {
    // update our dirty fields
    const tor = ev.currentTarget;
    this.dirtyTorrents.add(tor.getId());

    // enqueue ui refreshes
    this.refilterSoon();
  }

  updateTorrents(ids, fields) {
    this.remote.updateTorrents(ids, fields, (table, removed_ids) => {
      const needinfo = [];

      const keys = table.shift();
      const o = {};
      for (const row of table) {
        keys.forEach((key, idx) => {
          o[key] = row[idx];
        });
        const { id } = o;
        let t = this._torrents[id];
        if (t) {
          const needed = t.needsMetaData();
          t.refresh(o);
          if (needed && !t.needsMetaData()) {
            needinfo.push(id);
          }
        } else {
          t = this._torrents[id] = new Torrent(o);
          t.addEventListener('dataChanged', this._onTorrentChanged.bind(this));
          this.dirtyTorrents.add(id);
          // do we need more info for this torrent?
          if (!('name' in t.fields) || !('status' in t.fields)) {
            needinfo.push(id);
          }
        }
      }

      if (needinfo.length) {
        // whee, new torrents! get their initial information.
        const more_fields = ['id'].concat(
          Torrent.Fields.Metadata,
          Torrent.Fields.Stats
        );
        this.updateTorrents(needinfo, more_fields);
        this.refilterSoon();
      }

      if (removed_ids) {
        this._deleteTorrents(removed_ids);
        this.refilterSoon();
      }
    });
  }
  /*
FIXME: fix this when notifications get fixed
        t.notifyOnFieldChange('status', (newValue, oldValue) => {
          if (
            oldValue === Torrent._StatusDownload &&
            (newValue === Torrent._StatusSeed || newValue === Torrent._StatusSeedWait)
          ) {
            $(this).trigger('downloadComplete', [t]);
          } else if (
            oldValue === Torrent._StatusSeed &&
            newValue === Torrent._StatusStopped &&
            t.isFinished()
          ) {
            $(this).trigger('seedingComplete', [t]);
          } else {
            $(this).trigger('statusChange', [t]);
          }
        });
*/

  refreshTorrents() {
    const fields = ['id'].concat(Torrent.Fields.Stats);
    this.updateTorrents('recently-active', fields);
  }

  _initializeTorrents() {
    const fields = ['id'].concat(Torrent.Fields.Metadata, Torrent.Fields.Stats);
    this.updateTorrents(null, fields);
  }

  _onRowClicked(ev) {
    const meta_key = ev.metaKey || ev.ctrlKey,
      { row } = ev.currentTarget;

    if (this.popup && this.popup.name !== 'inspector') {
      this.setCurrentPopup(null);
      return;
    }

    // handle the per-row "torrent-resume" button
    if (ev.target.classList.contains('torrent-resume')) {
      this._startTorrents([row.getTorrent()]);
      return;
    }

    // handle the per-row "torrent-pause" button
    if (ev.target.classList.contains('torrent-pause')) {
      this._stopTorrents([row.getTorrent()]);
      return;
    }

    // Prevents click carrying to parent element
    // which deselects all on click
    ev.stopPropagation();

    // TODO: long-click should raise inspector
    if (ev.shiftKey) {
      this._selectRange(row);
      // Need to deselect any selected text
      window.focus();

      // Apple-Click, not selected
    } else if (!row.isSelected() && meta_key) {
      this._selectRow(row);

      // Regular Click, not selected
    } else if (!row.isSelected()) {
      this._setSelectedRow(row);

      // Apple-Click, selected
    } else if (row.isSelected() && meta_key) {
      this._deselectRow(row);

      // Regular Click, selected
    } else if (row.isSelected()) {
      this._setSelectedRow(row);
    }

    this._last_torrent_clicked = row.getTorrentId();
  }

  _deleteTorrents(ids) {
    if (ids && ids.length) {
      for (const id of ids) {
        this.dirtyTorrents.add(id);
        delete this._torrents[id];
      }
      this.refilterSoon();
    }
  }

  _removeSelectedTorrents(trash) {
    const torrents = this.getSelectedTorrents();
    if (torrents.length) {
      this.setCurrentPopup(
        new RemoveDialog({ remote: this.remote, torrents, trash })
      );
    }
  }

  _startSelectedTorrents(force) {
    this._startTorrents(this.getSelectedTorrents(), force);
  }

  _startTorrents(torrents, force) {
    this.remote.startTorrents(
      Transmission._getTorrentIds(torrents),
      force,
      this.refreshTorrents,
      this
    );
  }
  _verifyTorrents(torrents) {
    this.remote.verifyTorrents(
      Transmission._getTorrentIds(torrents),
      this.refreshTorrents,
      this
    );
  }

  _reannounceTorrents(torrents) {
    this.remote.reannounceTorrents(
      Transmission._getTorrentIds(torrents),
      this.refreshTorrents,
      this
    );
  }

  _stopTorrents(torrents) {
    this.remote.stopTorrents(
      Transmission._getTorrentIds(torrents),
      this.refreshTorrents,
      this
    );
  }
  changeFileCommand(torrentId, rowIndices, command) {
    this.remote.changeFileCommand(torrentId, rowIndices, command);
  }

  // Queue
  _moveTop() {
    this.remote.moveTorrentsToTop(
      this._getSelectedTorrentIds(),
      this.refreshTorrents,
      this
    );
  }
  _moveUp() {
    this.remote.moveTorrentsUp(
      this._getSelectedTorrentIds(),
      this.refreshTorrents,
      this
    );
  }
  _moveDown() {
    this.remote.moveTorrentsDown(
      this._getSelectedTorrentIds(),
      this.refreshTorrents,
      this
    );
  }
  _moveBottom() {
    this.remote.moveTorrentsToBottom(
      this._getSelectedTorrentIds(),
      this.refreshTorrents,
      this
    );
  }

  ///

  _updateGuiFromSession(o) {
    const [, version, checksum] = o.version.match(/(.*)\s\(([0-9a-f]+)\)/);
    this.version_info = {
      checksum,
      version,
    };

    const el = document.getElementById('toolbar-overflow');
    el.classList.toggle('alt-speed-enabled', o[RPC._TurtleState]);
  }

  _updateStatusbar() {
    const fmt = Formatter;
    const torrents = this._getAllTorrents();

    const u = torrents.reduce((acc, tor) => acc + tor.getUploadSpeed(), 0);
    const d = torrents.reduce((acc, tor) => acc + tor.getDownloadSpeed(), 0);
    const str = fmt.countString('Transfer', 'Transfers', this._rows.length);

    document.getElementById('speed-up-label').textContent = fmt.speedBps(u);
    document.getElementById('speed-dn-label').textContent = fmt.speedBps(d);
    document.getElementById('filter-count').textContent = str;
  }

  _updateFilterSelect() {
    const trackers = this._getTrackers();
    const names = Object.keys(trackers).sort();

    // build the new html
    let str = '';
    if (!this.filterTracker) {
      str += '<option value="all" selected="selected">All</option>';
    } else {
      str += '<option value="all">All</option>';
    }
    for (const name of names) {
      const o = trackers[name];
      str += `<option value="${o.domain}"`;
      if (trackers[name].domain === this.filterTracker) {
        str += ' selected="selected"';
      }
      str += `>${name}</option>`;
    }

    if (!this.filterTrackersStr || this.filterTrackersStr !== str) {
      this.filterTrackersStr = str;
      document.getElementById('filter-tracker').innerHTML = str;
    }
  }

  /// FILTER

  sortRows(rows) {
    const torrents = rows.map((row) => row.getTorrent());
    const id2row = rows.reduce((acc, row) => {
      acc[row.getTorrent().getId()] = row;
      return acc;
    }, {});
    Torrent.sortTorrents(
      torrents,
      this.prefs.sort_mode,
      this.prefs.sort_direction
    );
    torrents.forEach((tor, idx) => (rows[idx] = id2row[tor.getId()]));
  }

  _refilter(rebuildEverything) {
    const { sort_mode, sort_direction, filter_mode } = this.prefs;
    const filter_text = this.filterText;
    const filter_tracker = this.filterTracker;
    const renderer = this.torrentRenderer;
    const list = this.elements.torrent_list;

    const countRows = () => [...list.children].length;
    const countSelectedRows = () =>
      [...list.children].reduce(
        (n, e) => (n + e.classList.contains('selected') ? 1 : 0),
        0
      );
    const old_row_count = countRows();
    const old_sel_count = countSelectedRows();

    this._updateFilterSelect();

    clearTimeout(this.refilterTimer);
    delete this.refilterTimer;

    if (rebuildEverything) {
      while (list.firstChild) {
        list.removeChild(list.firstChild);
      }
      this._rows = [];
      this.dirtyTorrents = new Set(Object.keys(this._torrents));
    }

    // rows that overlap with dirtyTorrents need to be refiltered.
    // those that don't are 'clean' and don't need refiltering.
    const clean_rows = [];
    let dirty_rows = [];
    for (const row of this._rows) {
      if (this.dirtyTorrents.has(row.getTorrentId())) {
        dirty_rows.push(row);
      } else {
        clean_rows.push(row);
      }
    }

    // remove the dirty rows from the dom
    for (const row of dirty_rows) {
      row.getElement().remove();
    }

    // drop any dirty rows that don't pass the filter test
    const tmp = [];
    for (const row of dirty_rows) {
      const id = row.getTorrentId();
      const t = this._torrents[id];
      if (t && t.test(filter_mode, filter_text, filter_tracker)) {
        tmp.push(row);
      }
      this.dirtyTorrents.delete(id);
    }
    dirty_rows = tmp;

    // make new rows for dirty torrents that pass the filter test
    // but don't already have a row
    for (const id of this.dirtyTorrents.values()) {
      const t = this._torrents[id];
      if (t && t.test(filter_mode, filter_text, filter_tracker)) {
        const row = new TorrentRow(renderer, this, t);
        const e = row.getElement();
        e.row = row;
        dirty_rows.push(row);
        e.addEventListener('click', this._onRowClicked.bind(this));
        e.addEventListener('dblclick', () =>
          this.action_manager.click('show-inspector')
        );
      }
    }

    // sort the dirty rows
    this.sortRows(dirty_rows);

    // now we have two sorted arrays of rows
    // and can do a simple two-way sorted merge.
    const rows = [];
    const cmax = clean_rows.length;
    const dmax = dirty_rows.length;
    const frag = document.createDocumentFragment();
    let ci = 0;
    let di = 0;
    while (ci !== cmax || di !== dmax) {
      let push_clean = null;
      if (ci === cmax) {
        push_clean = false;
      } else if (di === dmax) {
        push_clean = true;
      } else {
        const c = Torrent.compareTorrents(
          clean_rows[ci].getTorrent(),
          dirty_rows[di].getTorrent(),
          sort_mode,
          sort_direction
        );
        push_clean = c < 0;
      }

      if (push_clean) {
        rows.push(clean_rows[ci++]);
      } else {
        const row = dirty_rows[di++];
        const e = row.getElement();

        if (ci !== cmax) {
          list.insertBefore(e, clean_rows[ci].getElement());
        } else {
          frag.appendChild(e);
        }

        rows.push(row);
      }
    }
    list.appendChild(frag);

    // update our implementation fields
    this._rows = rows;
    this.dirtyTorrents.clear();

    // set the odd/even property
    rows
      .map((row) => row.getElement())
      .forEach((e, idx) => {
        const even = idx % 2 === 0;
        e.classList.toggle('even', even);
        e.classList.toggle('odd', !even);
      });

    this._updateStatusbar();
    if (
      old_sel_count !== countSelectedRows() ||
      old_row_count !== countRows()
    ) {
      this.dispatchSelectionChangedSoon();
    }
  }

  setFilterTracker(domain) {
    const e = document.getElementById('filter-tracker');
    e.value = domain ? Transmission._getReadableDomain(domain) : 'all';

    this.filterTracker = domain;
    this.refilterAllSoon();
  }

  // example: "tracker.ubuntu.com" returns "ubuntu.com"
  static _getDomainName(host) {
    const dot = host.indexOf('.');
    if (dot !== host.lastIndexOf('.')) {
      host = host.slice(dot + 1);
    }

    return host;
  }

  // example: "ubuntu.com" returns "Ubuntu"
  static _getReadableDomain(name) {
    if (name.length) {
      name = name.charAt(0).toUpperCase() + name.slice(1);
    }
    const dot = name.indexOf('.');
    if (dot !== -1) {
      name = name.slice(0, dot);
    }
    return name;
  }

  _getTrackers() {
    const ret = {};

    for (const torrent of this._getAllTorrents()) {
      const names = new Set();

      for (const tracker of torrent.getTrackers()) {
        const { announce } = tracker;

        let uri = this.uriCache[announce];
        if (!uri) {
          uri = this.uriCache[announce] = new URL(announce);
          uri.domain = Transmission._getDomainName(uri.host);
          uri.name = Transmission._getReadableDomain(uri.domain);
        }

        if (!ret[uri.name]) {
          ret[uri.name] = {
            count: 0,
            domain: uri.domain,
            uri,
          };
        }

        names.add(uri.name);
      }

      for (const name of names.values()) {
        ++ret[name].count;
      }
    }

    return ret;
  }

  ///

  popupCloseListener(ev) {
    if (ev.target !== this.popup) {
      throw new Error(ev);
    }
    this.popup.removeEventListener('close', this.boundPopupCloseListener);
    delete this.popup;
  }

  setCurrentPopup(popup) {
    if (this.popup) {
      this.popup.close();
    }

    this.popup = popup;

    if (this.popup) {
      this.popup.addEventListener('close', this.boundPopupCloseListener);
    }
  }
}
