/**
 * Copyright © Charles Kerr, Dave Perrett, Malcolm Jarvis and Bruno Bierbaumer
 *
 * This file is licensed under the GPLv2.
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 */

import { isMobileDevice } from './common.js';
import { FileRow } from './file-row.js';
import { Formatter } from './formatter.js';
import { Torrent } from './torrent.js';
import { Utils } from './utils.js';

export class Inspector {
  constructor(controller) {
    this.data = {
      controller,
      elements: {
        availability_lb: document.getElementById('inspector-info-availability'),
        comment_lb: document.getElementById('inspector-info-comment'),
        downloaded_lb: document.getElementById('inspector-info-downloaded'),
        error_lb: document.getElementById('inspector-info-error'),
        file_list: document.getElementById('inspector-file-list'),
        files_page: document.getElementById('inspector-page-files'),
        foldername_lb: document.getElementById('inspector-info-location'),
        hash_lb: document.getElementById('inspector-info-hash'),
        have_lb: document.getElementById('inspector-info-have'),
        info_page: document.getElementById('inspector-page-info'),
        last_activity_lb: document.getElementById('inspector-info-last-activity'),
        name_lb: document.getElementById('torrent-inspector-name'),
        origin_lb: document.getElementById('inspector-info-origin'),
        peers_list: document.getElementById('inspector-peers-list'),
        peers_page: document.getElementById('inspector-page-peers'),
        privacy_lb: document.getElementById('inspector-info-privacy'),
        remaining_time_lb: document.getElementById('inspector-info-remaining-time'),
        running_time_lb: document.getElementById('inspector-info-running-time'),
        size_lb: document.getElementById('inspector-info-size'),
        state_lb: document.getElementById('inspector-info-state'),
        trackers_list: document.getElementById('inspector-trackers-list'),
        trackers_page: document.getElementById('inspector-page-trackers'),
        uploaded_lb: document.getElementById('inspector-info-uploaded'),
      },
      onTorrentChanged: () => this.updateInspector(),
      selectedPage: null,
      torrents: [],
    };

    // listen to tab clicks
    const cb = this.onTabClicked.bind(this);
    for (const e of document.getElementsByClassName('inspector-tab')) {
      e.addEventListener('click', cb);
    }

    this.selectTab(document.getElementById('inspector-tab-info'));
  }

  static needsExtraInfo(torrents) {
    return torrents.some((tor) => !tor.hasExtraInfo());
  }

  refreshTorrents(callback) {
    const { controller, torrents } = this.data;
    const ids = torrents.map((t) => t.getId());

    if (ids && ids.length) {
      const fields = ['id', ...Torrent.Fields.StatsExtra];
      if (Inspector.needsExtraInfo(torrents)) {
        fields.push(...Torrent.Fields.InfoExtra);
      }

      controller.updateTorrents(ids, fields, callback);
    }
  }

  /// GENERAL INFO PAGE

