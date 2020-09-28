/**
 * Copyright © Mnemosyne LLC
 *
 * This file is licensed under the GPLv2.
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 */

import { Formatter } from './formatter.js';
import { Torrent } from './torrent.js';
import { Utils } from './utils.js';

class TorrentRendererHelper {
  static getProgressInfo(controller, t) {
    const s = t.getStatus();
    const seed_ratio_limit = t.seedRatioLimit(controller);

    let pct = 100;
    if (t.needsMetaData()) {
      pct = t.getMetadataPercentComplete() * 100;
    } else if (!t.isDone()) {
      pct = Math.round(t.getPercentDone() * 100);
    } else if (seed_ratio_limit > 0 && t.isSeeding()) {
      // don't split up the bar if paused or queued
      pct = Math.round((t.getUploadRatio() * 100) / seed_ratio_limit);
    }

    let extra = '';
    if (s === Torrent._StatusStopped) {
      extra = 'paused';
    } else if (s === Torrent._StatusDownloadWait) {
      extra = 'leeching queued';
    } else if (t.needsMetaData()) {
      extra = 'magnet';
    } else if (s === Torrent._StatusDownload) {
      extra = 'leeching';
    } else if (s === Torrent._StatusSeedWait) {
      extra = 'seeding queued';
    } else if (s === Torrent._StatusSeed) {
      extra = 'seeding';
    }

    return {
      complete: ['torrent-progress-bar', 'complete', extra].join(' '),
      incomplete: ['torrent-progress-bar', 'incomplete', extra].join(' '),
      percent: pct,
    };
  }

  static createProgressbar(classes) {
    const complete = document.createElement('div');
    complete.className = 'torrent-progress-bar complete';

    const incomplete = document.createElement('div');
    incomplete.className = 'torrent-progress-bar incomplete';

    const progressbar = document.createElement('div');
    progressbar.className = `torrent-progress-bar-container ${classes}`;
    progressbar.appendChild(complete);
    progressbar.appendChild(incomplete);

    return {
      complete,
      element: progressbar,
      incomplete,
    };
  }

  static renderProgressbar(controller, t, progressbar) {
    const info = TorrentRendererHelper.getProgressInfo(controller, t);
    const width = `${info.percent}%`;

    // update the complete progressbar
    let e = progressbar.complete;
    let display = info.percent > 0 ? 'block' : 'none';
    if (e.style.width !== width || e.style.display !== display) {
      e.style.display = display;
      e.style.width = `${info.percent}%`;
    }

    if (e.className !== info.complete) {
      e.className = info.complete;
    }

    // update the incomplete progressbar
    e = progressbar.incomplete;
    display = info.percent < 100 ? 'block' : 'none';

    if (e.style.display !== display) {
      e.style.display = display;
    }

    if (e.className !== info.incomplete) {
      e.className = info.incomplete;
    }
  }

  static formatUL(t) {
    return `▲${Formatter.speedBps(t.getUploadSpeed())}`;
  }
  static formatDL(t) {
    return `▼${Formatter.speedBps(t.getDownloadSpeed())}`;
  }

  static formatETA(t) {
    const eta = t.getETA();
    if (eta < 0 || eta >= 999 * 60 * 60) {
      return '';
    }
    return `ETA: ${Formatter.timeInterval(eta)}`;
  }
}

///

