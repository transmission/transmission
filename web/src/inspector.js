/*
 * This file Copyright (C) 2020 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 */

import { FileRow } from './file-row.js';
import { Formatter } from './formatter.js';
import { Torrent } from './torrent.js';
import {
  OutsideClickListener,
  Utils,
  createTabsContainer,
  sanitizeText,
  setTextContent,
} from './utils.js';

const peer_column_classes = [
  'encryption',
  'speed-up',
  'speed-down',
  'percent-done',
  'status',
  'peer-address',
  'peer-app-name',
];

export class Inspector extends EventTarget {
  constructor(controller) {
    super();

    this.closed = false;
    this.controller = controller;
    this.elements = this._create();
    this.current_page = this.elements.info.root;
    this.interval = setInterval(this._refreshTorrents.bind(this), 3000);
    this.name = 'inspector';
    this.selection_listener = (ev) => this._setTorrents(ev.selected);
    this.torrent_listener = () => this._updateCurrentPage();
    this.torrents = [];
    this.file_torrent = null;
    this.file_torrent_n = null;
    this.file_rows = null;
    this.outside = new OutsideClickListener(this.elements.root);
    this.outside.addEventListener('click', () => this.close());
    Object.seal(this);

    controller.addEventListener(
      'torrent-selection-changed',
      this.selection_listener
    );
    this._setTorrents(this.controller.getSelectedTorrents());

    document.body.appendChild(this.elements.root);
  }

  close() {
    if (!this.closed) {
      this.outside.stop();
      clearInterval(this.interval);
      this._setTorrents([]);
      this.elements.root.remove();
      this.controller.removeEventListener(
        'torrent-selection-changed',
        this.selection_listener
      );
      this.dispatchEvent(new Event('close'));
      for (const prop of Object.keys(this)) {
        this[prop] = null;
      }
      this.closed = true;
    }
  }

  static _createInfoPage() {
    const root = document.createElement('div');
    root.classList.add('inspector-info-page');
    const elements = { root };

    const append_section_title = (text) => {
      const label = document.createElement('div');
      label.textContent = text;
      label.classList.add('section-label');
      root.appendChild(label);
    };

    const append_row = (text) => {
      const lhs = document.createElement('label');
      setTextContent(lhs, text);
      root.appendChild(lhs);

      const rhs = document.createElement('label');
      root.appendChild(rhs);
      return rhs;
    };

    append_section_title('Activity');
    let rows = [
      ['have', 'Have:'],
      ['availability', 'Availability:'],
      ['uploaded', 'Uploaded:'],
      ['downloaded', 'Downloaded:'],
      ['state', 'State:'],
      ['running_time', 'Running time:'],
      ['remaining_time', 'Remaining:'],
      ['last_activity', 'Last activity:'],
      ['error', 'Error:'],
    ];
    for (const [name, text] of rows) {
      elements[name] = append_row(text);
    }

    append_section_title('Details');
    rows = [
      ['size', 'Size:'],
      ['location', 'Location:'],
      ['hash', 'Hash:'],
      ['privacy', 'Privacy:'],
      ['origin', 'Origin:'],
      ['comment', 'Comment:'],
    ];
    for (const [name, text] of rows) {
      elements[name] = append_row(text);
    }

    return elements;
  }

  static _createListPage(list_type, list_id) {
    const root = document.createElement('div');
    const list = document.createElement(list_type);
    list.id = list_id;
    root.appendChild(list);
    return { list, root };
  }

  static _createTrackersPage() {
    return Inspector._createListPage('div', 'inspector-trackers-list');
  }

  static _createFilesPage() {
    return Inspector._createListPage('ul', 'inspector-file-list');
  }

  static _createPeersPage() {
    const table = document.createElement('table');
    table.classList.add('peer-list');
    const thead = document.createElement('thead');
    const tr = document.createElement('tr');
    const names = ['', 'Up', 'Down', 'Done', 'Status', 'Address', 'Client'];
    names.forEach((name, idx) => {
      const th = document.createElement('th');
      const classname = peer_column_classes[idx];
      if (classname === 'encryption') {
        th.dataset.encrypted = true;
      }
      th.classList.add(classname);
      setTextContent(th, name);
      tr.appendChild(th);
    });
    const tbody = document.createElement('tbody');
    thead.appendChild(tr);
    table.appendChild(thead);
    table.appendChild(tbody);
    return {
      root: table,
      tbody,
    };
  }

