/**
 * @license
 *
 * This file Copyright (C) 2020 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 */

export class ActionManager extends EventTarget {
  constructor() {
    super();
    this.actions = Object.seal({
      'deselect-all': {
        enabled: false,
        shortcut: 'Control+A',
        text: 'Deselect all',
      },
      'move-bottom': { enabled: false, text: 'Move to the back of the queue' },
      'move-down': { enabled: false, text: 'Move down in the queue' },
      'move-top': { enabled: false, text: 'Move to the front of the queue' },
      'move-up': { enabled: false, text: 'Move up in the queue' },
      'open-torrent': {
        enabled: true,
        shortcut: 'Alt+O',
        text: 'Open torrent…',
      },
      'pause-all-torrents': { enabled: false, text: 'Pause all' },
      'pause-selected-torrents': {
        enabled: false,
        shortcut: 'Alt+U',
        text: 'Pause',
      },
      'reannounce-selected-torrents': {
        enabled: false,
        text: 'Ask tracker for more peers',
      },
      'remove-selected-torrents': { enabled: false, text: 'Remove from list…' },
      'resume-selected-torrents': {
        enabled: false,
        shortcut: 'Alt+R',
        text: 'Resume',
      },
      'resume-selected-torrents-now': { enabled: false, text: 'Resume now' },
      'select-all': { enabled: false, shortcut: 'Alt+A', text: 'Select all' },
      'show-about-dialog': { enabled: true, text: 'About' },
      'show-inspector': {
        enabled: true,
        shortcut: 'Alt+I',
        text: 'Torrent Inspector',
      },
      'show-move-dialog': {
        enabled: false,
        shortcut: 'Alt+L',
        text: 'Set location…',
      },
      'show-overflow-menu': { enabled: true, text: 'More options…' },
      'show-preferences-dialog': {
        enabled: true,
        shortcut: 'Alt+P',
        text: 'Edit preferences',
      },
      'show-rename-dialog': {
        enabled: false,
        shortcut: 'Alt+N',
        text: 'Rename…',
      },
      'show-shortcuts-dialog': { enabled: true, text: 'Keyboard shortcuts' },
      'show-statistics-dialog': {
        enabled: true,
        shortcut: 'Alt+S',
        text: 'Statistics',
      },
      'start-all-torrents': { enabled: false, text: 'Start all' },
      'toggle-compact-rows': { enabled: true, text: 'Compact rows' },
      'trash-selected-torrents': {
        enabled: false,
        text: 'Trash data and remove from list…',
      },
      'verify-selected-torrents': {
        enabled: false,
        shortcut: 'Alt+V',
        text: 'Verify local data',
      },
    });
  }

  click(name) {
    if (this.isEnabled(name)) {
      const event_ = new Event('click');
      event_.action = name;
      this.dispatchEvent(event_);
    }
  }

  getActionForShortcut(shortcut) {
    for (const [name, properties] of Object.entries(this.actions)) {
      if (shortcut === properties.shortcut) {
        return name;
      }
    }
    return null;
  }

  // return a map of shortcuts to action names
  allShortcuts() {
    return new Map(
      Object.entries(this.actions)
        .filter(([, properties]) => properties.shortcut)
        .map(([name, properties]) => [properties.shortcut, name])
    );
  }

  isEnabled(name) {
    return this._getAction(name).enabled;
  }

  text(name) {
    return this._getAction(name).text;
  }

  keyshortcuts(name) {
    return this._getAction(name).shortcut;
  }

  update(event_) {
    const counts = ActionManager._recount(event_.selected, event_.nonselected);
    this._updateStates(counts);
  }

  _getAction(name) {
    const action = this.actions[name];
    if (!action) {
      throw new Error(`no such action: ${name}`);
    }
    return action;
  }

  static _recount(selected, nonselected) {
    const test = (tor) => tor.isStopped();
    const total = selected.length + nonselected.length;
    const selected_paused = selected.filter(test).length;
    const selected_active = selected.length - selected_paused;
    const nonselected_paused = nonselected.filter(test).length;
    const nonselected_active = nonselected.length - nonselected_paused;
    const paused = selected_paused + nonselected_paused;
    const active = selected_active + nonselected_active;
    const selected_queued = selected.filter((tor) => tor.isQueued()).length;

    return {
      active,
      nonselected_active,
      nonselected_paused,
      paused,
      selected: selected.length,
      selected_active,
      selected_paused,
      selected_queued,
      total,
    };
  }

  _updateStates(counts) {
    const set_enabled = (enabled, actions) => {
      for (const action of actions) {
        this._updateActionState(action, enabled);
      }
    };

    set_enabled(counts.selected_paused > 0, ['resume-selected-torrents']);

    set_enabled(counts.paused > 0, ['start-all-torrents']);

    set_enabled(counts.active > 0, ['pause-all-torrents']);

    set_enabled(counts.selected_paused > 0 || counts.selected_queued > 0, [
      'resume-selected-torrents-now',
    ]);

    set_enabled(counts.selected_active > 0, [
      'pause-selected-torrents',
      'reannounce-selected-torrents',
    ]);

    set_enabled(counts.selected > 0, [
      'deselect-all',
      'move-bottom',
      'move-down',
      'move-top',
      'move-up',
      'show-inspector',
      'show-move-dialog',
      'remove-selected-torrents',
      'trash-selected-torrents',
      'verify-selected-torrents',
    ]);

    set_enabled(counts.selected === 1, ['show-rename-dialog']);

    set_enabled(counts.selected < counts.total, ['select-all']);
  }

  _updateActionState(name, enabled) {
    const action = this.actions[name];
    if (!action) {
      throw new Error(`no such action: ${name}`);
    }

    if (action.enabled !== enabled) {
      action.enabled = enabled;

      const event = new Event('change');
      event.action = name;
      event.enabled = enabled;
      this.dispatchEvent(event);
    }
  }
}
