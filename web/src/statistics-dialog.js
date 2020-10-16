/*
 * This file Copyright (C) 2020 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 */

import { Formatter } from './formatter.js';
import { Utils, createDialogContainer, createInfoSection } from './utils.js';

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
    document.body.appendChild(this.elements.root);
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
    this.elements.session.up.textContent = fmt.size(s.uploadedBytes);
    this.elements.session.down.textContent = fmt.size(s.downloadedBytes);
    this.elements.session.ratio.textContent = fmt.ratioString(ratio);
    this.elements.session.time.textContent = fmt.timeInterval(s.secondsActive);

    s = stats['cumulative-stats'];
    ratio = Utils.ratio(s.uploadedBytes, s.downloadedBytes);
    this.elements.total.up.textContent = fmt.size(s.uploadedBytes);
    this.elements.total.down.textContent = fmt.size(s.downloadedBytes);
    this.elements.total.ratio.textContent = fmt.ratioString(ratio);
    this.elements.total.time.textContent = fmt.timeInterval(s.secondsActive);
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
    workarea.appendChild(section.root);

    section = createInfoSection('Total', labels);
    const [tup, tdown, tratio, ttime] = section.children;
    const total = (elements.total = {});
    total.up = tup;
    total.down = tdown;
    total.ratio = tratio;
    total.time = ttime;
    workarea.appendChild(section.root);

    return elements;
  }
}