  _create() {
    const pages = {
      files: Inspector._createFilesPage(),
      info: Inspector._createInfoPage(),
      peers: Inspector._createPeersPage(),
      trackers: Inspector._createTrackersPage(),
    };

    const on_activated = (page) => {
      this.current_page = page;
      this._updateCurrentPage();
    };

    const elements = createTabsContainer(
      'inspector',
      [
        ['inspector-tab-info', pages.info.root],
        ['inspector-tab-peers', pages.peers.root],
        ['inspector-tab-trackers', pages.trackers.root],
        ['inspector-tab-files', pages.files.root],
      ],
      on_activated.bind(this)
    );

    return { ...elements, ...pages };
  }

  _setTorrents(torrents) {
    // update the inspector when a selected torrent's data changes.
    const key = 'dataChanged';
    const cb = this.torrent_listener;
    this.torrents.forEach((t) => t.removeEventListener(key, cb));
    this.torrents = [...torrents];
    this.torrents.forEach((t) => t.addEventListener(key, cb));

    this._refreshTorrents();
    this._updateCurrentPage();
  }

  static _needsExtraInfo(torrents) {
    return torrents.some((tor) => !tor.hasExtraInfo());
  }

  _refreshTorrents() {
    const { controller, torrents } = this;
    const ids = torrents.map((t) => t.getId());

    if (ids && ids.length) {
      const fields = ['id', ...Torrent.Fields.StatsExtra];
      if (Inspector._needsExtraInfo(torrents)) {
        fields.push(...Torrent.Fields.InfoExtra);
      }

      controller.updateTorrents(ids, fields);
    }
  }

  _updateCurrentPage() {
    const { elements } = this;
    switch (this.current_page) {
      case elements.files.root:
        this._updateFiles();
        break;
      case elements.info.root:
        this._updateInfo();
        break;
      case elements.peers.root:
        this._updatePeers();
        break;
      case elements.trackers.root:
        this._updateTrackers();
        break;
      default:
        console.warn('unexpected page');
        console.log(this.current_page);
    }
  }

