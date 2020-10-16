/*
 * This file Copyright (C) 2020 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 */

import { Formatter } from './formatter.js';
import { Prefs } from './prefs.js';
import { RPC } from './remote.js';
import { OutsideClickListener, setEnabled } from './utils.js';

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
      this.session_listener
    );

    const { session_properties } = session_manager;
    Object.assign(this, this._create(session_properties));

    this.outside = new OutsideClickListener(this.root);
    this.outside.addEventListener('click', () => this.close());
    Object.seal(this);

    this.show();
  }

  show() {
    document.body.appendChild(this.root);
  }

  close() {
    if (!this.closed) {
      this.outside.stop();
      this.session_manager.removeEventListener(
        'session-change',
        this.session_listener
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

  _onSessionChange(ev) {
    const { alt_speed_check } = this.elements;
    const { session_properties } = ev;
    alt_speed_check.checked = session_properties[RPC._TurtleState];
  }

  _onPrefsChange(ev) {
    switch (ev.key) {
      case Prefs.SortDirection:
      case Prefs.SortMode:
        this.root.querySelector(`[data-pref="${ev.key}"]`).value = ev.value;
        break;
      default:
        break;
    }
  }

  _onActionChange(ev) {
    const el = this.actions[ev.action];
    if (el) {
      this._updateElement(el);
    }
  }

  _updateElement(el) {
    if (el.dataset.action) {
      const { action } = el.dataset;
      const shortcuts = this.action_manager.keyshortcuts(action);
      if (shortcuts) {
        el.setAttribute('aria-keyshortcuts', shortcuts);
      }
      setEnabled(el, this.action_manager.isEnabled(action));
    }
  }

  _onClick(ev) {
    const { action, pref } = ev.target.dataset;

    if (action) {
      this.action_manager.click(action);
      return;
    }

    if (pref) {
      this.prefs[pref] = ev.target.value;
      return;
    }

    console.log('unhandled');
    console.log(ev);
    console.trace();
  }

  _create(session_properties) {
    const actions = {};
    const on_click = this._onClick.bind(this);
    const elements = {};

    const make_section = (classname, title) => {
      const section = document.createElement('fieldset');
      section.classList.add('section', classname);
      const legend = document.createElement('legend');
      legend.classList.add('title');
      legend.textContent = title;
      section.appendChild(legend);
      return section;
    };

    const make_button = (parent, text, action) => {
      const e = document.createElement('button');
      e.textContent = text;
      e.addEventListener('click', on_click);
      parent.appendChild(e);
      if (action) {
        e.dataset.action = action;
      }
      return e;
    };

    const root = document.createElement('div');
    root.classList.add('overflow-menu', 'popup');

    let section = make_section('display', 'Display');
    root.appendChild(section);

    let options = document.createElement('div');
    options.id = 'display-options';
    section.appendChild(options);

    // sort mode

    let div = document.createElement('div');
    options.appendChild(div);

    let label = document.createElement('label');
    label.id = 'display-sort-mode-label';
    label.textContent = 'Sort by';
    div.appendChild(label);

    let select = document.createElement('select');
    select.id = 'display-sort-mode-select';
    select.dataset.pref = Prefs.SortMode;
    div.appendChild(select);

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
      select.appendChild(option);
    }

    label.setAttribute('for', select.id);
    select.value = this.prefs.sort_mode;
    select.addEventListener('change', (ev) => {
      this.prefs.sort_mode = ev.target.value;
    });

    // sort direction

    div = document.createElement('div');
    options.appendChild(div);

    let check = document.createElement('input');
    check.id = 'display-sort-reverse-check';
    check.dataset.pref = Prefs.SortDirection;
    check.type = 'checkbox';
    div.appendChild(check);

    label = document.createElement('label');
    label.id = 'display-sort-reverse-label';
    label.setAttribute('for', check.id);
    label.textContent = 'Reverse sort';
    div.appendChild(label);

    check.checked = this.prefs.sort_direction !== Prefs.SortAscending;
    check.addEventListener('input', (ev) => {
      this.prefs.sort_direction = ev.target.checked
        ? Prefs.SortDescending
        : Prefs.SortAscending;
    });

    // compact

    div = document.createElement('div');
    options.appendChild(div);

    const action = 'toggle-compact-rows';
    check = document.createElement('input');
    check.id = 'display-compact-check';
    check.dataset.action = action;
    check.type = 'checkbox';
    div.appendChild(check);

    label = document.createElement('label');
    label.id = 'display-compact-label';
    label.for = check.id;
    label.setAttribute('for', check.id);
    label.textContent = this.action_manager.text(action);
    div.appendChild(label);

    check.checked = this.prefs.display_mode === Prefs.DisplayCompact;
    check.addEventListener('input', (ev) => {
      const { checked } = ev.target;
      this.prefs.display_mode = checked
        ? Prefs.DisplayCompact
        : Prefs.DisplayFull;
    });

    // fullscreen

    div = document.createElement('div');
    options.appendChild(div);

    check = document.createElement('input');
    check.id = 'display-fullscreen-check';
    check.type = 'checkbox';
    const is_fullscreen = () => document.fullscreenElement !== null;
    check.checked = is_fullscreen();
    check.addEventListener('input', () => {
      if (is_fullscreen()) {
        document.exitFullscreen();
      } else {
        document.body.requestFullscreen();
      }
    });
    document.addEventListener('fullscreenchange', () => {
      check.checked = is_fullscreen();
    });
    div.appendChild(check);

    label = document.createElement('label');
    label.id = 'display-fullscreen-label';
    label.for = check.id;
    label.setAttribute('for', check.id);
    label.textContent = 'Fullscreen';
    div.appendChild(label);

    section = make_section('speed', 'Speed Limit');
    root.appendChild(section);

    options = document.createElement('div');
    options.id = 'speed-options';
    section.appendChild(options);

    // speed up

    div = document.createElement('div');
    div.classList.add('speed-up');
    options.appendChild(div);

    label = document.createElement('label');
    label.id = 'speed-up-label';
    label.textContent = 'Upload:';
    div.appendChild(label);

    const unlimited = 'Unlimited';
    select = document.createElement('select');
    select.id = 'speed-up-select';
    div.appendChild(select);

    const speeds = ['10', '100', '200', '500', '750', unlimited];
    for (const speed of [
      ...new Set(speeds)
        .add(`${session_properties[RPC._UpSpeedLimit]}`)
        .values(),
    ].sort()) {
      const option = document.createElement('option');
      option.value = speed;
      option.textContent =
        speed === unlimited ? unlimited : Formatter.speed(speed);
      select.appendChild(option);
    }

    label.setAttribute('for', select.id);
    select.value = session_properties[RPC._UpSpeedLimited]
      ? `${session_properties[RPC._UpSpeedLimit]}`
      : unlimited;
    select.addEventListener('change', (ev) => {
      const { value } = ev.target;
      console.log(ev);
      if (ev.target.value === unlimited) {
        this.remote.savePrefs({ [RPC._UpSpeedLimited]: false });
      } else {
        this.remote.savePrefs({
          [RPC._UpSpeedLimited]: true,
          [RPC._UpSpeedLimit]: parseInt(value, 10),
        });
      }
    });

    // speed down

    div = document.createElement('div');
    div.classList.add('speed-down');
    options.appendChild(div);

    label = document.createElement('label');
    label.id = 'speed-down-label';
    label.textContent = 'Download:';
    div.appendChild(label);

    select = document.createElement('select');
    select.id = 'speed-down-select';
    div.appendChild(select);

    for (const speed of [
      ...new Set(speeds)
        .add(`${session_properties[RPC._DownSpeedLimit]}`)
        .values(),
    ].sort()) {
      const option = document.createElement('option');
      option.value = speed;
      option.textContent = speed;
      select.appendChild(option);
    }

    label.setAttribute('for', select.id);
    select.value = session_properties[RPC._DownSpeedLimited]
      ? `${session_properties[RPC._DownSpeedLimit]}`
      : unlimited;
    select.addEventListener('change', (ev) => {
      const { value } = ev.target;
      console.log(ev);
      if (ev.target.value === unlimited) {
        this.remote.savePrefs({ [RPC._DownSpeedLimited]: false });
      } else {
        this.remote.savePrefs({
          [RPC._DownSpeedLimited]: true,
          [RPC._DownSpeedLimit]: parseInt(value, 10),
        });
      }
    });

    // alt speed

    div = document.createElement('div');
    div.classList.add('alt-speed');
    options.appendChild(div);

    check = document.createElement('input');
    check.id = 'alt-speed-check';
    check.type = 'checkbox';
    check.checked = session_properties[RPC._TurtleState];
    check.addEventListener('change', (ev) => {
      this.remote.savePrefs({
        [RPC._TurtleState]: ev.target.checked,
      });
    });
    div.appendChild(check);
    elements.alt_speed_check = check;

    label = document.createElement('label');
    label.id = 'alt-speed-image';
    label.setAttribute('for', check.id);
    div.appendChild(label);

    label = document.createElement('label');
    label.id = 'alt-speed-label';
    label.setAttribute('for', check.id);
    label.textContent = 'Use Temp limits';
    div.appendChild(label);

    label = document.createElement('label');
    label.id = 'alt-speed-values-label';
    label.setAttribute('for', check.id);

    const up = Formatter.speed(session_properties[RPC._TurtleUpSpeedLimit]);
    const dn = Formatter.speed(session_properties[RPC._TurtleDownSpeedLimit]);
    label.textContent = `(${up} up, ${dn} down)`;
    div.appendChild(label);

    section = make_section('actions', 'Actions');
    root.appendChild(section);

    for (const action_name of [
      'show-preferences-dialog',
      'pause-all-torrents',
      'start-all-torrents',
    ]) {
      const text = this.action_manager.text(action_name);
      actions[action_name] = make_button(section, text, action_name);
    }

    section = make_section('info', 'Info');
    root.appendChild(section);

    options = document.createElement('div');
    section.appendChild(options);

    for (const action_name of [
      'show-about-dialog',
      'show-shortcuts-dialog',
      'show-statistics-dialog',
    ]) {
      const text = this.action_manager.text(action_name);
      actions[action_name] = make_button(options, text, action_name);
    }

    section = make_section('links', 'Links');
    root.appendChild(section);

    options = document.createElement('div');
    section.appendChild(options);

    let e = document.createElement('a');
    e.href = 'https://transmissionbt.com/';
    e.tabindex = '0';
    e.textContent = 'Homepage';
    options.appendChild(e);

    e = document.createElement('a');
    e.href = 'https://transmissionbt.com/donate/';
    e.tabindex = '0';
    e.textContent = 'Tip Jar';
    options.appendChild(e);

    e = document.createElement('a');
    e.href = 'https://github.com/transmission/transmission/';
    e.tabindex = '0';
    e.textContent = 'Source Code';
    options.appendChild(e);

    Object.values(actions).forEach(this._updateElement.bind(this));
    return { actions, elements, root };
  }
}
