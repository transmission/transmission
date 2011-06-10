/*
 *	Copyright © Dave Perrett and Malcolm Jarvis
 *	This code is licensed under the GPL version 2.
 *	For details, see http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 *
 *	Class Torrent
 */

function Torrent( transferListParent, fileListParent, controller, data) {
	this.initialize( transferListParent, fileListParent, controller, data);
}

// Constants
Torrent._StatusWaitingToCheck  = 1;
Torrent._StatusChecking        = 2;
Torrent._StatusDownloading     = 4;
Torrent._StatusSeeding         = 8;
Torrent._StatusPaused          = 16;
Torrent._InfiniteTimeRemaining = 215784000; // 999 Hours - may as well be infinite

Torrent._RatioUseGlobal        = 0;
Torrent._RatioUseLocal         = 1;
Torrent._RatioUnlimited        = 2;

Torrent._ErrNone               = 0;
Torrent._ErrTrackerWarning     = 1;
Torrent._ErrTrackerError       = 2;
Torrent._ErrLocalError         = 3;

Torrent._TrackerInactive         = 0;
Torrent._TrackerWaiting          = 1;
Torrent._TrackerQueued           = 2;
Torrent._TrackerActive           = 3;


Torrent._StaticFields = [ 'hashString', 'id' ]

Torrent._MetaDataFields = [ 'addedDate', 'comment', 'creator', 'dateCreated',
		'isPrivate', 'name', 'totalSize', 'pieceCount', 'pieceSize' ]

