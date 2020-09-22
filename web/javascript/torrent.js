'use strict';

/**
 * Copyright © Mnemosyne LLC
 *
 * This file is licensed under the GPLv2.
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 */

class Torrent extends EventTarget {
  constructor(data) {
    super();

    const debounce = (callback, wait = 100) => {
      let timeout = null;
      return (...args) => {
        const context = this;
        clearTimeout(timeout);
        timeout = setTimeout(() => callback.apply(context, args), wait);
      };
    };

    this.dispatchDataChanged = debounce(() => this.dispatchEvent(new Event('dataChanged')));
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

    if (old_value === value) {
      return false;
    }

    const observers = this.fieldObservers[name];
    if (o === this.fields && observers && observers.length) {
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
    const keys = ['length', 'name', 'bytesCompleted', 'wanted', 'priority'];

    for (let i = 0; i < files.length; ++i) {
      const f = files[i];
      const myfile = myfiles[i] || {};
      for (const key of keys) {
        if (key in f) {
          changed |= this.setField(myfile, key, f[key]);
        }
      }
      myfiles[i] = myfile;
    }
    this.fields.files = myfiles;
    return changed;
  }

  static collateTrackers(trackers) {
    return trackers.map((t) => t.announce.toLowerCase()).join('\t');
  }

  refreshFields(data) {
    let changed = false;

    for (const [key, val] of Object.entries(data)) {
      switch (key) {
        case 'files':
        case 'fileStats': // merge files and fileStats together
          changed |= this.updateFiles(val);
          break;
        case 'trackerStats': // 'trackerStats' is a superset of 'trackers'...
          changed |= this.setField(this.fields, 'trackers', val);
          break;
        case 'trackers': // ...so only save 'trackers' if we don't have it already
          if (!(key in this.fields)) {
            changed |= this.setField(this.fields, key, val);
          }
          break;
        default:
          changed |= this.setField(this.fields, key, val);
      }
    }

    return changed;
  }

  refresh(data) {
    if (this.refreshFields(data)) {
      this.dispatchDataChanged();
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
    return this.fields.addedDate;
  }
  getDateCreated() {
    return this.fields.dateCreated;
  }
  getDesiredAvailable() {
    return this.fields.desiredAvailable;
  }
  getDownloadDir() {
    return this.fields.downloadDir;
  }
  getDownloadSpeed() {
    return this.fields.rateDownload;
  }
  getDownloadedEver() {
    return this.fields.downloadedEver;
  }
  getError() {
    return this.fields.error;
  }
  getErrorString() {
    return this.fields.errorString;
  }
  getETA() {
    return this.fields.eta;
  }
  getFailedEver() {
    return this.fields.corruptEver;
  }
  getFile(i) {
    return this.fields.files[i];
  }
  getFileCount() {
    return this.fields.files ? this.fields.files.length : 0;
  }
  getHashString() {
    return this.fields.hashString;
  }
  getHave() {
    return this.getHaveValid() + this.getHaveUnchecked();
  }
  getHaveUnchecked() {
    return this.fields.haveUnchecked;
  }
  getHaveValid() {
    return this.fields.haveValid;
  }
  getId() {
    return this.fields.id;
  }
  getLastActivity() {
    return this.fields.activityDate;
  }
  getLeftUntilDone() {
    return this.fields.leftUntilDone;
  }
  getMetadataPercentComplete() {
    return this.fields.metadataPercentComplete;
  }
  getName() {
    return this.fields.name || 'Unknown';
  }
  getPeers() {
    return this.fields.peers;
  }
  getPeersConnected() {
    return this.fields.peersConnected;
  }
  getPeersGettingFromUs() {
    return this.fields.peersGettingFromUs;
  }
  getPeersSendingToUs() {
    return this.fields.peersSendingToUs;
  }
  getPieceCount() {
    return this.fields.pieceCount;
  }
  getPieceSize() {
    return this.fields.pieceSize;
  }
  getPrivateFlag() {
    return this.fields.isPrivate;
  }
  getQueuePosition() {
    return this.fields.queuePosition;
  }
  getRecheckProgress() {
    return this.fields.recheckProgress;
  }
  getSeedRatioLimit() {
    return this.fields.seedRatioLimit;
  }
  getSeedRatioMode() {
    return this.fields.seedRatioMode;
  }
  getSizeWhenDone() {
    return this.fields.sizeWhenDone;
  }
  getStartDate() {
    return this.fields.startDate;
  }
  getStatus() {
    return this.fields.status;
  }
  getTotalSize() {
    return this.fields.totalSize;
  }
  getTrackers() {
    return this.fields.trackers;
  }
  getUploadSpeed() {
    return this.fields.rateUpload;
  }
  getUploadRatio() {
    return this.fields.uploadRatio;
  }
  getUploadedEver() {
    return this.fields.uploadedEver;
  }
  getWebseedsSendingToUs() {
    return this.fields.webseedsSendingToUs;
  }
  isFinished() {
    return this.fields.isFinished;
  }

  // derived accessors
  hasExtraInfo() {
    return 'hashString' in this.fields;
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
    return Transmission.fmt.percentString(100 * this.getPercentDone());
  }
  getPercentDone() {
    return this.fields.percentDone;
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
    const str = this.getErrorString();
    switch (this.getError()) {
      case Torrent._ErrTrackerWarning:
        return `Tracker returned a warning: ${str}`;
      case Torrent._ErrTrackerError:
        return `Tracker returned an error: ${str}`;
      case Torrent._ErrLocalError:
        return `Error: ${str}`;
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
      case Prefs._FilterActive:
        return (
          this.getPeersGettingFromUs() > 0 ||
          this.getPeersSendingToUs() > 0 ||
          this.getWebseedsSendingToUs() > 0 ||
          this.isChecking()
        );
      case Prefs._FilterSeeding:
        return s === Torrent._StatusSeed || s === Torrent._StatusSeedWait;
      case Prefs._FilterDownloading:
        return s === Torrent._StatusDownload || s === Torrent._StatusDownloadWait;
      case Prefs._FilterPaused:
        return this.isStopped();
      case Prefs._FilterFinished:
        return this.isFinished();
      default:
        return true;
    }
  }

  /**
   * @param filter one of Prefs._Filter*
   * @param search substring to look for, or null
   * @return true if it passes the test, false if it fails
   */
  test(state, search, tracker) {
    // flter by state...
    let pass = this.testState(state);

    // maybe filter by text...
    if (pass && search && search.length) {
      pass = this.getCollatedName().indexOf(search.toLowerCase()) !== -1;
    }

    // maybe filter by tracker...
    if (pass && tracker && tracker.length) {
      pass = this.getCollatedTrackers().indexOf(tracker) !== -1;
    }

    return pass;
  }

  static compareById(ta, tb) {
    return ta.getId() - tb.getId();
  }
  static compareByName(ta, tb) {
    return ta.getCollatedName().localeCompare(tb.getCollatedName()) || Torrent.compareById(ta, tb);
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

  static compareTorrents(a, b, sortMethod, sortDirection) {
    let i = 0;

    switch (sortMethod) {
      case Prefs._SortByActivity:
        i = Torrent.compareByActivity(a, b);
        break;
      case Prefs._SortByAge:
        i = Torrent.compareByAge(a, b);
        break;
      case Prefs._SortByQueue:
        i = Torrent.compareByQueue(a, b);
        break;
      case Prefs._SortByProgress:
        i = Torrent.compareByProgress(a, b);
        break;
      case Prefs._SortBySize:
        i = Torrent.compareBySize(a, b);
        break;
      case Prefs._SortByState:
        i = Torrent.compareByState(a, b);
        break;
      case Prefs._SortByRatio:
        i = Torrent.compareByRatio(a, b);
        break;
      default:
        i = Torrent.compareByName(a, b);
        break;
    }

    if (sortDirection === Prefs._SortDescending) {
      i = -i;
    }

    return i;
  }

  /**
   * @param torrents an array of Torrent objects
   * @param sortMethod one of Prefs._SortBy*
   * @param sortDirection Prefs._SortAscending or Prefs._SortDescending
   */
  static sortTorrents(torrents, sortMethod, sortDirection) {
    switch (sortMethod) {
      case Prefs._SortByActivity:
        torrents.sort(this.compareByActivity);
        break;
      case Prefs._SortByAge:
        torrents.sort(this.compareByAge);
        break;
      case Prefs._SortByQueue:
        torrents.sort(this.compareByQueue);
        break;
      case Prefs._SortByProgress:
        torrents.sort(this.compareByProgress);
        break;
      case Prefs._SortBySize:
        torrents.sort(this.compareBySize);
        break;
      case Prefs._SortByState:
        torrents.sort(this.compareByState);
        break;
      case Prefs._SortByRatio:
        torrents.sort(this.compareByRatio);
        break;
      default:
        torrents.sort(this.compareByName);
        break;
    }

    if (sortDirection === Prefs._SortDescending) {
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

// Torrent.fields.seedRatioMode
Torrent._RatioUseGlobal = 0;
Torrent._RatioUseLocal = 1;
Torrent._RatioUnlimited = 2;

// Torrent.fields.error
Torrent._ErrNone = 0;
Torrent._ErrTrackerWarning = 1;
Torrent._ErrTrackerError = 2;
Torrent._ErrLocalError = 3;

// TrackerStats' announceState
Torrent._TrackerInactive = 0;
Torrent._TrackerWaiting = 1;
Torrent._TrackerQueued = 2;
Torrent._TrackerActive = 3;

Torrent.Fields = {};

// commonly used fields which only need to be loaded once,
// either on startup or when a magnet finishes downloading its metadata
// finishes downloading its metadata
Torrent.Fields.Metadata = ['addedDate', 'name', 'totalSize'];

// commonly used fields which need to be periodically refreshed
Torrent.Fields.Stats = [
  'error',
  'errorString',
  'eta',
  'isFinished',
  'isStalled',
  'leftUntilDone',
  'metadataPercentComplete',
  'peersConnected',
  'peersGettingFromUs',
  'peersSendingToUs',
  'percentDone',
  'queuePosition',
  'rateDownload',
  'rateUpload',
  'recheckProgress',
  'seedRatioMode',
  'seedRatioLimit',
  'sizeWhenDone',
  'status',
  'trackers',
  'downloadDir',
  'uploadedEver',
  'uploadRatio',
  'webseedsSendingToUs',
];

// fields used by the inspector which only need to be loaded once
Torrent.Fields.InfoExtra = [
  'comment',
  'creator',
  'dateCreated',
  'files',
  'hashString',
  'isPrivate',
  'pieceCount',
  'pieceSize',
];

// fields used in the inspector which need to be periodically refreshed
Torrent.Fields.StatsExtra = [
  'activityDate',
  'corruptEver',
  'desiredAvailable',
  'downloadedEver',
  'fileStats',
  'haveUnchecked',
  'haveValid',
  'peers',
  'startDate',
  'trackerStats',
];
