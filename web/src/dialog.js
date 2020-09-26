/**
 * Copyright © Dave Perrett and Malcolm Jarvis
 *
 * This file is licensed under the GPLv2.
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 */

import { isMobileDevice, Utils } from './utils.js';

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
    this._confirm_button.addEventListener('click', this.executeCallback.bind(this));
  }

  /// EVENT FUNCTIONS

  executeCallback() {
    this._callback();
    this.hideDialog();
  }

  hideDialog() {
    document.body.classList.remove('dialog-showing');
    Utils.hide(this._container);
    // transmission.hideMobileAddressbar();
    // transmission.updateButtonStates();
  }

  static isVisible() {
    return document.body.classList.contains('dialog-showing');
  }

  static hideAllDialogs() {
    [...document.getElementsByClassName('dialog-container')].forEach((e) => Utils.hide(e));
  }

  /// INTERFACE FUNCTIONS

  // display a confirm dialog
  confirm(dialog_heading, dialog_message, confirm_button_label, callback, cancel_button_label) {
    Dialog.hideAllDialogs();
    Utils.setTextContent(this._heading, dialog_heading);
    Utils.setTextContent(this._message, dialog_message);
    Utils.setTextContent(this._cancel_button, cancel_button_label || 'Cancel');
    Utils.setTextContent(this._confirm_button, confirm_button_label);
    this._callback = callback;

    if (isMobileDevice) {
      // transmission.hideMobileAddressbar();
    }

    document.body.classList.add('dialog-showing');
    Utils.show(this._confirm_button);
    Utils.show(this._container);
    // transmission.updateButtonStates();
  }

  // display an alert dialog
  alert(dialog_heading, dialog_message, cancel_button_label) {
    Dialog.hideAllDialogs();
    Utils.setTextContent(this._heading, dialog_heading);
    Utils.setTextContent(this._message, dialog_message);
    Utils.setTextContent(this._cancel_button, cancel_button_label);

    if (isMobileDevice) {
      // transmission.hideMobileAddressbar();
    }

    document.body.classList.add('dialog-showing');
    Utils.hide(this._confirm_button);
    Utils.show(this._container);
    // transmission.updateButtonStates();
  }
}