Torrent._DynamicFields = [ 'downloadedEver', 'error', 'errorString', 'eta',
    'haveUnchecked', 'haveValid', 'leftUntilDone', 'metadataPercentComplete', 'peers',
    'peersConnected', 'peersGettingFromUs', 'peersSendingToUs', 'rateDownload', 'rateUpload',
    'recheckProgress', 'sizeWhenDone', 'status', 'trackerStats', 'desiredAvailable',
    'uploadedEver', 'uploadRatio', 'seedRatioLimit', 'seedRatioMode', 'downloadDir', 'isFinished' ]

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
	initialize: function( transferListParent, fileListParent, controller, data) {
		this._id            = data.id;
		this._hashString    = data.hashString;
		this._sizeWhenDone  = data.sizeWhenDone;
		this._trackerStats  = this.buildTrackerStats(data.trackerStats);
		this._file_model    = [ ];
		this._file_view     = [ ];
		this.initMetaData( data );

		// Create a new <li> element
		var top_e = document.createElement( 'li' );
		top_e.className = 'torrent';
		top_e.id = 'torrent_' + data.id;
		top_e._torrent = this;
		var element = $(top_e);
		$(element).bind('dblclick', function(e) { transmission.toggleInspector(); });
		element._torrent = this;
		element._id = this._id;
		this._element = element;
		this._controller = controller;
		controller._rows.push( element );

		// Create the 'name' <div>
		var e = document.createElement( 'div' );
		e.className = 'torrent_name';
		top_e.appendChild( e );
		element._name_container = e;

		// Create the 'peer details' <div>
		e = document.createElement( 'div' );
		e.className = 'torrent_peer_details';
		top_e.appendChild( e );
		element._peer_details_container = e;

		//Create a progress bar container
		top_a = document.createElement( 'div' );
		top_a.className = 'torrent_progress_bar_container';
		element._progress_bar_container = top_a;

		// Create the 'in progress' bar
		e = document.createElement( 'div' );
		e.className = 'torrent_progress_bar incomplete';
		e.style.width = '0%';
		top_a.appendChild( e );
		element._progress_complete_container = e;

		// Create the 'incomplete' bar (initially hidden)
		e = document.createElement( 'div' );
		e.className = 'torrent_progress_bar incomplete';
		e.style.display = 'none';
		top_a.appendChild( e );
		element._progress_incomplete_container = e;

		//Add the progress bar container to the torrent
		top_e.appendChild(top_a);

		// Add the pause/resume button - don't specify the
		// image or alt text until the 'refresh()' function
		// (depends on torrent state)
		var image = document.createElement( 'div' );
		image.className = 'torrent_pause';
		e = document.createElement( 'a' );
		e.appendChild( image );
		top_e.appendChild( e );
		element._pause_resume_button_image = image;
		if (!iPhone) $(e).bind('click', function(e) { element._torrent.clickPauseResumeButton(e); });

		// Create the 'progress details' <div>
		e = document.createElement( 'div' );
		e.className = 'torrent_progress_details';
		top_e.appendChild( e );
		element._progress_details_container = e;

		// Set the torrent click observer
		element.bind('click', function(e){ element._torrent.clickTorrent(e) });

		// Safari hack - first torrent needs to be moved down for some reason. Seems to be ok when
		// using <li>'s in straight html, but adding through the DOM gets a bit odd.
		if ($.browser.safari)
			this._element.css('margin-top', '7px');

		this.initializeTorrentFilesInspectorGroup( fileListParent );

		// Update all the labels etc
		this.refresh(data);

		// insert the element
		transferListParent.appendChild(top_e);
	},

	initializeTorrentFilesInspectorGroup: function( fileListParent ) {
		var e = document.createElement( 'ul' );
		e.className = 'inspector_torrent_file_list inspector_group';
		e.style.display = 'none';
		fileListParent.appendChild( e );
		this._fileList = e;
	},

	fileList: function() {
		return $(this._fileList);
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

	/* Return the DOM element for this torrent (a <LI> element) */
	element: function() {
		return this._element;
	},

	setElement: function( element ) {
		this._element = element;
		element._torrent = this;
		element[0]._torrent = this;
		this.refreshHTML( );
	},

	activity: function() { return this._download_speed + this._upload_speed; },
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
					 || this.state() == Torrent._StatusChecking; },
	isActive: function() { return this.state() != Torrent._StatusPaused; },
	isDownloading: function() { return this.state() == Torrent._StatusDownloading; },
	isFinished: function() { return this._isFinishedSeeding; },
	isSeeding: function() { return this.state() == Torrent._StatusSeeding; },
	name: function() { return this._name; },
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
			case Torrent._StatusSeeding:        return 'Seeding';
			case Torrent._StatusDownloading:    return 'Downloading';
			case Torrent._StatusPaused:         return this.isFinished() ? 'Seeding complete' : 'Paused';
			case Torrent._StatusChecking:       return 'Verifying local data';
			case Torrent._StatusWaitingToCheck: return 'Waiting to verify';
			default:                            return 'error';
		}
	},
	trackerStats: function() { return this._trackerStats; },
	uploadSpeed: function() { return this._upload_speed; },
	uploadTotal: function() { return this._upload_total; },
	showFileList: function() {
		if(this.fileList().is(':visible'))
			return;
		this.ensureFileListExists();
		this.refreshFileView();
		this.fileList().show();
	},
	hideFileList: function() { this.fileList().hide(); },
	seedRatioLimit: function(){
		switch( this._seed_ratio_mode ) {
			case Torrent._RatioUseGlobal: return this._controller.seedRatioLimit();
			case Torrent._RatioUseLocal:  return this._seed_ratio_limit;
			default:                      return -1;
		}
	},

	/*--------------------------------------------
	 *
	 *  E V E N T   F U N C T I O N S
	 *
	 *--------------------------------------------*/

	/*
	 * Process a click event on this torrent
	 */
	clickTorrent: function( event )
	{
		// Prevents click carrying to parent element
		// which deselects all on click
		event.stopPropagation();
		// but still hide the context menu if it is showing
		$('#jqContextMenu').hide();

		var torrent = this;

		// 'Apple' button emulation on PC :
		// Need settable meta-key and ctrl-key variables for mac emulation
		var meta_key = event.metaKey;
		var ctrl_key = event.ctrlKey;
		if (event.ctrlKey && navigator.appVersion.toLowerCase().indexOf("mac") == -1) {
			meta_key = true;
			ctrl_key = false;
		}

		// Shift-Click - Highlight a range between this torrent and the last-clicked torrent
		if (iPhone) {
			if ( torrent.isSelected() )
				torrent._controller.showInspector();
			torrent._controller.setSelectedTorrent( torrent, true );

		} else if (event.shiftKey) {
			torrent._controller.selectRange( torrent, true );
			// Need to deselect any selected text
			window.focus();

		// Apple-Click, not selected
		} else if (!torrent.isSelected() && meta_key) {
			torrent._controller.selectTorrent( torrent, true );

		// Regular Click, not selected
		} else if (!torrent.isSelected()) {
			torrent._controller.setSelectedTorrent( torrent, true );

		// Apple-Click, selected
		} else if (torrent.isSelected() && meta_key) {
			torrent._controller.deselectTorrent( torrent, true );

		// Regular Click, selected
		} else if (torrent.isSelected()) {
			torrent._controller.setSelectedTorrent( torrent, true );
		}

		torrent._controller.setLastTorrentClicked(torrent);
	},

	/*
	 * Process a click event on the pause/resume button
	 */
	clickPauseResumeButton: function( event )
	{
		// prevent click event resulting in selection of torrent
		event.stopPropagation();

		// either stop or start the torrent
		var torrent = this;
		if( torrent.isActive( ) )
			torrent._controller.stopTorrent( torrent );
		else
			torrent._controller.startTorrent( torrent );
	},

	/*--------------------------------------------
	 *
	 *  I N T E R F A C E   F U N C T I O N S
	 *
	 *--------------------------------------------*/

	refreshMetaData: function(data) {
		this.initMetaData( data );
		this.ensureFileListExists();
		this.refreshFileView();
		this.refreshHTML( );
	},

	refresh: function(data) {
		this.refreshData( data );
		this.refreshHTML( );
	},

	/*
	 * Refresh display
	 */
	refreshData: function(data)
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

	getErrorMessage: function()
	{
		if( this._error  == Torrent._ErrTrackerWarning )
			return 'Tracker returned a warning: ' + this._error_string;
		if( this._error  == Torrent._ErrTrackerError )
			return 'Tracker returned an error: ' + this._error_string;
		if( this._error  == Torrent._ErrLocalError )
			return 'Error: ' + this._error_string;
		return null;
	},

	formatUL: function() {
		return 'UL: ' + Transmission.fmt.speedBps(this._upload_speed);
	},
	formatDL: function() {
		return 'DL: ' + Transmission.fmt.speedBps(this._download_speed);
	},

	getPeerDetails: function()
	{
		var c;

		var compact_mode = this._controller[Prefs._CompactDisplayState];
		if(( c = this.getErrorMessage( )))
			return c;

		var st = this.state( );
		switch( st )
		{
			case Torrent._StatusPaused:
			case Torrent._StatusWaitingToCheck:
				c = this.stateStr( );
				break;

			case Torrent._StatusDownloading:
				var a = [ ];
				if(!compact_mode)
					a.push( 'Downloading from', this.peersSendingToUs(), 'of', this._peers_connected, 'peers', '-' );
				a.push( this.formatDL(), this.formatUL() );
				c = a.join(' ');
				break;

			case Torrent._StatusSeeding:
				if(compact_mode){
					c = this.formatUL();
				} else {
					// 'Seeding to 13 of 22 peers - UL: 36.2 KiB/s'
					c = [ 'Seeding to', this.peersGettingFromUs(), 'of', this._peers_connected, 'peers', '-', this.formatUL() ].join(' ');
				}
				break;

			case Torrent._StatusChecking:
				// 'Verifying local data (40% tested)'
				c = [ 'Verifying local data (', Transmission.fmt.percentString( 100.0 * this._recheckProgress ), '% tested)' ].join('');
				break;
		}
		return c;
	},

	refreshHTML: function() {
		var c;
		var e;
		var progress_details;
		var root = this._element;
		var MaxBarWidth = 100; // reduce this to make the progress bar shorter (%)
		var compact_mode = this._controller[Prefs._CompactDisplayState];
		var compact = '';
		if(compact_mode){
			compact = ' compact';
			root._peer_details_container.style.display = 'none';
		} else {
			root._peer_details_container.style.display = 'block';
		}

		root._progress_details_container.className = 'torrent_progress_details'+compact
		root._progress_bar_container.className = 'torrent_progress_bar_container'+compact;
		root._name_container.className = 'torrent_name'+compact;

		setInnerHTML( root._name_container, this._name );

		// Add the progress bar
		var notDone = this._leftUntilDone > 0;

		// Fix for situation
		// when a verifying/downloading torrent gets state seeding
		if( this._state === Torrent._StatusSeeding )
			notDone = false ;

		if( this.needsMetaData() ){
			var metaPercentComplete = this._metadataPercentComplete * 100;
			progress_details = [ "Magnetized transfer - retrieving metadata (",
			                     Transmission.fmt.percentString( metaPercentComplete ),
			                     "%)" ].join('');

			var empty = "";
			if(metaPercentComplete == 0)
				empty = "empty";

			root._progress_complete_container.style.width = metaPercentComplete + "%";
			root._progress_complete_container.className = 'torrent_progress_bar in_progress meta ' + empty+compact;
			root._progress_incomplete_container.style.width = 100 - metaPercentComplete + "%"
			root._progress_incomplete_container.className = 'torrent_progress_bar incomplete compact meta'+compact;
			root._progress_incomplete_container.style.display = 'block';
		}
		else if( notDone )
		{
			// Create the 'progress details' label
			// Eg: '101 MiB of 631 MiB (16.02%) - 2 hr remaining'

			c = [ Transmission.fmt.size( this._sizeWhenDone - this._leftUntilDone ),
			      ' of ', Transmission.fmt.size( this._sizeWhenDone ),
			      ' (', this.getPercentDoneStr(), '%)' ];
			if( this.isActive( ) ) {
				c.push( ' - ' );
				if (this._eta < 0 || this._eta >= Torrent._InfiniteTimeRemaining )
					c.push( 'remaining time unknown' );
				else
					c.push( Transmission.fmt.timeInterval(this._eta) + ' remaining' );
			}
			progress_details = c.join('');

			// Figure out the percent completed
			var css_completed_width = ( this.getPercentDone() * MaxBarWidth ).toTruncFixed( 2 );

			// Update the 'in progress' bar
			e = root._progress_complete_container;
			c = [ 'torrent_progress_bar'+compact,
			      this.isActive() ? 'in_progress' : 'incomplete_stopped' ];
			if(css_completed_width === 0) { c.push( 'empty' ); }
			e.className = c.join(' ');
			e.style.width = css_completed_width + '%';

			// Update the 'incomplete' bar
			e = root._progress_incomplete_container;
			e.className = 'torrent_progress_bar incomplete'
			e.style.width =  (MaxBarWidth - css_completed_width) + '%';
			e.style.display = 'block';
		}
		else
		{
			// Create the 'progress details' label

			if( this._size == this._sizeWhenDone )
			{
				// seed: '698.05 MiB'
				c = [ Transmission.fmt.size( this._size ) ];
			}
			else
			{
				// partial seed: '127.21 MiB of 698.05 MiB (18.2%)'
				c = [ Transmission.fmt.size( this._sizeWhenDone ), ' of ', Transmission.fmt.size( this._size ),
				      ' (', Transmission.fmt.percentString( 100.0 * this._sizeWhenDone / this._size ), '%)' ];
			}

			// append UL stats: ', uploaded 8.59 GiB (Ratio: 12.3)'
			c.push( ', uploaded ', Transmission.fmt.size( this._upload_total ),
			        ' (Ratio ', Transmission.fmt.ratioString( this._upload_ratio ), ')' );

			// maybe append remaining time
			if( this.isActive( ) && this.seedRatioLimit( ) > 0 )
			{
				c.push(' - ');

				if (this._eta < 0 || this._eta >= Torrent._InfiniteTimeRemaining )
					c.push( 'remaining time unknown' );
				else
					c.push( Transmission.fmt.timeInterval(this._eta), ' remaining' );
			}

			progress_details = c.join('');

			var status = this.isActive() ? 'complete' : 'complete_stopped';

			if(this.isActive() && this.seedRatioLimit() > 0){
				status = 'complete seeding'
				var seedRatioRatio = this._upload_ratio / this.seedRatioLimit();
				var seedRatioPercent = Math.round( seedRatioRatio * 100 * MaxBarWidth ) / 100;

				// Set progress to percent seeded
				root._progress_complete_container.style.width =	seedRatioPercent + '%';

				// Update the 'incomplete' bar
				root._progress_incomplete_container.className = 'torrent_progress_bar incomplete seeding'
				root._progress_incomplete_container.style.display = 'block';
				root._progress_incomplete_container.style.width = MaxBarWidth - seedRatioPercent + '%';
			}
			else
			{
				// Hide the 'incomplete' bar
				root._progress_incomplete_container.className = 'torrent_progress_bar incomplete'
				root._progress_incomplete_container.style.display = 'none';

				// Set progress to maximum
				root._progress_complete_container.style.width =	MaxBarWidth + '%';
			}

			// Update the 'in progress' bar
			e = root._progress_complete_container;
			e.className = 'torrent_progress_bar ' + status;
		}

		var hasError = this.getErrorMessage( ) != undefined;
		// Update the progress details
		if(compact_mode){
			progress_details = this.getPeerDetails();
			$(root._progress_details_container).toggleClass('error',hasError);
		} else {
			$(root._peer_details_container).toggleClass('error',hasError);
		}
		setInnerHTML( root._progress_details_container, progress_details );

		if( compact ){
			var width = root._progress_details_container.offsetLeft - root._name_container.offsetLeft;
			root._name_container.style.width = width + 'px';
		}
		else {
			root._name_container.style.width = '100%';
		}

		// Update the peer details and pause/resume button
		e = root._pause_resume_button_image;
		if ( this.state() === Torrent._StatusPaused ) {
			e.alt = 'Resume';
			e.className = "torrent_resume"+compact;
		} else {
			e.alt = 'Pause';
			e.className = "torrent_pause"+compact;
		}

		setInnerHTML( root._peer_details_container, this.getPeerDetails( ) );

		this.refreshFileView( );
	},

	refreshFileView: function() {
		if( this._file_view.length )
			for( var i=0; i<this._file_model.length; ++i )
				this._file_view[i].update( this._file_model[i] );
	},

	ensureFileListExists: function() {
		if( this._file_view.length == 0 ) {
			if(this._file_model.length == 1)
				this._fileList.className += ' single_file';
			var v, e;
			for( var i=0; i<this._file_model.length; ++i ) {
				v = new TorrentFile( this._file_model[i] );
				this._file_view[i] = v;
				e = v.domElement( );
				e.className = (i % 2 ? 'even' : 'odd') + ' inspector_torrent_file_list_entry';
				this._fileList.appendChild( e );
			}
		}
	},

	deleteFiles: function(){
		if (this._fileList)
			$(this._fileList).remove();
	},

	/*
	 * Return true if this torrent is selected
	 */
	isSelected: function() {
		return this.element()[0].className.indexOf('selected') != -1;
	},

	/**
	 * @param filter one of Prefs._Filter*
	 * @param search substring to look for, or null
	 * @return true if it passes the test, false if it fails
	 */
	test: function( filter, search )
	{
		var pass = false;

		switch( filter )
		{
			case Prefs._FilterActive:
				pass = this.isActiveFilter();
				break;
			case Prefs._FilterSeeding:
				pass = this.isSeeding();
				break;
			case Prefs._FilterDownloading:
				pass = this.isDownloading();
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
Torrent.compareByAge = function( a, b ) {
	return a.dateAdded() - b.dateAdded();
};

/** Helper function for sortTorrents(). */
Torrent.compareByName = function( a, b ) {
	return a._name_lc.compareTo( b._name_lc );
};

/** Helper function for sortTorrents(). */
Torrent.compareByState = function( a, b ) {
	return a.state() - b.state();
};

/** Helper function for sortTorrents(). */
Torrent.compareByActivity = function( a, b ) {
	return a.activity() - b.activity();
};

/** Helper function for sortTorrents(). */
Torrent.compareByRatio = function( a, b ) {
	var a_ratio = Math.ratio( a._upload_total, a._download_total );
	var b_ratio = Math.ratio( b._upload_total, b._download_total );
	return a_ratio - b_ratio;
};

/** Helper function for sortTorrents(). */
Torrent.compareByProgress = function( a, b ) {
	if( a.getPercentDone() !== b.getPercentDone() )
		return a.getPercentDone() - b.getPercentDone();
	return this.compareByRatio( a, b );
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
			torrents.sort( this.compareById );
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

/**
 * @brief fast binary search to find a torrent
 * @param torrents an array of torrents sorted by Id
 * @param id the id to search for
 * @return the index, or -1
 */
Torrent.indexOf = function( torrents, id )
{
	var low = 0;
	var high = torrents.length;
	while( low < high ) {
		var mid = Math.floor( ( low + high ) / 2 );
		if( torrents[mid].id() < id )
			low = mid + 1;
		else
			high = mid;
	}
	if( ( low < torrents.length ) && ( torrents[low].id() == id ) ) {
		return low;
	} else {
		return -1; // not found
	}
};

function TorrentFile(file_data) {
	this.initialize(file_data);
}

TorrentFile.prototype = {
	initialize: function(file_data) {
		this._dirty = true;
		this._torrent = file_data.torrent;
		this._index = file_data.index;
		var name = file_data.name.substring (file_data.name.lastIndexOf('/')+1);
		this.readAttributes(file_data);

		var li = document.createElement('li');
		li.id = 't' + this._torrent.id() + 'f' + this._index;
		li.classNameConst = 'inspector_torrent_file_list_entry ' + ((this._index%2)?'odd':'even');
		li.className = li.classNameConst;

		var wanted_div = document.createElement('div');
		wanted_div.className = "file_wanted_control";

		var pri_div = document.createElement('div');
		pri_div.classNameConst = "file_priority_control";
		pri_div.className = pri_div.classNameConst;

		var file_div = document.createElement('div');
		file_div.className = "inspector_torrent_file_list_entry_name";
		file_div.innerHTML = name.replace(/([\/_\.])/g, "$1&#8203;");

		var prog_div = document.createElement('div');
		prog_div.className = "inspector_torrent_file_list_entry_progress";

		li.appendChild(wanted_div);
		li.appendChild(pri_div);
		li.appendChild(file_div);
		li.appendChild(prog_div);

		this._element = li;
		this._priority_control = pri_div;
		this._progress = $(prog_div);
	},

	update: function(file_data) {
		this.readAttributes(file_data);
		this.refreshHTML();
	},

	isDone: function () {
		return this._done >= this._size;
	},

	isEditable: function () {
		return (this._torrent._file_model.length>1) && !this.isDone();
	},

	readAttributes: function(file_data) {
		if( file_data.index !== undefined && file_data.index !== this._index ) {
			this._index = file_data.index;
			this._dirty = true;
		}
		if( file_data.bytesCompleted !== undefined && file_data.bytesCompleted !== this._done ) {
			this._done   = file_data.bytesCompleted;
			this._dirty = true;
		}
		if( file_data.length !== undefined && file_data.length !== this._size ) {
			this._size   = file_data.length;
			this._dirty = true;
		}
		if( file_data.priority !== undefined && file_data.priority !== this._prio ) {
			this._prio   = file_data.priority;
			this._dirty = true;
		}
		if( file_data.wanted !== undefined && file_data.wanted !== this._wanted ) {
			this._wanted = file_data.wanted;
			this._dirty = true;
		}
	},

	element: function() {
		return $(this._element);
	},

	domElement: function() {
		return this._element;
	},

	setPriority: function(prio) {
		if (this.isEditable()) {
			var cmd;
			switch( prio ) {
				case  1: cmd = 'priority-high';   break;
				case -1: cmd = 'priority-low';    break;
				default: cmd = 'priority-normal'; break;
			}
			this._prio = prio;
			this._dirty = true;
			this._torrent._controller.changeFileCommand( cmd, this._torrent, this );
		}
	},

	setWanted: function(wanted, process) {
		this._dirty = true;
		this._wanted = wanted;
		if(!iPhone)
			this.element().toggleClass( 'skip', !wanted );
		if (process) {
			var command = wanted ? 'files-wanted' : 'files-unwanted';
			this._torrent._controller.changeFileCommand(command, this._torrent, this);
		}
	},

	toggleWanted: function() {
		if (this.isEditable())
			this.setWanted( !this._wanted, true );
	},

	refreshHTML: function() {
		if( this._dirty ) {
			this._dirty = false;
			this.refreshProgressHTML();
			this.refreshWantedHTML();
			this.refreshPriorityHTML();
		}
	},

	refreshProgressHTML: function() {
		var c = [ Transmission.fmt.size(this._done),
		          ' of ',
		          Transmission.fmt.size(this._size),
		          ' (',
		          this._size ? Transmission.fmt.percentString(100 * this._done / this._size) : '100',
		          '%)' ].join('');
		setInnerHTML(this._progress[0], c);
	},

	refreshWantedHTML: function() {
		var e = this.domElement();
		var c = [ e.classNameConst ];
		if(!this._wanted) { c.push( 'skip' ); }
		if(this.isDone()) { c.push( 'complete' ); }
		e.className = c.join(' ');
	},

	refreshPriorityHTML: function() {
		var e = this._priority_control;
		var c = [ e.classNameConst ];
		switch( this._prio ) {
			case 1  : c.push( 'high'   ); break;
			case -1 : c.push( 'low'    ); break;
			default : c.push( 'normal' ); break;
		}
		e.className = c.join(' ');
	},

	fileWantedControlClicked: function(event) {
		this.toggleWanted();
	},

	filePriorityControlClicked: function(event, element) {
		var x = event.pageX;
		while (element !== null) {
			x = x - element.offsetLeft;
			element = element.offsetParent;
		}

		var prio;
		if(iPhone)
		{
			if( x < 8 ) prio = -1;
			else if( x < 27 ) prio = 0;
			else prio = 1;
		}
		else
		{
			if( x < 12 ) prio = -1;
			else if( x < 23 ) prio = 0;
			else prio = 1;
		}
		this.setPriority( prio );
	}
};
