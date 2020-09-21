'use strict';

/**
 * Copyright © Charles Kerr, Dave Perrett, Malcolm Jarvis and Bruno Bierbaumer
 *
 * This file is licensed under the GPLv2.
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 */

function Inspector(controller) {
  const data = {
      controller: null,
      elements: {},
      torrents: [],
    },
    needsExtraInfo = function (torrents) {
      return torrents.some((tor) => !tor.hasExtraInfo());
    },
    refreshTorrents = function (callback) {
      const ids = data.torrents.map((t) => t.getId());

      if (ids && ids.length) {
        const fields = ['id'].concat(Torrent.Fields.StatsExtra);
        if (needsExtraInfo(data.torrents)) {
          fields.push(...Torrent.Fields.InfoExtra);
        }

        data.controller.updateTorrents(ids, fields, callback);
      }
    },
    /// GENERAL INFO PAGE

    updateInfoPage = function () {
      const none = 'None';
      const mixed = 'Mixed';
      const unknown = 'Unknown';
      const { fmt } = Transmission;
      const now = Date.now();
      const { torrents } = data;
      const e = data.elements;
      const sizeWhenDone = torrents.reduce((acc, t) => acc + t.getSizeWhenDone(), 0);

      //
      //  state_lb
      //

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
      setTextContent(e.state_lb, str);
      const stateString = str;

      //
      //  have_lb
      //

      if (torrents.length < 1) {
        str = none;
      } else {
        const verified = torrents.reduce((acc, t) => acc + t.getHaveValid(), 0);
        const unverified = torrents.reduce((acc, t) => acc + t.getHaveUnchecked(), 0);
        const leftUtilDone = torrents.reduce((acc, t) => acc + t.getLeftUntilDone(), 0);

        const d = 100.0 * (sizeWhenDone ? (sizeWhenDone - leftUtilDone) / sizeWhenDone : 1);
        str = fmt.percentString(d);

        if (!unverified && !leftUtilDone) {
          str = `${fmt.size(verified)} (100%)`;
        } else if (!unverified) {
          str = `${fmt.size(verified)} of ${fmt.size(sizeWhenDone)} (${str}%)`;
        } else {
          str = `${fmt.size(verified)} of ${fmt.size(sizeWhenDone)} (${str}%), ${fmt.size(
            unverified
          )} Unverified`;
        }
      }
      setTextContent(e.have_lb, str);

      //  availability_lb

      if (torrents.length < 1) {
        str = none;
      } else if (sizeWhenDone === 0) {
        str = none;
      } else {
        const available = torrents.reduce((acc, t) => t.getHave() + t.getDesiredAvailable(), 0);
        str = `${fmt.percentString((100.0 * available) / sizeWhenDone)}%`;
      }
      setTextContent(e.availability_lb, str);

      //
      //  downloaded_lb
      //

      if (torrents.length < 1) {
        str = none;
      } else {
        const d = torrents.reduce((acc, t) => acc + t.getDownloadedEver(), 0);
        const f = torrents.reduce((acc, t) => acc + t.getFailedEver(), 0);
        str = f ? `${fmt.size(d)} (${fmt.size(f)} corrupt)` : fmt.size(d);
      }
      setTextContent(e.downloaded_lb, str);

      //
      //  uploaded_lb
      //

      if (torrents.length < 1) {
        str = none;
      } else {
        const u = torrents.reduce((acc, t) => acc + t.getUploadedEver(), 0);
        const d =
          torrents.reduce((acc, t) => acc + t.getDownloadedEver(), 0) ||
          torrents.reduce((acc, t) => acc + t.getHaveValid(), 0);
        str = `${fmt.size(u)} (Ratio: ${fmt.ratioString(Math.ratio(u, d))})`;
      }
      setTextContent(e.uploaded_lb, str);

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
      setTextContent(e.running_time_lb, str);

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
      setTextContent(e.remaining_time_lb, str);

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
      setTextContent(e.last_activity_lb, str);

      //
      // error
      //

      if (torrents.length < 1) {
        str = none;
      } else {
        const get = (t) => t.getErrorString();
        const first = get(torrents[0]);
        str = torrents.every((t) => get(t) === first) ? first : mixed;
      }
      setTextContent(e.error_lb, str || none);

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
          const pieceSize = get(torrents[0]);
          if (torrents.every((t) => get(t) === pieceSize)) {
            str = `${fmt.size(size)} (${pieceCount.toStringWithCommas()} pieces @ ${fmt.mem(
              pieceSize
            )})`;
          } else {
            str = `${fmt.size(size)} (${pieceCount.toStringWithCommas()} pieces)`;
          }
        }
      }
      setTextContent(e.size_lb, str);

      //
      //  hash
      //

      if (torrents.length < 1) {
        str = none;
      } else {
        const get = (t) => t.getHashString();
        const first = get(torrents[0]);
        str = torrents.every((t) => get(t) === first) ? first : mixed;
      }
      setTextContent(e.hash_lb, str);

      //
      //  privacy
      //

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
      setTextContent(e.privacy_lb, str);

      //
      //  comment
      //

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
        setInnerHTML(e.comment_lb, `<a href="${str}" target="_blank" >${str}</a>`);
      } else {
        setTextContent(e.comment_lb, str);
      }

      //
      //  origin
      //

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
      setTextContent(e.origin_lb, str);

      // donwload dir
      if (torrents.length < 1) {
        str = none;
      } else {
        const get = (t) => t.getDownloadDir();
        const first = get(torrents[0]);
        str = torrents.every((t) => get(t) === first) ? first : mixed;
      }
      setTextContent(e.foldername_lb, str);
    },
    ///  PEERS PAGE

    updatePeersPage = function () {
      const html = [];
      const { fmt } = Transmission;
      const { peers_list } = data.elements;
      const { torrents } = data;

      for (const tor of torrents) {
        const peers = tor.getPeers();
        html.push('<div class="inspector_group">');
        if (torrents.length > 1) {
          html.push('<div class="inspector_torrent_label">', sanitizeText(tor.getName()), '</div>');
        }
        if (!peers || !peers.length) {
          html.push('<br></div>'); // firefox won't paint the top border if the div is empty
          continue;
        }
        html.push(
          '<table class="peer_list">',
          '<tr class="inspector_peer_entry even">',
          '<th class="encryptedCol"></th>',
          '<th class="upCol">Up</th>',
          '<th class="downCol">Down</th>',
          '<th class="percentCol">%</th>',
          '<th class="statusCol">Status</th>',
          '<th class="addressCol">Address</th>',
          '<th class="clientCol">Client</th>',
          '</tr>'
        );
        peers.forEach((peer, idx) => {
          html.push(
            '<tr class="inspector_peer_entry ',
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
            '<td class="percentCol">',
            Math.floor(peer.progress * 100),
            '%',
            '</td>',
            '<td>',
            fmt.peerStatus(peer.flagStr),
            '</td>',
            '<td>',
            sanitizeText(peer.address),
            '</td>',
            '<td class="clientCol">',
            sanitizeText(peer.clientName),
            '</td>',
            '</tr>'
          );
        });
        html.push('</table></div>');
      }

      setInnerHTML(peers_list, html.join(''));
    },
    /// TRACKERS PAGE

    getAnnounceState = function (tracker) {
      let s = '';
      switch (tracker.announceState) {
        case Torrent._TrackerActive:
          s = 'Announce in progress';
          break;
        case Torrent._TrackerWaiting: {
          const timeUntilAnnounce = Math.max(
            0,
            tracker.nextAnnounceTime - new Date().getTime() / 1000
          );
          s = `Next announce in ${Transmission.fmt.timeInterval(timeUntilAnnounce)}`;
          break;
        }
        case Torrent._TrackerQueued:
          s = 'Announce is queued';
          break;
        case Torrent._TrackerInactive:
          s = tracker.isBackup ? 'Tracker will be used as a backup' : 'Announce not scheduled';
          break;
        default:
          s = `unknown announce state: ${tracker.announceState}`;
      }
      return s;
    },
    lastAnnounceStatus = function (tracker) {
      let lastAnnounceLabel = 'Last Announce';
      let lastAnnounce = ['N/A'];

      if (tracker.hasAnnounced) {
        const lastAnnounceTime = Transmission.fmt.timestamp(tracker.lastAnnounceTime);
        if (tracker.lastAnnounceSucceeded) {
          lastAnnounce = [
            lastAnnounceTime,
            ' (got ',
            Transmission.fmt.countString('peer', 'peers', tracker.lastAnnouncePeerCount),
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
    },
    lastScrapeStatus = function (tracker) {
      let lastScrapeLabel = 'Last Scrape';
      let lastScrape = 'N/A';

      if (tracker.hasScraped) {
        const lastScrapeTime = Transmission.fmt.timestamp(tracker.lastScrapeTime);
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
    },
    updateTrackersPage = function () {
      const na = 'N/A';
      const { trackers_list } = data.elements;
      const { torrents } = data;

      // By building up the HTML as as string, then have the browser
      // turn this into a DOM tree, this is a fast operation.
      const html = [];
      for (const tor of torrents) {
        html.push('<div class="inspector_group">');

        if (torrents.length > 1) {
          html.push('<div class="inspector_torrent_label">', sanitizeText(tor.getName()), '</div>');
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
              '<div class="inspector_group_label">',
              'Tier ',
              tier + 1,
              '</div>',
              '<ul class="tier_list">'
            );
          }

          // Display construction
          const lastAnnounceStatusHash = lastAnnounceStatus(tracker);
          const announceState = getAnnounceState(tracker);
          const lastScrapeStatusHash = lastScrapeStatus(tracker);
          html.push(
            '<li class="inspector_tracker_entry ',
            idx % 2 ? 'odd' : 'even',
            '"><div class="tracker_host" title="',
            sanitizeText(tracker.announce),
            '">',
            sanitizeText(tracker.host || tracker.announce),
            '</div>',
            '<div class="tracker_activity">',
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

        html.push('</div>'); // inspector_group
      }

      setInnerHTML(trackers_list, html.join(''));
    },
    ///  FILES PAGE

    changeFileCommand = function (fileIndices, command) {
      const torrentId = data.file_torrent.getId();
      data.controller.changeFileCommand(torrentId, fileIndices, command);
    },
    onFileWantedToggled = function (ev, fileIndices, want) {
      changeFileCommand(fileIndices, want ? 'files-wanted' : 'files-unwanted');
    },
    onFilePriorityToggled = function (ev, fileIndices, priority) {
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
      changeFileCommand(fileIndices, command);
    },
    onNameClicked = function (ev, fileRow) {
      $(fileRow.getElement()).siblings().slideToggle();
    },
    clearFileList = function () {
      $(data.elements.file_list).empty();
      delete data.file_torrent;
      delete data.file_torrent_n;
      delete data.file_rows;
    },
    createFileTreeModel = function (tor) {
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
    },
    addNodeToView = function (tor, parent, sub, i) {
      const row = new FileRow(tor, sub.depth, sub.name, sub.file_indices, i % 2);
      data.file_rows.push(row);
      parent.appendChild(row.getElement());
      const e = $(row);
      e.bind('wantedToggled', onFileWantedToggled);
      e.bind('priorityToggled', onFilePriorityToggled);
      e.bind('nameClicked', onNameClicked);
    },
    addSubtreeToView = function (tor, parent, sub, i) {
      const div = document.createElement('div');
      if (sub.parent) {
        addNodeToView(tor, div, sub, i++);
      }
      if (sub.children) {
        for (const val of Object.values(sub.children)) {
          i = addSubtreeToView(tor, div, val);
        }
      }
      parent.appendChild(div);
      return i;
    },
    updateFilesPage = function () {
      const { file_list } = data.elements;
      const { torrents } = data;

      // only show one torrent at a time
      if (torrents.length !== 1) {
        clearFileList();
        return;
      }

      const [tor] = torrents;
      const n = tor ? tor.getFileCount() : 0;
      if (tor !== data.file_torrent || n !== data.file_torrent_n) {
        // rebuild the file list...
        clearFileList();
        data.file_torrent = tor;
        data.file_torrent_n = n;
        data.file_rows = [];
        const fragment = document.createDocumentFragment();
        const tree = createFileTreeModel(tor);
        addSubtreeToView(tor, fragment, tree, 0);
        file_list.appendChild(fragment);
      } else {
        // ...refresh the already-existing file list
        data.file_rows.forEach((row) => row.refresh());
      }
    },
    updateInspector = function () {
      const e = data.elements;
      const { torrents } = data;

      // update the name, which is shown on all the pages
      let name = null;
      if (!torrents || !torrents.length) {
        name = 'No Selection';
      } else if (torrents.length === 1) {
        name = torrents[0].getName();
      } else {
        name = `${torrents.length} Transfers Selected`;
      }
      setTextContent(e.name_lb, name || 'Not Applicable');

      // update the visible page
      if ($(e.info_page).is(':visible')) {
        updateInfoPage();
      } else if ($(e.peers_page).is(':visible')) {
        updatePeersPage();
      } else if ($(e.trackers_page).is(':visible')) {
        updateTrackersPage();
      } else if ($(e.files_page).is(':visible')) {
        updateFilesPage();
      }
    },
    onTabClicked = function (ev) {
      const tab = ev.currentTarget;

      if (isMobileDevice) {
        ev.stopPropagation();
      }

      // select this tab and deselect the others
      $(tab).addClass('selected').siblings().removeClass('selected');

      // show this tab and hide the others
      $(`#${tab.id.replace('tab', 'page')}`)
        .show()
        .siblings('.inspector-page')
        .hide();

      updateInspector();
    },
    initialize = function (controller_arg) {
      data.controller = controller_arg;

      $('.inspector-tab').click(onTabClicked);

      data.elements.info_page = document.getElementById('inspector-page-info');
      data.elements.files_page = document.getElementById('inspector-page-files');
      data.elements.peers_page = document.getElementById('inspector-page-peers');
      data.elements.trackers_page = document.getElementById('inspector-page-trackers');

      data.elements.file_list = document.getElementById('inspector_file_list');
      data.elements.peers_list = document.getElementById('inspector_peers_list');
      data.elements.trackers_list = document.getElementById('inspector_trackers_list');

      data.elements.have_lb = document.getElementById('inspector-info-have');
      data.elements.availability_lb = document.getElementById('inspector-info-availability');
      data.elements.downloaded_lb = document.getElementById('inspector-info-downloaded');
      data.elements.uploaded_lb = document.getElementById('inspector-info-uploaded');
      data.elements.state_lb = document.getElementById('inspector-info-state');
      data.elements.running_time_lb = document.getElementById('inspector-info-running-time');
      data.elements.remaining_time_lb = document.getElementById('inspector-info-remaining-time');
      data.elements.last_activity_lb = document.getElementById('inspector-info-last-activity');
      data.elements.error_lb = document.getElementById('inspector-info-error');
      data.elements.size_lb = document.getElementById('inspector-info-size');
      data.elements.foldername_lb = document.getElementById('inspector-info-location');
      data.elements.hash_lb = document.getElementById('inspector-info-hash');
      data.elements.privacy_lb = document.getElementById('inspector-info-privacy');
      data.elements.origin_lb = document.getElementById('inspector-info-origin');
      data.elements.comment_lb = document.getElementById('inspector-info-comment');
      data.elements.name_lb = document.getElementById('torrent_inspector_name');

      // force initial 'N/A' updates on all the pages
      updateInspector();
      updateInfoPage();
      updatePeersPage();
      updateTrackersPage();
      updateFilesPage();
    };

  /****
   *****  PUBLIC FUNCTIONS
   ****/

  this.setTorrents = function (torrents) {
    const d = data;

    // update the inspector when a selected torrent's data changes.
    $(d.torrents).unbind('dataChanged.inspector');
    $(torrents).bind('dataChanged.inspector', $.proxy(updateInspector, this));
    d.torrents = torrents;

    // periodically ping the server for updates to this torrent
    clearInterval(data.timer);
    data.timer = null;
    if (d.torrents && d.torrents.length) {
      refreshTorrents();
      data.timer = setInterval(refreshTorrents, 2000);
    }

    // refresh the inspector's UI
    updateInspector();
  };

  initialize(controller);
}
