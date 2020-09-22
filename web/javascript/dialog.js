'use strict';

/**
 * Copyright © Dave Perrett and Malcolm Jarvis
 *
 * This file is licensed under the GPLv2.
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 */

class Dialog {
  constructor() {
    // private interface variables
    this._container = document.getElementById('dialog_container');
    this._heading = document.getElementById('dialog_heading');
    this._message = document.getElementById('dialog_message');
    this._cancel_button = document.getElementById('dialog_cancel_button');
    this._confirm_button = document.getElementById('dialog_confirm_button');
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
    document.body.classList.remove('dialog_showing');
    Utils.hide(this._container);
    transmission.hideMobileAddressbar();
    transmission.updateButtonStates();
  }

  static isVisible() {
    return document.body.classList.contains('dialog_showing');
  }

  static hideAllDialogs() {
    Utils.hide(document.getElementsByClassName('dialog_container'));
  }

  /// INTERFACE FUNCTIONS

  // display a confirm dialog
  confirm(dialog_heading, dialog_message, confirm_button_label, callback, cancel_button_label) {
    Dialog.hideAllDialogs();
    setTextContent(this._heading, dialog_heading);
    setTextContent(this._message, dialog_message);
    setTextContent(this._cancel_button, cancel_button_label || 'Cancel');
    setTextContent(this._confirm_button, confirm_button_label);
    this._callback = callback;

    if (isMobileDevice) {
      transmission.hideMobileAddressbar();
    }

    document.body.classList.add('dialog_showing');
    Utils.show([this._confirm_button, this._container]);
    transmission.updateButtonStates();
  }

  // display an alert dialog
  alert(dialog_heading, dialog_message, cancel_button_label) {
    Dialog.hideAllDialogs();
    setTextContent(this._heading, dialog_heading);
    setTextContent(this._message, dialog_message);
    setTextContent(this._cancel_button, cancel_button_label);

    if (isMobileDevice) {
      transmission.hideMobileAddressbar();
    }

    document.body.classList.add('dialog_showing');
    Utils.hide(this._confirm_button);
    Utils.show(this._container);

    transmission.updateButtonStates();
  }
}
