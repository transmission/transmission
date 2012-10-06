/**
 * Copyright © Mnemosyne LLC
 *
 * This file is licensed under the GPLv2.
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 */

function Torrent(data)
{
	this.initialize(data);
}

/***
****
****  Constants
****
***/

// Torrent.fields.status
Torrent._StatusStopped         = 0;
Torrent._StatusCheckWait       = 1;
Torrent._StatusCheck           = 2;
Torrent._StatusDownloadWait    = 3;
Torrent._StatusDownload        = 4;
Torrent._StatusSeedWait        = 5;
Torrent._StatusSeed            = 6;

// Torrent.fields.seedRatioMode
Torrent._RatioUseGlobal        = 0;
Torrent._RatioUseLocal         = 1;
Torrent._RatioUnlimited        = 2;

// Torrent.fields.error
Torrent._ErrNone               = 0;
Torrent._ErrTrackerWarning     = 1;
Torrent._ErrTrackerError       = 2;
Torrent._ErrLocalError         = 3;

// TrackerStats' announceState
Torrent._TrackerInactive       = 0;
Torrent._TrackerWaiting        = 1;
Torrent._TrackerQueued         = 2;
Torrent._TrackerActive         = 3;


Torrent.Fields = { };

// commonly used fields which only need to be loaded once,
// either on startup or when a magnet finishes downloading its metadata
// finishes downloading its metadata
Torrent.Fields.Metadata = [
	'addedDate',
	'name',
	'totalSize'
];

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
	'webseedsSendingToUs'
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
	'pieceSize'
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
	'trackerStats'
];

/***
****
****  Methods
****
***/

