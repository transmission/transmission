/*
 *	Copyright © Dave Perrett and Malcolm Jarvis
 *	This code is licensed under the GPL version 2.
 *	For details, see http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 *
 *	Class Torrent
 */

function Torrent( controller, data) {
	this.initialize( controller, data);
}

// Constants
Torrent._StatusStopped         = 0; /* torrent is stopped */
Torrent._StatusCheckWait       = 1; /* waiting in queue to check files */
Torrent._StatusCheck           = 2; /* checking files */
Torrent._StatusDownloadWait    = 3; /* queued to download */
Torrent._StatusDownload        = 4; /* downloading */
Torrent._StatusSeedWait        = 5; /* queeud to seed */
Torrent._StatusSeed            = 6; /* seeding */

Torrent._InfiniteTimeRemaining = 215784000; // 999 Hours - may as well be infinite

Torrent._RatioUseGlobal        = 0;
Torrent._RatioUseLocal         = 1;
Torrent._RatioUnlimited        = 2;

Torrent._ErrNone               = 0;
Torrent._ErrTrackerWarning     = 1;
Torrent._ErrTrackerError       = 2;
Torrent._ErrLocalError         = 3;

Torrent._TrackerInactive       = 0;
Torrent._TrackerWaiting        = 1;
Torrent._TrackerQueued         = 2;
Torrent._TrackerActive         = 3;


Torrent._StaticFields = [ 'hashString', 'id' ]

Torrent._MetaDataFields = [ 'addedDate', 'comment', 'creator', 'dateCreated',
		'isPrivate', 'name', 'totalSize', 'pieceCount', 'pieceSize' ]

Torrent._DynamicFields = [ 'downloadedEver', 'error', 'errorString', 'eta',
    'haveUnchecked', 'haveValid', 'leftUntilDone', 'metadataPercentComplete',
    'peers', 'peersConnected', 'peersGettingFromUs', 'peersSendingToUs',
    'queuePosition', 'rateDownload', 'rateUpload', 'recheckProgress',
    'sizeWhenDone', 'status', 'trackerStats', 'desiredAvailable',
    'uploadedEver', 'uploadRatio', 'seedRatioLimit', 'seedRatioMode',
    'downloadDir', 'isFinished' ]

