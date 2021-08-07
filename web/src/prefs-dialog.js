/**
 * @license
 *
 * This file Copyright (C) 2020 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 */

import { Formatter } from './formatter.js';
import {
  OutsideClickListener,
  createTabsContainer,
  makeUUID,
  setEnabled,
  setTextContent,
} from './utils.js';

export class PrefsDialog extends EventTarget {
  static _initTimeDropDown(e) {
    for (let index = 0; index < 24 * 4; ++index) {
      const hour = Number.parseInt(index / 4, 10);
      const mins = (index % 4) * 15;
      const value = index * 15;
      const content = `${hour}:${mins || '00'}`;
      e.options[index] = new Option(content, value);
    }
  }

  static _initDayDropDown(e) {
    const options = [
      ['Everyday', '127'],
      ['Weekdays', '62'],
      ['Weekends', '65'],
      ['Sunday', '1'],
      ['Monday', '2'],
      ['Tuesday', '4'],
      ['Wednesday', '8'],
      ['Thursday', '16'],
      ['Friday', '32'],
      ['Saturday', '64'],
    ];
    for (let index = 0; options[index]; ++index) {
      const [text, value] = options[index];
      e.options[index] = new Option(text, value);
    }
  }

  _checkPort() {
    const element = this.elements.network.port_status_label;
    element.removeAttribute('data-open');
    setTextContent(element, 'Checking...');
    this.remote.checkPort(this._onPortChecked, this);
  }

  _onPortChecked(response) {
    const element = this.elements.network.port_status_label;
    const is_open = response.arguments['port-is-open'];
    element.dataset.open = is_open;
    setTextContent(element, is_open ? 'Open' : 'Closed');
  }

  _setBlocklistButtonEnabled(b) {
    const e = this.elements.peers.blocklist_update_button;
    setEnabled(e, b);
    e.value = b ? 'Update' : 'Updating...';
  }

  static _getValue(e) {
    switch (e.type) {
      case 'checkbox':
      case 'radio':
        return e.checked;

      case 'number':
      case 'text':
      case 'url': {
        const string = e.value;
        if (Number.parseInt(string, 10).toString() === string) {
          return Number.parseInt(string, 10);
        }
        if (Number.parseFloat(string).toString() === string) {
          return Number.parseFloat(string);
        }
        return string;
      }

      default:
        return null;
    }
  }

  // this callback is for controls whose changes can be applied
  // immediately, like checkboxs, radioboxes, and selects
  _onControlChanged(event_) {
    const { key } = event_.target.dataset;
    this.remote.savePrefs({
      [key]: PrefsDialog._getValue(event_.target),
    });
    if (key === 'peer-port' || key === 'port-forwarding-enabled') {
      this._checkPort();
    }
  }

  _onDialogClosed() {
    this.dispatchEvent(new Event('closed'));
  }

  // update the dialog's controls
  _update(o) {
    if (!o) {
      return;
    }

    this._setBlocklistButtonEnabled(true);

    for (const [key, value] of Object.entries(o)) {
      for (const element of this.elements.root.querySelectorAll(
        `[data-key="${key}"]`
      )) {
        if (key === 'blocklist-size') {
          const n = Formatter.number(value);
          element.innerHTML = `Blocklist has <span class="blocklist-size-number">${n}</span> rules`;
          setTextContent(this.elements.peers.blocklist_update_button, 'Update');
        } else {
          switch (element.type) {
            case 'checkbox':
            case 'radio':
              if (element.checked !== value) {
                element.checked = value;
                element.dispatchEvent(new Event('change'));
              }
              break;
            case 'text':
            case 'url':
            case 'email':
            case 'number':
            case 'search':
              // don't change the text if the user's editing it.
              // it's very annoying when that happens!
              if (
                // eslint-disable-next-line eqeqeq
                element.value != value &&
                element !== document.activeElement
              ) {
                element.value = value;
                element.dispatchEvent(new Event('change'));
              }
              break;
            case 'select-one':
              if (element.value !== value) {
                element.value = value;
                element.dispatchEvent(new Event('change'));
              }
              break;
            default:
              break;
          }
        }
      }
    }
  }