export class TorrentRendererFull {
  static getPeerDetails(t) {
    const fmt = Formatter;

    const err = t.getErrorMessage();
    if (err) {
      return err;
    }

    if (t.isDownloading()) {
      const peer_count = t.getPeersConnected();
      const webseed_count = t.getWebseedsSendingToUs();

      if (webseed_count && peer_count) {
        // Downloading from 2 of 3 peer(s) and 2 webseed(s)
        return [
          'Downloading from',
          t.getPeersSendingToUs(),
          'of',
          fmt.countString('peer', 'peers', peer_count),
          'and',
          fmt.countString('web seed', 'web seeds', webseed_count),
          '–',
          TorrentRendererHelper.formatDL(t),
          TorrentRendererHelper.formatUL(t),
        ].join(' ');
      }
      if (webseed_count) {
        // Downloading from 2 webseed(s)
        return [
          'Downloading from',
          fmt.countString('web seed', 'web seeds', webseed_count),
          '–',
          TorrentRendererHelper.formatDL(t),
          TorrentRendererHelper.formatUL(t),
        ].join(' ');
      }

      // Downloading from 2 of 3 peer(s)
      return [
        'Downloading from',
        t.getPeersSendingToUs(),
        'of',
        fmt.countString('peer', 'peers', peer_count),
        '–',
        TorrentRendererHelper.formatDL(t),
        TorrentRendererHelper.formatUL(t),
      ].join(' ');
    }

    if (t.isSeeding()) {
      return [
        'Seeding to',
        t.getPeersGettingFromUs(),
        'of',
        fmt.countString('peer', 'peers', t.getPeersConnected()),
        '-',
        TorrentRendererHelper.formatUL(t),
      ].join(' ');
    }

    if (t.isChecking()) {
      return [
        'Verifying local data (',
        Formatter.percentString(100.0 * t.getRecheckProgress()),
        '% tested)',
      ].join('');
    }

    return t.getStateString();
  }

  static getProgressDetails(controller, t) {
    if (t.needsMetaData()) {
      let MetaDataStatus = 'retrieving';
      if (t.isStopped()) {
        MetaDataStatus = 'needs';
      }
      const percent = 100 * t.getMetadataPercentComplete();
      return [
        `Magnetized transfer - ${MetaDataStatus} metadata (`,
        Formatter.percentString(percent),
        '%)',
      ].join('');
    }

    const sizeWhenDone = t.getSizeWhenDone();
    const totalSize = t.getTotalSize();
    const is_done = t.isDone() || t.isSeeding();
    const c = [];

    if (is_done) {
      if (totalSize === sizeWhenDone) {
        // seed: '698.05 MiB'
        c.push(Formatter.size(totalSize));
      } else {
        // partial seed: '127.21 MiB of 698.05 MiB (18.2%)'
        c.push(
          Formatter.size(sizeWhenDone),
          ' of ',
          Formatter.size(t.getTotalSize()),
          ' (',
          t.getPercentDoneStr(),
          '%)'
        );
      }
      // append UL stats: ', uploaded 8.59 GiB (Ratio: 12.3)'
      c.push(
        ', uploaded ',
        Formatter.size(t.getUploadedEver()),
        ' (Ratio ',
        Formatter.ratioString(t.getUploadRatio()),
        ')'
      );
    } else {
      // not done yet
      c.push(
        Formatter.size(sizeWhenDone - t.getLeftUntilDone()),
        ' of ',
        Formatter.size(sizeWhenDone),
        ' (',
        t.getPercentDoneStr(),
        '%)'
      );
    }

    // maybe append eta
    if (!t.isStopped() && (!is_done || t.seedRatioLimit(controller) > 0)) {
      c.push(' - ');
      const eta = t.getETA();
      if (eta < 0 || eta >= 999 * 60 * 60 /* arbitrary */) {
        c.push('remaining time unknown');
      } else {
        c.push(Formatter.timeInterval(t.getETA()), ' remaining');
      }
    }

    return c.join('');
  }

  // eslint-disable-next-line class-methods-use-this
  render(controller, t, root) {
    // name
    Utils.setTextContent(root._name_container, t.getName());

    // progressbar
    TorrentRendererHelper.renderProgressbar(controller, t, root._progressbar);

    // peer details
    const has_error = t.getError() !== Torrent._ErrNone;
    let e = root._peer_details_container;
    e.classList.toggle('error', has_error);
    Utils.setTextContent(e, TorrentRendererFull.getPeerDetails(t));

    // progress details
    e = root._progress_details_container;
    Utils.setTextContent(e, TorrentRendererFull.getProgressDetails(controller, t));

    // pause/resume button
    const is_stopped = t.isStopped();
    e = root._pause_resume_button_image;
    e.alt = is_stopped ? 'Resume' : 'Pause';
    e.className = is_stopped ? 'torrent-resume' : 'torrent-pause';
  }

