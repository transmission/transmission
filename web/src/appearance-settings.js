/* @license This file Copyright © Mnemosyne LLC.
   It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
   or any future license endorsed by Mnemosyne LLC.
   License text can be found in the licenses/ folder. */

import { Prefs } from './prefs.js';
import { createDialogContainer } from './utils.js';

export class Appearance extends EventTarget {
  constructor(prefs, action_manager) {
    super();

    this.action_manager = action_manager;

    this.prefs_listener = this._onPrefsChange.bind(this);
    this.prefs = prefs;
    this.prefs.addEventListener('change', this.prefs_listener);

    this.elements = this._create();
    this.elements.dismiss.addEventListener('click', () => this.close());
    document.body.append(this.elements.root);
    this.elements.dismiss.focus();
  }

  close() {
    this.elements.root.remove();
    this.dispatchEvent(new Event('close'));
    delete this.elements;
  }

  _onPrefsChange(event_) {
    switch (event_.key) {
      case Prefs.SortDirection:
      case Prefs.SortMode:
        this.root.querySelector(`[data-pref="${event_.key}"]`).value =
          event_.value;
        break;
      default:
        break;
    }
  }

  _create() {
    const elements = createDialogContainer('dis-appearance');
    const { dismiss, heading, message, workarea } = elements;

    heading.textContent = 'Appearance';
    dismiss.textContent = 'Close';

    // contrast

    let legend = document.createElement('h4');
    message.append(legend);
    legend.textContent = 'Theme';

    let div = document.createElement('div');
    div.classList.add('table-row');
    message.append(div);

    let action = 'toggle-contrast';
    let check = document.createElement('input');
    check.id = 'contrast-more-check';
    check.dataset.action = action;
    check.type = 'checkbox';
    check.classList.add('switch');

    let label = document.createElement('label');
    label.id = 'contrast-more-label';
    label.for = check.id;
    label.setAttribute('for', check.id);
    label.textContent = this.action_manager.text(action);

    check.checked = this.prefs.contrast_mode === Prefs.ContrastMore;

    div.append(check);
    div.append(label);

    check.addEventListener('input', (event_) => {
      const { checked } = event_.target;
      this.prefs.contrast_mode = checked
        ? Prefs.ContrastMore
        : Prefs.ContrastLess;
    });

    // accent

    legend = document.createElement('h4');
    message.append(legend);
    legend.textContent = 'Highlight color';

    div = document.createElement('div');
    div.classList.add('table-row');
    message.append(div);

    const highlight_style_option = (name, text, value) => {
      const input = document.createElement('input');
      input.id = value || 'highlight-default';
      input.name = name;
      input.type = 'radio';
      input.value = value;
      input.checked = document.body.classList.contains(value) || !value;
      div.append(input);

      input.addEventListener('change', (event_) => {
        const { checked, value } = event_.target;
        if (checked) {
          this.prefs.highlight = value;
        }
      });

      const label = document.createElement('label');
      label.for = input.id;
      label.setAttribute('for', input.id);
      label.textContent = text;
      div.append(label, document.createElement('BR'));
    }

    highlight_style_option('accent-picker', 'Legacy', null);
    highlight_style_option('accent-picker', 'System', 'highlight-system');

    elements.confirm.remove();
    delete elements.confirm;

    return elements;
  }
}