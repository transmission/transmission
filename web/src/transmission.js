/* @license This file Copyright © Charles Kerr, Dave Perrett, Malcolm Jarvis and Bruno Bierbaumer
   It may be used under GPLv2 (SPDX: GPL-2.0-only).
   License text can be found in the licenses/ folder. */

import { AboutDialog } from './about-dialog.js';
import { Appearance } from './appearance-settings.js';
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
import { LabelsDialog } from './labels-dialog.js';
import { ShortcutsDialog } from './shortcuts-dialog.js';
import { StatisticsDialog } from './statistics-dialog.js';
import { Torrent } from './torrent.js';
import {
  TorrentRendererCompact,
  TorrentRendererFull,
} from './torrent-row.js';
import {
  newOpts,
  icon,
  debounce,
  deepEqual,
  setEnabled,
  setTextContent,
} from './utils.js';
import Clusterize from 'clusterize.js';

export class Transmission extends EventTarget {
  constructor(action_manager, notifications, prefs) {
    super();

    // Initialize the helper classes
    this.action_manager = action_manager;
    this.handler = null;
    this.notifications = notifications;
    this.prefs = prefs;
    this.remote = new Remote(this);
    this.speed = {
      down: document.querySelector('#speed-down'),
      up: document.querySelector('#speed-up'),
    };

    for (const [selector, name] of [
      ['#toolbar-open', 'open'],
      ['#toolbar-delete', 'delete'],
      ['#toolbar-start', 'start'],
      ['#toolbar-pause', 'pause'],
      ['#toolbar-inspector', 'inspector'],
      ['#toolbar-overflow', 'overflow'],
    ]) {
      document
        .querySelector(selector)
        .prepend(icon[name](), document.createElement('BR'));
    }

    document.querySelector('.speed-container').append(icon.speedDown());
    document
      .querySelector('.speed-container + .speed-container')
      .append(icon.speedUp());

    this.addEventListener('torrent-selection-changed', (event_) =>
      this.action_manager.update(event_),
    );

    // Initialize the implementation fields
    this.filterText = '';
    this._torrents = {};
    this.oldTrackers = [];
    this.dirtyTorrents = new Set();
    this._selectedTorrentIds = new Set(); // Track selected torrents by ID
    this._torrentOrder = []; // Track torrent display order
    this.clusterize = null; // Will be initialized later

    this.changeStatus = false;
    this.refilterSoon = debounce(() => this._refilter());
    this.refilterAllSoon = debounce(() => this._refilter());

    this.pointer_device = Object.seal({
      is_touch_device: 'ontouchstart' in globalThis,
      long_press_callback: null,
      x: 0,
      y: 0,
    });
    this.popup = Array.from({ length: Transmission.max_popups }).fill(null);

    this.busytyping = false;

    // listen to actions
    // TODO: consider adding a mutator listener here to see dynamic additions
    for (const element of document.querySelectorAll(`button[data-action]`)) {
      const { action } = element.dataset;
      setEnabled(element, this.action_manager.isEnabled(action));
      element.addEventListener('click', () => {
        this.action_manager.click(action);
      });
    }

    document
      .querySelector('#filter-tracker')
      .addEventListener('change', (event_) => {
        this.setFilterTracker(event_.target.value);
      });

    this.action_manager.addEventListener('change', (event_) => {
      for (const element of document.querySelectorAll(
        `[data-action="${event_.action}"]`,
      )) {
        setEnabled(element, event_.enabled);
      }
    });

    this.action_manager.addEventListener('click', (event_) => {
      switch (event_.action) {
        case 'copy-name':
          if (navigator.clipboard) {
            navigator.clipboard.writeText(this.handler.subtree.name);
          } else {
            // navigator.clipboard requires HTTPS or localhost
            // Emergency approach
            prompt('Select all then copy', this.handler.subtree.name);
          }
          this.handler.classList.remove('selected');
          break;
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
        case 'open-appearance-settings':
          if (
            this.popup[Transmission.default_popup_level] instanceof Appearance
          ) {
            this.popup[Transmission.default_popup_level].close();
          } else {
            this.setCurrentPopup(
              new Appearance(this.prefs, this.action_manager),
            );
          }
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
          if (this.popup[0] instanceof Inspector) {
            this.popup[0].close();
          } else {
            this.setCurrentPopup(new Inspector(this), 0);
          }
          break;
        case 'show-move-dialog':
          this.setCurrentPopup(new MoveDialog(this, this.remote));
          break;
        case 'show-overflow-menu':
          if (
            this.popup[Transmission.default_popup_level] instanceof OverflowMenu
          ) {
            this.popup[Transmission.default_popup_level].close();
          } else {
            this.setCurrentPopup(
              new OverflowMenu(
                this,
                this.prefs,
                this.remote,
                this.action_manager,
              ),
            );
          }
          break;
        case 'show-preferences-dialog':
          this.setCurrentPopup(new PrefsDialog(this, this.remote), 0);
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
        case 'show-labels-dialog':
          this.setCurrentPopup(new LabelsDialog(this, this.remote));
          break;
        case 'start-all-torrents':
          this._startTorrents(this._getAllTorrents());
          break;
        case 'toggle-compact-rows':
          this.prefs.display_mode =
            this.prefs.display_mode === Prefs.DisplayCompact
              ? Prefs.DisplayFull
              : Prefs.DisplayCompact;
          break;
        case 'trash-selected-torrents':
          this._removeSelectedTorrents(true);
          break;
        case 'verify-selected-torrents':
          this._verifyTorrents(this.getSelectedTorrents());
          break;
        default:
          console.warn(`unhandled action: ${event_.action}`);
      }
    });

    let e = document.querySelector('#filter-mode');
    // Initialize filter options
    newOpts(e, null, [['All', Prefs.FilterAll]]);
    newOpts(e, 'status', [
      ['Active', Prefs.FilterActive],
      ['Downloading', Prefs.FilterDownloading],
      ['Seeding', Prefs.FilterSeeding],
      ['Paused', Prefs.FilterPaused],
      ['Finished', Prefs.FilterFinished],
      ['Error', Prefs.FilterError],
    ]);
    newOpts(e, 'list', [
      ['Private torrents', Prefs.FilterPrivate],
      ['Public torrents', Prefs.FilterPublic],
    ]);

    // listen to filter changes
    e.value = this.prefs.filter_mode;
    e.addEventListener('change', (event_) => {
      this.prefs.filter_mode = event_.target.value;
      this.refilterAllSoon();
    });

    e = document.querySelector('#filter-tracker');
    newOpts(e, null, [['All', Prefs.FilterAll]]);

    const s = document.querySelector('#torrent-search');
    e = document.querySelector('#reset');
    e.addEventListener('click', () => {
      s.value = '';
      this._setFilterText(s.value);
      this.refilterAllSoon();
    });

    if (s.value.trim()) {
      this.filterText = s.value;
      e.style.display = 'block';
      this.refilterAllSoon();
    }

    e = document.querySelector('#turtle');
    e.addEventListener('click', (event_) => {
      this.remote.savePrefs({
        [RPC._TurtleState]:
          !event_.target.classList.contains('alt-speed-enabled'),
      });
    });

    document.addEventListener('keydown', this._keyDown.bind(this));
    document.addEventListener('keyup', this._keyUp.bind(this));
    e = document.querySelector('#torrent-container');
    e.addEventListener('click', (e_) => {
      if (this.popup[Transmission.default_popup_level]) {
        this.setCurrentPopup(null);
      }
      if (e_.target === e_.currentTarget) {
        this._deselectAll();
      }
    });
    e.addEventListener('dblclick', () => {
      if (!this.popup[0] || this.popup[0].name !== 'inspector') {
        this.action_manager.click('show-inspector');
      }
    });
    e.addEventListener('dragenter', Transmission._dragenter);
    e.addEventListener('dragover', Transmission._dragenter);
    e.addEventListener('drop', this._drop.bind(this));
    this._setupSearchBox();

    this.elements = {
      torrent_container: document.querySelector('#torrent-container'),
      torrent_list: document.querySelector('#torrent-list'),
    };


    const context_menu = () => {
      // open context menu
      const popup = new ContextMenu(this.action_manager);
      this.setCurrentPopup(popup);

      const boundingElement = document.querySelector('#torrent-container');
      const bounds = boundingElement.getBoundingClientRect();
      const x = Math.min(
        this.pointer_device.x,
        bounds.right + globalThis.scrollX - popup.root.clientWidth,
      );
      const y = Math.min(
        this.pointer_device.y,
        bounds.bottom + globalThis.scrollY - popup.root.clientHeight,
      );
      popup.root.style.left = `${Math.max(x, 0)}px`;
      popup.root.style.top = `${Math.max(y, 0)}px`;
    };

    // Setup clusterize for virtual scrolling
    this._initializeClusterize();

    const right_click = (event_) => {
      // if not already, highlight the torrent
      let row_element = event_.target;
      while (row_element && !row_element.classList.contains('torrent')) {
        row_element = row_element.parentNode;
      }

      // Find torrent by data-torrent-id instead of row object
      const torrentId = row_element?.dataset?.torrentId;
      if (torrentId) {
        const torrentIdNum = Number.parseInt(torrentId, 10);
        if (!this._selectedTorrentIds.has(torrentIdNum)) {
          this._setSelectedTorrent(torrentIdNum);
          this._last_torrent_clicked = torrentIdNum;
        }
      }

      if (this.handler) {
        this.handler.classList.remove('selected');
        this.handler = null;
      }

      this.context_menu('#torrent-container');
      event_.preventDefault();
    };

    this.pointer_event(this.elements.torrent_list, right_click);

    this.elements.torrent_list.addEventListener('click', this._onRowClicked.bind(this));

    // Get preferences & torrents from the daemon
    this.loadDaemonPrefs();
    this._initializeTorrents();
    this.refreshTorrents();
    this.togglePeriodicSessionRefresh(true);

    // this.updateButtonsSoon();

    this.prefs.addEventListener('change', ({ key, value }) =>
      this._onPrefChanged(key, value),
    );
    for (const [key, value] of this.prefs.entries()) {
      this._onPrefChanged(key, value);
    }
  }