  shouldAddedTorrentsStart() {
    return this.data.elements.root.find('#start-added-torrents')[0].checked;
  }

  static _createCheckAndLabel(id, text) {
    const root = document.createElement('div');
    root.id = id;

    const check = document.createElement('input');
    check.id = makeUUID();
    check.type = 'checkbox';
    root.append(check);

    const label = document.createElement('label');
    label.textContent = text;
    label.setAttribute('for', check.id);
    root.append(label);

    return { check, label, root };
  }

  static _enableIfChecked(element, check) {
    const callback = () => {
      if (element.tagName === 'INPUT') {
        setEnabled(element, check.checked);
      } else {
        element.classList.toggle('disabled', !check.checked);
      }
    };
    check.addEventListener('change', callback);
    callback();
  }

  static _createTorrentsPage() {
    const root = document.createElement('div');
    root.classList.add('prefs-torrents-page');

    let label = document.createElement('div');
    label.textContent = 'Downloading';
    label.classList.add('section-label');
    root.append(label);

    label = document.createElement('label');
    label.textContent = 'Download to:';
    root.append(label);

    let input = document.createElement('input');
    input.type = 'text';
    input.id = makeUUID();
    input.dataset.key = 'download-dir';
    label.setAttribute('for', input.id);
    root.append(input);
    const download_dir = input;

    let cal = PrefsDialog._createCheckAndLabel(
      'autostart-div',
      'Start when added'
    );
    cal.check.dataset.key = 'start-added-torrents';
    root.append(cal.root);
    const autostart_check = cal.check;

    cal = PrefsDialog._createCheckAndLabel(
      'suffix-div',
      `Append "part" to incomplete files' names`
    );
    cal.check.dataset.key = 'rename-partial-files';
    root.append(cal.root);
    const suffix_check = cal.check;

    label = document.createElement('div');
    label.textContent = 'Seeding';
    label.classList.add('section-label');
    root.append(label);

    cal = PrefsDialog._createCheckAndLabel(
      'stop-ratio-div',
      'Stop seeding at ratio:'
    );
    cal.check.dataset.key = 'seedRatioLimited';
    root.append(cal.root);
    const stop_ratio_check = cal.check;

    input = document.createElement('input');
    input.type = 'number';
    input.dataset.key = 'seedRatioLimit';
    root.append(input);
    PrefsDialog._enableIfChecked(input, cal.check);
    const stop_ratio_input = input;

    cal = PrefsDialog._createCheckAndLabel(
      'stop-idle-div',
      'Stop seeding if idle for N mins:'
    );
    cal.check.dataset.key = 'idle-seeding-limit-enabled';
    root.append(cal.root);
    const stop_idle_check = cal.check;

    input = document.createElement('input');
    input.type = 'number';
    input.dataset.key = 'idle-seeding-limit';
    root.append(input);
    PrefsDialog._enableIfChecked(input, cal.check);
    const stop_idle_input = input;

    return {
      autostart_check,
      download_dir,
      root,
      stop_idle_check,
      stop_idle_input,
      stop_ratio_check,
      stop_ratio_input,
      suffix_check,
    };
  }

