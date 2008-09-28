/*
 *	Copyright © Dave Perrett and Malcolm Jarvis
 *	This code is licensed under the GPL version 2.
 *	For details, see http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 *
 *	Class Torrent
 */

function Torrent(controller,data) {
    this.initialize(controller,data);
} 

// Constants
Torrent._StatusWaitingToCheck  = 1;
Torrent._StatusChecking        = 2;
Torrent._StatusDownloading     = 4;
Torrent._StatusSeeding         = 8;
Torrent._StatusPaused          = 16;
Torrent._InfiniteTimeRemaining = 215784000; // 999 Hours - may as well be infinite

Torrent.prototype =
{
	/*
	 * Constructor
	 */
	initialize: function(controller,data)
	{
		// Create a new <li> element
		var element = $('<li/>');
		element.addClass('torrent');
		element[0].id = 'torrent_' + data.id;
		element._torrent = this;
		this._element = element;
		this._controller = controller;
		controller._rows.push( element );
		
		// Create the 'name' <div>
		var e = $('<div/>');
		e.addClass('torrent_name');
		element.append( e );
		element._name_container = e;
		
		// Create the 'progress details' <div>
		e = $('<div/>');
		e.addClass('torrent_progress_details');
		element.append(e);
		element._progress_details_container = e;

		// Create the 'in progress' bar
		e = $('<div/>');
		e.addClass('torrent_progress_bar incomplete');
		e.css('width', '0%');
		element.append( e );
		element._progress_complete_container = e;
			
		// Create the 'incomplete' bar (initially hidden)
		e = $('<div/>');
		e.addClass('torrent_progress_bar incomplete');
		e.hide();
		element.append( e );
		element._progress_incomplete_container = e;
		
		// Add the pause/resume button - don't specify the
		// image or alt text until the 'refresh()' function
		// (depends on torrent state)
		var image = $('<div/>');
		image.addClass('torrent_pause');
		e = $('<a/>');
		e.append( image );
		element.append( e );
		element._pause_resume_button_image = image;
		element._pause_resume_button = e;
		if (!iPhone) e.bind('click', {element: element}, this.clickPauseResumeButton);
		
		// Create the 'peer details' <div>
		e = $('<div/>');
		e.addClass('torrent_peer_details');
		element.append( e );
		element._peer_details_container = e;
			
		// Set the torrent click observer
		element.bind('click', {element: element}, this.clickTorrent);
		if (!iPhone) element.bind('contextmenu', {element: element}, this.rightClickTorrent);		
		
		// Safari hack - first torrent needs to be moved down for some reason. Seems to be ok when
		// using <li>'s in straight html, but adding through the DOM gets a bit odd.
		if ($.browser.safari)
			this._element.css('margin-top', '7px');
		
		// insert the element
		$('#torrent_list').append(this._element);
		
		// Update all the labels etc
		this.refresh(data);
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
		this.refreshHTML( );
	},

	activity: function() { return this._download_speed + this._upload_speed; },
	comment: function() { return this._comment; },
	completed: function() { return this._completed; },
	creator: function() { return this._creator; },
	dateAdded: function() { return this._date; },
	downloadSpeed: function() { return this._download_speed; },
	downloadTotal: function() { return this._download_total; },
	errorMessage: function() { return this._error_message; },
	hash: function() { return this._hashString; },
	id: function() { return this._id; },
	isActive: function() { return this.state() != Torrent._StatusPaused; },
	isDownloading: function() { return this.state() == Torrent._StatusDownloading; },
	isSeeding: function() { return this.state() == Torrent._StatusSeeding; },
	name: function() { return this._name; },
	peersSendingToUs: function() { return this._peers_sending_to_us; },
	peersGettingFromUs: function() { return this._peers_getting_from_us; },
	getPercentDone: function() {
		if( !this._sizeWhenDone ) return 1.0;
		if( !this._leftUntilDone ) return 1.0;
		return ( this._sizeWhenDone - this._leftUntilDone )
		       / this._sizeWhenDone;
	},
	getPercentDoneStr: function() {
		return Math.ratio( 100 * ( this._sizeWhenDone - this._leftUntilDone ),
		                           this._sizeWhenDone );
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
	swarmSpeed: function() { return this._swarm_speed; },
	totalLeechers: function() { return this._total_leechers; },
	totalSeeders: function() { return this._total_seeders; },
	uploadSpeed: function() { return this._upload_speed; },
	uploadTotal: function() { return this._upload_total; },

	/*--------------------------------------------
	 * 
	 *  E V E N T   F U N C T I O N S
	 * 
	 *--------------------------------------------*/
	
	/*
	 * Process a right-click event on this torrent
	 */
	rightClickTorrent: function(event)
	{
		// don't stop the event! need it for the right-click menu
		
		var t = event.data.element._torrent;
		if ( !t.isSelected( ) )
			t._controller.setSelectedTorrent( t );
	},
	
	/*
	 * Process a click event on this torrent
	 */
	clickTorrent: function( event )
	{
		// Prevents click carrying to parent element
		// which deselects all on click
		event.stopPropagation();
		var torrent = event.data.element._torrent;
		
		// 'Apple' button emulation on PC :
		// Need settable meta-key and ctrl-key variables for mac emulation
		var meta_key = event.metaKey
		var ctrl_key = event.ctrlKey
		if (event.ctrlKey && navigator.appVersion.toLowerCase().indexOf("mac") == -1) {
			meta_key = true;
			ctrl_key = false;
		}
		
		// Shift-Click - Highlight a range between this torrent and the last-clicked torrent
		if (iPhone) {
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
		var torrent = event.data.element._torrent;
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
	refreshData: function(data)
	{
		// These variables never change after the inital load
		if (data.isPrivate)     this._is_private    = data.isPrivate;
		if (data.hashString)    this._hashString    = data.hashString;
		if (data.addedDate)     this._date          = data.addedDate;
		if (data.totalSize)     this._size          = data.totalSize;
		if (data.announceURL)   this._tracker       = data.announceURL;
		if (data.comment)       this._comment       = data.comment;
		if (data.creator)       this._creator       = data.creator;
		if (data.dateCreated)   this._creator_date  = data.dateCreated;
                if (data.leftUntilDone) this._leftUntilDone = data.leftUntilDone;
                if (data.sizeWhenDone)  this._sizeWhenDone  = data.sizeWhenDone;
		if (data.path)          this._torrent_file  = data.path;//FIXME
		if (data.name) {
			this._name = data.name;
			this._name_lc = this._name.toLowerCase( );
		}
		
		// Set the regularly-changing torrent variables
		this._id                    = data.id;
		this._completed             = data.haveUnchecked + data.haveValid;
		this._verified              = data.haveValid;
		this._download_total        = data.downloadedEver;
		this._upload_total          = data.uploadedEver;
		this._download_speed        = data.rateDownload;
		this._upload_speed          = data.rateUpload;
		this._peers_connected       = data.peersConnected;
		this._peers_getting_from_us = data.peersGettingFromUs;
		this._peers_sending_to_us   = data.peersSendingToUs;
		this._error                 = data.error;
		this._error_message         = data.errorString;
		this._eta                   = data.eta;
		this._swarm_speed           = data.swarmSpeed;
		this._total_leechers        = Math.max( 0, data.leechers );
		this._total_seeders         = Math.max( 0, data.seeders );
		this._state                 = data.status;
	},

	refreshHTML: function()
	{
		var progress_details;
		var peer_details;
		var root = this._element;
		var MaxBarWidth = 100; // reduce this to make the progress bar shorter (%)
		
		setInnerHTML( root._name_container[0], this._name );
		
		// Add the progress bar
                var notDone = this._leftUntilDone > 0;

		// Fix for situation
		// when a verifying/downloading torrent gets state seeding
		if( this._state == Torrent._StatusSeeding )
			notDone = false ;
		
		if( notDone )
		{
			var eta = '';
			
			if( this.isActive( ) )
			{
				eta = '-';
				if (this._eta < 0 || this._eta >= Torrent._InfiniteTimeRemaining )
					eta += 'remaining time unknown';
				else
					eta += Math.formatSeconds(this._eta) + ' remaining';
			}
			
			// Create the 'progress details' label
			// Eg: '101 MB of 631 MB (16.02%) - 2 hr remaining'
			progress_details = Math.formatBytes( this._sizeWhenDone - this._leftUntilDone )
			                 + ' of '
			                 + Math.formatBytes( this._sizeWhenDone )
			                 + ' ('
			                 + this.getPercentDoneStr()
			                 + '%)'
			                 + eta;
		
			// Figure out the percent completed
			var css_completed_width = Math.floor( this.getPercentDone() * MaxBarWidth );
			
			// Update the 'in progress' bar
			var class_name = this.isActive() ? 'in_progress' : 'incomplete_stopped';
			var e = root._progress_complete_container;
			e.removeClass( );
			e.addClass( 'torrent_progress_bar' );
			e.addClass( class_name );
			e.css( 'width', css_completed_width + '%' );
			if(css_completed_width == 0) { e.addClass( 'empty' ); }
			
			// Update the 'incomplete' bar
			e = root._progress_incomplete_container;
			if( !e.is('.incomplete')) {
				e.removeClass();
				e.addClass('torrent_progress_bar in_progress');
			}
			e.css('width', (MaxBarWidth - css_completed_width) + '%');
			e.show();
			
			// Create the 'peer details' label
			// Eg: 'Downloading from 36 of 40 peers - DL: 60.2 KB/s UL: 4.3 KB/s'
			if( !this.isDownloading( ) )
				peer_details = this.stateStr( );
			else {
				peer_details = 'Downloading from '
				             + this.peersSendingToUs()
				             + ' of '
				             + this._peers_connected
				             + ' peers - DL: '
				             + Math.formatBytes(this._download_speed)
				             + '/s UL: '
				             + Math.formatBytes(this._upload_speed)
				             + '/s';
			}
			
		}
		else
		{
			// Update the 'in progress' bar
			var class_name = (this.isActive()) ? 'complete' : 'complete_stopped';
			var e = root._progress_complete_container;
			e.removeClass();
			e.addClass('torrent_progress_bar ' + class_name );
			
			// Create the 'progress details' label
			// Eg: '698.05 MB, uploaded 8.59 GB (Ratio: 12.3)'
			progress_details = Math.formatBytes( this._size )
			                 + ', uploaded '
			                 + Math.formatBytes( this._upload_total )
			                 + ' (Ratio '
			                 + Math.ratio( this._upload_total, this._download_total )
			                 + ')';
			
			// Hide the 'incomplete' bar
			root._progress_incomplete_container.hide();
			
			// Set progress to maximum
			root._progress_complete_container.css('width', MaxBarWidth + '%');
			
			// Create the 'peer details' label
			// Eg: 'Seeding to 13 of 22 peers - UL: 36.2 KB/s'
			if( !this.isSeeding( ) )
				peer_details = this.stateStr( );
			else
				peer_details = 'Seeding to '
				             + this.peersGettingFromUs()
				             + ' of '
				             + this._peers_connected
				             + ' peers - UL: '
				             + Math.formatBytes(this._upload_speed)
				             + '/s';
		}
		
		// Update the progress details
		setInnerHTML( root._progress_details_container[0], progress_details );
		
		// Update the peer details and pause/resume button
		e = root._pause_resume_button_image[0];
		if ( this.state() == Torrent._StatusPaused ) {
			e.alt = 'Resume';
			e.className = "torrent_resume";
		} else {
			e.alt = 'Pause';
			e.className = "torrent_pause";
		}
		
		if( this._error_message &&
		    this._error_message != '' &&
		    this._error_message != 'other' ) {
			peer_details = this._error_message;
		}
		
		setInnerHTML( root._peer_details_container[0], peer_details );
	},

	/*
	 * Return true if this torrent is selected
	 */
	isSelected: function() {
		var e = this.element( );
		return e && $.className.has( e[0], 'selected' );
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
		
		var pos = this._name_lc.indexOf( search.toLowerCase() );
		pass = pos != -1;
		return pass;
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
Torrent.compareByTracker = function( a, b ) {
	return a._tracker.compareTo( b._tracker );
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
}

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
		case Prefs._SortByTracker:
			torrents.sort( this.compareByTracker );
			break;
		case Prefs._SortByName:
			torrents.sort( this.compareByName );
			break;
		default:
			console.warn( "unknown sort method: " + sortMethod );
			break;
	}
	
	if( sortDirection == Prefs._SortDescending )
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

/**
 * @param torrents an array of torrents sorted by Id
 * @param id the id to search for
 * @return the torrent, or null
 */
Torrent.lookup = function( torrents, id )
{
	var pos = Torrent.indexOf( torrents, id );
	return pos >= 0 ? torrents[pos] : null;
};
