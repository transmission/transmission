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
    this._container = $('#dialog_container');
    this._heading = $('#dialog_heading');
    this._message = $('#dialog_message');
    this._cancel_button = $('#dialog_cancel_button');
    this._confirm_button = $('#dialog_confirm_button');
    this._callback = null;

    // Observe the buttons
    this._cancel_button.bind(
      'click',
      {
        dialog: this,
      },
      this.onCancelClicked
    );
    this._confirm_button.bind(
      'click',
      {
        dialog: this,
      },
      this.onConfirmClicked
    );
  }

  /// EVENT FUNCTIONS

  executeCallback() {
    this._callback();
    dialog.hideDialog();
  }

  hideDialog() {
    $('body.dialog_showing').removeClass('dialog_showing');
    this._container.hide();
    transmission.hideMobileAddressbar();
    transmission.updateButtonStates();
  }

  isVisible() {
    return this._container.is(':visible');
  }

  static onCancelClicked(event) {
    event.data.dialog.hideDialog();
  }

  static onConfirmClicked(event) {
    event.data.dialog.executeCallback();
  }

  /// INTERFACE FUNCTIONS

  // display a confirm dialog
  confirm(dialog_heading, dialog_message, confirm_button_label, callback, cancel_button_label) {
    if (!isMobileDevice) {
      $('.dialog_container').hide();
    }
    setTextContent(this._heading[0], dialog_heading);
    setTextContent(this._message[0], dialog_message);
    setTextContent(this._cancel_button[0], cancel_button_label || 'Cancel');
    setTextContent(this._confirm_button[0], confirm_button_label);
    this._confirm_button.show();
    this._callback = callback;
    $('body').addClass('dialog_showing');
    this._container.show();
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
    setTextContent(this._heading[0], dialog_heading);
    setTextContent(this._message[0], dialog_message);
    // jquery::hide() doesn't work here in Safari for some odd reason
    this._confirm_button.css('display', 'none');
    setTextContent(this._cancel_button[0], cancel_button_label);
    // Just in case
    $('#upload_container').hide();
    $('#move_container').hide();
    $('body').addClass('dialog_showing');
    transmission.updateButtonStates();
    if (isMobileDevice) {
      transmission.hideMobileAddressbar();
    }
    this._container.show();
  }
}
