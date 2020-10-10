/*
 * This file Copyright (C) 2020 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 */

export class ActionManager extends EventTarget {
  constructor() {
    super();
    this.actions = {
      'deselect-all': { enabled: false, hotkey: 'Control+A' },
      'move-bottom': { enabled: false },
      'move-down': { enabled: false },
      'move-top': { enabled: false },
      'move-up': { enabled: false },
      'pause-all-torrents': { enabled: false },
      'pause-selected-torrents': { enabled: false },
      'reannounce-selected-torrents': { enabled: false },
      'remove-selected-torrents': { enabled: false },
      'resume-selected-torrents': { enabled: false },
      'resume-selected-torrents-now': { enabled: false },
      'select-all': { enabled: false, hotkey: 'Alt+A' },
      'show-about-dialog': { enabled: true },
      'show-hotkeys-dialog': { enabled: true },
      'show-move-dialog': { enabled: false, hotkey: 'Alt+L' },
      'show-rename-dialog': { enabled: false, hotkey: 'Alt+N' },
      'start-all-torrents': { enabled: false },
      'trash-selected-torrents': { enabled: false },
      'verify-selected-torrents': { enabled: false, hotkey: 'Alt+V' },
    };
  }

  click(name) {
    if (this.isEnabled(name)) {
      const ev = new Event('click');
      ev.action = name;
      this.dispatchEvent(ev);
    }
  }

  isEnabled(name) {
    return this._getAction(name).enabled;
  }

  keyshortcuts(name) {
    return this._getAction(name).hotkey;
  }

  update(ev) {
    const counts = ActionManager._recount(ev.selected, ev.nonselected);
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
      console.log(`updating action enabled: ${name}, ${enabled}`);

      const event = new Event('action-state-changed');
      event.action = name;
      event.enabled = enabled;
      this.dispatchEvent(event);
    }
  }
}