  updateInfoPage() {
    const none = 'None';
    const mixed = 'Mixed';
    const unknown = 'Unknown';
    const fmt = Formatter;
    const now = Date.now();
    const { torrents } = this.data;
    const e = this.data.elements;
    const sizeWhenDone = torrents.reduce((acc, t) => acc + t.getSizeWhenDone(), 0);

    // state_lb
    let str = null;
    if (torrents.length < 1) {
      str = none;
    } else if (torrents.every((t) => t.isFinished())) {
      str = 'Finished';
    } else if (torrents.every((t) => t.isStopped())) {
      str = 'Paused';
    } else {
      const get = (t) => t.getStateString();
      const first = get(torrents[0]);
      str = torrents.every((t) => get(t) === first) ? first : mixed;
    }
    Utils.setTextContent(e.state_lb, str);
    const stateString = str;

    // have_lb
    if (torrents.length < 1) {
      str = none;
    } else {
      const verified = torrents.reduce((acc, t) => acc + t.getHaveValid(), 0);
      const unverified = torrents.reduce((acc, t) => acc + t.getHaveUnchecked(), 0);
      const leftUntilDone = torrents.reduce((acc, t) => acc + t.getLeftUntilDone(), 0);

      const d = 100.0 * (sizeWhenDone ? (sizeWhenDone - leftUntilDone) / sizeWhenDone : 1);
      str = fmt.percentString(d);

      if (!unverified && !leftUntilDone) {
        str = `${fmt.size(verified)} (100%)`;
      } else if (!unverified) {
        str = `${fmt.size(verified)} of ${fmt.size(sizeWhenDone)} (${str}%)`;
      } else {
        str = `${fmt.size(verified)} of ${fmt.size(sizeWhenDone)} (${str}%), ${fmt.size(
          unverified
        )} Unverified`;
      }
    }
    Utils.setTextContent(e.have_lb, str);

    // availability_lb
    if (torrents.length < 1) {
      str = none;
    } else if (sizeWhenDone === 0) {
      str = none;
    } else {
      const available = torrents.reduce((acc, t) => t.getHave() + t.getDesiredAvailable(), 0);
      str = `${fmt.percentString((100.0 * available) / sizeWhenDone)}%`;
    }
    Utils.setTextContent(e.availability_lb, str);

    //  downloaded_lb
    if (torrents.length < 1) {
      str = none;
    } else {
      const d = torrents.reduce((acc, t) => acc + t.getDownloadedEver(), 0);
      const f = torrents.reduce((acc, t) => acc + t.getFailedEver(), 0);
      str = f ? `${fmt.size(d)} (${fmt.size(f)} corrupt)` : fmt.size(d);
    }
    Utils.setTextContent(e.downloaded_lb, str);

    // uploaded_lb
    if (torrents.length < 1) {
      str = none;
    } else {
      const u = torrents.reduce((acc, t) => acc + t.getUploadedEver(), 0);
      const d =
        torrents.reduce((acc, t) => acc + t.getDownloadedEver(), 0) ||
        torrents.reduce((acc, t) => acc + t.getHaveValid(), 0);
      str = `${fmt.size(u)} (Ratio: ${fmt.ratioString(Math.ratio(u, d))})`;
    }
    Utils.setTextContent(e.uploaded_lb, str);

    // running time
    if (torrents.length < 1) {
      str = none;
    } else if (torrents.every((t) => t.isStopped())) {
      str = stateString; // paused || finished}
    } else {
      const get = (t) => t.getStartDate();
      const first = get(torrents[0]);
      if (!torrents.every((t) => get(t) === first)) {
        str = mixed;
      } else {
        str = fmt.timeInterval(now / 1000 - first);
      }
    }
    Utils.setTextContent(e.running_time_lb, str);

    // remaining time
    if (torrents.length < 1) {
      str = none;
    } else {
      const get = (t) => t.getETA();
      const first = get(torrents[0]);
      if (!torrents.every((t) => get(t) === first)) {
        str = mixed;
      } else if (first < 0) {
        str = unknown;
      } else {
        str = fmt.timeInterval(first);
      }
    }
    Utils.setTextContent(e.remaining_time_lb, str);

    // last active at
    if (torrents.length < 1) {
      str = none;
    } else {
      const latest = torrents.reduce((acc, t) => Math.max(acc, t.getLastActivity()), -1);
      const now_seconds = Math.floor(now / 1000);
      if (0 < latest && latest <= now_seconds) {
        const idle_secs = now_seconds - latest;
        str = idle_secs < 5 ? 'Active now' : `${fmt.timeInterval(idle_secs)} ago`;
      } else {
        str = none;
      }
    }
    Utils.setTextContent(e.last_activity_lb, str);

    // error
    if (torrents.length < 1) {
      str = none;
    } else {
      const get = (t) => t.getErrorString();
      const first = get(torrents[0]);
      str = torrents.every((t) => get(t) === first) ? first : mixed;
    }
    Utils.setTextContent(e.error_lb, str || none);

    // size
    if (torrents.length < 1) {
      str = none;
    } else {
      const size = torrents.reduce((acc, t) => acc + t.getTotalSize(), 0);
      if (!size) {
        str = 'None';
      } else {
        const get = (t) => t.getPieceSize();
        const pieceCount = torrents.reduce((acc, t) => acc + t.getPieceCount(), 0);
        const pieceStr = fmt.toStringWithCommas(pieceCount);
        const pieceSize = get(torrents[0]);
        if (torrents.every((t) => get(t) === pieceSize)) {
          str = `${fmt.size(size)} (${pieceStr} pieces @ ${fmt.mem(pieceSize)})`;
        } else {
          str = `${fmt.size(size)} (${pieceStr} pieces)`;
        }
      }
    }
    Utils.setTextContent(e.size_lb, str);

    // hash
    if (torrents.length < 1) {
      str = none;
    } else {
      const get = (t) => t.getHashString();
      const first = get(torrents[0]);
      str = torrents.every((t) => get(t) === first) ? first : mixed;
    }
    Utils.setTextContent(e.hash_lb, str);

    // privacy
    if (torrents.length < 1) {
      str = none;
    } else {
      const get = (t) => t.getPrivateFlag();
      const first = get(torrents[0]);
      if (!torrents.every((t) => get(t) === first)) {
        str = mixed;
      } else if (first) {
        str = 'Private to this tracker -- DHT and PEX disabled';
      } else {
        str = 'Public torrent';
      }
    }
    Utils.setTextContent(e.privacy_lb, str);

    // comment
    if (torrents.length < 1) {
      str = none;
    } else {
      const get = (t) => t.getComment();
      const first = get(torrents[0]);
      str = torrents.every((t) => get(t) === first) ? first : mixed;
    }
    str = str || none;
    if (str.startsWith('https://') || str.startsWith('http://')) {
      str = encodeURI(str);
      Utils.setInnerHTML(e.comment_lb, `<a href="${str}" target="_blank" >${str}</a>`);
    } else {
      Utils.setTextContent(e.comment_lb, str);
    }

    // origin
    if (torrents.length < 1) {
      str = none;
    } else {
      let get = (t) => t.getCreator();
      const creator = get(torrents[0]);
      const mixed_creator = !torrents.every((t) => get(t) === creator);

      get = (t) => t.getDateCreated();
      const date = get(torrents[0]);
      const mixed_date = !torrents.every((t) => get(t) === date);

      const empty_creator = !creator || !creator.length;
      const empty_date = !date;
      if (mixed_creator || mixed_date) {
        str = mixed;
      } else if (empty_creator && empty_date) {
        str = unknown;
      } else if (empty_date && !empty_creator) {
        str = `Created by ${creator}`;
      } else if (empty_creator && !empty_date) {
        str = `Created on ${new Date(date * 1000).toDateString()}`;
      } else {
        str = `Created by ${creator} on ${new Date(date * 1000).toDateString()}`;
      }
    }
    Utils.setTextContent(e.origin_lb, str);

    // donwload dir
    if (torrents.length < 1) {
      str = none;
    } else {
      const get = (t) => t.getDownloadDir();
      const first = get(torrents[0]);
      str = torrents.every((t) => get(t) === first) ? first : mixed;
    }
    Utils.setTextContent(e.foldername_lb, str);
  }

