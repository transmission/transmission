/**
 * Copyright © Dave Perrett and Malcolm Jarvis
 *
 * This file is licensed under the GPLv2.
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 */

export class Dialog {
  static CreateContainer(id) {
    const root = document.createElement('div');
    root.classList.add('dialog-container');
    root.classList.add('popup');
    root.classList.add(id);
    root.setAttribute('role', 'dialog');

    const win = document.createElement('div');
    win.classList.add('dialog-window');
    root.appendChild(win);

    const logo = document.createElement('div');
    logo.classList.add('dialog-logo');
    win.appendChild(logo);

    const heading = document.createElement('div');
    heading.classList.add('dialog-heading');
    win.appendChild(heading);

    const message = document.createElement('div');
    message.classList.add('dialog-message');
    win.appendChild(message);

    const workarea = document.createElement('div');
    workarea.classList.add('dialog-workarea');
    win.appendChild(workarea);

    const buttons = document.createElement('div');
    buttons.classList.add('dialog-buttons');
    win.appendChild(buttons);

    const bbegin = document.createElement('span');
    bbegin.classList.add('dialog-buttons-begin');
    buttons.appendChild(bbegin);

    const dismiss = document.createElement('button');
    dismiss.classList.add('dialog-dismiss-button');
    dismiss.textContent = 'Cancel';
    buttons.appendChild(dismiss);

    const confirm = document.createElement('button');
    confirm.textContent = 'OK';
    buttons.appendChild(confirm);

    const bend = document.createElement('span');
    bend.classList.add('dialog-buttons-end');
    buttons.appendChild(bend);

    return {
      confirm,
      dismiss,
      heading,
      message,
      root,
      workarea,
    };
  }
}
