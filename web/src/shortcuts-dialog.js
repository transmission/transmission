/* @license This file Copyright © 2020-2023 Mnemosyne LLC.
   It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
   or any future license endorsed by Mnemosyne LLC.
   License text can be found in the licenses/ folder. */

import { createDialogContainer } from './utils.js';

export class ShortcutsDialog extends EventTarget {
  constructor(action_manager) {
    super();

    this.elements = ShortcutsDialog._create(action_manager);
    this.elements.dismiss.addEventListener('click', () => this._onDismiss());
    document.body.append(this.elements.root);
    this.elements.dismiss.focus();
  }

  close() {
    this.elements.root.remove();
    this.dispatchEvent(new Event('close'));
    delete this.elements;
  }

  _onDismiss() {
    this.close();
  }

  static _create(action_manager) {
    const elements = createDialogContainer('shortcuts-dialog');
    elements.root.setAttribute('aria-label', 'Keyboard Shortcuts');

    const table = document.createElement('table');
    const thead = document.createElement('thead');
    table.append(thead);

    let tr = document.createElement('tr');
    thead.append(tr);

    let th = document.createElement('th');
    th.textContent = 'Key';
    tr.append(th);

    th = document.createElement('th');
    th.textContent = 'Action';
    tr.append(th);

    const tbody = document.createElement('tbody');
    table.append(tbody);

    const o = {};
    for (const [shortcut, name] of action_manager.allShortcuts().entries()) {
      const tokens = shortcut.split('+');
      const sortKey = [tokens.pop(), ...tokens].join('+');
      o[sortKey] = { name, shortcut };
    }

    for (const [, values] of Object.entries(o).sort()) {
      const { name, shortcut } = values;
      tr = document.createElement('tr');
      tbody.append(tr);

      let td = document.createElement('td');
      td.textContent = shortcut.replaceAll('+', ' + ');
      tr.append(td);

      td = document.createElement('td');
      td.textContent = action_manager.text(name);
      tr.append(td);
    }

    elements.heading.textContent = 'Transmission';
    elements.dismiss.textContent = 'Close';

    elements.heading.textContent = 'Keyboard shortcuts';
    elements.message.append(table);
    elements.confirm.remove();
    delete elements.confirm;
    return elements;
  }
}