  ///  PEERS PAGE

  updatePeersPage() {
    const html = [];
    const fmt = Formatter;
    const { peers_list } = this.data.elements;
    const { torrents } = this.data;

    for (const tor of torrents) {
      const peers = tor.getPeers();
      html.push('<div class="inspector-group">');
      if (torrents.length > 1) {
        html.push(
          '<div class="inspector_torrent_label">',
          Utils.sanitizeText(tor.getName()),
          '</div>'
        );
      }
      if (!peers || !peers.length) {
        html.push('<br></div>'); // firefox won't paint the top border if the div is empty
        continue;
      }
      html.push(
        '<table class="peer-list">',
        '<tr class="inspector-peer-entry even">',
        '<th class="encrypted-col"></th>',
        '<th class="up-col">Up</th>',
        '<th class="down-col">Down</th>',
        '<th class="percent-col">%</th>',
        '<th class="status-col">Status</th>',
        '<th class="address-col">Address</th>',
        '<th class="client-col">Client</th>',
        '</tr>'
      );
      peers.forEach((peer, idx) => {
        html.push(
          '<tr class="inspector-peer-entry ',
          idx % 2 ? 'odd' : 'even',
          '">',
          '<td>',
          peer.isEncrypted
            ? '<div class="encrypted-peer-cell" title="Encrypted Connection">'
            : '<div class="unencrypted-peer-cell">',
          '</div>',
          '</td>',
          '<td>',
          peer.rateToPeer ? fmt.speedBps(peer.rateToPeer) : '',
          '</td>',
          '<td>',
          peer.rateToClient ? fmt.speedBps(peer.rateToClient) : '',
          '</td>',
          '<td class="percent-col">',
          Math.floor(peer.progress * 100),
          '%',
          '</td>',
          '<td>',
          Inspector._peerStatus(peer.flagStr),
          '</td>',
          '<td>',
          Utils.sanitizeText(peer.address),
          '</td>',
          '<td class="client-col">',
          Utils.sanitizeText(peer.clientName),
          '</td>',
          '</tr>'
        );
      });
      html.push('</table></div>');
    }

    Utils.setInnerHTML(peers_list, html.join(''));
  }

