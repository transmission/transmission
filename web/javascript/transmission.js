/*
 *	Copyright © Dave Perrett and Malcolm Jarvis
 *	This code is licensed under the GPL version 2.
 *	For details, see http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 *
 * Class Transmission
 */
 
function Transmission(){
	this.initialize();
} 
 
Transmission.prototype =
{
	/*--------------------------------------------
	 * 
	 *  C O N S T R U C T O R
	 * 
	 *--------------------------------------------*/

	initialize: function()
	{
		// Before we do anything, browser compatability test
		if ($.browser.msie) {
			$('div.torrent_footer').hide();
			$('div#unsupported_browser').show();
			return;
		}
		
		// Initialize the helper classes
		this.remote = new TransmissionRemote(this);

		// Initialize the implementation fields
		this._current_search         = '';
		this._torrents               = [ ];
		this._rows                   = [ ];
	
		// Initialize the clutch preferences
		Prefs.getClutchPrefs( this );
		
		this.preloadImages();
		
		// Set up user events
		$('#pause_all_link').bind('click', this.stopAllClicked );
		$('#resume_all_link').bind('click', this.startAllClicked);
		$('#pause_selected_link').bind('click', this.stopSelectedClicked );
		$('#resume_selected_link').bind('click', this.startSelectedClicked);
		$('#remove_link').bind('click',  this.removeClicked);
		$('#filter_all_link').parent().bind('click', this.showAllClicked);
		$('#filter_downloading_link').parent().bind('click', this.showDownloadingClicked);
		$('#filter_seeding_link').parent().bind('click', this.showSeedingClicked);
		$('#filter_paused_link').parent().bind('click', this.showPausedClicked);
		$('#prefs_save_button').bind('click', this.savePrefsClicked);
		$('#prefs_cancel_button').bind('click', this.cancelPrefsClicked);
		$('#inspector_tab_info').bind('click', this.inspectorTabClicked);
		$('#inspector_tab_activity').bind('click', this.inspectorTabClicked);
		if (iPhone) {
			$('#torrent_inspector').bind('click', this.hideInspector);
			$('#preferences_link').bind('click', this.releaseClutchPreferencesButton);
		} else {
			$(document).bind('keydown',  this.keyDown);
			$('#torrent_container').bind('click', this.deselectAll);
			$('#open_link').bind('click', this.openTorrentClicked);
			$('#filter_toggle_link').bind('click', this.toggleFilterClicked);
			$('#inspector_link').bind('click', this.toggleInspectorClicked);
			$('#upload_confirm_button').bind('click', this.confirmUploadClicked);
			$('#upload_cancel_button').bind('click', this.cancelUploadClicked);
		
			this.setupSearchBox();
			this.createContextMenu();
			this.createSettingsMenu();
		}
		
		// Setup the preference box
		this.setupPrefConstraints();
		
		// Setup the prefs gui
		this.initializeSettings( );
		
		// Get preferences & torrents from the daemon
		this.remote.loadDaemonPrefs( );
		this.remote.loadTorrents( );
		this.togglePeriodicRefresh( true );
	},

	preloadImages: function() {
		if (iPhone) {
			this.loadImages(
				'images/buttons/info_activity.png',
				'images/buttons/info_general.png',
				'images/buttons/toolbar_buttons.png',
				'images/graphics/filter_bar.png',
				'images/graphics/iphone_chrome.png',
				'images/graphics/logo.png',
				'images/progress/progress.png'
			);
		} else {
			this.loadImages(
				'images/buttons/info_activity.png',
				'images/buttons/info_general.png',
				'images/buttons/tab_backgrounds.png',
				'images/buttons/toolbar_buttons.png',
				'images/buttons/torrent_buttons.png',
				'images/graphics/chrome.png',
				'images/graphics/filter_bar.png',
				'images/graphics/logo.png',
				'images/graphics/transfer_arrows.png',
				'images/progress/progress.png'
			);
		}
	},
	loadImages: function() {
		for(var i = 0; i<arguments.length; i++) {
			jQuery("<img>").attr("src", arguments[i]);
		}
	},
    
	/*
	 * Set up the preference validation
	 */
	setupPrefConstraints: function() {
		// only allow integers for speed limit & port options
		$('div.preference input[@type=text]:not(#download_location)').blur( function() {
			this.value = this.value.replace(/[^0-9]/gi, '');
			if (this.value == '') {
				if ($(this).is('#refresh_rate')) {
					this.value = 5;
				} else {
					this.value = 0;
				}
			}
		});
	},
    
	/*
	 * Load the clutch prefs and init the GUI according to those prefs
	 */
	initializeSettings: function( )
	{
		Prefs.getClutchPrefs( this );

		// iPhone conditions in the section allow us to not
		// include transmenu js to save some bandwidth; if we
		// start using prefs on iPhone we need to weed
		// transmenu refs out of that too.
		
		$('#filter_' + this[Prefs._FilterMode] + '_link').parent().addClass('selected');
		
		if (!iPhone) $('#sort_by_' + this[Prefs._SortMethod] ).selectMenuItem();
		
		if (!iPhone && ( this[Prefs._SortDirection] == Prefs._SortDescending ) )
			$('#reverse_sort_order').selectMenuItem();
		
		if( this[Prefs._ShowFilter] )
			this.showFilter( );
		
		if( this[Prefs._ShowInspector] )
			this.showInspector( );

	},

	/*
	 * Set up the search box
	 */
	setupSearchBox: function()
	{
		var tr = this;
		var search_box = $('#torrent_search');
		search_box.bind('keyup click', {transmission: this}, function(event) {
			tr.setSearch(this.value);
		});
		if (!$.browser.safari)
		{
			search_box.addClass('blur');
			search_box[0].value = 'Filter';
			search_box.bind('blur', {transmission: this}, function(event) {
				if (this.value == '') {
					$(this).addClass('blur');
					this.value = 'Filter';
					tr.setSearch(null);
				}
			}).bind('focus', {}, function(event) {
				if ($(this).is('.blur')) {
					this.value = '';
					$(this).removeClass('blur');
				}
			});
		}
	},

	contextStopSelected: function( ) {
		transmission.stopSelectedTorrents( );
	},
	contextStartSelected: function( ) {
		transmission.startSelectedTorrents( );
	},
	contextRemoveSelected: function( ) {
		transmission.removeSelectedTorrents( );
	},
	contextToggleInspector: function( ) {
		transmission.toggleInspector( );
	},
	contextSelectAll: function( ) {
		transmission.selectAll( true );
	},
	contextDeselectAll: function( ) {
		transmission.deselectAll( true );
	},
    
	/*
	 * Create the torrent right-click menu
	 */
	createContextMenu: function() {
		
		var bindings = {
			context_pause_selected:    this.contextStopSelected,
			context_resume_selected:   this.contextStartSelected,
			context_remove:            this.contextRemoveSelected,
			context_toggle_inspector:  this.contextToggleInspector,
			context_select_all:        this.contextSelectAll,
			context_deselect_all:      this.contextDeselectAll
		};
		
		// Setup the context menu
		$('ul#torrent_list').contextMenu('torrent_context_menu', {
			bindings:          bindings,
			menuStyle:         Menu.context.menu_style,
			itemStyle:         Menu.context.item_style,
			itemHoverStyle:    Menu.context.item_hover_style,
			itemDisabledStyle: Menu.context.item_disabled_style,
			shadow:            false,
			boundingElement:   $('div#torrent_container'),
			boundingRightPad:  20,
			boundingBottomPad: 5
		});
	},
    
	/*
	 * Create the footer settings menu
	 */
	createSettingsMenu: function() {
		$('#settings_menu').transMenu({
			selected_char: '&#x2714;',
			direction: 'up',
			onClick: this.processSettingsMenuEvent
		});
		
		$('#unlimited_download_rate').selectMenuItem();
		$('#unlimited_upload_rate').selectMenuItem();
	},
    

	/*--------------------------------------------
	 * 
	 *  U T I L I T I E S
	 * 
	 *--------------------------------------------*/

	getAllTorrents: function()
	{
		return this._torrents;
	},

	getVisibleTorrents: function()
	{
		var torrents = [ ];
		for( var i=0, len=this._rows.length; i<len; ++i )
			if( this._rows[i]._torrent )
				if( this._rows[i][0].style.display != 'none' )
					torrents.push( this._rows[i]._torrent );
		return torrents;
	},

	getSelectedTorrents: function()
	{
		var v = this.getVisibleTorrents( );
		var s = [ ];
		for( var i=0, len=v.length; i<len; ++i )
			if( v[i].isSelected( ) )
				s.push( v[i] );
		return s;
	},

	getVisibleRows: function()
	{
		var rows = [ ];
		for( var i=0, len=this._rows.length; i<len; ++i )
			if( this._rows[i][0].style.display != 'none' )
				rows.push( this._rows[i] );
		return rows;
	},

	getTorrentIndex: function( rows, torrent )
	{
		for( var i=0, len=rows.length; i<len; ++i )
			if( rows[i]._torrent == torrent )
				return i;
		return null;
	},

	setPref: function( key, val )
	{
		this[key] = val;
		Prefs.setValue( key, val );
	},

	scrollToElement: function( e )
	{
		if( iPhone )
			return;

		var container = $('#torrent_container');
		var scrollTop = container.scrollTop( );
		var innerHeight = container.innerHeight( );

		var offsetTop = e[0].offsetTop;
		var offsetHeight = e.outerHeight( );

		if( offsetTop < scrollTop )
			container.scrollTop( offsetTop );
		else if( innerHeight + scrollTop < offsetTop + offsetHeight )
			container.scrollTop( offsetTop + offsetHeight - innerHeight );
	},

	/*--------------------------------------------
	 * 
	 *  S E L E C T I O N
	 * 
	 *--------------------------------------------*/

	setSelectedTorrent: function( torrent, doUpdate ) {
		this.deselectAll( );
		this.selectTorrent( torrent, doUpdate );
	},

	selectElement: function( e, doUpdate ) {
		$.className.add( e[0], 'selected' );
		this.scrollToElement( e );
		if( doUpdate )
			this.selectionChanged( );
		$.className.add( e[0], 'selected' );
	},
	selectRow: function( rowIndex, doUpdate ) {
		this.selectElement( this._rows[rowIndex], doUpdate );
	},
	selectTorrent: function( torrent, doUpdate ) {
		if( torrent._element )
			this.selectElement( torrent._element, doUpdate );
	},

	deselectElement: function( e, doUpdate ) {
		$.className.remove( e[0], 'selected' );
		if( doUpdate )
			this.selectionChanged( );
	},
	deselectTorrent: function( torrent, doUpdate ) {
		if( torrent._element )
			this.deselectElement( torrent._element, doUpdate );
	},

	selectAll: function( doUpdate ) {
		var tr = transmission;
		for( var i=0, len=tr._rows.length; i<len; ++i )
			tr.selectElement( tr._rows[i] );
		if( doUpdate )
			tr.selectionChanged();
	},
	deselectAll: function( doUpdate ) {
		var tr = transmission;
		for( var i=0, len=tr._rows.length; i<len; ++i )
			tr.deselectElement( tr._rows[i] );
		tr._last_torrent_clicked = null;
		if( doUpdate )
			tr.selectionChanged( );
	},

	/*
	 * Select a range from this torrent to the last clicked torrent
	 */
	selectRange: function( torrent, doUpdate )
	{
		if( !this._last_torrent_clicked )
		{
			this.selectTorrent( torrent );
		}
		else // select the range between the prevous & current
		{
			var rows = this.getVisibleRows( );
			var i = this.getTorrentIndex( rows, this._last_torrent_clicked );
			var end = this.getTorrentIndex( rows, torrent );
			var step = i < end ? 1 : -1;
			for( ; i!=end; i+=step )
				this.selectRow( i );
			this.selectRow( i );
		}

		if( doUpdate )
			this.selectionChanged( );
	},
    
	selectionChanged: function()
	{
		this.updateButtonStates();
		this.updateInspector();
	},

	/*--------------------------------------------
	 * 
	 *  E V E N T   F U N C T I O N S
	 * 
	 *--------------------------------------------*/
    
	/*
	 * Process key event
	 */
	keyDown: function(event)
	{
		var tr = transmission;
		var sel = tr.getSelectedTorrents( );
		var rows = tr.getVisibleRows( );
		var i = -1;
		
		if( event.keyCode == 40 ) // down arrow
		{
			var t = sel.length ? sel[sel.length-1] : null;
			i = t==null ? null : tr.getTorrentIndex(rows,t)+1;
			if( i == rows.length || i == null )
				i = 0;
		}
		else if( event.keyCode == 38 ) // up arrow
		{
			var t = sel.length ? sel[0] : null
			i = t==null ? null : tr.getTorrentIndex(rows,t)-1;
			if( i == -1 || i == null )
				i = rows.length - 1;
		}

		if( 0<=i && i<rows.length ) {
			tr.deselectAll( );
			tr.selectRow( i, true );
		}
	},
    
	isButtonEnabled: function(e) {
		var p = e.target ? e.target.parentNode : e.srcElement.parentNode;
		return p.className!='disabled' && p.parentNode.className!='disabled';
	},

	stopAllClicked: function( event ) {
		var tr = transmission;
		if( tr.isButtonEnabled( event ) ) {
			tr.stopAllTorrents( );
			tr.hideiPhoneAddressbar( );
		}
	},

	stopSelectedClicked: function( event ) {
		var tr = transmission;
		if( tr.isButtonEnabled( event ) ) {
			tr.stopSelectedTorrents( );
			tr.hideiPhoneAddressbar( );
		}
	},

	startAllClicked: function( event ) {
		var tr = transmission;
		if( tr.isButtonEnabled( event ) ) {
			tr.startAllTorrents( );
			tr.hideiPhoneAddressbar( );
		}
	},

	startSelectedClicked: function( event ) {
		var tr = transmission;
		if( tr.isButtonEnabled( event ) ) {
			tr.startSelectedTorrents( );
			tr.hideiPhoneAddressbar( );
		}
	},

	openTorrentClicked: function( event ) {
		var tr = transmission;
		if( tr.isButtonEnabled( event ) ) {
			$('body').addClass('open_showing');
			tr.uploadTorrentFile( );
		}
		tr.updateButtonStates();
	},

	hideUploadDialog: function( ) {
		$('body.open_showing').removeClass('open_showing');
		if (!iPhone && Safari3) {
			$('div#upload_container div.dialog_window').css('top', '-205px');
			setTimeout("$('#upload_container').hide();",500);
		} else {
			$('#upload_container').hide();
		}
		transmission.updateButtonStates();
	},

	cancelUploadClicked: function(event) {
		transmission.hideUploadDialog( );
	},

	confirmUploadClicked: function(event) {
		transmission.uploadTorrentFile( true );
		transmission.hideUploadDialog( );
	},

	cancelPrefsClicked: function(event) {
		transmission.hidePrefsDialog( );
	},

	savePrefsClicked: function(event)
	{
		// handle the clutch prefs locally
		var tr = transmission;
		tr.setPref( Prefs._AutoStart, $('#prefs_form #auto_start')[0].checked );
		var rate = parseInt( $('#prefs_form #refresh_rate')[0].value );
		if( rate != tr[Prefs._RefreshRate] ) {
			tr.setPref( Prefs._RefreshRate, rate );
			tr.togglePeriodicRefresh( false );
			tr.togglePeriodicRefresh( true );
		}
		
		// pass the new prefs upstream to the RPC server
		var o = { };
		o[RPC._PeerPort]         = parseInt( $('#prefs_form #port')[0].value );
		o[RPC._UpSpeedLimit]     = parseInt( $('#prefs_form #upload_rate')[0].value );
		o[RPC._DownSpeedLimit]   = parseInt( $('#prefs_form #download_rate')[0].value );
		o[RPC._DownloadDir]      = $('#prefs_form #download_location')[0].value;
		o[RPC._UpSpeedLimited]   = $('#prefs_form #limit_upload')[0].checked ? 1 : 0;
		o[RPC._DownSpeedLimited] = $('#prefs_form #limit_download')[0].checked ? 1 : 0;
		o[RPC._Encryption]       = $('#prefs_form #encryption')[0].checked
		                               ? RPC._EncryptionRequired
		                               : RPC._EncryptionPreferred;
		tr.remote.savePrefs( o );
		
		tr.hidePrefsDialog( );
	},

	removeClicked: function( event ) {	
		var tr = transmission;
		if( tr.isButtonEnabled( event ) ) {
			tr.removeSelectedTorrents( );
			tr.hideiPhoneAddressbar( );
		}
	},

	toggleInspectorClicked: function( event ) {
		var tr = transmission;
		if( tr.isButtonEnabled( event ) )
			tr.toggleInspector( );
	},

	inspectorTabClicked: function(event) {
	
		if (iPhone) event.stopPropagation();
		
		// Select the clicked tab, unselect the others,
		// and display the appropriate info
		var tab_ids = ['inspector_tab_info', 'inspector_tab_activity'];
		for( var i=0; i<tab_ids.length; ++i ) {
			if (this.id == tab_ids[i]) {
				$('#' + tab_ids[i]).addClass('selected');
				$('#' + tab_ids[i] + '_container').show();
			} else {
				$('#' + tab_ids[i]).removeClass('selected');
				$('#' + tab_ids[i] + '_container').hide();
			}
		}
		transmission.hideiPhoneAddressbar();
	},
	
	toggleFilterClicked: function(event) {
		if (transmission.isButtonEnabled(event))
			transmission.toggleFilter();
	},
	setFilter: function( mode )
	{
		// update the radiobuttons
		var c;
		switch( mode ) {
			case Prefs._FilterAll:         c = '#filter_all_link'; break;
			case Prefs._FilterSeeding:     c = '#filter_seeding_link'; break;
			case Prefs._FilterDownloading: c = '#filter_downloading_link'; break;
			case Prefs._FilterPaused:      c = '#filter_paused_link'; break;
		}
		$(c).parent().siblings().removeClass('selected');
		$(c).parent().addClass('selected');

		// do the filtering
		this.setPref( Prefs._FilterMode, mode );
		this.refilter( );
	},
	showAllClicked: function( event ) {	
		transmission.setFilter( Prefs._FilterAll );
	},
	showDownloadingClicked: function( event ) {
		transmission.setFilter( Prefs._FilterDownloading );
	},
	showSeedingClicked: function(event) {	
		transmission.setFilter( Prefs._FilterSeeding );
	},
	showPausedClicked: function(event) {
		transmission.setFilter( Prefs._FilterPaused );
	},

	/*
	 * 'Clutch Preferences' was clicked (iPhone only)
	 */
	releaseClutchPreferencesButton: function(event) {
		$('div#prefs_container div#pref_error').hide();
		$('div#prefs_container h2.dialog_heading').show();
		this.showPrefsDialog( );
	},

	/*
	 * Turn the periodic ajax-refresh on & off
	 */
	togglePeriodicRefresh: function(state) {
		if (state && this._periodic_refresh == null) {
			// sanity check
			if( !this[Prefs._RefreshRate] )
			     this[Prefs._RefreshRate] = 5;
			this._periodic_refresh = setInterval('transmission.remote.loadTorrents()', this[Prefs._RefreshRate] * 1000 );
		} else {
			clearInterval(this._periodic_refresh);
			this._periodic_refresh = null;
		}
	},

	/*--------------------------------------------
	 * 
	 *  I N T E R F A C E   F U N C T I O N S
	 * 
	 *--------------------------------------------*/
    
	showPrefsDialog: function( )
	{
		$('body').addClass('prefs_showing');
		$('#prefs_container').show();
		transmission.hideiPhoneAddressbar();
		if( Safari3 )
			setTimeout("$('div#prefs_container div.dialog_window').css('top', '0px');",10);
		this.updateButtonStates( );
	},

	hidePrefsDialog: function( )
	{
		$('body.prefs_showing').removeClass('prefs_showing');
		if (iPhone) {
			transmission.hideiPhoneAddressbar();
			$('#prefs_container').hide();
		} else if (Safari3) {
			$('div#prefs_container div.dialog_window').css('top', '-425px');
			setTimeout("$('#prefs_container').hide();",500);
		} else {
			$('#prefs_container').hide();
		}
		this.updateButtonStates( );
	},
    
	/*
	 * Process got some new session data from the server
	 */
	updatePrefs: function( prefs )
	{
		// remember them for later
		this._prefs = prefs;

		var down_limit    = prefs[RPC._DownSpeedLimit];
		var down_limited  = prefs[RPC._DownSpeedLimited];
		var up_limit      = prefs[RPC._UpSpeedLimit];
		var up_limited    = prefs[RPC._UpSpeedLimited];
		
		$('div.download_location input')[0].value = prefs['download-dir'];
		$('div.port input')[0].value              = prefs['port'];
		$('div.auto_start input')[0].checked      = prefs[Prefs._AutoStart];
		$('input#limit_download')[0].checked      = down_limited == 1;
		$('input#download_rate')[0].value         = down_limit;
		$('input#limit_upload')[0].checked        = up_limited == 1;
		$('input#upload_rate')[0].value           = up_limit;
		$('input#refresh_rate')[0].value          = prefs[Prefs._RefreshRate];
		$('div.encryption input')[0].checked      = prefs[RPC._Encryption] == RPC._EncryptionRequired;

		if (!iPhone)
		{
			setInnerHTML( $('#limited_download_rate')[0], 'Limit (' + down_limit + ' KB/s)' );
			var key = down_limited ? '#limited_download_rate'
			                       : '#unlimited_download_rate';
			$(key).deselectMenuSiblings().selectMenuItem();
		
			setInnerHTML( $('#limited_upload_rate')[0], 'Limit (' + up_limit + ' KB/s)' );
			key = up_limited ? '#limited_upload_rate'
			                 : '#unlimited_upload_rate';
			$(key).deselectMenuSiblings().selectMenuItem();
		}
	},
    
	setSearch: function( search ) {
		this._current_search = search ? search.trim() : null;
		this.refilter( );
	},

	setSortMethod: function( sort_method ) {
		this.setPref( Prefs._SortMethod, sort_method );
		this.refilter( );
	},

	setSortDirection: function( direction ) {
		this.setPref( Prefs._SortDirection, direction );
		this.refilter( );
	},

	/*
	 * Process an event in the footer-menu
	 */
	processSettingsMenuEvent: function(event) {
		// Don't use 'this' in the function to avoid confusion (this != transmission instance)
		var element = this;
		
		// Figure out which menu has been clicked
		switch ($(element).parent()[0].id) {
			
			// Display the preferences dialog
			case 'footer_super_menu':
				if ($(element)[0].id == 'preferences') {
					$('div#prefs_container div#pref_error').hide();
					$('div#prefs_container h2.dialog_heading').show();
					transmission.showPrefsDialog( );
				}
				break;
			
			// Limit the download rate
			case 'footer_download_rate_menu':
				var args = { };
				var rate = (this.innerHTML).replace(/[^0-9]/ig, '');
				if ($(this).is('#unlimited_download_rate')) {
					$(this).deselectMenuSiblings().selectMenuItem();
					args[RPC._DownSpeedLimited] = false;
				} else {
					setInnerHTML( $('#limited_download_rate')[0], 'Limit (' + rate + ' KB/s)' );
					$('#limited_download_rate').deselectMenuSiblings().selectMenuItem();
					$('div.preference input#download_rate')[0].value = rate;
					args[RPC._DownSpeedLimit] = parseInt( rate );
					args[RPC._DownSpeedLimited] = true;
				}
				$('div.preference input#limit_download')[0].checked = args[RPC._DownSpeedLimited];
				transmission.remote.savePrefs( args );
				break;
			
			// Limit the upload rate
			case 'footer_upload_rate_menu':
				var args = { };
				var rate = (this.innerHTML).replace(/[^0-9]/ig, '');
				if ($(this).is('#unlimited_upload_rate')) {
					$(this).deselectMenuSiblings().selectMenuItem();
					args[RPC._UpSpeedLimited] = false;
				} else {
					setInnerHTML( $('#limited_upload_rate')[0], 'Limit (' + rate + ' KB/s)' );
					$('#limited_upload_rate').deselectMenuSiblings().selectMenuItem();
					$('div.preference input#upload_rate')[0].value = rate;
					args[RPC._UpSpeedLimit] = parseInt( rate );
					args[RPC._UpSpeedLimited] = true;
				}
				$('div.preference input#limit_upload')[0].checked = args[RPC._UpSpeedLimited];
				transmission.remote.savePrefs( args );
				break;
			
			// Sort the torrent list 
			case 'footer_sort_menu':

				// The 'reverse sort' option state can be toggled independently of the other options
				if ($(this).is('#reverse_sort_order')) {
					var dir;
					if ($(this).menuItemIsSelected()) {
						$(this).deselectMenuItem();
						dir = Prefs._SortAscending;
					} else {
						$(this).selectMenuItem();
						dir = Prefs._SortDescending;
					}
					transmission.setSortDirection( dir );

				// Otherwise, deselect all other options (except reverse-sort) and select this one
				} else {
					$(this).parent().find('span.selected').each( function() {
						if (! $(this).parent().is('#reverse_sort_order')) {
							$(this).parent().deselectMenuItem();
						}
					});
					$(this).selectMenuItem();
					var method = $(this)[0].id.replace(/sort_by_/, '');
					transmission.setSortMethod( method );
				}
				break;
		}
	},

	setLastTorrentClicked: function( torrent )
	{
		this._last_torrent_clicked = torrent;
	},
    
	/*
	 * Update the inspector with the latest data for the selected torrents
	 */
	updateInspector: function()
	{
		if( !this[Prefs._ShowInspector] )
			return;

		var torrents = this.getSelectedTorrents( );
		if( !torrents.length && iPhone ) {
			transmission.hideInspector();
			return;
		}

		var creator = 'N/A';
		var comment = 'N/A';
		var date_created = 'N/A';
		var error = '';
		var hash = 'N/A';
		var have_public = false;
		var have_private = false;
		var name;
		var sizeWhenDone = 0;
		var sizeDone = 0;
		var total_completed = 0;
		var total_download = 0;
		var total_download_peers = 0;
		var total_download_speed = 0;
		var total_have = 0;
		var total_leechers = 0;
		var total_size = 0;
		var total_seeders = 0;
		var total_state = null;
		var total_swarm_speed = 0;
		var total_tracker = null;
		var total_upload = 0;
		var total_upload_peers = 0;
		var total_upload_speed = 0;
		var total_verified = 0;
		var na = 'N/A';
		
		$("#torrent_inspector_size, .inspector_row div").css('color', '#222');

		if( torrents.length == 0 )
		{
			var ti = '#torrent_inspector_';
			setInnerHTML( $(ti+'name')[0], 'No Selection' );
			setInnerHTML( $(ti+'size')[0], na );
			setInnerHTML( $(ti+'tracker')[0], na );
			setInnerHTML( $(ti+'hash')[0], na );
			setInnerHTML( $(ti+'state')[0], na );
			setInnerHTML( $(ti+'download_speed')[0], na );
			setInnerHTML( $(ti+'upload_speed')[0], na );
			setInnerHTML( $(ti+'uploaded')[0], na );
			setInnerHTML( $(ti+'downloaded')[0], na );
			setInnerHTML( $(ti+'ratio')[0], na );
			setInnerHTML( $(ti+'total_seeders')[0], na );
			setInnerHTML( $(ti+'total_leechers')[0], na );
			setInnerHTML( $(ti+'swarm_speed')[0], na );
			setInnerHTML( $(ti+'have')[0], na );
			setInnerHTML( $(ti+'upload_to')[0], na );
			setInnerHTML( $(ti+'download_from')[0], na );
			setInnerHTML( $(ti+'secure')[0], na );
			setInnerHTML( $(ti+'creator_date')[0], na );
			setInnerHTML( $(ti+'progress')[0], na );
			setInnerHTML( $(ti+'comment')[0], na );
			setInnerHTML( $(ti+'creator')[0], na );
			setInnerHTML( $(ti+'error')[0], na );		
			$("#torrent_inspector_size, .inspector_row > div:contains('N/A')").css('color', '#666');
			return;
		}

		name = torrents.length == 1
			? torrents[0].name()
			: torrents.length+' Transfers Selected';

		if( torrents.length == 1 )
		{
			var t = torrents[0];
			if( t._error_message )
			{
				error = t._error_message ;
			}
			if( t._comment)
			{
				comment = t._comment ;
			}
			if( t._creator )
			{
				creator = t._creator ;
			}
			hash = t.hash();
			date_created = Math.formatTimestamp( t._creator_date );
		}

		for( i=0; i<torrents.length; ++i ) {
			var t = torrents[i];
			sizeWhenDone         += t._sizeWhenDone;
			sizeDone             += t._sizeWhenDone - t._leftUntilDone;
			total_completed      += t.completed();
			total_verified       += t._verified;
			total_size           += t.size();
			total_upload         += t.uploadTotal();
			total_download       += t.downloadTotal();
			total_upload_speed   += t.uploadSpeed();
			total_download_speed += t.downloadSpeed();
			total_seeders        += t.totalSeeders();
			total_leechers       += t.totalLeechers();
			total_upload_peers   += t.peersGettingFromUs();
			total_download_peers += t.peersSendingToUs();
			total_swarm_speed    += t.swarmSpeed();
			if( total_state == null )
				total_state = t.stateStr();
			else if ( total_state.search ( t.stateStr() ) == -1 )
				total_state += '/' + t.stateStr();
			var tracker = t._tracker;
			if( total_tracker == null )
				total_tracker = tracker;
			else if ( total_tracker.search ( tracker ) == -1 )  
				total_tracker += ', ' + tracker;
			if( t._is_private )
				have_private = true;
			else
				have_public = true;
		}

		var private_string = '';
		if( have_private && have_public ) private_string = 'Mixed';
		else if( have_private ) private_string = 'Private Torrent';
		else if( have_public ) private_string = 'Public Torrent';	

		var ti = '#torrent_inspector_';
		$(ti+'name')[0].innerHTML            = name;
		$(ti+'size')[0].innerHTML            = torrents.length ? Math.formatBytes( total_size ) : 'N/A';
		$(ti+'tracker')[0].innerHTML         = total_tracker;
		$(ti+'hash')[0].innerHTML            = hash;
		$(ti+'state')[0].innerHTML           = total_state;
		$(ti+'download_speed')[0].innerHTML  = torrents.length ? Math.formatBytes( total_download_speed ) + '/s' : 'N/A';
		$(ti+'upload_speed')[0].innerHTML    = torrents.length ? Math.formatBytes( total_upload_speed ) + '/s' : 'N/A';
		$(ti+'uploaded')[0].innerHTML        = torrents.length ? Math.formatBytes( total_upload ) : 'N/A';
		$(ti+'downloaded')[0].innerHTML      = torrents.length ? Math.formatBytes( total_download ) : 'N/A';
		$(ti+'ratio')[0].innerHTML           = torrents.length ? Math.ratio( total_upload, total_download ) : 'N/A';
		$(ti+'total_seeders')[0].innerHTML   = torrents.length ? total_seeders : 'N/A';
		$(ti+'total_leechers')[0].innerHTML  = torrents.length ? total_leechers : 'N/A';
		$(ti+'swarm_speed')[0].innerHTML     = torrents.length ? Math.formatBytes(total_swarm_speed) + '/s' : 'N/A';
		$(ti+'have')[0].innerHTML            = torrents.length ? Math.formatBytes(total_completed) + ' (' + Math.formatBytes(total_verified) + ' verified)' : 'N/A';
		$(ti+'upload_to')[0].innerHTML       = torrents.length ? total_upload_peers : 'N/A';
		$(ti+'download_from')[0].innerHTML   = torrents.length ? total_download_peers : 'N/A';
		$(ti+'secure')[0].innerHTML          = private_string;
		$(ti+'creator_date')[0].innerHTML    = date_created;
		$(ti+'progress')[0].innerHTML        = torrents.length ? Math.ratio( sizeDone*100, sizeWhenDone ) + '%' : 'N/A';
		$(ti+'comment')[0].innerHTML         = comment;
		$(ti+'creator')[0].innerHTML         = creator;
		$(ti+'error')[0].innerHTML           = error;
		
		$(".inspector_row > div:contains('N/A')").css('color', '#666');
	},
    
	/*
	 * Toggle the visibility of the inspector (used by the context menu)
	 */
	toggleInspector: function() {
		if( this[Prefs._ShowInspector] )
			this.hideInspector( );
		else
			this.showInspector( );
	},
    
	showInspector: function() {
		$('#torrent_inspector').show();
		if (iPhone) {
			$('body').addClass('inspector_showing');
			this.hideiPhoneAddressbar();
		} else {
			var w = $('#torrent_inspector').width() + 1 + 'px';
			$('#torrent_filter_bar')[0].style.right = w;
			$('#torrent_container')[0].style.right = w;
		}

		setInnerHTML( $('ul li#context_toggle_inspector')[0], 'Hide Inspector' );

		this.setPref( Prefs._ShowInspector, true );
		this.updateInspector( );
	},
    
	/*
	 * Hide the inspector
	 */
	hideInspector: function() {

		$('#torrent_inspector').hide();
		
		if (iPhone) {
			transmsision.deselectAll( );
			$('body.inspector_showing').removeClass('inspector_showing');
			transmission.hideiPhoneAddressbar();
		} else {
			$('#torrent_filter_bar')[0].style.right = '0px';
			$('#torrent_container')[0].style.right = '0px';
			setInnerHTML( $('ul li#context_toggle_inspector')[0], 'Show Inspector' );
		}
		
		this.setPref( Prefs._ShowInspector, false );
	},
	
	/*
	 * Toggle the visibility of the filter bar
	 */
	toggleFilter: function() {
		if( this[Prefs._ShowFilter] )
			this.hideFilter();
		else
			this.showFilter();
	},

	showFilter: function( ) {
		var container_top = parseInt($('#torrent_container').css('top')) + $('#torrent_filter_bar').height() + 1;
		$('#torrent_container').css('top', container_top + 'px');
		$('#torrent_filter_bar').show();
		this.setPref( Prefs._ShowFilter, true );
	},
	
	hideFilter: function()
	{
		var container_top = parseInt($('#torrent_container').css('top')) - $('#torrent_filter_bar').height() - 1;
		$('#torrent_container').css('top', container_top + 'px');
		$('#torrent_filter_bar').hide();
		this.setPref( Prefs._ShowFilter, false );
		this.setFilter( Prefs._FilterAll );
	},

	/*
	 * Process got some new torrent data from the server
	 */
	updateTorrents: function( torrent_list )
	{
		var torrent_data;
		var new_torrents = [];
		var torrent_ids = [];
		var handled = [];
		
		// refresh existing torrents
		for( var i=0, len=torrent_list.length; i<len; ++i ) {
			var data = torrent_list[i];
			var t = Torrent.lookup( this._torrents, data.id );
			if( !t )
				new_torrents.push( data );
			else {
				t.refresh( data );
				handled.push( t );
			}
		}
		
		// Add any torrents that aren't already being displayed
		if( new_torrents.length ) {
			for( var i=0, len=new_torrents.length; i<len; ++i ) {
				var t = new Torrent( this, new_torrents[i] );
				this._torrents.push( t );
				handled.push( t );
			}
			this._torrents.sort( Torrent.compareById ); 
		}
		
		// Remove any torrents that weren't in the refresh list
		var removedAny = false;
		handled.sort( Torrent.compareById ); // for Torrent.indexOf
		var allTorrents = this._torrents.clone();
		for( var i=0, len=allTorrents.length; i<len; ++i ) {
			var t = allTorrents[i];
			if( Torrent.indexOf( handled, t.id() ) == -1 ) {
				var pos = Torrent.indexOf( this._torrents, t.id( ) );
				var e = this._torrents[pos].element();
				if( e ) {
					delete e._torrent;
					e.hide( );
				}
				this._torrents.splice( pos, 1 );
				removedAny = true;
			}
		}
		
		if( ( new_torrents.length != 0 ) || removedAny ) {
			this.hideiPhoneAddressbar();
			this.deselectAll( true );
		}
		
		// FIXME: not sure if this is possible in RPC
		// Update the disk space remaining
		//var disk_space_msg = 'Free Space: '
		//+ Math.formatBytes(data.free_space_bytes)
		//+ ' (' + data.free_space_percent + '% )';
		//setInnerHTML( $('div#disk_space_container')[0], disk_space_msg );
		
		this.refilter( );
	},

	/*
	 * Set the alternating background colors for torrents
	 */
	setTorrentBgColors: function( )
	{
		var rows = this.getVisibleRows( );
		for( var i=0, len=rows.length; i<len; ++i )
			if ((i+1) % 2 == 0)
				$.className.add( rows[i][0], 'even' );
			else
				$.className.remove( rows[i][0], 'even' );
	},
    
	updateStatusbar: function()
	{
		var torrents = this.getAllTorrents();
		var torrentCount = torrents.length;
		var visibleCount = this.getVisibleTorrents().length;
		
		// calculate the overall speed
		var upSpeed = 0;
		var downSpeed = 0;
		for( var i=0; i<torrentCount; ++i ) {
			upSpeed += torrents[i].uploadSpeed( );
			downSpeed += torrents[i].downloadSpeed( );
		}
		
		// update torrent count label
		var s;
		if( torrentCount == visibleCount )
			s = torrentCount + ' Transfers';
		else
			s = visibleCount + ' of ' + torrentCount + ' Transfers';
		setInnerHTML( $('#torrent_global_transfer')[0], s );
		
		// update the speeds
		s = Math.formatBytes( upSpeed ) + '/s';
		if( iPhone ) s = 'UL: ' + s;
		setInnerHTML( $('#torrent_global_upload')[0], s );
		
		// download speeds
		s = Math.formatBytes( downSpeed ) + '/s';
		if( iPhone ) s = 'DL: ' + s;
		setInnerHTML( $('#torrent_global_download')[0], s );
	},
    
	/*
	 * Select a torrent file to upload
	 * FIXME
	 */
	uploadTorrentFile: function(confirmed)
	{
		// Display the upload dialog
		if (! confirmed) {
				$('input#torrent_upload_file').attr('value', '');
				$('input#torrent_upload_url').attr('value', ''); 
				$('#upload_container').show();
			if (!iPhone && Safari3) {
				setTimeout("$('div#upload_container div.dialog_window').css('top', '0px');",10);
			}
			
		// Submit the upload form
		} else {
			var tr = this;
			var args = { };
			args.url = '/transmission/upload?paused=' + (this[Prefs._AutoStart] ? 'false' : 'true');
			args.type = 'POST';
			args.dataType = 'xml';
			args.iframe = true;
			args.success = function( data ) {
				tr.remote.loadTorrents( );
				tr.togglePeriodicRefresh( true );
			};
			this.togglePeriodicRefresh( false );
			$('#torrent_upload_form').ajaxSubmit( args );
		}
	},
   
	removeSelectedTorrents: function() {
		var torrents = this.getSelectedTorrents( );
		if( torrents.length )
			this.promptToRemoveTorrents( torrents );
	},

	promptToRemoveTorrents:function( torrents )
	{
		if( torrents.length == 1 )
		{
			var torrent = torrents[0];
			var header = 'Remove ' + torrent.name() + '?';
			var message = 'Once removed, continuing the transfer will require the torrent file. Are you sure you want to remove it?';
			dialog.confirm( header, message, 'Remove', 'transmission.removeTorrents', torrents );
		}
		else 
		{
			var header = 'Remove ' + torrents.length + ' transfers?';
			var message = 'Once removed, continuing the transfers will require the torrent files. Are you sure you want to remove them?';
			dialog.confirm( header, message, 'Remove', 'transmission.removeTorrents', torrents );
		}
	},

	removeTorrents: function( torrents ) {
		this.remote.removeTorrents( torrents );
	},

	startSelectedTorrents: function( ) {
		this.startTorrents( this.getSelectedTorrents( ) );
	},
	startAllTorrents: function( ) {
		this.startTorrents( this.getAllTorrents( ) );
	},
	startTorrent: function( torrent ) {
		this.startTorrents( [ torrent ] );
	},
	startTorrents: function( torrents ) {
		this.remote.startTorrents( torrents );
	},
    
	stopSelectedTorrents: function( ) {
		this.stopTorrents( this.getSelectedTorrents( ) );
	},
	stopAllTorrents: function( ) {
		this.stopTorrents( this.getAllTorrents( ) );
	},
	stopTorrent: function( torrent ) {
		this.stopTorrents( [ torrent ] );
	},
	stopTorrents: function( torrents ) {
		this.remote.stopTorrents( torrents );
	},
    
	hideiPhoneAddressbar: function(timeInSeconds) {
		if( iPhone ) {
			var delayLength = timeInSeconds ? timeInSeconds*1000 : 150;
			// not currently supported on iPhone
			if(/*document.body.scrollTop!=1 && */scroll_timeout==null) {
				scroll_timeout = setTimeout("transmission.doToolbarHide()", delayLength);
			}
		}
	},
	doToolbarHide: function() {
		window.scrollTo(0,1);
		scroll_timeout=null;
	},

	/***
	****
	***/

	refilter: function()
	{
		// decide which torrents to keep showing
		var allTorrents = this.getAllTorrents( );
		var keep = [ ];
		for( var i=0, len=allTorrents.length; i<len; ++i ) {
			var t = allTorrents[i];
			if( t.test( this[Prefs._FilterMode], this._current_search ) )
				keep.push( t );
		}

		// sort the keepers
		Torrent.sortTorrents( keep, this[Prefs._SortMethod],
		                            this[Prefs._SortDirection] );

		// make a backup of the selection
		var sel = this.getSelectedTorrents( );
		this.deselectAll( );

		// hide the ones we're not keeping
		for( var i=keep.length; i<this._rows.length; ++i ) {
			var e = this._rows[i];
			delete e._torrent;
			e[0].style.display = 'none';
		}

		// show the ones we're keeping
		sel.sort( Torrent.compareById );
		for( var i=0, len=keep.length; i<len; ++i ) {
			var e = this._rows[i];
			e[0].style.display = 'block';
			var t = keep[i];
			t.setElement( e );
			if( Torrent.indexOf( sel, t.id() ) != -1 )
				this.selectElement( e );
		}

		// sync gui
		this.setTorrentBgColors( );
		this.updateStatusbar( );
		this.selectionChanged( );
	},

	setEnabled: function( key, flag )
	{
		if( flag )
			$(key + '.disabled').removeClass('disabled');
		else
			$(key).addClass('disabled');
	},

	updateButtonStates: function()
	{
		var showing_dialog = new RegExp("(prefs_showing|dialog_showing|open_showing)").test(document.body.className);
		if (showing_dialog)
		{
			$('.torrent_global_menu ul li').addClass('disabled');
		}
		else
		{
			var torrents = this.getVisibleTorrents( );
			var haveSelection = false;
			var haveActive = false;
			var haveActiveSelection = false;
			var havePaused = false;
			var havePausedSelection = false;

			for( var i=0, len=torrents.length; !haveSelection && i<len; ++i ) {
				var isActive = torrents[i].isActive( );
				var isSelected = torrents[i].isSelected( );
				if( isActive ) haveActive = true;
				if( !isActive ) havePaused = true;
				if( isSelected ) haveSelection = true;
				if( isSelected && isActive ) haveActiveSelection = true;
				if( isSelected && !isActive ) havePausedSelection = true;
			}

			$('.torrent_global_menu ul li.disabled').removeClass('disabled');

			this.setEnabled( 'li#pause_selected', haveActiveSelection );
			this.setEnabled( 'li.context_pause_selected', haveActiveSelection );
			this.setEnabled( 'li#resume_selected', havePausedSelection );
			this.setEnabled( 'li.context_resume_selected', havePausedSelection );
			this.setEnabled( 'li#remove', haveSelection );
			this.setEnabled( 'li#pause_all', haveActive );
			this.setEnabled( 'li#resume_all', havePaused );
		}
	}
};
