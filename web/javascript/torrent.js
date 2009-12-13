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

Torrent._StaticFields = [ 'addedDate', 'comment', 'creator', 'dateCreated',
		'hashString', 'id', 'isPrivate', 'name', 'totalSize', 'pieceCount', 'pieceSize' ]
Torrent._DynamicFields = [ 'downloadedEver', 'error', 'errorString', 'eta',
    'haveUnchecked', 'haveValid', 'leftUntilDone', 'metadataPercentComplete', 'peersConnected',
    'peersGettingFromUs', 'peersSendingToUs', 'rateDownload', 'rateUpload',
    'recheckProgress', 'sizeWhenDone', 'status',
    'uploadedEver', 'uploadRatio', 'seedRatioLimit', 'seedRatioMode', 'downloadDir' ]

Torrent.prototype =
{
	/*
	 * Constructor
	 */
	initialize: function( transferListParent, fileListParent, controller, data) {
		this._id            = data.id;
		this._is_private    = data.isPrivate;
		this._hashString    = data.hashString;
		this._date          = data.addedDate;
		this._size          = data.totalSize;
		this._pieceCount    = data.pieceCount;
		this._pieceSize     = data.pieceSize;
		this._comment       = data.comment;
		this._creator       = data.creator;
		this._creator_date  = data.dateCreated;
		this._sizeWhenDone  = data.sizeWhenDone;
		this._name          = data.name;
		this._name_lc       = this._name.toLowerCase( );
		this._file_model    = [ ];
		this._file_view     = [ ];

		// Create a new <li> element
		var top_e = document.createElement( 'li' );
		top_e.className = 'torrent';
		top_e.id = 'torrent_' + data.id;
		top_e._torrent = this;
		var element = $(top_e);
                $(element).bind('dblclick', function(e) { transmission.toggleInspector(); });
		element._torrent = this;
		this._element = element;
		this._controller = controller;
		controller._rows.push( element );
		
		// Create the 'name' <div>
		var e = document.createElement( 'div' );
		e.className = 'torrent_name';
		top_e.appendChild( e );
		element._name_container = e;
		
		// Create the 'progress details' <div>
		e = document.createElement( 'div' );
		e.className = 'torrent_progress_details';
		top_e.appendChild( e );
		element._progress_details_container = e;

		// Create the 'in progress' bar
		e = document.createElement( 'div' );
		e.className = 'torrent_progress_bar incomplete';
		e.style.width = '0%';
		top_e.appendChild( e );
		element._progress_complete_container = e;
			
		// Create the 'incomplete' bar (initially hidden)
		e = document.createElement( 'div' );
		e.className = 'torrent_progress_bar incomplete';
		e.style.display = 'none';
		top_e.appendChild( e );
		element._progress_incomplete_container = e;
		
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
		
		// Create the 'peer details' <div>
		e = document.createElement( 'div' );
		e.className = 'torrent_peer_details';
		top_e.appendChild( e );
		element._peer_details_container = e;
		
		// Set the torrent click observer
		element.bind('click', function(e){ element._torrent.clickTorrent(e) });
		
		// Safari hack - first torrent needs to be moved down for some reason. Seems to be ok when
		// using <li>'s in straight html, but adding through the DOM gets a bit odd.
		if ($.browser.safari)
			this._element.css('margin-top', '7px');

		this.initializeTorrentFilesInspectorGroup( fileListParent );

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
	isActive: function() { return this.state() != Torrent._StatusPaused; },
	isDownloading: function() { return this.state() == Torrent._StatusDownloading; },
	isSeeding: function() { return this.state() == Torrent._StatusSeeding; },
	name: function() { return this._name; },
	peersSendingToUs: function() { return this._peers_sending_to_us; },
	peersGettingFromUs: function() { return this._peers_getting_from_us; },
	needsMetaData: function(){ return this._metadataPercentComplete < 1 },
	getPercentDone: function() {
		if( !this._sizeWhenDone ) return 1.0;
		if( !this._leftUntilDone ) return 1.0;
		return ( this._sizeWhenDone - this._leftUntilDone ) / this._sizeWhenDone;
	},
	getPercentDoneStr: function() {
		return Math.floor(100 * Math.ratio( 100 * ( this._sizeWhenDone - this._leftUntilDone ),
		                           this._sizeWhenDone )) / 100;
	},
	size: function() { return this._size; },
	state: function() { return this._state; },
	stateStr: function() {
		switch( this.state() ) {
			case Torrent._StatusSeeding:        return 'Seeding';
			case Torrent._StatusDownloading:    return 'Downloading';
			case Torrent._StatusPaused:         return 'Paused';
			case Torrent._StatusChecking:       return 'Verifying local data';
			case Torrent._StatusWaitingToCheck: return 'Waiting to verify';
			default:                            return 'error';
		}
	},
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
	
	refresh: function(data) {
		this.refreshData( data );
		this.refreshHTML( );
	},

	/*
	 * Refresh display
	 */
	refreshData: function(data) {
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
		this._peers_connected         = data.peersConnected;
		this._peers_getting_from_us   = data.peersGettingFromUs;
		this._peers_sending_to_us     = data.peersSendingToUs;
		this._sizeWhenDone            = data.sizeWhenDone;
		this._recheckProgress         = data.recheckProgress;
		this._error                   = data.error;
		this._error_string            = data.errorString;
		this._eta                     = data.eta;
		this._state                   = data.status;
		this._download_dir            = data.downloadDir;
		this._metadataPercentComplete = data.metadataPercentComplete;

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

	getPeerDetails: function()
	{
		var c;

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
				// 'Downloading from 36 of 40 peers - DL: 60.2 KB/s UL: 4.3 KB/s'
				c = 'Downloading from ';
				c += this.peersSendingToUs();
				c += ' of ';
				c += this._peers_connected;
				c += ' peers - DL: ';
				c += Math.formatBytes(this._download_speed);
				c += '/s UL: ';
				c += Math.formatBytes(this._upload_speed);
				c += '/s';
				break;

			case Torrent._StatusSeeding:
				// 'Seeding to 13 of 22 peers - UL: 36.2 KB/s'
				c = 'Seeding to ';
				c += this.peersGettingFromUs();
				c += ' of ';
				c += this._peers_connected;
				c += ' peers - UL: ';
				c += Math.formatBytes(this._upload_speed);
				c += '/s';
				break;

			case Torrent._StatusChecking:
				// 'Verifying local data (40% tested)'
				c = 'Verifying local data (';
				c += Math.roundWithPrecision( 100.0 * this._recheckProgress, 0 );
				c += '% tested)';
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
		
		setInnerHTML( root._name_container, this._name );
		
		// Add the progress bar
		var notDone = this._leftUntilDone > 0;

		// Fix for situation
		// when a verifying/downloading torrent gets state seeding
		if( this._state === Torrent._StatusSeeding )
			notDone = false ;

		if( this.needsMetaData() ){
			var metaPercentComplete = this._metadataPercentComplete * 1000 / 100
			progress_details = "Magnetized transfer - retrieving metadata (";
			progress_details += metaPercentComplete;
			progress_details += "%)";

			var empty = "";
			if(metaPercentComplete == 0)
				empty = "empty";

			root._progress_complete_container.style.width = metaPercentComplete + "%";
			root._progress_complete_container.className = 'torrent_progress_bar in_progress meta ' + empty;
			root._progress_incomplete_container.style.width = 100 - metaPercentComplete + "%"
			root._progress_incomplete_container.className = 'torrent_progress_bar incomplete meta';
			root._progress_incomplete_container.style.display = 'block';

		}
		else if( notDone )
		{
			var eta = '';
			
			if( this.isActive( ) )
			{
				eta = ' - ';
				if (this._eta < 0 || this._eta >= Torrent._InfiniteTimeRemaining )
					eta += 'remaining time unknown';
				else
					eta += Math.formatSeconds(this._eta) + ' remaining';
			}
			
			// Create the 'progress details' label
			// Eg: '101 MB of 631 MB (16.02%) - 2 hr remaining'
			c = Math.formatBytes( this._sizeWhenDone - this._leftUntilDone );
			c += ' of ';
			c += Math.formatBytes( this._sizeWhenDone );
			c += ' (';
			c += this.getPercentDoneStr();
			c += '%)';
			c += eta;
			progress_details = c;
		
			// Figure out the percent completed
			var css_completed_width = Math.floor( this.getPercentDone() * 100 * MaxBarWidth ) / 100;
			
			// Update the 'in progress' bar
			e = root._progress_complete_container;
			c = 'torrent_progress_bar';
			c += this.isActive() ? ' in_progress' : ' incomplete_stopped';
			if(css_completed_width === 0) { c += ' empty'; }
			e.className = c;
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
			// Eg: '698.05 MB, uploaded 8.59 GB (Ratio: 12.3)'
			c = Math.formatBytes( this._size );
			c += ', uploaded ';
			c += Math.formatBytes( this._upload_total );
			c += ' (Ratio ';
			c += Math.round(this._upload_ratio*100)/100;
			c += ')';
			progress_details = c;

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
		
		// Update the progress details
		setInnerHTML( root._progress_details_container, progress_details );
		
		// Update the peer details and pause/resume button
		e = root._pause_resume_button_image;
		if ( this.state() === Torrent._StatusPaused ) {
			e.alt = 'Resume';
			e.className = "torrent_resume";
		} else {
			e.alt = 'Pause';
			e.className = "torrent_pause";
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
			case Prefs._FilterSeeding:
				pass = this.isSeeding();
				break;
			case Prefs._FilterDownloading:
				pass = this.isDownloading();
				break;
			case Prefs._FilterPaused:
				pass = !this.isActive();
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
Torrent.compareByProgress = function( a, b ) {
	if( a.getPercentDone() !== b.getPercentDone() )
		return a.getPercentDone() - b.getPercentDone();
	var a_ratio = Math.ratio( a._upload_total, a._download_total );
	var b_ratio = Math.ratio( b._upload_total, b._download_total );
	return a_ratio - b_ratio;
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
		var c = Math.formatBytes(this._done);
		c += ' of ';
		c += Math.formatBytes(this._size);
		c += ' (';
		c += Math.ratio(100 * this._done, this._size);
		c += '%)';
		setInnerHTML(this._progress[0], c);
	},
	
	refreshWantedHTML: function() {
		var e = this.domElement();
		var c = e.classNameConst;
		if(!this._wanted) { c += ' skip'; }
		if(this.isDone()) { c += ' complete'; }
		e.className = c;
	},
	
	refreshPriorityHTML: function() {
		var e = this._priority_control;
		var c = e.classNameConst;
		switch( this._prio ) {
			case 1: c += ' high'; break;
			case -1: c += ' low'; break;
			default: c += ' normal'; break;
		}
		e.className = c;
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