  static _createSpeedPage() {
    const root = document.createElement('div');
    root.classList.add('prefs-speed-page');

    let label = document.createElement('div');
    label.textContent = 'Speed Limits';
    label.classList.add('section-label');
    root.append(label);

    let cal = PrefsDialog._createCheckAndLabel(
      'upload-speed-div',
      'Upload (kB/s):'
    );
    cal.check.dataset.key = 'speed-limit-up-enabled';
    root.append(cal.root);
    const upload_speed_check = cal.check;

    let input = document.createElement('input');
    input.type = 'number';
    input.dataset.key = 'speed-limit-up';
    root.append(input);
    PrefsDialog._enableIfChecked(input, cal.check);
    const upload_speed_input = input;

    cal = PrefsDialog._createCheckAndLabel(
      'download-speed-div',
      'Download (kB/s):'
    );
    cal.check.dataset.key = 'speed-limit-down-enabled';
    root.append(cal.root);
    const download_speed_check = cal.check;

    input = document.createElement('input');
    input.type = 'number';
    input.dataset.key = 'speed-limit-down';
    root.append(input);
    PrefsDialog._enableIfChecked(input, cal.check);
    const download_speed_input = input;

    label = document.createElement('div');
    label.textContent = 'Alternative Speed Limits';
    label.classList.add('section-label', 'alt-speed-section-label');
    root.append(label);

    label = document.createElement('div');
    label.textContent =
      'Override normal speed limits manually or at scheduled times';
    label.classList.add('alt-speed-label');
    root.append(label);

    label = document.createElement('label');
    label.textContent = 'Upload (kB/s):';
    root.append(label);

    input = document.createElement('input');
    input.type = 'number';
    input.dataset.key = 'alt-speed-up';
    input.id = makeUUID();
    label.setAttribute('for', input.id);
    root.append(input);
    const alt_upload_speed_input = input;

    label = document.createElement('label');
    label.textContent = 'Download (kB/s):';
    root.append(label);

    input = document.createElement('input');
    input.type = 'number';
    input.dataset.key = 'alt-speed-down';
    input.id = makeUUID();
    label.setAttribute('for', input.id);
    root.append(input);
    const alt_download_speed_input = input;

    cal = PrefsDialog._createCheckAndLabel('alt-times-div', 'Scheduled times');
    cal.check.dataset.key = 'alt-speed-time-enabled';
    root.append(cal.root);
    const alt_times_check = cal.check;

    label = document.createElement('label');
    label.textContent = 'From:';
    PrefsDialog._enableIfChecked(label, cal.check);
    root.append(label);

    let select = document.createElement('select');
    select.id = makeUUID();
    select.dataset.key = 'alt-speed-time-begin';
    PrefsDialog._initTimeDropDown(select);
    label.setAttribute('for', select.id);
    root.append(select);
    PrefsDialog._enableIfChecked(select, cal.check);
    const alt_from_select = select;

    label = document.createElement('label');
    label.textContent = 'To:';
    PrefsDialog._enableIfChecked(label, cal.check);
    root.append(label);

    select = document.createElement('select');
    select.id = makeUUID();
    select.dataset.key = 'alt-speed-time-end';
    PrefsDialog._initTimeDropDown(select);
    label.setAttribute('for', select.id);
    root.append(select);
    PrefsDialog._enableIfChecked(select, cal.check);
    const alt_to_select = select;

    label = document.createElement('label');
    label.textContent = 'On days:';
    PrefsDialog._enableIfChecked(label, cal.check);
    root.append(label);

    select = document.createElement('select');
    select.id = makeUUID();
    select.dataset.key = 'alt-speed-time-day';
    PrefsDialog._initDayDropDown(select);
    label.setAttribute('for', select.id);
    root.append(select);
    PrefsDialog._enableIfChecked(select, cal.check);
    const alt_days_select = select;

    return {
      alt_days_select,
      alt_download_speed_input,
      alt_from_select,
      alt_times_check,
      alt_to_select,
      alt_upload_speed_input,
      download_speed_check,
      download_speed_input,
      root,
      upload_speed_check,
      upload_speed_input,
    };
  }

