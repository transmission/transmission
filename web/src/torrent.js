/* @license This file Copyright © Mnemosyne LLC.
   It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
   or any future license endorsed by Mnemosyne LLC.
   License text can be found in the licenses/ folder. */

import { Formatter } from './formatter.js';
import { Prefs } from './prefs.js';
import { deepEqual } from './utils.js';

///

export class Torrent extends EventTarget {
  constructor(data) {
    super();

    this.fieldObservers = {};
    this.fields = {};
    this.refresh(data);
  }

  notifyOnFieldChange(field, callback) {
    this.fieldObservers[field] = this.fieldObservers[field] || [];
    this.fieldObservers[field].push(callback);
  }

  setField(o, name, value) {
    const old_value = o[name];

    if (deepEqual(old_value, value)) {
      return false;
    }

    const observers = this.fieldObservers[name];
    if (o === this.fields && observers && observers.length > 0) {
      for (const observer of observers) {
        observer.call(this, value, old_value, name);
      }
    }
    o[name] = value;
    return true;
  }

  // fields.files is an array of unions of RPC's "files" and "fileStats" objects.
  updateFiles(files) {
    let changed = false;
    const myfiles = this.fields.files || [];
    const keys = ['length', 'name', 'bytes_completed', 'wanted', 'priority'];

    for (const [index, f] of files.entries()) {
      const myfile = myfiles[index] || {};
      for (const key of keys) {
        if (key in f) {
          changed |= this.setField(myfile, key, f[key]);
        }
      }
      myfiles[index] = myfile;
    }
    this.fields.files = myfiles;
    return changed;
  }

  static collateTrackers(trackers) {
    return trackers.map((t) => t.announce.toLowerCase()).join('\t');
  }

  refreshFields(data) {
    let changed = false;

    for (const [key, value] of Object.entries(data)) {
      switch (key) {
        case 'files':
        case 'file_stats': // merge files and file_stats together
          changed |= this.updateFiles(value);
          break;
        case 'tracker_stats': // 'tracker_stats' is a superset of 'trackers'...
          changed |= this.setField(this.fields, 'trackers', value);
          break;
        case 'trackers': // ...so only save 'trackers' if we don't have it already
          if (!(key in this.fields)) {
            changed |= this.setField(this.fields, key, value);
          }
          break;
        case 'name':
          if (this.setField(this.fields, key, data[key])) {
            this.fields.collatedName = '';
            changed = true;
          }
          break;
        default:
          changed |= this.setField(this.fields, key, value);
      }
    }

    return changed;
  }

  refresh(data) {
    if (this.refreshFields(data)) {
      this.dispatchEvent(new Event('dataChanged'));
    }
  }

  ///

