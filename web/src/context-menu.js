/**
 * @license
 *
 * This file Copyright (C) 2020 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 */

import { setEnabled } from './utils.js';

export class ContextMenu extends EventTarget {
  constructor(action_manager) {
    super();

    this.action_listener = this._update.bind(this);
    this.action_manager = action_manager;
    this.action_manager.addEventListener('change', this.action_listener);

    Object.assign(this, this._create());
    this.show();
  }

  show() {
    for (const [action, item] of Object.entries(this.actions)) {
      setEnabled(item, this.action_manager.isEnabled(action));
    }
    document.body.append(this.root);
  }

  close() {
    if (!this.closed) {
      this.action_manager.removeEventListener('change', this.action_listener);
      this.root.remove();
      this.dispatchEvent(new Event('close'));
      for (const key of Object.keys(this)) {
        delete this[key];
      }
      this.closed = true;
    }
  }

  _update(event_) {
    const e = this.actions[event_.action];
    if (e) {
      setEnabled(e, event_.enabled);
    }
  }

  _create() {
    const root = document.createElement('div');
    root.role = 'menu';
    root.classList.add('context-menu', 'popup');

    const actions = {};
    const add_item = (action) => {
      const item = document.createElement('div');
      const text = this.action_manager.text(action);
      item.role = 'menuitem';
      item.classList.add('context-menuitem');
      item.dataset.action = action;
      item.textContent = text;
      const keyshortcuts = this.action_manager.keyshortcuts(action);
      if (keyshortcuts) {
        item.setAttribute('aria-keyshortcuts', keyshortcuts);
      }
      item.addEventListener('click', () => {
        this.action_manager.click(action);
        this.close();
      });
      actions[action] = item;
      root.append(item);
    };

    const add_separator = () => {
      const item = document.createElement('div');
      item.classList.add('context-menu-separator');
      root.append(item);
    };

    add_item('resume-selected-torrents');
    add_item('resume-selected-torrents-now');
    add_item('pause-selected-torrents');
    add_separator();
    add_item('move-top');
    add_item('move-up');
    add_item('move-down');
    add_item('move-bottom');
    add_separator();
    add_item('remove-selected-torrents');
    add_item('trash-selected-torrents');
    add_separator();
    add_item('verify-selected-torrents');
    add_item('show-move-dialog');
    add_item('show-rename-dialog');
    add_separator();
    add_item('reannounce-selected-torrents');
    add_separator();
    add_item('select-all');
    add_item('deselect-all');

    return { actions, root };
  }
}