Torrent.prototype =
{
	initialize: function(data)
	{
		this.fields = {};
		this.fieldObservers = {};
		this.refresh (data);
	},

	notifyOnFieldChange: function(field, callback) {
		this.fieldObservers[field] = this.fieldObservers[field] || [];
		this.fieldObservers[field].push(callback);
	},

	setField: function(o, name, value)
	{
		var i, observer;
		
		if (o[name] === value)
			return false;
		if (o == this.fields && this.fieldObservers[name] && this.fieldObservers[name].length) {
			for (i=0; observer=this.fieldObservers[name][i]; ++i) {
				observer.call(this, value, o[name], name);
			}
		}
		o[name] = value;
		return true;
	},

	// fields.files is an array of unions of RPC's "files" and "fileStats" objects.
	updateFiles: function(files)
	{
		var changed = false,
		    myfiles = this.fields.files || [],
		    keys = [ 'length', 'name', 'bytesCompleted', 'wanted', 'priority' ],
		    i, f, j, key, myfile;

		for (i=0; f=files[i]; ++i) {
			myfile = myfiles[i] || {};
			for (j=0; key=keys[j]; ++j)
				if(key in f)
					changed |= this.setField(myfile,key,f[key]);
			myfiles[i] = myfile;
		}
		this.fields.files = myfiles;
		return changed;
	},

	collateTrackers: function(trackers)
	{
		var i, t, announces = [];

		for (i=0; t=trackers[i]; ++i)
			announces.push(t.announce.toLowerCase());
		return announces.join('\t');
	},

	refreshFields: function(data)
	{
		var key,
		    changed = false;

		for (key in data) {
			switch (key) {
				case 'files':
				case 'fileStats': // merge files and fileStats together
					changed |= this.updateFiles(data[key]);
					break;
				case 'trackerStats': // 'trackerStats' is a superset of 'trackers'...
					changed |= this.setField(this.fields,'trackers',data[key]);
					break;
				case 'trackers': // ...so only save 'trackers' if we don't have it already
					if (!(key in this.fields))
						changed |= this.setField(this.fields,key,data[key]);
					break;
				default:
					changed |= this.setField(this.fields,key,data[key]);
			}
		}

		return changed;
	},

	refresh: function(data)
	{
		if (this.refreshFields(data))
			$(this).trigger('dataChanged', this);
	},

	/****
	*****
	****/

	// simple accessors
	getComment: function() { return this.fields.comment; },
	getCreator: function() { return this.fields.creator; },
	getDateAdded: function() { return this.fields.addedDate; },
	getDateCreated: function() { return this.fields.dateCreated; },
	getDesiredAvailable: function() { return this.fields.desiredAvailable; },
	getDownloadDir: function() { return this.fields.downloadDir; },
	getDownloadSpeed: function() { return this.fields.rateDownload; },
	getDownloadedEver: function() { return this.fields.downloadedEver; },
	getError: function() { return this.fields.error; },
	getErrorString: function() { return this.fields.errorString; },
	getETA: function() { return this.fields.eta; },
	getFailedEver: function(i) { return this.fields.corruptEver; },
	getFile: function(i) { return this.fields.files[i]; },
	getFileCount: function() { return this.fields.files ? this.fields.files.length : 0; },
	getHashString: function() { return this.fields.hashString; },
	getHave: function() { return this.getHaveValid() + this.getHaveUnchecked() },
	getHaveUnchecked: function() { return this.fields.haveUnchecked; },
	getHaveValid: function() { return this.fields.haveValid; },
	getId: function() { return this.fields.id; },
	getLastActivity: function() { return this.fields.activityDate; },
	getLeftUntilDone: function() { return this.fields.leftUntilDone; },
	getMetadataPercentComplete: function() { return this.fields.metadataPercentComplete; },
	getName: function() { return this.fields.name || 'Unknown'; },
	getPeers: function() { return this.fields.peers; },
	getPeersConnected: function() { return this.fields.peersConnected; },
	getPeersGettingFromUs: function() { return this.fields.peersGettingFromUs; },
	getPeersSendingToUs: function() { return this.fields.peersSendingToUs; },
	getPieceCount: function() { return this.fields.pieceCount; },
	getPieceSize: function() { return this.fields.pieceSize; },
	getPrivateFlag: function() { return this.fields.isPrivate; },
	getQueuePosition: function() { return this.fields.queuePosition; },
	getRecheckProgress: function() { return this.fields.recheckProgress; },
	getSeedRatioLimit: function() { return this.fields.seedRatioLimit; },
	getSeedRatioMode: function() { return this.fields.seedRatioMode; },
	getSizeWhenDone: function() { return this.fields.sizeWhenDone; },
	getStartDate: function() { return this.fields.startDate; },
	getStatus: function() { return this.fields.status; },
	getTotalSize: function() { return this.fields.totalSize; },
	getTrackers: function() { return this.fields.trackers; },
	getUploadSpeed: function() { return this.fields.rateUpload; },
	getUploadRatio: function() { return this.fields.uploadRatio; },
	getUploadedEver: function() { return this.fields.uploadedEver; },
	getWebseedsSendingToUs: function() { return this.fields.webseedsSendingToUs; },
	isFinished: function() { return this.fields.isFinished; },

	// derived accessors
	hasExtraInfo: function() { return 'hashString' in this.fields; },
	isSeeding: function() { return this.getStatus() === Torrent._StatusSeed; },
	isStopped: function() { return this.getStatus() === Torrent._StatusStopped; },
	isChecking: function() { return this.getStatus() === Torrent._StatusCheck; },
	isDownloading: function() { return this.getStatus() === Torrent._StatusDownload; },
	isDone: function() { return this.getLeftUntilDone() < 1; },
	needsMetaData: function(){ return this.getMetadataPercentComplete() < 1; },
	getActivity: function() { return this.getDownloadSpeed() + this.getUploadSpeed(); },
	getPercentDoneStr: function() { return Transmission.fmt.percentString(100*this.getPercentDone()); },
	getPercentDone: function() { return this.fields.percentDone; },
	getStateString: function() {
		switch(this.getStatus()) {
			case Torrent._StatusStopped:        return this.isFinished() ? 'Seeding complete' : 'Paused';
			case Torrent._StatusCheckWait:      return 'Queued for verification';
			case Torrent._StatusCheck:          return 'Verifying local data';
			case Torrent._StatusDownloadWait:   return 'Queued for download';
			case Torrent._StatusDownload:       return 'Downloading';
			case Torrent._StatusSeedWait:       return 'Queued for seeding';
			case Torrent._StatusSeed:           return 'Seeding';
			case null:
			case undefined:                     return 'Unknown';
			default:                            return 'Error';
		}
	},
	seedRatioLimit: function(controller){
		switch(this.getSeedRatioMode()) {
			case Torrent._RatioUseGlobal: return controller.seedRatioLimit();
			case Torrent._RatioUseLocal:  return this.getSeedRatioLimit();
			default:                      return -1;
		}
	},
	getErrorMessage: function() {
		var str = this.getErrorString();
		switch(this.getError()) {
			case Torrent._ErrTrackerWarning:
				return 'Tracker returned a warning: ' + str;
			case Torrent._ErrTrackerError:
				return 'Tracker returned an error: ' + str;
			case Torrent._ErrLocalError:
				return 'Error: ' + str;
			default:
				return null;
		}
	},
	getCollatedName: function() {
		var f = this.fields;
		if (!f.collatedName && f.name)
			f.collatedName = f.name.toLowerCase();
		return f.collatedName || '';
	},
	getCollatedTrackers: function() {
		var f = this.fields;
		if (!f.collatedTrackers && f.trackers)
			f.collatedTrackers = this.collateTrackers(f.trackers);
		return f.collatedTrackers || '';
	},

	/****
	*****
	****/

	testState: function(state)
	{
		var s = this.getStatus();

		switch(state)
		{
			case Prefs._FilterActive:
				return this.getPeersGettingFromUs() > 0
				    || this.getPeersSendingToUs() > 0
				    || this.getWebseedsSendingToUs() > 0
				    || this.isChecking();
			case Prefs._FilterSeeding:
				return (s === Torrent._StatusSeed)
				    || (s === Torrent._StatusSeedWait);
			case Prefs._FilterDownloading:
				return (s === Torrent._StatusDownload)
				    || (s === Torrent._StatusDownloadWait);
			case Prefs._FilterPaused:
				return this.isStopped();
			case Prefs._FilterFinished:
				return this.isFinished();
			default:
				return true;
		}
	},

	/**
	 * @param filter one of Prefs._Filter*
	 * @param search substring to look for, or null
	 * @return true if it passes the test, false if it fails
	 */
	test: function(state, search, tracker)
	{
		// flter by state...
		var pass = this.testState(state);

		// maybe filter by text...
		if (pass && search && search.length)
			pass = this.getCollatedName().indexOf(search.toLowerCase()) !== -1;

		// maybe filter by tracker...
		if (pass && tracker && tracker.length)
			pass = this.getCollatedTrackers().indexOf(tracker) !== -1;

		return pass;
	}
};