  // simple accessors
  getComment() {
    return this.fields.comment;
  }
  getCreator() {
    return this.fields.creator;
  }
  getDateAdded() {
    return this.fields.added_date;
  }
  getDateCreated() {
    return this.fields.date_created;
  }
  getDesiredAvailable() {
    return this.fields.desired_available;
  }
  getDownloadDir() {
    return this.fields.download_dir;
  }
  getDownloadSpeed() {
    return this.fields.rate_download;
  }
  getDownloadedEver() {
    return this.fields.downloaded_ever;
  }
  getError() {
    return this.fields.error;
  }
  getErrorString() {
    return this.fields.error_string;
  }
  getETA() {
    return this.fields.eta;
  }
  getFailedEver() {
    return this.fields.corrupt_ever;
  }
  getFiles() {
    return this.fields.files || [];
  }
  getFile(index) {
    return this.fields.files[index];
  }
  getFileCount() {
    return this.fields.file_count;
  }
  getHashString() {
    return this.fields.hash_string;
  }
  getHave() {
    return this.getHaveValid() + this.getHaveUnchecked();
  }
  getHaveUnchecked() {
    return this.fields.have_unchecked;
  }
  getHaveValid() {
    return this.fields.have_valid;
  }
  getId() {
    return this.fields.id;
  }
  getLabels() {
    return this.fields.labels.sort();
  }
  getLastActivity() {
    return this.fields.activity_date;
  }
  getLeftUntilDone() {
    return this.fields.left_until_done;
  }
  getMagnetLink() {
    return this.fields.magnet_link;
  }
  getMetadataPercentComplete() {
    return this.fields.metadata_percent_complete;
  }
  getName() {
    return this.fields.name || 'Unknown';
  }
  getPeers() {
    return this.fields.peers || [];
  }
  getPeersConnected() {
    return this.fields.peers_connected;
  }
  getPeersGettingFromUs() {
    return this.fields.peers_getting_from_us;
  }
  getPeersSendingToUs() {
    return this.fields.peers_sending_to_us;
  }
  getPieceCount() {
    return this.fields.piece_count;
  }
  getPieceSize() {
    return this.fields.piece_size;
  }
  getPrimaryMimeType() {
    return this.fields.primary_mime_type || 'application/octet-stream';
  }
  getPrivateFlag() {
    return this.fields.is_private;
  }
  getQueuePosition() {
    return this.fields.queue_position;
  }
  getRecheckProgress() {
    return this.fields.recheck_progress;
  }
  getSeedRatioLimit() {
    return this.fields.seed_ratio_limit;
  }
  getSeedRatioMode() {
    return this.fields.seed_ratio_mode;
  }
  getSizeWhenDone() {
    return this.fields.size_when_done;
  }
  getStartDate() {
    return this.fields.start_date;
  }
  getStatus() {
    return this.fields.status;
  }
  getTotalSize() {
    return this.fields.total_size;
  }
  getTrackers() {
    return this.fields.trackers || [];
  }
  getUploadSpeed() {
    return this.fields.rate_upload;
  }
  getUploadRatio() {
    return this.fields.upload_ratio;
  }
  getUploadedEver() {
    return this.fields.uploaded_ever;
  }
  getWebseedsSendingToUs() {
    return this.fields.webseeds_sending_to_us;
  }
  isFinished() {
    return this.fields.is_finished;
  }

  // derived accessors
  hasExtraInfo() {
    return 'hash_string' in this.fields;
  }
  isSeeding() {
    return this.getStatus() === Torrent._StatusSeed;
  }
  isStopped() {
    return this.getStatus() === Torrent._StatusStopped;
  }
  isChecking() {
    return this.getStatus() === Torrent._StatusCheck;
  }
  isDownloading() {
    return this.getStatus() === Torrent._StatusDownload;
  }
  isQueued() {
    return (
      this.getStatus() === Torrent._StatusDownloadWait ||
      this.getStatus() === Torrent._StatusSeedWait
    );
  }
  isDone() {
    return this.getLeftUntilDone() < 1;
  }
  needsMetaData() {
    return this.getMetadataPercentComplete() < 1;
  }
  getActivity() {
    return this.getDownloadSpeed() + this.getUploadSpeed();
  }
  getPercentDoneStr() {
    return Formatter.percentString(100 * this.getPercentDone(), 1);
  }
  getPercentDone() {
    return this.fields.percent_done;
  }
  getStateString() {
    switch (this.getStatus()) {
      case Torrent._StatusStopped:
        return this.isFinished() ? 'Seeding complete' : 'Paused';
      case Torrent._StatusCheckWait:
        return 'Queued for verification';
      case Torrent._StatusCheck:
        return 'Verifying local data';
      case Torrent._StatusDownloadWait:
        return 'Queued for download';
      case Torrent._StatusDownload:
        return 'Downloading';
      case Torrent._StatusSeedWait:
        return 'Queued for seeding';
      case Torrent._StatusSeed:
        return 'Seeding';
      case null:
        return 'Unknown';
      default:
        return 'Error';
    }
  }
  seedRatioLimit(controller) {
    switch (this.getSeedRatioMode()) {
      case Torrent._RatioUseGlobal:
        return controller.seedRatioLimit();
      case Torrent._RatioUseLocal:
        return this.getSeedRatioLimit();
      default:
        return -1;
    }
  }
  getErrorMessage() {
    const string = this.getErrorString();
    switch (this.getError()) {
      case Torrent._ErrTrackerWarning:
        return `Tracker returned a warning: ${string}`;
      case Torrent._ErrTrackerError:
        return `Tracker returned an error: ${string}`;
      case Torrent._ErrLocalError:
        return `Error: ${string}`;
      default:
        return null;
    }
  }
  getCollatedName() {
    const f = this.fields;
    if (!f.collatedName && f.name) {
      f.collatedName = f.name.toLowerCase();
    }
    return f.collatedName || '';
  }
  getCollatedTrackers() {
    const f = this.fields;
    if (!f.collatedTrackers && f.trackers) {
      f.collatedTrackers = Torrent.collateTrackers(f.trackers);
    }
    return f.collatedTrackers || '';
  }

