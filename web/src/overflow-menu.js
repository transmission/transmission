/* @license This file Copyright © Mnemosyne LLC.
   It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
   or any future license endorsed by Mnemosyne LLC.
   License text can be found in the licenses/ folder. */

import { Formatter } from './formatter.js';
import { Prefs } from './prefs.js';
import { RPC } from './remote.js';
import { makeUUID, OutsideClickListener, setEnabled } from './utils.js';

function make_section(classname, title) {
  const section = document.createElement('fieldset');
  section.classList.add('section', classname);
  const legend = document.createElement('legend');
  legend.classList.add('title');
  legend.textContent = title;
  section.append(legend);
  return section;
}

function make_button(parent, text, action, on_click) {
  const button = document.createElement('button');
  button.textContent = text;
  button.addEventListener('click', on_click);
  parent.append(button);
  button.dataset.action = action;
  return button;
}

export class OverflowMenu extends EventTarget {
  constructor(session_manager, prefs, remote, action_manager) {
    super();

    this.action_listener = this._onActionChange.bind(this);
    this.action_manager = action_manager;
    this.action_manager.addEventListener('change', this.action_listener);

    this.prefs_listener = this._onPrefsChange.bind(this);
    this.prefs = prefs;
    this.prefs.addEventListener('change', this.prefs_listener);

    this.closed = false;
    this.remote = remote;
    this.name = 'overflow-menu';

    this.session_listener = this._onSessionChange.bind(this);
    this.session_manager = session_manager;
    this.session_manager.addEventListener(
      'session-change',
      this.session_listener,
    );

    const { session_properties } = session_manager;
    Object.assign(this, this._create(session_properties));

    this.outside = new OutsideClickListener(this.root);
    this.outside.addEventListener('click', () => this.close());
    Object.seal(this);

    this.show();
  }

  show() {
    document.body.append(this.root);
  }

  close() {
    if (!this.closed) {
      this.outside.stop();
      this.session_manager.removeEventListener(
        'session-change',
        this.session_listener,
      );
      this.action_manager.removeEventListener('change', this.action_listener);
      this.prefs.removeEventListener('change', this.prefs_listener);

      this.root.remove();
      this.dispatchEvent(new Event('close'));

      for (const key of Object.keys(this)) {
        this[key] = null;
      }
      this.closed = true;
    }
  }

