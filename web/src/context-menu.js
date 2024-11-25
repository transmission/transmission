/* @license This file Copyright © Mnemosyne LLC.
   It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
   or any future license endorsed by Mnemosyne LLC.
   License text can be found in the licenses/ folder. */

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
    root.addEventListener('contextmenu', (e_) => {
      e_.preventDefault();
    });
    root.style.pointerEvents = 'none';

    const actions = {};
    const add_item = (action, warn = false) => {
      const item = document.createElement('div');
      const text = this.action_manager.text(action);
      item.role = 'menuitem';
      if (warn) {
        item.classList.add('context-menuitem', 'warning');
      } else {
        item.classList.add('context-menuitem');
      }
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
      return item;
    };

    const add_separator = () => {
      const item = document.createElement('div');
      item.classList.add('context-menu-separator');
      return item;
    };

    const add_submenu = (e) => {
      e.addEventListener('mousedown', (e_) => {
        const t = e.lastChild.lastChild;
        if (
          !e_.target.classList.contains('right') &&
          !e_.target.parentNode.classList.contains('right') &&
          !e_.target.classList.contains('left') &&
          !e_.target.parentNode.classList.contains('left') &&
          t.style.display === 'block'
        ) {
          root.dismissed = true;
          t.style.display = 'none';
        }
      });

      e.addEventListener('click', () => {
        if (root.dismissed) {
          root.dismissed = false;
          return;
        }

        for (const p of document.querySelectorAll('.context-submenu')) {
          p.style.display = 'none';
        }

        const t = e.lastChild.lastChild;
        t.style.display = 'block';
        const where = e.getBoundingClientRect();
        const wheret = t.lastChild.getBoundingClientRect();
        const y = Math.min(
          0,
          document.documentElement.clientHeight - window.visualViewport.offsetTop - where.top - t.clientHeight + 3,
        );
        const x = Math.min(
          0,
          document.documentElement.clientWidth - window.visualViewport.offsetLeft - where.right - t.clientWidth - 3,
        );

        t.style.top = y + 'px';
        if (x) {
          t.lastChild.classList = 'poppy left';
          t.style.left = -where.width - wheret.width + 'px';
        } else {
          t.lastChild.classList = 'poppy right';
          t.style.left = x + 'px';
        }
      });
    };

    const add_arrow = (text, ...args) => {
      const arrow = document.createElement('DIV');
      arrow.classList = 'poppy right';

      for (const arg of args) {
        arrow.append(add_item(arg));
      };

      const e = document.createElement('DIV');
      e.textContent = text;
      e.classList.add('context-menuitem');

      const a = document.createElement('DIV');
      a.classList = 'context-arrow';

      const p = document.createElement('DIV');
      p.classList = 'context-submenu';

      p.append(arrow);
      a.append(p);
      e.append(a);

      add_submenu(e);

      return e;
    }

    root.dismissed = false;

    root.append(
      add_item('resume-selected-torrents'),
      add_item('resume-selected-torrents-now'),
      add_item('pause-selected-torrents'),
      add_separator(),
      add_item('move-top'),
      add_item('move-up'),
      add_item('move-down'),
      add_item('move-bottom'),
      add_separator(),
      add_item('remove-selected-torrents', true),
      add_item('trash-selected-torrents', true),
      add_separator(),
      add_item('verify-selected-torrents'),
      add_item('show-move-dialog'),
      add_item('show-rename-dialog'),
      add_item('show-labels-dialog'),
      add_separator(),
      add_item('reannounce-selected-torrents'),
      add_separator(),
      add_arrow('Select operation', 'select-all', 'deselect-all'),
    );

    return { actions, root };
  }
}