  /****
   *****
   ****/

  testState(state) {
    const s = this.getStatus();

    switch (state) {
      case Prefs.FilterActive:
        return (
          this.getPeersGettingFromUs() > 0 ||
          this.getPeersSendingToUs() > 0 ||
          this.getWebseedsSendingToUs() > 0 ||
          this.isChecking()
        );
      case Prefs.FilterSeeding:
        return s === Torrent._StatusSeed || s === Torrent._StatusSeedWait;
      case Prefs.FilterDownloading:
        return (
          s === Torrent._StatusDownload || s === Torrent._StatusDownloadWait
        );
      case Prefs.FilterPaused:
        return this.isStopped();
      case Prefs.FilterFinished:
        return this.isFinished();
      default:
        return true;
    }
  }

  /**
   * @param state one of Prefs.Filter*
   * @param tracker tracker name
   * @param search substring to look for, or null
   * @param labels array of labels. Empty array matches all.
   * @return true if it passes the test, false if it fails
   */
  test(state, tracker, search, labels) {
    // filter by state...
    let pass = this.testState(state);

    // maybe filter by text...
    if (pass && search) {
      pass = this.getCollatedName().includes(search.toLowerCase());
    }

    // maybe filter by labels...
    if (pass) {
      // pass if this torrent has any of these labels
      const torrent_labels = this.getLabels();
      if (labels.length > 0) {
        pass = labels.some((label) => torrent_labels.includes(label));
      }
    }

    // maybe filter by tracker...
    if (pass && tracker && tracker.length > 0) {
      pass = this.getCollatedTrackers().includes(tracker);
    }

    return pass;
  }

  static compareById(ta, tb) {
    return ta.getId() - tb.getId();
  }
  static compareByName(ta, tb) {
    return (
      ta.getCollatedName().localeCompare(tb.getCollatedName()) ||
      Torrent.compareById(ta, tb)
    );
  }
  static compareByQueue(ta, tb) {
    return ta.getQueuePosition() - tb.getQueuePosition();
  }
  static compareByAge(ta, tb) {
    const a = ta.getDateAdded();
    const b = tb.getDateAdded();

    return b - a || Torrent.compareByQueue(ta, tb);
  }
  static compareByState(ta, tb) {
    const a = ta.getStatus();
    const b = tb.getStatus();

    return b - a || Torrent.compareByQueue(ta, tb);
  }
  static compareByActivity(ta, tb) {
    const a = ta.getActivity();
    const b = tb.getActivity();

    return b - a || Torrent.compareByState(ta, tb);
  }
  static compareByRatio(ta, tb) {
    const a = ta.getUploadRatio();
    const b = tb.getUploadRatio();

    if (a < b) {
      return 1;
    }
    if (a > b) {
      return -1;
    }
    return Torrent.compareByState(ta, tb);
  }
  static compareByProgress(ta, tb) {
    const a = ta.getPercentDone();
    const b = tb.getPercentDone();

    return a - b || Torrent.compareByRatio(ta, tb);
  }
  static compareBySize(ta, tb) {
    const a = ta.getTotalSize();
    const b = tb.getTotalSize();

    return a - b || Torrent.compareByName(ta, tb);
  }

  static compareTorrents(a, b, sortMode, sortDirection) {
    let index = 0;

    switch (sortMode) {
      case Prefs.SortByActivity:
        index = Torrent.compareByActivity(a, b);
        break;
      case Prefs.SortByAge:
        index = Torrent.compareByAge(a, b);
        break;
      case Prefs.SortByQueue:
        index = Torrent.compareByQueue(a, b);
        break;
      case Prefs.SortByProgress:
        index = Torrent.compareByProgress(a, b);
        break;
      case Prefs.SortBySize:
        index = Torrent.compareBySize(a, b);
        break;
      case Prefs.SortByState:
        index = Torrent.compareByState(a, b);
        break;
      case Prefs.SortByRatio:
        index = Torrent.compareByRatio(a, b);
        break;
      case Prefs.SortByName:
        index = Torrent.compareByName(a, b);
        break;
      default:
        console.log(`Unrecognized sort mode: ${sortMode}`);
        index = Torrent.compareByName(a, b);
        break;
    }

    if (sortDirection === Prefs.SortDescending) {
      index = -index;
    }

    return index;
  }