  // eslint-disable-next-line class-methods-use-this
  createRow() {
    const root = document.createElement('li');
    root.className = 'torrent';

    const name = document.createElement('div');
    name.className = 'torrent-name';

    const peers = document.createElement('div');
    peers.className = 'torrent-peer-details';

    const progress = document.createElement('div');
    progress.classList.add('torrent-progress');
    const progressbar = TorrentRendererHelper.createProgressbar('full');
    progress.appendChild(progressbar.element);
    const button = document.createElement('a');
    const image = document.createElement('div');
    button.appendChild(image);
    progress.appendChild(button);

    const details = document.createElement('div');
    details.className = 'torrent-progress-details';

    root.appendChild(name);
    root.appendChild(peers);
    root.appendChild(progress);
    root.appendChild(details);

    root._name_container = name;
    root._peer_details_container = peers;
    root._progress_details_container = details;
    root._progressbar = progressbar;
    root._pause_resume_button_image = image;
    root._toggle_running_button = button;

    return root;
  }
}

///

export class TorrentRendererCompact {
  static getPeerDetails(t) {
    const errMsg = t.getErrorMessage();
    if (errMsg) {
      return errMsg;
    }
    if (t.isDownloading()) {
      const have_dn = t.getDownloadSpeed() > 0;
      const have_up = t.getUploadSpeed() > 0;

      if (!have_up && !have_dn) {
        return 'Idle';
      }

      const s = [`${TorrentRendererHelper.formatETA(t)} `];
      if (have_dn) {
        s.push(TorrentRendererHelper.formatDL(t));
      }
      if (have_up) {
        s.push(TorrentRendererHelper.formatUL(t));
      }
      return s.join(' ');
    }
    if (t.isSeeding()) {
      return `Ratio: ${Formatter.ratioString(t.getUploadRatio())}, ${TorrentRendererHelper.formatUL(
        t
      )}`;
    }
    return t.getStateString();
  }

  // eslint-disable-next-line class-methods-use-this
  render(controller, t, root) {
    // name
    const is_stopped = t.isStopped();
    let e = root._name_container;
    e.classList.toggle('paused', is_stopped);
    Utils.setTextContent(e, t.getName());

    // peer details
    const has_error = t.getError() !== Torrent._ErrNone;
    e = root._details_container;
    e.classList.toggle('error', has_error);
    Utils.setTextContent(e, TorrentRendererCompact.getPeerDetails(t));

    // progressbar
    TorrentRendererHelper.renderProgressbar(controller, t, root._progressbar);
  }

  // eslint-disable-next-line class-methods-use-this
  createRow() {
    const progressbar = TorrentRendererHelper.createProgressbar('compact');

    const details = document.createElement('div');
    details.className = 'torrent-peer-details compact';

    const name = document.createElement('div');
    name.className = 'torrent-name compact';

    const root = document.createElement('li');
    root.appendChild(progressbar.element);
    root.appendChild(details);
    root.appendChild(name);
    root.className = 'torrent compact';
    root._progressbar = progressbar;
    root._details_container = details;
    root._name_container = name;
    return root;
  }
}

///

export class TorrentRow {
  constructor(view, controller, torrent) {
    this._view = view;
    this._torrent = torrent;
    this._element = view.createRow();
    this._torrent.addEventListener('dataChanged', () => this.render(controller));
    this.render(controller);
  }

  getElement() {
    return this._element;
  }

  render(controller) {
    const tor = this.getTorrent();
    if (tor) {
      this._view.render(controller, tor, this.getElement());
    }
  }

  isSelected() {
    return this.getElement().className.indexOf('selected') !== -1;
  }

  getTorrent() {
    return this._torrent;
  }

  getTorrentId() {
    return this.getTorrent().getId();
  }
}
