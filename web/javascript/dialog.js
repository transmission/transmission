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
    this._container.style.display = 'none';
    transmission.hideMobileAddressbar();
    transmission.updateButtonStates();
  }

  static isVisible() {
    document.body.classList.contains('dialog_showing');
  }

  /// INTERFACE FUNCTIONS

  // display a confirm dialog
  confirm(dialog_heading, dialog_message, confirm_button_label, callback, cancel_button_label) {
    if (!isMobileDevice) {
      $('.dialog_container').hide();
    }
    setTextContent(this._heading, dialog_heading);
    setTextContent(this._message, dialog_message);
    setTextContent(this._cancel_button, cancel_button_label || 'Cancel');
    setTextContent(this._confirm_button, confirm_button_label);
    this._confirm_button.style.display = 'block';
    this._callback = callback;
    document.body.classList.add('dialog_showing');
    this._container.style.display = 'block';
    transmission.updateButtonStates();
    if (isMobileDevice) {
      transmission.hideMobileAddressbar();
    }
  }

  // display an alert dialog
  alert(dialog_heading, dialog_message, cancel_button_label) {
    if (!isMobileDevice) {
      $('.dialog_container').hide();
    }
    setTextContent(this._heading, dialog_heading);
    setTextContent(this._message, dialog_message);
    // jquery::hide() doesn't work here in Safari for some odd reason
    this._confirm_button.style.display = 'none';
    setTextContent(this._cancel_button, cancel_button_label);
    // Just in case
    $('#upload_container').hide();
    $('#move_container').hide();
    document.body.classList.add('dialog_showing');
    transmission.updateButtonStates();
    if (isMobileDevice) {
      transmission.hideMobileAddressbar();
    }
    this._container.style.display = 'block';
  }
}
