/**
 * Copyright © Dave Perrett, Malcolm Jarvis and Artem Vorotnikov
 *
 * This file is licensed under the GPLv2.
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 */

import 'jquery-ui/ui/core';
import 'jquery-ui/ui/position';
import 'jquery-ui/ui/widget';
import 'jquery-ui/ui/widgets/dialog';
import 'jquery-ui/ui/widgets/menu';
import 'jquery-ui/ui/widgets/tabs';

import 'jquery-ui/themes/base/base.css';
import 'jquery-ui/themes/base/button.css';
import 'jquery-ui/themes/base/core.css';
import 'jquery-ui/themes/base/dialog.css';
import 'jquery-ui/themes/base/menu.css';
import 'jquery-ui/themes/base/selectmenu.css';
import 'jquery-ui/themes/base/tabs.css';
import 'jquery-ui/themes/base/theme.css';

import './deprecated/jquery.ui-contextmenu.js';
import './deprecated/jquery.transmenu.js';

import { Dialog } from './dialog.js';
import { Notifications } from './notifications.js';
import { Prefs } from './prefs.js';
import { Transmission } from './transmission.js';
import { Utils } from './utils.js';

import '../style/transmission-app.scss';

// http://forum.jquery.com/topic/combining-ui-dialog-and-tabs
$.fn.tabbedDialog = function (dialog_opts) {
  this.tabs({
    selected: 0,
  });
  this.dialog(dialog_opts);
  this.find('.ui-tab-dialog-close').append(this.parent().find('.ui-dialog-titlebar-close'));
  this.find('.ui-tab-dialog-close').css({
    position: 'absolute',
    right: '0',
    top: '16px',
  });
  this.find('.ui-tab-dialog-close > a').css({
    float: 'none',
    padding: '0',
  });
  const tabul = this.find('ul:first');
  this.parent().addClass('ui-tabs').prepend(tabul).draggable('option', 'handle', tabul);
  this.siblings('.ui-dialog-titlebar').remove();
  tabul.addClass('ui-dialog-titlebar');
};

function main() {
  const scroll_soon = Utils.debounce(() => window.scrollTo(0, 1));
  window.onload = scroll_soon;
  window.onorientationchange = scroll_soon;

  const prefs = new Prefs();
  const dialog = new Dialog();
  const notifications = new Notifications(prefs);
  // eslint-disable-next-line no-unused-vars
  const transmission = new Transmission(dialog, notifications, prefs);
}

document.addEventListener('DOMContentLoaded', main);