Torrent.prototype =
{
	initMetaData: function( data ) {
		this._date          = data.addedDate;
		this._comment       = data.comment;
		this._creator       = data.creator;
		this._creator_date  = data.dateCreated;
		this._is_private    = data.isPrivate;
		this._name          = data.name;
		this._name_lc       = this._name.toLowerCase( );
		this._size          = data.totalSize;
		this._pieceCount    = data.pieceCount;
		this._pieceSize     = data.pieceSize;

		if( data.files ) {
			for( var i=0, row; row=data.files[i]; ++i ) {
				this._file_model[i] = {
					'index': i,
					'torrent': this,
					'length': row.length,
					'name': row.name
				};
			}
		}
	},

	/*
	 * Constructor
	 */
	initialize: function( controller, data) {
		this._id            = data.id;
		this._hashString    = data.hashString;
		this._sizeWhenDone  = data.sizeWhenDone;
		this._trackerStats  = this.buildTrackerStats(data.trackerStats);
		this._file_model    = [ ];
		this.initMetaData( data );

		// Update all the labels etc
		this.refresh(data);
	},

	buildTrackerStats: function(trackerStats) {
		result = [];
		for( var i=0, tracker; tracker=trackerStats[i]; ++i ) {
			tier = result[tracker.tier] || [];
			tier[tier.length] = {
				'host': tracker.host,
				'announce': tracker.announce,
				'hasAnnounced': tracker.hasAnnounced,
				'lastAnnounceTime': tracker.lastAnnounceTime,
				'lastAnnounceSucceeded': tracker.lastAnnounceSucceeded,
				'lastAnnounceResult': tracker.lastAnnounceResult,
				'lastAnnouncePeerCount': tracker.lastAnnouncePeerCount,
				'announceState': tracker.announceState,
				'nextAnnounceTime': tracker.nextAnnounceTime,
				'isBackup': tracker.isBackup,
				'hasScraped': tracker.hasScraped,
				'lastScrapeTime': tracker.lastScrapeTime,
				'lastScrapeSucceeded': tracker.lastScrapeSucceeded,
				'lastScrapeResult': tracker.lastScrapeResult,
				'seederCount': tracker.seederCount,
				'leecherCount': tracker.leecherCount,
				'downloadCount': tracker.downloadCount
			};
			result[tracker.tier] = tier;
		}
		return result;
	},

	/*--------------------------------------------
	 *
	 *  S E T T E R S   /   G E T T E R S
	 *
	 *--------------------------------------------*/

	activity: function() { return this.downloadSpeed() + this.uploadSpeed(); },
	comment: function() { return this._comment; },
	completed: function() { return this._completed; },
	creator: function() { return this._creator; },
	dateAdded: function() { return this._date; },
	downloadSpeed: function() { return this._download_speed; },
	downloadTotal: function() { return this._download_total; },
	hash: function() { return this._hashString; },
	id: function() { return this._id; },
	isActiveFilter: function() { return this.peersGettingFromUs() > 0
					 || this.peersSendingToUs() > 0
					 || this.webseedsSendingToUs() > 0
					 || this.state() == Torrent._StatusCheck; },
	isStopped: function() { return this.state() === Torrent._StatusStopped; },
	isActive: function() { return this.state() != Torrent._StatusStopped; },
	isDownloading: function() { return this.state() == Torrent._StatusDownload; },
	isFinished: function() { return this._isFinishedSeeding; },
	isDone: function() { return this._leftUntilDone < 1; },
	isSeeding: function() { return this.state() == Torrent._StatusSeed; },
	name: function() { return this._name; },
        queuePosition: function() { return this._queue_position; },
	webseedsSendingToUs: function() { return this._webseeds_sending_to_us; },
	peersSendingToUs: function() { return this._peers_sending_to_us; },
	peersGettingFromUs: function() { return this._peers_getting_from_us; },
	needsMetaData: function(){ return this._metadataPercentComplete < 1 },
	getPercentDone: function() {
		if( !this._sizeWhenDone ) return 1.0;
		if( !this._leftUntilDone ) return 1.0;
		return ( this._sizeWhenDone - this._leftUntilDone ) / this._sizeWhenDone;
	},
	getPercentDoneStr: function() {
		return Transmission.fmt.percentString( 100 * this.getPercentDone() );
	},
	size: function() { return this._size; },
	state: function() { return this._state; },
	stateStr: function() {
		switch( this.state() ) {
			case Torrent._StatusStopped:        return this.isFinished() ? 'Seeding complete' : 'Paused';
			case Torrent._StatusCheckWait:      return 'Queued for verification';
			case Torrent._StatusCheck:          return 'Verifying local data';
			case Torrent._StatusDownloadWait:   return 'Queued for download';
			case Torrent._StatusDownload:       return 'Downloading';
			case Torrent._StatusSeedWait:       return 'Queued for seeding';
			case Torrent._StatusSeed:           return 'Seeding';
			default:                            return 'error';
		}
	},
	trackerStats: function() { return this._trackerStats; },
	uploadSpeed: function() { return this._upload_speed; },
	uploadTotal: function() { return this._upload_total; },
	seedRatioLimit: function(controller){
		switch( this._seed_ratio_mode ) {
			case Torrent._RatioUseGlobal: return controller.seedRatioLimit();
			case Torrent._RatioUseLocal:  return this._seed_ratio_limit;
			default:                      return -1;
		}
	},
        getErrorMessage: function() {
                if( this._error  == Torrent._ErrTrackerWarning )
                        return 'Tracker returned a warning: ' + this._error_string;
                if( this._error  == Torrent._ErrTrackerError )
                        return 'Tracker returned an error: ' + this._error_string;
                if( this._error  == Torrent._ErrLocalError )
                        return 'Error: ' + this._error_string;
                return null;
        },


	/*--------------------------------------------
	 *
	 *  I N T E R F A C E   F U N C T I O N S
	 *
	 *--------------------------------------------*/

	fireDataChanged: function()
	{
		$(this).trigger('dataChanged',[]);
	},

	refreshMetaData: function(data)
	{
		this.initMetaData( data );
		this.fireDataChanged();
	},

	refresh: function(data)
	{
		if( this.needsMetaData() && ( data.metadataPercentComplete >= 1 ) )
			transmission.refreshMetaData( [ this._id ] );

		this._completed               = data.haveUnchecked + data.haveValid;
		this._verified                = data.haveValid;
		this._leftUntilDone           = data.leftUntilDone;
		this._download_total          = data.downloadedEver;
		this._upload_total            = data.uploadedEver;
		this._upload_ratio            = data.uploadRatio;
		this._seed_ratio_limit        = data.seedRatioLimit;
		this._seed_ratio_mode         = data.seedRatioMode;
		this._download_speed          = data.rateDownload;
		this._upload_speed            = data.rateUpload;
		this._peers                   = data.peers;
		this._peers_connected         = data.peersConnected;
		this._peers_getting_from_us   = data.peersGettingFromUs;
		this._peers_sending_to_us     = data.peersSendingToUs;
		this._queue_position          = data.queuePosition;
		this._webseeds_sending_to_us  = data.webseedsSendingToUs;
		this._sizeWhenDone            = data.sizeWhenDone;
		this._recheckProgress         = data.recheckProgress;
		this._error                   = data.error;
		this._error_string            = data.errorString;
		this._eta                     = data.eta;
		this._trackerStats            = this.buildTrackerStats(data.trackerStats);
		this._state                   = data.status;
		this._download_dir            = data.downloadDir;
		this._metadataPercentComplete = data.metadataPercentComplete;
		this._isFinishedSeeding       = data.isFinished;
		this._desiredAvailable        = data.desiredAvailable;

		if (data.fileStats)
			this.refreshFileModel( data );

		this.fireDataChanged();
	},

	refreshFileModel: function(data) {
		for( var i=0; i<data.fileStats.length; ++i ) {
			var src = data.fileStats[i];
			var tgt = this._file_model[i];
			if( !tgt )
				tgt = this._file_model[i] = { };
			tgt.wanted = src.wanted;
			tgt.priority = src.priority;
			tgt.bytesCompleted = src.bytesCompleted;
		}
	},

	/**
	 * @param filter one of Prefs._Filter*
	 * @param search substring to look for, or null
	 * @return true if it passes the test, false if it fails
	 */
	test: function( filter, search )
	{
		var pass = false;
                var s = this.state( );

		switch( filter )
		{
			case Prefs._FilterActive:
				pass = this.isActiveFilter();
				break;
			case Prefs._FilterSeeding:
				pass = ( s == Torrent._StatusSeed ) || ( s == Torrent._StatusSeedWait );
				break;
			case Prefs._FilterDownloading:
				pass = ( s == Torrent._StatusDownload ) || ( s == Torrent._StatusDownloadWait );
				break;
			case Prefs._FilterPaused:
				pass = !this.isActive();
				break;
			case Prefs._FilterFinished:
				pass = this.isFinished();
				break;
			default:
				pass = true;
				break;
		}

		if( !pass )
			return false;

		if( !search || !search.length )
			return pass;

		return this._name_lc.indexOf( search.toLowerCase() ) !== -1;
	}
};