  static _createPeersPage() {
    const root = document.createElement('div');
    root.classList.add('prefs-peers-page');

    let label = document.createElement('div');
    label.textContent = 'Connections';
    label.classList.add('section-label');
    root.append(label);

    let cal = PrefsDialog._createCheckAndLabel(
      'max-peers-per-torrent-div',
      'Max peers per torrent:'
    );
    root.append(cal.root);
    const max_peers_per_torrent_check = cal.check;

    let input = document.createElement('input');
    input.type = 'number';
    input.dataset.key = 'peer-limit-per-torrent';
    root.append(input);
    PrefsDialog._enableIfChecked(input, cal.check);
    const max_peers_per_torrent_input = input;

    cal = PrefsDialog._createCheckAndLabel(
      'max-peers-overall-div',
      'Max peers overall:'
    );
    root.append(cal.root);
    const max_peers_overall_check = cal.check;

    input = document.createElement('input');
    input.type = 'number';
    input.dataset.key = 'peer-limit-global';
    root.append(input);
    PrefsDialog._enableIfChecked(input, cal.check);
    const max_peers_overall_input = input;

    label = document.createElement('div');
    label.textContent = 'Options';
    label.classList.add('section-label');
    root.append(label);

    label = document.createElement('label');
    label.textContent = 'Encryption mode:';
    root.append(label);

    const select = document.createElement('select');
    select.id = makeUUID();
    select.dataset.key = 'encryption';
    select.options[0] = new Option('Prefer encryption', 'preferred');
    select.options[1] = new Option('Allow encryption', 'tolerated');
    select.options[2] = new Option('Require encryption', 'required');
    root.append(select);
    const encryption_select = select;

    cal = PrefsDialog._createCheckAndLabel(
      'use-pex-div',
      'Use PEX to find more peers'
    );
    cal.check.title =
      "PEX is a tool for exchanging peer lists with the peers you're connected to.";
    cal.check.dataset.key = 'pex-enabled';
    cal.label.title = cal.check.title;
    root.append(cal.root);
    const pex_check = cal.check;

    cal = PrefsDialog._createCheckAndLabel(
      'use-dht-div',
      'Use DHT to find more peers'
    );
    cal.check.title = 'DHT is a tool for finding peers without a tracker.';
    cal.check.dataset.key = 'dht-enabled';
    cal.label.title = cal.check.title;
    root.append(cal.root);
    const dht_check = cal.check;

    cal = PrefsDialog._createCheckAndLabel(
      'use-lpd-div',
      'Use LPD to find more peers'
    );
    cal.check.title = 'LPD is a tool for finding peers on your local network.';
    cal.check.dataset.key = 'lpd-enabled';
    cal.label.title = cal.check.title;
    root.append(cal.root);
    const lpd_check = cal.check;

    label = document.createElement('div');
    label.textContent = 'Blocklist';
    label.classList.add('section-label');
    root.append(label);

    cal = PrefsDialog._createCheckAndLabel(
      'blocklist-enabled-div',
      'Enable blocklist:'
    );
    cal.check.dataset.key = 'blocklist-enabled';
    root.append(cal.root);
    const blocklist_enabled_check = cal.check;

    input = document.createElement('input');
    input.type = 'url';
    input.value = 'http://www.example.com/blocklist';
    input.dataset.key = 'blocklist-url';
    root.append(input);
    PrefsDialog._enableIfChecked(input, cal.check);
    const blocklist_url_input = input;

    label = document.createElement('label');
    label.textContent = 'Blocklist has {n} rules';
    label.dataset.key = 'blocklist-size';
    label.classList.add('blocklist-size-label');
    PrefsDialog._enableIfChecked(label, cal.check);
    root.append(label);

    const button = document.createElement('button');
    button.classList.add('blocklist-update-button');
    button.textContent = 'Update';
    root.append(button);
    PrefsDialog._enableIfChecked(button, cal.check);
    const blocklist_update_button = button;

    return {
      blocklist_enabled_check,
      blocklist_update_button,
      blocklist_url_input,
      dht_check,
      encryption_select,
      lpd_check,
      max_peers_overall_check,
      max_peers_overall_input,
      max_peers_per_torrent_check,
      max_peers_per_torrent_input,
      pex_check,
      root,
    };
  }

