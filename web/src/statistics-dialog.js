/*
 * This file Copyright (C) 2020 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 */

import { Formatter } from './formatter.js';
import { Utils, createDialogContainer, makeUUID } from './utils.js';

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

    const heading_text = 'Statistics';
    elements.root.setAttribute('aria-label', heading_text);
    elements.heading.textContent = heading_text;

    const make_section = (classname, title) => {
      const root = document.createElement('fieldset');
      root.classList.add('section', classname);

      const legend = document.createElement('legend');
      legend.classList.add('title');
      legend.textContent = title;
      root.appendChild(legend);

      const content = document.createElement('div');
      content.classList.add('content');
      root.appendChild(content);

      return { content, root };
    };

    const make_row = (section, text) => {
      const label = document.createElement('label');
      label.textContent = text;
      section.appendChild(label);

      const item = document.createElement('div');
      item.id = makeUUID();
      section.appendChild(item);
      label.setAttribute('for', item.id);

      return item;
    };

    let o = make_section('current-session', 'Current session');
    workarea.appendChild(o.root);

    const session = (elements.session = {});
    session.up = make_row(o.content, 'Uploaded:');
    session.down = make_row(o.content, 'Downloaded:');
    session.ratio = make_row(o.content, 'Ratio:');
    session.time = make_row(o.content, 'Running time:');

    o = make_section('total', 'Total');
    workarea.appendChild(o.root);

    const total = (elements.total = {});
    total.up = make_row(o.content, 'Uploaded:');
    total.down = make_row(o.content, 'Downloaded:');
    total.ratio = make_row(o.content, 'Ratio:');
    total.time = make_row(o.content, 'Running time:');

    elements.heading.textContent = 'Statistics';
    elements.dismiss.textContent = 'Close';
    elements.heading.textContent = heading_text;
    elements.confirm.remove();
    delete elements.confirm;

    return elements;
  }
}