  /// TRACKERS PAGE

  static getAnnounceState(tracker) {
    switch (tracker.announceState) {
      case Torrent._TrackerActive:
        return 'Announce in progress';
      case Torrent._TrackerWaiting: {
        const timeUntilAnnounce = Math.max(
          0,
          tracker.nextAnnounceTime - new Date().getTime() / 1000
        );
        return `Next announce in ${Formatter.timeInterval(timeUntilAnnounce)}`;
      }
      case Torrent._TrackerQueued:
        return 'Announce is queued';
      case Torrent._TrackerInactive:
        return tracker.isBackup ? 'Tracker will be used as a backup' : 'Announce not scheduled';
      default:
        return `unknown announce state: ${tracker.announceState}`;
    }
  }

  static lastAnnounceStatus(tracker) {
    let lastAnnounceLabel = 'Last Announce';
    let lastAnnounce = ['N/A'];

    if (tracker.hasAnnounced) {
      const lastAnnounceTime = Formatter.timestamp(tracker.lastAnnounceTime);
      if (tracker.lastAnnounceSucceeded) {
        lastAnnounce = [
          lastAnnounceTime,
          ' (got ',
          Formatter.countString('peer', 'peers', tracker.lastAnnouncePeerCount),
          ')',
        ];
      } else {
        lastAnnounceLabel = 'Announce error';
        lastAnnounce = [
          tracker.lastAnnounceResult ? `${tracker.lastAnnounceResult} - ` : '',
          lastAnnounceTime,
        ];
      }
    }
    return {
      label: lastAnnounceLabel,
      value: lastAnnounce.join(''),
    };
  }

  static lastScrapeStatus(tracker) {
    let lastScrapeLabel = 'Last Scrape';
    let lastScrape = 'N/A';

    if (tracker.hasScraped) {
      const lastScrapeTime = Formatter.timestamp(tracker.lastScrapeTime);
      if (tracker.lastScrapeSucceeded) {
        lastScrape = lastScrapeTime;
      } else {
        lastScrapeLabel = 'Scrape error';
        lastScrape =
          (tracker.lastScrapeResult ? `${tracker.lastScrapeResult} - ` : '') + lastScrapeTime;
      }
    }
    return {
      label: lastScrapeLabel,
      value: lastScrape,
    };
  }

  updateTrackersPage() {
    const na = 'N/A';
    const { trackers_list } = this.data.elements;
    const { torrents } = this.data;

    // By building up the HTML as as string, then have the browser
    // turn this into a DOM tree, this is a fast operation.
    const html = [];
    for (const tor of torrents) {
      html.push('<div class="inspector-group">');

      if (torrents.length > 1) {
        html.push(
          '<div class="inspector_torrent_label">',
          Utils.sanitizeText(tor.getName()),
          '</div>'
        );
      }

      let tier = -1;
      tor.getTrackers().forEach((tracker, idx) => {
        if (tier !== tracker.tier) {
          if (tier !== -1) {
            // close previous tier
            html.push('</ul></div>');
          }

          ({ tier } = tracker);

          html.push(
            '<div class="inspector-group_label">',
            'Tier ',
            tier + 1,
            '</div>',
            '<ul class="tier-list">'
          );
        }

        // Display construction
        const lastAnnounceStatusHash = Inspector.lastAnnounceStatus(tracker);
        const announceState = Inspector.getAnnounceState(tracker);
        const lastScrapeStatusHash = Inspector.lastScrapeStatus(tracker);
        html.push(
          '<li class="inspector-tracker-entry ',
          idx % 2 ? 'odd' : 'even',
          '"><div class="tracker-host" title="',
          Utils.sanitizeText(tracker.announce),
          '">',
          Utils.sanitizeText(tracker.host || tracker.announce),
          '</div>',
          '<div class="tracker-activity">',
          '<div>',
          lastAnnounceStatusHash['label'],
          ': ',
          Utils.sanitizeText(lastAnnounceStatusHash['value']),
          '</div>',
          '<div>',
          announceState,
          '</div>',
          '<div>',
          lastScrapeStatusHash['label'],
          ': ',
          Utils.sanitizeText(lastScrapeStatusHash['value']),
          '</div>',
          '</div><table class="tracker_stats">',
          '<tr><th>Seeders:</th><td>',
          tracker.seederCount > -1 ? tracker.seederCount : na,
          '</td></tr>',
          '<tr><th>Leechers:</th><td>',
          tracker.leecherCount > -1 ? tracker.leecherCount : na,
          '</td></tr>',
          '<tr><th>Downloads:</th><td>',
          tracker.downloadCount > -1 ? tracker.downloadCount : na,
          '</td></tr>',
          '</table></li>'
        );
      });
      if (tier !== -1) {
        // close last tier
        html.push('</ul></div>');
      }

      html.push('</div>'); // inspector-group
    }

    Utils.setInnerHTML(trackers_list, html.join(''));
  }