/***
****
****  SORTING
****
***/

Torrent.compareById = function(ta, tb)
{
	return ta.getId() - tb.getId();
};
Torrent.compareByName = function(ta, tb)
{
	return ta.getCollatedName().localeCompare(tb.getCollatedName())
	    || Torrent.compareById(ta, tb);
};
Torrent.compareByQueue = function(ta, tb)
{
	return ta.getQueuePosition() - tb.getQueuePosition();
};
Torrent.compareByAge = function(ta, tb)
{
	var a = ta.getDateAdded(),
	    b = tb.getDateAdded();

	return (b - a) || Torrent.compareByQueue(ta, tb);
};
Torrent.compareByState = function(ta, tb)
{
	var a = ta.getStatus(),
	    b = tb.getStatus();

	return (b - a) || Torrent.compareByQueue(ta, tb);
};
Torrent.compareByActivity = function(ta, tb)
{
	var a = ta.getActivity(),
	    b = tb.getActivity();

	return (b - a) || Torrent.compareByState(ta, tb);
};
Torrent.compareByRatio = function(ta, tb)
{
	var a = ta.getUploadRatio(),
	    b = tb.getUploadRatio();

	if (a < b) return 1;
	if (a > b) return -1;
	return Torrent.compareByState(ta, tb);
};
Torrent.compareByProgress = function(ta, tb)
{
	var a = ta.getPercentDone(),
	    b = tb.getPercentDone();

	return (a - b) || Torrent.compareByRatio(ta, tb);
};

Torrent.compareBySize = function(ta, tb)
{
    var a = ta.getTotalSize(),
        b = tb.getTotalSize();

    return (a - b) || Torrent.compareByName(ta, tb);
}

Torrent.compareTorrents = function(a, b, sortMethod, sortDirection)
{
	var i;

	switch(sortMethod)
	{
		case Prefs._SortByActivity:
			i = Torrent.compareByActivity(a,b);
			break;
		case Prefs._SortByAge:
			i = Torrent.compareByAge(a,b);
			break;
		case Prefs._SortByQueue:
			i = Torrent.compareByQueue(a,b);
			break;
		case Prefs._SortByProgress:
			i = Torrent.compareByProgress(a,b);
			break;
        case Prefs._SortBySize:
            i = Torrent.compareBySize(a,b);
            break;
		case Prefs._SortByState:
			i = Torrent.compareByState(a,b);
			break;
		case Prefs._SortByRatio:
			i = Torrent.compareByRatio(a,b);
			break;
		default:
			i = Torrent.compareByName(a,b);
			break;
	}

	if (sortDirection === Prefs._SortDescending)
		i = -i;

	return i;
};

/**
 * @param torrents an array of Torrent objects
 * @param sortMethod one of Prefs._SortBy*
 * @param sortDirection Prefs._SortAscending or Prefs._SortDescending
 */
Torrent.sortTorrents = function(torrents, sortMethod, sortDirection)
{
	switch(sortMethod)
	{
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

	if (sortDirection === Prefs._SortDescending)
		torrents.reverse();

	return torrents;
};