  _initializeClusterize() {
    // Initialize clusterize.js for virtual scrolling
    this.clusterize = new Clusterize({
      blocks_in_cluster: 4,
      callbacks: {
        clusterChanged: () => {
          // Update selections on newly rendered rows
          this._updateVisibleSelections();
        }
      },
      contentId: 'torrent-list',
      no_data_class: 'clusterize-no-data',
      no_data_text: '',
      rows: ['<li class="clusterize-no-data"></li>'],
      rows_in_block: 25,
      scrollId: 'torrent-container',
      show_no_data_row: true,
      tag: 'li',
    });
  }

  _generateTorrentRowHTML(torrent) {
    // Use existing renderers to create a temporary DOM element, then extract HTML
    const isCompact = this.prefs.display_mode === Prefs.DisplayCompact;
    const renderer = isCompact ? new TorrentRendererCompact() : new TorrentRendererFull();

    // Create temporary row using existing renderer
    const tempRow = renderer.createRow(torrent);
    tempRow.dataset.torrentId = torrent.getId();

    // Add selection class if needed
    if (this._selectedTorrentIds.has(torrent.getId())) {
      tempRow.classList.add('selected');
    }

    // Render the content using existing renderer
    renderer.render(this, torrent, tempRow);

    // Return the HTML string
    return tempRow.outerHTML;
  }

