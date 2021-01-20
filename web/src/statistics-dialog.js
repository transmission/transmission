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
  Utils,
  setTextContent,
  createDialogContainer,
  createInfoSection,
} from './utils.js';

export class StatisticsDialog extends EventTarget {
  constructor(remote) {
    super();

    this.remote = remote;

    const updateDaemon = () =>
      this.remote.loadDaemonStats((data) => this._update(data.arguments));
    const delay_msec = 5000;
    this.interval = setInterval(updateDaemon, delay_msec);
    updateDaemon();

    this.elements = StatisticsDialog._create();
    this.elements.dismiss.addEventListener('click', () => this._onDismiss());
    document.body.append(this.elements.root);
    this.elements.dismiss.focus();
  }

  close() {
    if (!this.closed) {
      clearInterval(this.interval);
      this.elements.root.remove();
      for (const key of Object.keys(this)) {
        delete this[key];
      }
      this.closed = true;
    }
  }

  _onDismiss() {
    this.close();
  }

  _update(stats) {
    console.log(stats);
    const fmt = Formatter;

    let s = stats['current-stats'];
    let ratio = Utils.ratio(s.uploadedBytes, s.downloadedBytes);
    setTextContent(this.elements.session.up, fmt.size(s.uploadedBytes));
    setTextContent(this.elements.session.down, fmt.size(s.downloadedBytes));
    setTextContent(this.elements.session.ratio, fmt.ratioString(ratio));
    setTextContent(
      this.elements.session.time,
      fmt.timeInterval(s.secondsActive)
    );

    s = stats['cumulative-stats'];
    ratio = Utils.ratio(s.uploadedBytes, s.downloadedBytes);
    setTextContent(this.elements.total.up, fmt.size(s.uploadedBytes));
    setTextContent(this.elements.total.down, fmt.size(s.downloadedBytes));
    setTextContent(this.elements.total.ratio, fmt.ratioString(ratio));
    setTextContent(this.elements.total.time, fmt.timeInterval(s.secondsActive));
  }

  static _create() {
    const elements = createDialogContainer('statistics-dialog');
    const { workarea } = elements;
    elements.confirm.remove();
    elements.dismiss.textContent = 'Close';
    delete elements.confirm;

    const heading_text = 'Statistics';
    elements.root.setAttribute('aria-label', heading_text);
    elements.heading.textContent = heading_text;

    const labels = ['Uploaded:', 'Downloaded:', 'Ratio:', 'Running time:'];
    let section = createInfoSection('Current session', labels);
    const [sup, sdown, sratio, stime] = section.children;
    const session = (elements.session = {});
    session.up = sup;
    session.down = sdown;
    session.ratio = sratio;
    session.time = stime;
    workarea.append(section.root);

    section = createInfoSection('Total', labels);
    const [tup, tdown, tratio, ttime] = section.children;
    const total = (elements.total = {});
    total.up = tup;
    total.down = tdown;
    total.ratio = tratio;
    total.time = ttime;
    workarea.append(section.root);

    return elements;
  }
}