  ///  FILES PAGE

  changeFileCommand(fileIndices, command) {
    const { controller, file_torrent } = this.data;
    const torrentId = file_torrent.getId();
    controller.changeFileCommand(torrentId, fileIndices, command);
  }

  onFileWantedToggled(ev, fileIndices, want) {
    this.changeFileCommand(fileIndices, want ? 'files-wanted' : 'files-unwanted');
  }

  onFilePriorityToggled(ev, fileIndices, priority) {
    let command = null;
    switch (priority) {
      case -1:
        command = 'priority-low';
        break;
      case 1:
        command = 'priority-high';
        break;
      default:
        command = 'priority-normal';
        break;
    }
    this.changeFileCommand(fileIndices, command);
  }

  static onNameClicked(ev, fileRow) {
    $(fileRow.getElement()).siblings().slideToggle();
  }

  clearFileList() {
    const e = this.data.elements.file_list;
    while (e.firstChild) {
      e.removeChild(e.firstChild);
    }

    delete this.data.file_torrent;
    delete this.data.file_torrent_n;
    delete this.data.file_rows;
  }

  static createFileTreeModel(tor) {
    const leaves = [];
    const tree = {
      children: {},
      file_indices: [],
    };

    const n = tor.getFileCount();
    for (let i = 0; i < n; ++i) {
      const { name } = tor.getFile(i);
      const tokens = name.split('/');
      let walk = tree;
      for (let j = 0; j < tokens.length; ++j) {
        const token = tokens[j];
        let sub = walk.children[token];
        if (!sub) {
          walk.children[token] = sub = {
            children: {},
            depth: j,
            file_indices: [],
            name: token,
            parent: walk,
          };
        }
        walk = sub;
      }
      walk.file_index = i;
      delete walk.children;
      leaves.push(walk);
    }

    for (const leaf of leaves) {
      const { file_index } = leaf;
      let walk = leaf;
      do {
        walk.file_indices.push(file_index);
        walk = walk.parent;
      } while (walk);
    }

    return tree;
  }

  addNodeToView(tor, parent, sub, i) {
    const row = new FileRow(tor, sub.depth, sub.name, sub.file_indices, i % 2);
    this.data.file_rows.push(row);
    parent.appendChild(row.getElement());
    const e = $(row);
    e.bind('wantedToggled', this.onFileWantedToggled.bind(this));
    e.bind('priorityToggled', this.onFilePriorityToggled.bind(this));
    e.bind('nameClicked', Inspector.onNameClicked);
  }

  addSubtreeToView(tor, parent, sub, i) {
    const div = document.createElement('div');
    if (sub.parent) {
      this.addNodeToView(tor, div, sub, i++);
    }
    if (sub.children) {
      for (const val of Object.values(sub.children)) {
        i = this.addSubtreeToView(tor, div, val);
      }
    }
    parent.appendChild(div);
    return i;
  }

