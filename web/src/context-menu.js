/*
 * This file Copyright (C) 2020 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 */

export class ContextMenu extends EventTarget {
  constructor(action_manager, mouse_event) {
    super();

    this.action_listener = this._update.bind(this);
    this.action_manager = action_manager;
    this.action_manager.addEventListener(
      'action-state-changed',
      this.action_listener
    );
    Object.assign(this, this._create());
    this.show();
    this._setBounds(mouse_event);
    mouse_event.preventDefault();
  }

  show() {
    for (const [action, item] of Object.entries(this.actions)) {
      const is_enabled = this.action_manager.isEnabled(action);
      item.classList.toggle('disabled', !is_enabled);
    }
    document.body.appendChild(this.root);
  }

  close() {
    if (this.closed) {
      return;
    }

    this.closed = true;
    this.action_manager.removeEventListener(
      'action-state-changed',
      this.action_listener
    );
    this.root.remove();
    this.dispatchEvent(new Event('close'));

    delete this.actions;
    delete this.remote;
    delete this.root;
    delete this.torrents;
  }

  _setBounds(ev) {
    const getBestMenuPos = (r, bounds) => {
      let { x, y } = r;
      const { width, height } = r;
      if (x > bounds.x + bounds.width - width) {
        x -= width;
      }
      if (y > bounds.y + bounds.height - height) {
        y -= height;
      }
      // x = Math.min(x, bounds.x + bounds.width - width);
      // y = Math.min(y, bounds.y + bounds.height - height);
      return new DOMRect(x, y, width, height);
    };
    const bounding_ancestor = document.getElementById('torrent-container');
    const e = this.root;
    const initial_pos = new DOMRect(ev.x, ev.y, e.clientWidth, e.clientHeight);
    const clamped_pos = getBestMenuPos(
      initial_pos,
      bounding_ancestor.getBoundingClientRect()
    );
    e.style.left = `${clamped_pos.left}px`;
    e.style.top = `${clamped_pos.top}px`;
  }

  _update(ev) {
    const e = this.root.querySelector(`[data-action="${ev.action}"]`);
    if (e) {
      e.classList.toggle('disabled', !ev.enabled);
    }
  }

  _create() {
    const root = document.createElement('div');
    root.role = 'menu';
    root.classList.add('context-menu');
    root.classList.add('popup');
    root.id = 'torrent-context-menu'; // FIXME: is this needed?

    const actions = {};
    const create_menuitem = (action, text) => {
      const item = document.createElement('div');
      item.role = 'menuitem';
      item.classList.add('context-menuitem');
      item.setAttribute('data-action', action);
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

    const create_separator = () => {
      const item = document.createElement('div');
      item.classList.add('context-menu-separator');
      return item;
    };

    root.appendChild(create_menuitem('pause-selected-torrents', 'Pause'));
    root.appendChild(create_menuitem('resume-selected-torrents', 'Resume'));
    root.appendChild(
      create_menuitem('resume-selected-torrents-now', 'Resume now')
    );
    root.appendChild(create_separator());
    root.appendChild(
      create_menuitem('move-top', 'Move to the front of the queue')
    );
    root.appendChild(create_menuitem('move-up', 'Move up in the queue'));
    root.appendChild(create_menuitem('move-down', 'Move down in the queue'));
    root.appendChild(
      create_menuitem('move-bottom', 'Move to the back of the queue')
    );
    root.appendChild(create_separator());
    root.appendChild(
      create_menuitem('remove-selected-torrents', 'Remove from list…')
    );
    root.appendChild(
      create_menuitem(
        'trash-selected-torrents',
        'Trash data and remove from list…'
      )
    );
    root.appendChild(create_separator());
    root.appendChild(
      create_menuitem('verify-selected-torrents', 'Verify local data')
    );
    root.appendChild(create_menuitem('show-move-dialog', 'Set location…'));
    root.appendChild(create_menuitem('show-rename-dialog', 'Rename…'));
    root.appendChild(create_separator());
    root.appendChild(
      create_menuitem(
        'reannounce-selected-torrents',
        'Ask tracker for more peers'
      )
    );
    root.appendChild(create_separator());
    root.appendChild(create_menuitem('select-all', 'Select all'));
    root.appendChild(create_menuitem('deselect-all', 'Deselect all'));

    return { actions, root };
  }
}