  static _createNetworkPage() {
    const root = document.createElement('div');
    root.classList.add('prefs-network-page');

    let label = document.createElement('div');
    label.textContent = 'Listening Port';
    label.classList.add('section-label');
    root.append(label);

    label = document.createElement('label');
    label.textContent = 'Peer listening port:';
    root.append(label);

    const input = document.createElement('input');
    input.type = 'number';
    input.dataset.key = 'peer-port';
    input.id = makeUUID();
    label.setAttribute('for', input.id);
    root.append(input);
    const port_input = input;

    const port_status_div = document.createElement('div');
    port_status_div.classList.add('port-status');
    label = document.createElement('label');
    label.textContent = 'Port is';
    port_status_div.append(label);
    const port_status_label = document.createElement('label');
    port_status_label.textContent = '?';
    port_status_label.classList.add('port-status-label');
    port_status_div.append(port_status_label);
    root.append(port_status_div);

    let cal = PrefsDialog._createCheckAndLabel(
      'randomize-port',
      'Randomize port on launch'
    );
    cal.check.dataset.key = 'peer-port-random-on-start';
    root.append(cal.root);
    const random_port_check = cal.check;

    cal = PrefsDialog._createCheckAndLabel(
      'port-forwarding',
      'Use port forwarding from my router'
    );
    cal.check.dataset.key = 'port-forwarding-enabled';
    root.append(cal.root);
    const port_forwarding_check = cal.check;

    label = document.createElement('div');
    label.textContent = 'Options';
    label.classList.add('section-label');
    root.append(label);

    cal = PrefsDialog._createCheckAndLabel(
      'utp-enabled',
      'Enable uTP for peer communication'
    );
    cal.check.dataset.key = 'utp-enabled';
    root.append(cal.root);
    const utp_check = cal.check;

    return {
      port_forwarding_check,
      port_input,
      port_status_label,
      random_port_check,
      root,
      utp_check,
    };
  }

  static _create() {
    const pages = {
      network: PrefsDialog._createNetworkPage(),
      peers: PrefsDialog._createPeersPage(),
      speed: PrefsDialog._createSpeedPage(),
      torrents: PrefsDialog._createTorrentsPage(),
    };

    const elements = createTabsContainer('prefs-dialog', [
      ['prefs-tab-torrent', pages.torrents.root],
      ['prefs-tab-speed', pages.speed.root],
      ['prefs-tab-peers', pages.peers.root],
      ['prefs-tab-network', pages.network.root],
    ]);

    return { ...elements, ...pages };
  }

  constructor(session_manager, remote) {
    super();

    this.closed = false;
    this.session_manager = session_manager;
    this.remote = remote;
    this.update_soon = () =>
      this._update(this.session_manager.session_properties);

    this.elements = PrefsDialog._create();
    this.elements.peers.blocklist_update_button.addEventListener(
      'click',
      (event_) => {
        setTextContent(event_.target, 'Updating blocklist...');
        this.remote.updateBlocklist();
        this._setBlocklistButtonEnabled(false);
      }
    );
    this.outside = new OutsideClickListener(this.elements.root);
    this.outside.addEventListener('click', () => this.close());

    Object.seal(this);

    // listen for user input
    const on_change = this._onControlChanged.bind(this);
    const walk = (o) => {
      for (const element of Object.values(o)) {
        if (element.tagName === 'INPUT') {
          switch (element.type) {
            case 'checkbox':
            case 'radio':
            case 'number':
            case 'text':
            case 'url':
              element.addEventListener('change', on_change);
              break;
            default:
              console.trace(`unhandled input: ${element.type}`);
              break;
          }
        }
      }
    };
    walk(this.elements.network);
    walk(this.elements.peers);
    walk(this.elements.speed);
    walk(this.elements.torrents);

    this.session_manager.addEventListener('session-change', this.update_soon);
    this.update_soon();

    document.body.append(this.elements.root);
  }

  close() {
    if (!this.closed) {
      this.outside.stop();
      this.session_manager.removeEventListener(
        'session-change',
        this.update_soon
      );
      this.elements.root.remove();
      dispatchEvent(new Event('close'));
      for (const key of Object.keys(this)) {
        this[key] = null;
      }
      this.closed = true;
    }
  }
}