  _openTorrentFromUrl() {
    setTimeout(() => {
      const addTorrent = new URLSearchParams(globalThis.location.search).get(
        'addtorrent',
      );
      if (addTorrent) {
        this.setCurrentPopup(new OpenDialog(this, this.remote, addTorrent));
        const newUrl = new URL(globalThis.location);
        newUrl.search = '';
        globalThis.history.pushState('', '', newUrl.toString());
      }
    }, 0);
  }

  loadDaemonPrefs() {
    this.remote.loadDaemonPrefs((data) => {
      this.session_properties = data.result;
      this._openTorrentFromUrl();
    });
  }

  get session_properties() {
    return this._session_properties;
  }
  set session_properties(o) {
    if (deepEqual(this._session_properties, o)) {
      return;
    }

    this._session_properties = Object.seal(o);
    const event = new Event('session-change');
    event.session_properties = o;
    this.dispatchEvent(event);

    // TODO: maybe have this in a listener handler?
    this._updateGuiFromSession(o);
  }

  _setupSearchBox() {
    const e = document.querySelector('#torrent-search');
    const blur_token = 'blur';
    e.classList.add(blur_token);
    e.addEventListener('blur', () => e.classList.add(blur_token));
    e.addEventListener('focus', () => e.classList.remove(blur_token));
    e.addEventListener('input', () => {
      if (e.value.trim() !== this.filterText) {
        this._setFilterText(e.value);
      }
    });
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
      case Prefs.ContrastMode: {
        // Add custom class to the body/html element to get the appropriate contrast color scheme
        document.body.classList.remove('contrast-more', 'contrast-less');
        document.body.classList.add(`contrast-${value}`);
        break;
      }

      case Prefs.FilterMode:
      case Prefs.SortDirection:
      case Prefs.SortMode:
        this.refilterAllSoon();
        break;

      case Prefs.HighlightColor: {
        document.body.classList.remove('highlight-legacy', 'highlight-system');
        if (!value) {
          document.body.classList.add('highlight-legacy');
        } else if (value === 'Highlight') {
          document.body.classList.add('highlight-system');
        }
        break;
      }

      case Prefs.RefreshRate: {
        clearInterval(this.refreshTorrentsInterval);
        const callback = this.refreshTorrents.bind(this);
        const pref = this.prefs.refresh_rate_sec;
        const msec = pref > 0 ? pref * 1000 : 1000;
        this.refreshTorrentsInterval = setInterval(callback, msec);
        break;
      }

      default:
        /*noop*/
        break;
    }
  }

  context_menu(container_id, menu_items) {
    // open context menu
    const popup = new ContextMenu(this, menu_items);
    this.setCurrentPopup(popup);

    const bounds = document.querySelector(container_id).getBoundingClientRect();
    const x = Math.min(
      this.pointer_device.x,
      bounds.right + globalThis.scrollX - popup.root.clientWidth,
    );
    const y = Math.min(
      this.pointer_device.y,
      bounds.bottom + globalThis.scrollY - popup.root.clientHeight,
    );
    popup.root.style.left = `${Math.max(x, 0)}px`;
    popup.root.style.top = `${Math.max(y, 0)}px`;
  }

  pointer_event(e_, right_click) {
    if (this.pointer_device.is_touch_device) {
      const touch = this.pointer_device;
      e_.addEventListener('touchstart', (event_) => {
        touch.x = event_.touches[0].pageX;
        touch.y = event_.touches[0].pageY;

        if (touch.long_press_callback) {
          clearTimeout(touch.long_press_callback);
          touch.long_press_callback = null;
        } else {
          touch.long_press_callback = setTimeout(() => {
            if (event_.touches.length === 1) {
              right_click(event_);
            }
          }, 500);
        }
      });
      e_.addEventListener('touchend', () => {
        clearTimeout(touch.long_press_callback);
        touch.long_press_callback = null;
        setTimeout(() => {
          const popup = this.popup[Transmission.default_popup_level];
          if (popup) {
            popup.root.style.pointerEvents = 'auto';
          }
        }, 1);
      });
      e_.addEventListener('touchmove', (event_) => {
        touch.x = event_.touches[0].pageX;
        touch.y = event_.touches[0].pageY;

        clearTimeout(touch.long_press_callback);
        touch.long_press_callback = null;
      });
      e_.addEventListener('contextmenu', (event_) => {
        event_.preventDefault();
      });
    } else {
      e_.addEventListener('mousemove', (event_) => {
        this.pointer_device.x = event_.pageX;
        this.pointer_device.y = event_.pageY;
      });
      e_.addEventListener('contextmenu', (event_) => {
        right_click(event_);
        const popup = this.popup[Transmission.default_popup_level];
        if (popup) {
          popup.root.style.pointerEvents = 'auto';
        }
      });
    }
  }

  /// UTILITIES

  static get max_popups() {
    return 2;
  }

  static get default_popup_level() {
    return Transmission.max_popups - 1;
  }

  _getAllTorrents() {
    return Object.values(this._torrents);
  }

  static _getTorrentIds(torrents) {
    return torrents.map((t) => t.getId());
  }

  seedRatioLimit() {
    const p = this.session_properties;
    if (p && p.seed_ratio_limited) {
      return p.seed_ratio_limit;
    }
    return -1;
  }

  /// SELECTION

  _getSelectedRows() {
    // For compatibility, return torrent objects that match selected IDs
    return this.getSelectedTorrents();
  }

  getSelectedTorrents() {
    return [...this._selectedTorrentIds]
      .map(id => this._torrents[id])
      .filter(Boolean);
  }

  _getSelectedTorrentIds() {
    return [...this._selectedTorrentIds];
  }

  _setSelectedTorrent(torrentId) {
    this._selectedTorrentIds.clear();
    if (torrentId) {
      this._selectedTorrentIds.add(torrentId);
    }
    this._updateVisibleSelections();
    this._dispatchSelectionChanged();
  }

  _selectTorrent(torrentId) {
    this._selectedTorrentIds.add(torrentId);
    this._updateVisibleSelections();
    this._dispatchSelectionChanged();
  }

  _deselectTorrent(torrentId) {
    this._selectedTorrentIds.delete(torrentId);
    this._updateVisibleSelections();
    this._dispatchSelectionChanged();
  }

  _selectAll() {
    for (const torrent of this._torrentOrder) {
      this._selectedTorrentIds.add(torrent.getId());
    }
    this._updateVisibleSelections();
    this._dispatchSelectionChanged();
  }

  _deselectAll() {
    this._selectedTorrentIds.clear();
    this._updateVisibleSelections();
    this._dispatchSelectionChanged();
    delete this._last_torrent_clicked;
  }

  _updateVisibleSelections() {
    // Update selection classes on visible DOM elements
    if (this.elements.torrent_list) {
      for (const element of this.elements.torrent_list.children) {
        const torrentId = Number.parseInt(element.dataset.torrentId, 10);
        if (torrentId) {
          element.classList.toggle('selected', this._selectedTorrentIds.has(torrentId));
        }
      }
    }
  }

  // Legacy methods for compatibility with existing code
  _setSelectedRow(row) {
    if (row && row.getTorrent) {
      this._setSelectedTorrent(row.getTorrent().getId());
    } else {
      this._setSelectedTorrent(null);
    }
  }

  _selectRow(row) {
    if (row && row.getTorrent) {
      this._selectTorrent(row.getTorrent().getId());
    }
  }

  _deselectRow(row) {
    if (row && row.getTorrent) {
      this._deselectTorrent(row.getTorrent().getId());
    }
  }

  _indexOfLastTorrent() {
    if (!this._last_torrent_clicked) {
      return -1;
    }
    return this._torrentOrder.findIndex(
      (torrent) => torrent.getId() === this._last_torrent_clicked,
    );
  }

  // Select a range from this row to the last clicked torrent
  _selectRange(row) {
    // Convert row to torrent ID and use new implementation
    if (row && row.getTorrent) {
      this._selectRangeToTorrent(row.getTorrent().getId());
    }
  }

  // Select a range from the given torrent ID to the last clicked torrent
  _selectRangeToTorrent(torrentId) {
    if (!this._last_torrent_clicked) {
      this._selectTorrent(torrentId);
      return;
    }

    // Find indices in the current torrent order
    const currentIndex = this._torrentOrder.findIndex(t => t.getId() === torrentId);
    const lastIndex = this._torrentOrder.findIndex(t => t.getId() === this._last_torrent_clicked);

    if (currentIndex === -1 || lastIndex === -1) {
      this._selectTorrent(torrentId);
      return;
    }

    // Select the range between the previous & current
    const min = Math.min(lastIndex, currentIndex);
    const max = Math.max(lastIndex, currentIndex);
    for (let index = min; index <= max; ++index) {
      this._selectTorrent(this._torrentOrder[index].getId());
    }
  }

  _dispatchSelectionChanged() {
    const selected = [];
    const nonselected = [];

    for (const torrent of Object.values(this._torrents)) {
      if (this._selectedTorrentIds.has(torrent.getId())) {
        selected.push(torrent);
      } else {
        nonselected.push(torrent);
      }
    }

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

  static _createKeyShortcutFromKeyboardEvent(event_) {
    const a = [];
    if (event_.ctrlKey) {
      a.push('Control');
    }
    if (event_.altKey) {
      a.push('Alt');
    }
    if (event_.metaKey) {
      a.push('Meta');
    }
    if (event_.shiftKey) {
      a.push('Shift');
    }
    a.push(event_.key.length === 1 ? event_.key.toUpperCase() : event_.key);
    return a.join('+');
  }

  // Process key events
  _keyDown(event_) {
    const { ctrlKey, keyCode, metaKey, shiftKey, target } = event_;

    // look for a shortcut
    const is_input_focused = ['INPUT', 'TEXTAREA'].includes(target.tagName);
    if (!is_input_focused) {
      const shortcut = Transmission._createKeyShortcutFromKeyboardEvent(event_);
      const action = this.action_manager.getActionForShortcut(shortcut);
      if (action) {
        event_.preventDefault();
        this.action_manager.click(action);
        return;
      }
    }

    const esc_key = keyCode === 27; // esc key pressed
    if (esc_key && this.popup.some(Boolean)) {
      this.setCurrentPopup(null, 0);
      event_.preventDefault();
      return;
    }

    const any_popup_active = document.querySelector('.popup:not(.hidden)');
    const torrents = this._torrentOrder;

    // Some shortcuts can only be used if the following conditions are met:
    // 1. when no input fields are focused
    // 2. when no other dialogs are visible
    // 3. when the meta or ctrl key isn't pressed (i.e. opening dev tools shouldn't trigger the info panel)
    if (!is_input_focused && !any_popup_active && !metaKey && !ctrlKey) {
      const shift_key = keyCode === 16; // shift key pressed
      const up_key = keyCode === 38; // up key pressed
      const dn_key = keyCode === 40; // down key pressed
      if ((up_key || dn_key) && torrents.length > 0) {
        const last = this._indexOfLastTorrent();
        const anchor = this._shift_index;
        const min = 0;
        const max = torrents.length - 1;
        let index = last;

        if (dn_key && index + 1 <= max) {
          ++index;
        } else if (up_key && index - 1 >= min) {
          --index;
        }

        const torrent = torrents[index];

        if (anchor >= 0) {
          // user is extending the selection
          // with the shift + arrow keys...
          if (
            (anchor <= last && last < index) ||
            (anchor >= last && last > index)
          ) {
            this._selectTorrent(torrent.getId());
          } else if (
            (anchor >= last && index > last) ||
            (anchor <= last && last > index)
          ) {
            this._deselectTorrent(torrents[last].getId());
          }
        } else {
          if (shiftKey) {
            this._selectRangeToTorrent(torrent.getId());
          } else {
            this._setSelectedTorrent(torrent.getId());
          }
        }
        if (torrent) {
          event_.preventDefault();
          this._last_torrent_clicked = torrent.getId();
          const rowElem = Array.from(this.elements.torrent_list.children).find(
            (element) => Number.parseInt(element.dataset.torrentId, 10) === torrent.getId()
          );
          if (rowElem) {
            rowElem.scrollIntoView({
              block: 'nearest',
              inline: 'nearest'
            });
          }
        }
      } else if (shift_key) {
        this._shift_index = this._indexOfLastTorrent();
      }
    }
  }

  _keyUp(event_) {
    if (event_.keyCode === 16) {
      // shift key pressed
      delete this._shift_index;
    }
  }

  static _dragenter(event_) {
    if (event_.dataTransfer && event_.dataTransfer.types) {
      const copy_types = new Set(['text/uri-list', 'text/plain']);
      if (
        event_.dataTransfer.types.some((type) => copy_types.has(type)) ||
        event_.dataTransfer.types.includes('Files')
      ) {
        event_.stopPropagation();
        event_.preventDefault();
        event_.dataTransfer.dropEffect = 'copy';
        return false;
      }
    } else if (event_.dataTransfer) {
      event_.dataTransfer.dropEffect = 'none';
    }
    return true;
  }

  static _isValidURL(string) {
    try {
      const url = new URL(string);
      return Boolean(url);
    } catch {
      return false;
    }
  }

  shouldAddedTorrentsStart() {
    return this.session_properties.start_added_torrents;
  }

  _drop(event_) {
    const paused = !this.shouldAddedTorrentsStart();

    if (!event_.dataTransfer || !event_.dataTransfer.types) {
      return true;
    }

    const type = event_.dataTransfer.types.findLast((t) =>
      ['text/uri-list', 'text/plain'].includes(t),
    );
    for (const uri of event_.dataTransfer
      .getData(type)
      .split('\n')
      .map((string) => string.trim())
      .filter((string) => Transmission._isValidURL(string))) {
      this.remote.addTorrentByUrl(uri, paused);
    }

    const { files } = event_.dataTransfer;

    if (files.length > 0) {
      this.setCurrentPopup(new OpenDialog(this, this.remote, '', files));
    }
    event_.preventDefault();
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
          msec,
        );
      }
    }
  }

  _setFilterText(search) {
    clearTimeout(this.busytyping);
    this.busytyping = setTimeout(
      () => {
        this.busytyping = false;
        this.filterText = search.trim();
        this.refilterAllSoon();
      },
      search ? 250 : 0,
    );
  }

  _onTorrentChanged(event_) {
    if (this.changeStatus) {
      this._dispatchSelectionChanged();
      this.changeStatus = false;
    }

    // update our dirty fields
    const tor = event_.currentTarget;
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
        for (const [index, key] of keys.entries()) {
          o[key] = row[index];
        }
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

      if (needinfo.length > 0) {
        // whee, new torrents! get their initial information.
        const more_fields = [
          'id',
          ...Torrent.Fields.Metadata,
          ...Torrent.Fields.Stats,
        ];
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
TODO: fix this when notifications get fixed
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
    const fields = ['id', ...Torrent.Fields.Stats];
    this.updateTorrents('recently_active', fields);
  }

  _initializeTorrents() {
    const fields = ['id', ...Torrent.Fields.Metadata, ...Torrent.Fields.Stats];
    this.updateTorrents(null, fields);
  }

  _onRowClicked(event_) {
    // Find the torrent row element
    let rowElement = event_.target;
    while (rowElement && !rowElement.classList.contains('torrent')) {
      rowElement = rowElement.parentNode;
    }

    if (!rowElement || !rowElement.dataset.torrentId) {
      return;
    }

    const torrentId = Number.parseInt(rowElement.dataset.torrentId, 10);
    const torrent = this._torrents[torrentId];
    if (!torrent) {
      return;
    }

    const meta_key = event_.metaKey || event_.ctrlKey;
    const isSelected = this._selectedTorrentIds.has(torrentId);

    if (this.popup[Transmission.default_popup_level]) {
      this.setCurrentPopup(null);
    }

    // Prevents click carrying to parent element
    // which deselects all on click
    event_.stopPropagation();

    if (event_.shiftKey) {
      this._selectRangeToTorrent(torrentId);
      // Need to deselect any selected text
      globalThis.focus();

      // Apple-Click, not selected
    } else if (!isSelected && meta_key) {
      this._selectTorrent(torrentId);

      // Regular Click, not selected
    } else if (!isSelected) {
      this._setSelectedTorrent(torrentId);

      // Apple-Click, selected
    } else if (isSelected && meta_key) {
      this._deselectTorrent(torrentId);

      // Regular Click, selected
    } else if (isSelected) {
      this._setSelectedTorrent(torrentId);
    }

    this._last_torrent_clicked = torrentId;
  }

  _deleteTorrents(ids) {
    if (ids && ids.length > 0) {
      for (const id of ids) {
        this.dirtyTorrents.add(id);
        delete this._torrents[id];
      }
      this.refilterSoon();
    }
  }

  _removeSelectedTorrents(trash) {
    const torrents = this.getSelectedTorrents();
    if (torrents.length > 0) {
      this.setCurrentPopup(
        new RemoveDialog({ remote: this.remote, torrents, trash }),
      );
    }
  }

  _startSelectedTorrents(force) {
    this._startTorrents(this.getSelectedTorrents(), force);
  }

  _startTorrents(torrents, force) {
    this.changeStatus = true;
    this.remote.startTorrents(
      Transmission._getTorrentIds(torrents),
      force,
      this.refreshTorrents,
      this,
    );
  }
  _verifyTorrents(torrents) {
    this.remote.verifyTorrents(
      Transmission._getTorrentIds(torrents),
      this.refreshTorrents,
      this,
    );
  }

  _reannounceTorrents(torrents) {
    this.remote.reannounceTorrents(
      Transmission._getTorrentIds(torrents),
      this.refreshTorrents,
      this,
    );
  }

  _stopTorrents(torrents) {
    this.changeStatus = true;
    this.remote.stopTorrents(
      Transmission._getTorrentIds(torrents),
      () => {
        setTimeout(() => {
          this.refreshTorrents();
        }, 500);
      },
      this,
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
      this,
    );
  }
  _moveUp() {
    this.remote.moveTorrentsUp(
      this._getSelectedTorrentIds(),
      this.refreshTorrents,
      this,
    );
  }
  _moveDown() {
    this.remote.moveTorrentsDown(
      this._getSelectedTorrentIds(),
      this.refreshTorrents,
      this,
    );
  }
  _moveBottom() {
    this.remote.moveTorrentsToBottom(
      this._getSelectedTorrentIds(),
      this.refreshTorrents,
      this,
    );
  }

  ///

  _updateGuiFromSession(o) {
    const [, version, checksum] = o.version.match(/^(.*)\s\(([\da-f]+)\)/);
    this.version_info = {
      checksum,
      version,
    };

    const element = document.querySelector('#turtle');
    element.classList.toggle('alt-speed-enabled', o[RPC._TurtleState]);
  }

  _updateStatusbar() {
    const fmt = Formatter;
    const torrents = this._getAllTorrents();

    const u = torrents.reduce(
      (accumulator, tor) => accumulator + tor.getUploadSpeed(),
      0,
    );
    const d = torrents.reduce(
      (accumulator, tor) => accumulator + tor.getDownloadSpeed(),
      0,
    );
    const string = fmt.countString('Transfer', 'Transfers', this._torrentOrder.length);

    setTextContent(this.speed.down, fmt.speedBps(d));
    setTextContent(this.speed.up, fmt.speedBps(u));
    setTextContent(document.querySelector('#filter-count'), string);
  }

  static _displayName(hostname) {
    let name = hostname;
    if (name.length > 0) {
      name = name.charAt(0).toUpperCase() + name.slice(1);
    }
    return name;
  }

  _updateFilterSelect() {
    const trackers = this._getTrackerCounts();
    const sitenames = Object.keys(trackers).toSorted();

    // Update select box only when list of trackers has changed
    if (
      sitenames.length !== this.oldTrackers.length ||
      sitenames.some((ele, idx) => ele !== this.oldTrackers[idx])
    ) {
      this.oldTrackers = sitenames;

      const a = [
        ['All', Prefs.FilterAll, !this.filterTracker],
        ...sitenames.map((sitename) => [
          Transmission._displayName(sitename),
          sitename,
          sitename === this.filterTracker,
        ]),
      ];

      const e = document.querySelector('#filter-tracker');
      while (e.firstChild) {
        e.lastChild.remove();
      }
      newOpts(e, null, a);
    }
  }

  /// FILTER

  sortRows(rows) {
    const torrents = rows.map((row) => row.getTorrent());
    const id2row = rows.reduce((accumulator, row) => {
      accumulator[row.getTorrent().getId()] = row;
      return accumulator;
    }, {});
    Torrent.sortTorrents(
      torrents,
      this.prefs.sort_mode,
      this.prefs.sort_direction,
    );
    for (const [index, tor] of torrents.entries()) {
      rows[index] = id2row[tor.getId()];
    }
  }

  _refilter() {
    const { sort_mode, sort_direction, filter_mode } = this.prefs;
    const filter_tracker = this.filterTracker;

    let filter_text = null;
    let labels = null;
    // TODO: This regex is wrong and is about to be removed in https://github.com/transmission/transmission/pull/7008,
    // so it is left alone for now.
    // eslint-disable-next-line sonarjs/slow-regex
    const m = /^labels:([\w,-\s]*)(.*)$/.exec(this.filterText);
    if (m) {
      filter_text = m[2].trim();
      labels = m[1].split(',');
    } else {
      filter_text = this.filterText;
      labels = [];
    }

    this._updateFilterSelect();

    // Get filtered and sorted torrents
    const filteredTorrents = [];
    for (const torrent of Object.values(this._torrents)) {
      if (torrent.test(filter_mode, filter_tracker, filter_text, labels)) {
        filteredTorrents.push(torrent);
      }
    }

    // Sort the torrents
    filteredTorrents.sort((a, b) =>
      Torrent.compareTorrents(a, b, sort_mode, sort_direction)
    );

    // Update torrent order for range selection
    this._torrentOrder = filteredTorrents;

    // Generate HTML for each torrent
    const rowsHTML = filteredTorrents.map(torrent =>
      this._generateTorrentRowHTML(torrent)
    );

    // Update clusterize with new data
    if (rowsHTML.length === 0) {
      this.clusterize.update(['<li class="clusterize-no-data"></li>']);
    } else {
      this.clusterize.update(rowsHTML);
    }

    // Force refresh to recalculate row heights for large lists
    if (rowsHTML.length > 1000) {
      setTimeout(() => {
        this.clusterize.refresh(true);
      }, 50);
    }

    // Clear dirty torrents set
    this.dirtyTorrents.clear();

    // Update status bar
    this._updateStatusbar();

    // Update visible selections after clusterize renders
    setTimeout(() => {
      this._updateVisibleSelections();
    }, 0);
  }

  setFilterTracker(sitename) {
    const e = document.querySelector('#filter-tracker');
    e.value = sitename;

    this.filterTracker = sitename === Prefs.FilterAll ? '' : sitename;
    this.refilterAllSoon();
  }

  _getTrackerCounts() {
    const counts = {};

    for (const torrent of this._getAllTorrents()) {
      for (const tracker of torrent.getTrackers()) {
        const { sitename } = tracker;
        counts[sitename] = (counts[sitename] || 0) + 1;
      }
    }

    return counts;
  }

  ///

  setCurrentPopup(popup, level = Transmission.default_popup_level) {
    for (let index = level; index < Transmission.max_popups; index++) {
      if (this.popup[index]) {
        this.popup[index].close();
      }
    }

    this.popup[level] = popup;

    if (this.popup[level]) {
      const listener = () => {
        if (this.popup[level]) {
          this.popup[level].removeEventListener('close', listener);
          this.popup[level] = null;
        }
      };
      this.popup[level].addEventListener('close', listener);
    } else if (this.handler) {
      this.handler.classList.remove('selected');
    }
  }
}
