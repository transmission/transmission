/**
 * Copyright © Dave Perrett and Malcolm Jarvis
 *
 * This file is licensed under the GPLv2.
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 */

import { Utils } from './utils.js';

export class Dialog {
  constructor() {
    // private interface variables
    this._container = document.getElementById('dialog-container');
    this._heading = document.getElementById('dialog-heading');
    this._message = document.getElementById('dialog-message');
    this._cancel_button = document.getElementById('dialog-cancel-button');
    this._confirm_button = document.getElementById('dialog-confirm-button');
    this._callback = null;

    // Observe the buttons
    this._cancel_button.addEventListener('click', this.hideDialog.bind(this));
    this._confirm_button.addEventListener(
      'click',
      this.executeCallback.bind(this)
    );
  }

  /// EVENT FUNCTIONS

  executeCallback() {
    if (this._callback) {
      this._callback();
    }
    this.hideDialog();
  }

  hideDialog() {
    Utils.hide(this._container);
    this._callback = null;
  }

  /// INTERFACE FUNCTIONS

  // display a confirm dialog
  confirm(
    dialog_heading,
    dialog_message,
    confirm_button_label,
    callback,
    cancel_button_label
  ) {
    this._callback = callback;
    Utils.setTextContent(this._heading, dialog_heading);
    Utils.setTextContent(this._message, dialog_message);
    Utils.setTextContent(this._cancel_button, cancel_button_label || 'Cancel');
    Utils.setTextContent(this._confirm_button, confirm_button_label);
    Utils.show(this._confirm_button);
    Utils.show(this._container);
  }

  // display an alert dialog
  alert(dialog_heading, dialog_message, cancel_button_label) {
    Utils.setTextContent(this._heading, dialog_heading);
    Utils.setTextContent(this._message, dialog_message);
    Utils.setTextContent(this._cancel_button, cancel_button_label);
    Utils.hide(this._confirm_button);
    Utils.show(this._container);
  }
}