/** Helper function for Torrent.sortTorrents(). */
Torrent.compareById = function( a, b ) {
	return a.id() - b.id();
};

/** Helper function for sortTorrents(). */
Torrent.compareByName = function( a, b ) {
	var i = a._name_lc.compareTo( b._name_lc );
	if( i )
		return i;
	return Torrent.compareById( a, b );
};

/** Helper function for sortTorrents(). */
Torrent.compareByQueue = function( a, b )
{
	return a.queuePosition( ) - b.queuePosition();
};

/** Helper function for sortTorrents(). */
Torrent.compareByAge = function( a, b )
{
	var a_age = a.dateAdded();
	var b_age = b.dateAdded();
	if( a_age != b_age )
		return a_age - b_age;

	return Torrent.compareByQueue( a, b );
};

/** Helper function for sortTorrents(). */
Torrent.compareByState = function( a, b )
{
	var a_state = a.state( );
	var b_state = b.state( );
	if( a_state != b_state )
		return b_state - a_state;

	return Torrent.compareByQueue( a, b );
};

/** Helper function for sortTorrents(). */
Torrent.compareByActivity = function( a, b )
{
	var a_activity = a.activity( );
	var b_activity = b.activity( );
	if( a_activity != b_activity )
		return a_activity - b_activity;

	return Torrent.compareByState( a, b );
};

/** Helper function for sortTorrents(). */
Torrent.compareByRatio = function( a, b ) {
	var a_ratio = Math.ratio( a._upload_total, a._download_total );
	var b_ratio = Math.ratio( b._upload_total, b._download_total );
	if( a_ratio != b_ratio )
		return a_ratio - b_ratio;
	return Torrent.compareByState( a, b );
};

Torrent.compareByProgress = function( a, b ) {
	if( a.getPercentDone() !== b.getPercentDone() )
		return a.getPercentDone() - b.getPercentDone();
	return Torrent.compareByRatio( a, b );
};

/**
 * @param torrents an array of Torrent objects
 * @param sortMethod one of Prefs._SortBy*
 * @param sortDirection Prefs._SortAscending or Prefs._SortDescending
 */
Torrent.sortTorrents = function( torrents, sortMethod, sortDirection )
{
	switch( sortMethod )
	{
		case Prefs._SortByActivity:
			torrents.sort( this.compareByActivity );
			break;
		case Prefs._SortByAge:
			torrents.sort( this.compareByAge );
			break;
		case Prefs._SortByQueue:
			torrents.sort( this.compareByQueue );
			break;
		case Prefs._SortByProgress:
			torrents.sort( this.compareByProgress );
			break;
		case Prefs._SortByState:
			torrents.sort( this.compareByState );
			break;
		case Prefs._SortByName:
			torrents.sort( this.compareByName );
			break;
		case Prefs._SortByRatio:
			torrents.sort( this.compareByRatio );
			break;
		default:
			console.warn( "unknown sort method: " + sortMethod );
			break;
	}

	if( sortDirection === Prefs._SortDescending )
		torrents.reverse( );

	return torrents;
};