  /**
   * @param torrents an array of Torrent objects
   * @param sortMode one of Prefs.SortBy*
   * @param sortDirection Prefs.SortAscending or Prefs.SortDescending
   */
  static sortTorrents(torrents, sortMode, sortDirection) {
    switch (sortMode) {
      case Prefs.SortByActivity:
        torrents.sort(this.compareByActivity);
        break;
      case Prefs.SortByAge:
        torrents.sort(this.compareByAge);
        break;
      case Prefs.SortByName:
        torrents.sort(this.compareByName);
        break;
      case Prefs.SortByProgress:
        torrents.sort(this.compareByProgress);
        break;
      case Prefs.SortByQueue:
        torrents.sort(this.compareByQueue);
        break;
      case Prefs.SortByRatio:
        torrents.sort(this.compareByRatio);
        break;
      case Prefs.SortBySize:
        torrents.sort(this.compareBySize);
        break;
      case Prefs.SortByState:
        torrents.sort(this.compareByState);
        break;
      default:
        console.log(`Unrecognized sort mode: ${sortMode}`);
        torrents.sort(this.compareByName);
        break;
    }

    if (sortDirection === Prefs.SortDescending) {
      torrents.reverse();
    }

    return torrents;
  }
}

// Torrent.fields.status
Torrent._StatusStopped = 0;
Torrent._StatusCheckWait = 1;
Torrent._StatusCheck = 2;
Torrent._StatusDownloadWait = 3;
Torrent._StatusDownload = 4;
Torrent._StatusSeedWait = 5;
Torrent._StatusSeed = 6;

// Torrent.fields.seed_ratio_mode
Torrent._RatioUseGlobal = 0;
Torrent._RatioUseLocal = 1;
Torrent._RatioUnlimited = 2;

// Torrent.fields.error
Torrent._ErrNone = 0;
Torrent._ErrTrackerWarning = 1;
Torrent._ErrTrackerError = 2;
Torrent._ErrLocalError = 3;

// tracker_stats' announceState
Torrent._TrackerInactive = 0;
Torrent._TrackerWaiting = 1;
Torrent._TrackerQueued = 2;
Torrent._TrackerActive = 3;

Torrent.Fields = {};

// commonly used fields which only need to be loaded once,
// either on startup or when a magnet finishes downloading its metadata
// finishes downloading its metadata
Torrent.Fields.Metadata = [
  'added_date',
  'file_count',
  'name',
  'primary_mime_type',
  'total_size',
];

// commonly used fields which need to be periodically refreshed
Torrent.Fields.Stats = [
  'error',
  'error_string',
  'eta',
  'is_finished',
  'is_stalled',
  'labels',
  'left_until_done',
  'metadata_percent_complete',
  'peers_connected',
  'peers_getting_from_us',
  'peers_sending_to_us',
  'percent_done',
  'queue_position',
  'rate_download',
  'rate_upload',
  'recheck_progress',
  'seed_ratio_mode',
  'seed_ratio_limit',
  'size_when_done',
  'status',
  'trackers',
  'download_dir',
  'uploaded_ever',
  'upload_ratio',
  'webseeds_sending_to_us',
];

// fields used by the inspector which only need to be loaded once
Torrent.Fields.InfoExtra = [
  'comment',
  'creator',
  'date_created',
  'files',
  'hash_string',
  'is_private',
  'magnet_link',
  'piece_count',
  'piece_size',
];

// fields used in the inspector which need to be periodically refreshed
Torrent.Fields.StatsExtra = [
  'activity_date',
  'corrupt_ever',
  'desired_available',
  'downloaded_ever',
  'file_stats',
  'have_unchecked',
  'have_valid',
  'peers',
  'start_date',
  'tracker_stats',
];