  updateFilesPage() {
    const { file_list } = this.data.elements;
    const { torrents } = this.data;

    // only show one torrent at a time
    if (torrents.length !== 1) {
      this.clearFileList();
      return;
    }

    const [tor] = torrents;
    const n = tor ? tor.getFileCount() : 0;
    if (tor !== this.data.file_torrent || n !== this.data.file_torrent_n) {
      // rebuild the file list...
      this.clearFileList();
      this.data.file_torrent = tor;
      this.data.file_torrent_n = n;
      this.data.file_rows = [];
      const fragment = document.createDocumentFragment();
      const tree = Inspector.createFileTreeModel(tor);
      this.addSubtreeToView(tor, fragment, tree, 0);
      file_list.appendChild(fragment);
    } else {
      // ...refresh the already-existing file list
      this.data.file_rows.forEach((row) => row.refresh());
    }
  }

  ///

  onTabClicked(ev) {
    const tab = ev.currentTarget;

    if (isMobileDevice) {
      ev.stopPropagation();
    }

    this.selectTab(tab);
  }

  selectTab(tab) {
    // select this tab and deselect the others
    for (const e of tab.parentNode.children) {
      e.classList.toggle('selected', e === tab);
    }

    // show this tab and hide the others
    const id = tab.id.replace('tab', 'page');
    this.selectedPage = document.getElementById(id);
    for (const e of document.getElementsByClassName('inspector-page')) {
      Utils.setVisible(e, e === this.selectedPage);
    }

    this.updateInspector();
  }

  updateInspector() {
    const { elements, torrents } = this.data;

    // update the name, which is shown on all the pages
    let name = null;
    if (!torrents || !torrents.length) {
      name = 'No Selection';
    } else if (torrents.length === 1) {
      name = torrents[0].getName();
    } else {
      name = `${torrents.length} Transfers Selected`;
    }
    Utils.setTextContent(elements.name_lb, name || 'Not Applicable');

    // update the visible page
    switch (this.selectedPage) {
      case elements.files_page:
        this.updateFilesPage();
        break;
      case elements.peers_page:
        this.updatePeersPage();
        break;
      case elements.trackers_page:
        this.updateTrackersPage();
        break;
      default:
        this.updateInfoPage();
        break;
    }
  }

  static _peerStatus(flagStr) {
    const formattedFlags = [];
    for (const flag of flagStr) {
      let explanation = null;
      switch (flag) {
        case 'O':
          explanation = 'Optimistic unchoke';
          break;
        case 'D':
          explanation = 'Downloading from this peer';
          break;
        case 'd':
          explanation = "We would download from this peer if they'd let us";
          break;
        case 'U':
          explanation = 'Uploading to peer';
          break;
        case 'u':
          explanation = "We would upload to this peer if they'd ask";
          break;
        case 'K':
          explanation = "Peer has unchoked us, but we're not interested";
          break;
        case '?':
          explanation = "We unchoked this peer, but they're not interested";
          break;
        case 'E':
          explanation = 'Encrypted Connection';
          break;
        case 'H':
          explanation = 'Peer was discovered through Distributed Hash Table (DHT)';
          break;
        case 'X':
          explanation = 'Peer was discovered through Peer Exchange (PEX)';
          break;
        case 'I':
          explanation = 'Peer is an incoming connection';
          break;
        case 'T':
          explanation = 'Peer is connected via uTP';
          break;
        default:
          explanation = null;
          break;
      }

      if (!explanation) {
        formattedFlags.push(flag);
      } else {
        formattedFlags.push(`<span title="${flag}: ${explanation}">${flag}</span>`);
      }
    }

    return formattedFlags.join('');
  }

  /// PUBLIC

  setTorrents(torrents) {
    // update the inspector when a selected torrent's data changes.
    const key = 'dataChanged';
    const cb = this.data.onTorrentChanged;
    this.data.torrents.forEach((t) => t.removeEventListener(key, cb));
    this.data.torrents = torrents || [];
    this.data.torrents.forEach((t) => t.addEventListener(key, cb));

    // periodically ping the server for updates to these torrents
    if (this.data.interval) {
      clearInterval(this.data.interval);
      delete this.data.interval;
    }
    if (this.data.torrents.length) {
      this.data.interval = setInterval(this.refreshTorrents.bind(this), 2000);
      this.refreshTorrents();
    }

    this.updateInspector();
  }
}