  _onSessionChange(event_) {
    const { alt_speed_check } = this.elements;
    const { session_properties } = event_;
    alt_speed_check.checked = session_properties[RPC._TurtleState];
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

  _onActionChange(event_) {
    const element = this.actions[event_.action];
    if (element) {
      this._updateElement(element);
    }
  }

  _updateElement(element) {
    if (element.dataset.action) {
      const { action } = element.dataset;
      const shortcuts = this.action_manager.keyshortcuts(action);
      if (shortcuts) {
        element.setAttribute('aria-keyshortcuts', shortcuts);
      }
      setEnabled(element, this.action_manager.isEnabled(action));
    }
  }

  _onClick(event_) {
    const { action, pref } = event_.target.dataset;

    if (action) {
      this.action_manager.click(action);
      return;
    }

    if (pref) {
      this.prefs[pref] = event_.target.value;
      return;
    }

    console.log('unhandled');
    console.log(event_);
    console.trace();
  }

  _create(session_properties) {
    const actions = {};
    const elements = {};
    const on_click = this._onClick.bind(this);

    const root = document.createElement('div');
    root.classList.add('overflow-menu', 'popup');

    let div = document.createElement('div');

    const add_checkbox = (text, listener) => {
      const check = document.createElement('input');
      check.id = makeUUID();
      check.type = 'checkbox';

      const label = document.createElement('label');
      label.htmlFor = check.id;
      label.textContent = text;

      div.append(check, label);
      listener(check);
    };

    // DISPLAY

    let section = make_section('display', 'Display');
    root.append(section);

    let options = document.createElement('div');
    options.id = 'display-options';
    section.append(options);

    // sort mode

    div.classList.add('table-row');
    options.append(div);

    let label = document.createElement('label');
    label.id = 'display-sort-mode-label';
    label.textContent = 'Sort by';
    div.append(label);

    let select = document.createElement('select');
    select.id = 'display-sort-mode-select';
    select.dataset.pref = Prefs.SortMode;
    div.append(select);

    const sort_modes = [
      [Prefs.SortByActivity, 'Activity'],
      [Prefs.SortByAge, 'Age'],
      [Prefs.SortByName, 'Name'],
      [Prefs.SortByProgress, 'Progress'],
      [Prefs.SortByQueue, 'Queue order'],
      [Prefs.SortByRatio, 'Ratio'],
      [Prefs.SortBySize, 'Size'],
      [Prefs.SortByState, 'State'],
    ];
    for (const [value, text] of sort_modes) {
      const option = document.createElement('option');
      option.value = value;
      option.textContent = text;
      select.append(option);
    }

    label.htmlFor = select.id;
    select.value = this.prefs.sort_mode;
    select.addEventListener('change', (event_) => {
      this.prefs.sort_mode = event_.target.value;
    });

    // sort direction

    div = document.createElement('div');
    div.classList.add('table-row');
    options.append(div);

    let listener = (e) => {
      e.dataset.pref = Prefs.SortDirection;
      e.checked = this.prefs.sort_direction !== Prefs.SortAscending;
      e.addEventListener('input', (event_) => {
        this.prefs.sort_direction = event_.target.checked
          ? Prefs.SortDescending
          : Prefs.SortAscending;
      });
    };

    add_checkbox('Reverse sort', listener);

    // compact

    div = document.createElement('div');
    div.classList.add('table-row');
    options.append(div);

    listener = (e) => {
      e.checked = this.prefs.display_mode === Prefs.DisplayCompact;
      e.addEventListener('change', (event_) => {
        const { checked } = event_.target;
        this.prefs.display_mode = checked
          ? Prefs.DisplayCompact
          : Prefs.DisplayFull;
      });
    };

    add_checkbox(this.action_manager.text('toggle-compact-rows'), listener);

    // contrast

    div = document.createElement('div');
    div.classList.add('table-row');
    options.append(div);

    listener = (e) => {
      e.checked = this.prefs.contrast_mode === Prefs.ContrastMore;
      e.addEventListener('change', (event_) => {
        const { checked } = event_.target;
        this.prefs.contrast_mode = checked
          ? Prefs.ContrastMore
          : Prefs.ContrastLess;
      });
    };

    add_checkbox(this.action_manager.text('toggle-contrast'), listener);

    // fullscreen

    div = document.createElement('div');
    div.classList.add('table-row', 'display-fullscreen-row');
    options.append(div);

    listener = (e) => {
      const is_fullscreen = () => document.fullscreenElement !== null;
      e.checked = is_fullscreen();
      document.addEventListener('fullscreenchange', () => {
        e.checked = is_fullscreen();
      });

      e.addEventListener('change', () => {
        if (is_fullscreen()) {
          document.exitFullscreen();
        } else {
          document.body.requestFullscreen();
        }
      });
    };

    add_checkbox('Fullscreen', listener);

    // SPEED LIMIT

    section = make_section('speed', 'Speed Limit');
    root.append(section);

    options = document.createElement('div');
    options.id = 'speed-options';
    section.append(options);

    // speed up

    div = document.createElement('div');
    div.classList.add('speed-up');
    options.append(div);

    label = document.createElement('label');
    label.id = 'speed-up-label';
    label.textContent = 'Upload:';
    div.append(label);

    const unlimited = 'Unlimited';
    select = document.createElement('select');
    select.id = 'speed-up-select';
    div.append(select);

    const speeds = [
      '50',
      '100',
      '250',
      '500',
      '1000',
      '2500',
      '5000',
      '10000',
      unlimited,
    ];
    for (const speed of [
      ...new Set(speeds)
        .add(`${session_properties[RPC._UpSpeedLimit]}`)
        .values(),
    ].sort((a, b) => a - b)) {
      const option = document.createElement('option');
      option.value = speed;
      option.textContent =
        speed === unlimited ? unlimited : Formatter.speed(speed);
      select.append(option);
    }

    label.htmlFor = select.id;
    select.value = session_properties[RPC._UpSpeedLimited]
      ? `${session_properties[RPC._UpSpeedLimit]}`
      : unlimited;
    select.addEventListener('change', (event_) => {
      const { value } = event_.target;
      console.log(event_);
      if (value === unlimited) {
        this.remote.savePrefs({ [RPC._UpSpeedLimited]: false });
      } else {
        this.remote.savePrefs({
          [RPC._UpSpeedLimited]: true,
          [RPC._UpSpeedLimit]: Number.parseInt(value, 10),
        });
      }
    });

    // speed down

    div = document.createElement('div');
    div.classList.add('speed-down');
    options.append(div);

    label = document.createElement('label');
    label.id = 'speed-down-label';
    label.textContent = 'Download:';
    div.append(label);

    select = document.createElement('select');
    select.id = 'speed-down-select';
    div.append(select);

    for (const speed of [
      ...new Set(speeds)
        .add(`${session_properties[RPC._DownSpeedLimit]}`)
        .values(),
    ].sort((a, b) => a - b)) {
      const option = document.createElement('option');
      option.value = speed;
      option.textContent =
        speed === unlimited ? unlimited : Formatter.speed(speed);
      select.append(option);
    }

    label.htmlFor = select.id;
    select.value = session_properties[RPC._DownSpeedLimited]
      ? `${session_properties[RPC._DownSpeedLimit]}`
      : unlimited;
    select.addEventListener('change', (event_) => {
      const { value } = event_.target;
      console.log(event_);
      if (value === unlimited) {
        this.remote.savePrefs({ [RPC._DownSpeedLimited]: false });
      } else {
        this.remote.savePrefs({
          [RPC._DownSpeedLimited]: true,
          [RPC._DownSpeedLimit]: Number.parseInt(value, 10),
        });
      }
    });

    // alt speed

    div = document.createElement('div');
    div.classList.add('alt-speed');
    options.append(div);

    listener = (e) => {
      e.checked = session_properties[RPC._TurtleState];
      elements.alt_speed_check = e;
      e.addEventListener('change', (event_) => {
        this.remote.savePrefs({
          [RPC._TurtleState]: event_.target.checked,
        });
      });

      const turtle = document.createElement('label');
      turtle.htmlFor = e.id;
      turtle.id = 'alt-speed-image';

      const parentheses = document.createElement('label');
      parentheses.htmlFor = e.id;

      const up = Formatter.speed(session_properties[RPC._TurtleUpSpeedLimit]);
      const dn = Formatter.speed(session_properties[RPC._TurtleDownSpeedLimit]);
      parentheses.textContent = `(${up} up, ${dn} down)`;

      div.append(turtle, parentheses);
    };

    add_checkbox('Use Temp limits', listener);

    // ACTIONS

    section = make_section('actions', 'Actions');
    root.append(section);

    for (const action_name of [
      'show-preferences-dialog',
      'show-shortcuts-dialog',
      'pause-all-torrents',
      'start-all-torrents',
    ]) {
      const text = this.action_manager.text(action_name);
      actions[action_name] = make_button(section, text, action_name, on_click);
    }

    // HELP

    section = make_section('help', 'Help');
    root.append(section);

    options = document.createElement('div');
    section.append(options);

    for (const action_name of ['show-statistics-dialog', 'show-about-dialog']) {
      const text = this.action_manager.text(action_name);
      actions[action_name] = make_button(options, text, action_name, on_click);
    }

    const e = document.createElement('a');
    e.href = 'https://transmissionbt.com/donate.html';
    e.target = '_blank';
    e.textContent = 'Donate';
    options.append(e);

    this._updateElement = this._updateElement.bind(this);

    return { actions, elements, root };
  }
}