  _updateInfo() {
    const none = 'None';
    const mixed = 'Mixed';
    const unknown = 'Unknown';
    const fmt = Formatter;
    const now = Date.now();
    const { torrents } = this;
    const e = this.elements;
    const sizeWhenDone = torrents.reduce(
      (acc, t) => acc + t.getSizeWhenDone(),
      0
    );

    // state
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
    setTextContent(e.info.state, str);
    const stateString = str;

    // have
    if (torrents.length < 1) {
      str = none;
    } else {
      const verified = torrents.reduce((acc, t) => acc + t.getHaveValid(), 0);
      const unverified = torrents.reduce(
        (acc, t) => acc + t.getHaveUnchecked(),
        0
      );
      const leftUntilDone = torrents.reduce(
        (acc, t) => acc + t.getLeftUntilDone(),
        0
      );

      const d =
        100.0 *
        (sizeWhenDone ? (sizeWhenDone - leftUntilDone) / sizeWhenDone : 1);
      str = fmt.percentString(d);

      if (!unverified && !leftUntilDone) {
        str = `${fmt.size(verified)} (100%)`;
      } else if (!unverified) {
        str = `${fmt.size(verified)} of ${fmt.size(sizeWhenDone)} (${str}%)`;
      } else {
        str = `${fmt.size(verified)} of ${fmt.size(
          sizeWhenDone
        )} (${str}%), ${fmt.size(unverified)} Unverified`;
      }
    }
    setTextContent(e.info.have, str);

    // availability
    if (torrents.length < 1) {
      str = none;
    } else if (sizeWhenDone === 0) {
      str = none;
    } else {
      const available = torrents.reduce(
        (acc, t) => t.getHave() + t.getDesiredAvailable(),
        0
      );
      str = `${fmt.percentString((100.0 * available) / sizeWhenDone)}%`;
    }
    setTextContent(e.info.availability, str);

    //  downloaded
    if (torrents.length < 1) {
      str = none;
    } else {
      const d = torrents.reduce((acc, t) => acc + t.getDownloadedEver(), 0);
      const f = torrents.reduce((acc, t) => acc + t.getFailedEver(), 0);
      str = f ? `${fmt.size(d)} (${fmt.size(f)} corrupt)` : fmt.size(d);
    }
    setTextContent(e.info.downloaded, str);

    // uploaded
    if (torrents.length < 1) {
      str = none;
    } else {
      const u = torrents.reduce((acc, t) => acc + t.getUploadedEver(), 0);
      const d =
        torrents.reduce((acc, t) => acc + t.getDownloadedEver(), 0) ||
        torrents.reduce((acc, t) => acc + t.getHaveValid(), 0);
      str = `${fmt.size(u)} (Ratio: ${fmt.ratioString(Utils.ratio(u, d))})`;
    }
    setTextContent(e.info.uploaded, str);

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
    setTextContent(e.info.running_time, str);

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
    setTextContent(e.info.remaining_time, str);

    // last active at
    if (torrents.length < 1) {
      str = none;
    } else {
      const latest = torrents.reduce(
        (acc, t) => Math.max(acc, t.getLastActivity()),
        -1
      );
      const now_seconds = Math.floor(now / 1000);
      if (0 < latest && latest <= now_seconds) {
        const idle_secs = now_seconds - latest;
        str =
          idle_secs < 5 ? 'Active now' : `${fmt.timeInterval(idle_secs)} ago`;
      } else {
        str = none;
      }
    }
    setTextContent(e.info.last_activity, str);

    // error
    if (torrents.length < 1) {
      str = none;
    } else {
      const get = (t) => t.getErrorString();
      const first = get(torrents[0]);
      str = torrents.every((t) => get(t) === first) ? first : mixed;
    }
    setTextContent(e.info.error, str || none);

    // size
    if (torrents.length < 1) {
      str = none;
    } else {
      const size = torrents.reduce((acc, t) => acc + t.getTotalSize(), 0);
      if (!size) {
        str = 'None';
      } else {
        const get = (t) => t.getPieceSize();
        const pieceCount = torrents.reduce(
          (acc, t) => acc + t.getPieceCount(),
          0
        );
        const pieceStr = fmt.number(pieceCount);
        const pieceSize = get(torrents[0]);
        if (torrents.every((t) => get(t) === pieceSize)) {
          str = `${fmt.size(size)} (${pieceStr} pieces @ ${fmt.mem(
            pieceSize
          )})`;
        } else {
          str = `${fmt.size(size)} (${pieceStr} pieces)`;
        }
      }
    }
    setTextContent(e.info.size, str);

    // hash
    if (torrents.length < 1) {
      str = none;
    } else {
      const get = (t) => t.getHashString();
      const first = get(torrents[0]);
      str = torrents.every((t) => get(t) === first) ? first : mixed;
    }
    setTextContent(e.info.hash, str);

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
    setTextContent(e.info.privacy, str);

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
      Utils.setInnerHTML(
        e.info.comment,
        `<a href="${str}" target="_blank" >${str}</a>`
      );
    } else {
      setTextContent(e.info.comment, str);
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
        str = `Created by ${creator} on ${new Date(
          date * 1000
        ).toDateString()}`;
      }
    }
    setTextContent(e.info.origin, str);

    // location
    if (torrents.length < 1) {
      str = none;
    } else {
      const get = (t) => t.getDownloadDir();
      const first = get(torrents[0]);
      str = torrents.every((t) => get(t) === first) ? first : mixed;
    }
    setTextContent(e.info.location, str);
  }

  ///  PEERS PAGE

  static _peerStatusTitle(flag_str) {
    const texts = Object.seal({
      '?': "We unchoked this peer, but they're not interested",
      D: 'Downloading from this peer',
      E: 'Encrypted Connection',
      H: 'Peer was discovered through Distributed Hash Table (DHT)',
      I: 'Peer is an incoming connection',
      K: "Peer has unchoked us, but we're not interested",
      O: 'Optimistic unchoke',
      T: 'Peer is connected via uTP',
      U: 'Uploading to peer',
      X: 'Peer was discovered through Peer Exchange (PEX)',
      d: "We would download from this peer if they'd let us",
      u: "We would upload to this peer if they'd ask",
    });

    return Array.from(flag_str)
      .filter((ch) => texts[ch])
      .map((ch) => `${ch}: ${texts[ch]}`)
      .join('\n');
  }

  _updatePeers() {
    const fmt = Formatter;
    const { torrents } = this;
    const { tbody } = this.elements.peers;

    const cell_setters = [
      (peer, td) => {
        td.dataset.encrypted = peer.isEncrypted;
      },
      (peer, td) =>
        setTextContent(
          td,
          peer.rateToPeer ? fmt.speedBps(peer.rateToPeer) : ''
        ),
      (peer, td) =>
        setTextContent(
          td,
          peer.rateToClient ? fmt.speedBps(peer.rateToClient) : ''
        ),
      (peer, td) => setTextContent(td, `${Math.floor(peer.progress * 100)}%`),
      (peer, td) => {
        setTextContent(td, peer.flagStr);
        td.setAttribute('title', Inspector._peerStatusTitle(peer.flagStr));
      },
      (peer, td) => setTextContent(td, peer.address),
      (peer, td) => setTextContent(td, peer.clientName),
    ];

    const rows = [];
    for (const tor of torrents) {
      // torrent name
      const tortr = document.createElement('tr');
      tortr.classList.add('torrent-row');
      const tortd = document.createElement('td');
      tortd.setAttribute('colspan', cell_setters.length);
      setTextContent(tortd, tor.getName());
      tortr.appendChild(tortd);
      rows.push(tortr);

      // peers
      for (const peer of tor.getPeers()) {
        const tr = document.createElement('tr');
        tr.classList.add('peer-row');
        cell_setters.forEach((setter, idx) => {
          const td = document.createElement('td');
          td.classList.add(peer_column_classes[idx]);
          setter(peer, td);
          tr.appendChild(td);
        });
        rows.push(tr);
      }

      // TODO: modify instead of rebuilding wholesale?
      while (tbody.firstChild) {
        tbody.removeChild(tbody.firstChild);
      }
      rows.forEach((row) => tbody.append(row));
    }
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
        return tracker.isBackup
          ? 'Tracker will be used as a backup'
          : 'Announce not scheduled';
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
          (tracker.lastScrapeResult ? `${tracker.lastScrapeResult} - ` : '') +
          lastScrapeTime;
      }
    }
    return {
      label: lastScrapeLabel,
      value: lastScrape,
    };
  }

  _updateTrackers() {
    const na = 'N/A';
    const { list } = this.elements.trackers;
    const { torrents } = this;

    // By building up the HTML as as string, then have the browser
    // turn this into a DOM tree, this is a fast operation.
    const html = [];
    for (const tor of torrents) {
      html.push('<div class="inspector-group">');

      if (torrents.length > 1) {
        html.push(
          '<div class="inspector_torrent_label">',
          sanitizeText(tor.getName()),
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
          sanitizeText(tracker.announce),
          '">',
          sanitizeText(tracker.host || tracker.announce),
          '</div>',
          '<div class="tracker-activity">',
          '<div>',
          lastAnnounceStatusHash['label'],
          ': ',
          sanitizeText(lastAnnounceStatusHash['value']),
          '</div>',
          '<div>',
          announceState,
          '</div>',
          '<div>',
          lastScrapeStatusHash['label'],
          ': ',
          sanitizeText(lastScrapeStatusHash['value']),
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

    Utils.setInnerHTML(list, html.join(''));
  }

  ///  FILES PAGE

  _changeFileCommand(fileIndices, command) {
    const { controller, file_torrent } = this;
    const torrentId = file_torrent.getId();
    controller.changeFileCommand(torrentId, fileIndices, command);
  }

  _onFileWantedToggled(ev) {
    const { indices, wanted } = ev;
    this._changeFileCommand(
      indices,
      wanted ? 'files-wanted' : 'files-unwanted'
    );
  }

  _onFilePriorityToggled(ev) {
    const { indices, priority } = ev;

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

    this._changeFileCommand(indices, command);
  }

  _clearFileList() {
    const { list } = this.elements.files;
    while (list.firstChild) {
      list.removeChild(list.firstChild);
    }

    this.file_torrent = null;
    this.file_torrent_n = null;
    this.file_rows = null;
  }

  static createFileTreeModel(tor) {
    const leaves = [];
    const tree = {
      children: {},
      file_indices: [],
    };

    tor.getFiles().forEach((file, i) => {
      const { name } = file;
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
    });

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
    row.addEventListener('wantedToggled', this._onFileWantedToggled.bind(this));
    row.addEventListener(
      'priorityToggled',
      this._onFilePriorityToggled.bind(this)
    );
    this.file_rows.push(row);
    parent.appendChild(row.getElement());
  }

  addSubtreeToView(tor, parent, sub, i) {
    if (sub.parent) {
      this.addNodeToView(tor, parent, sub, i++);
    }
    if (sub.children) {
      for (const val of Object.values(sub.children)) {
        i = this.addSubtreeToView(tor, parent, val, i);
      }
    }
    return i;
  }

  _updateFiles() {
    const { list } = this.elements.files;
    const { torrents } = this;

    // only show one torrent at a time
    if (torrents.length !== 1) {
      this._clearFileList();
      return;
    }

    const [tor] = torrents;
    const n = tor.getFiles().length;
    if (tor !== this.file_torrent || n !== this.file_torrent_n) {
      // rebuild the file list...
      this._clearFileList();
      this.file_torrent = tor;
      this.file_torrent_n = n;
      this.file_rows = [];
      const fragment = document.createDocumentFragment();
      const tree = Inspector.createFileTreeModel(tor);
      this.addSubtreeToView(tor, fragment, tree, 0);
      list.appendChild(fragment);
    } else {
      // ...refresh the already-existing file list
      this.file_rows.forEach((row) => row.refresh());
    }
  }
}
