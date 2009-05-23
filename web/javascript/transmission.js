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
		// IE specific fixes here
		if ($.browser.msie) {
			try {
			  document.execCommand("BackgroundImageCache", false, true);
			} catch(err) {}
			$('head').append('<link media="screen" href="./stylesheets/common.css" type="text/css" rel="stylesheet" />');
			$('head').append('<link media="screen" href="./stylesheets/ie'+$.browser.version.substr(0,1)+'.css" type="text/css" rel="stylesheet" />');
			$('.dialog_container').css('height',$(window).height()+'px');
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
		var tr = this;
		$('#pause_all_link').bind('click', function(e){ tr.stopAllClicked(e); });
		$('#resume_all_link').bind('click', function(e){ tr.startAllClicked(e); });
		$('#pause_selected_link').bind('click', function(e){ tr.stopSelectedClicked(e); } );
		$('#resume_selected_link').bind('click', function(e){ tr.startSelectedClicked(e); });
		$('#remove_link').bind('click',  function(e){ tr.removeClicked(e); });
		$('#removedata_link').bind('click',  function(e){ tr.removeDataClicked(e); });
		$('#filter_all_link').parent().bind('click', function(e){ tr.showAllClicked(e); });
		$('#filter_downloading_link').parent().bind('click', function(e){ tr.showDownloadingClicked(e); });
		$('#filter_seeding_link').parent().bind('click', function(e){ tr.showSeedingClicked(e); });
		$('#filter_paused_link').parent().bind('click', function(e){ tr.showPausedClicked(e); });
		$('#prefs_save_button').bind('click', function(e) { tr.savePrefsClicked(e); return false;});
		$('#prefs_cancel_button').bind('click', function(e){ tr.cancelPrefsClicked(e); return false; });
		$('#inspector_tab_info').bind('click', function(e){ tr.inspectorTabClicked(e); });
		$('#inspector_tab_activity').bind('click', function(e){ tr.inspectorTabClicked(e); });
		$('#inspector_tab_files').bind('click', function(e){ tr.inspectorTabClicked(e); });
		if (iPhone) {
			$('#torrent_inspector').bind('click', function(e){ tr.hideInspector(); });
			$('#preferences_link').bind('click', function(e){ tr.releaseClutchPreferencesButton(e); });
		} else {
			$(document).bind('keydown',  function(e){ tr.keyDown(e); });
			$('#torrent_container').bind('click', function(e){ tr.deselectAll( true ); });
			$('#open_link').bind('click', function(e){ tr.openTorrentClicked(e); });
			$('#filter_toggle_link').bind('click', function(e){ tr.toggleFilterClicked(e); });
			$('#inspector_link').bind('click', function(e){ tr.toggleInspectorClicked(e); });
			$('#upload_confirm_button').bind('click', function(e){ tr.confirmUploadClicked(e); return false;});
			$('#upload_cancel_button').bind('click', function(e){ tr.cancelUploadClicked(e); return false; });
		
			this.setupSearchBox();
			this.createContextMenu();
			this.createSettingsMenu();
		}
		
		// Setup the preference box
		this.setupPrefConstraints();
		
		// Setup the prefs gui
		this.initializeSettings( );
		
		// Get preferences & torrents from the daemon
		var tr = this;
		this.remote.loadDaemonPrefs( );
		this.initalizeAllTorrents();

		this.togglePeriodicRefresh( true );
	},

	preloadImages: function() {
		if (iPhone) {
			this.loadImages(
				'images/buttons/info_general.png',
				'images/buttons/info_activity.png',
				'images/buttons/info_files.png',
				'images/buttons/toolbar_buttons.png',
				'images/graphics/filter_bar.png',
				'images/graphics/iphone_chrome.png',
				'images/graphics/logo.png'
			);
		} else {
			this.loadImages(
				'images/buttons/info_general.png',
				'images/buttons/info_activity.png',
				'images/buttons/info_files.png',
				'images/buttons/tab_backgrounds.png',
				'images/buttons/toolbar_buttons.png',
				'images/buttons/torrent_buttons.png',
				'images/buttons/file_wanted_buttons.png',
				'images/buttons/file_priority_buttons.png',
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
		this.stopSelectedTorrents( );
	},
	contextStartSelected: function( ) {
		this.startSelectedTorrents( );
	},
	contextRemoveSelected: function( ) {
		this.removeSelectedTorrents( );
	},
	contextRemoveDataSelected: function( ) {
		this.removeSelectedTorrentsAndData( );
	},
	contextVerifySelected: function( ) {
		this.verifySelectedTorrents( );
	},
	contextToggleInspector: function( ) {
		this.toggleInspector( );
	},
	contextSelectAll: function( ) {
		this.selectAll( true );
	},
	contextDeselectAll: function( ) {
		this.deselectAll( true );
	},
    
	/*
	 * Create the torrent right-click menu
	 */
	createContextMenu: function() {
		var tr = this;
		var bindings = {
			context_pause_selected:    function(e){ tr.contextStopSelected(e); },
			context_resume_selected:   function(e){ tr.contextStartSelected(e); },
			context_remove:            function(e){ tr.contextRemoveSelected(e); },
			context_removedata:        function(e){ tr.contextRemoveDataSelected(e); },
			context_verify:            function(e){ tr.contextVerifySelected(e); },
			context_toggle_inspector:  function(e){ tr.contextToggleInspector(e); },
			context_select_all:        function(e){ tr.contextSelectAll(e); },
			context_deselect_all:      function(e){ tr.contextDeselectAll(e); }
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
		var tr = this;
		$('#settings_menu').transMenu({
			selected_char: '&#x2714;',
			direction: 'up',
			onClick: function(e){ return tr.processSettingsMenuEvent(e); }
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
	
	getDeselectedTorrents: function() {
		var visible_torrent_ids = jQuery.map(this.getVisibleTorrents(), function(t) { return t.id(); } );
		var s = [ ];
		jQuery.each( this.getAllTorrents( ), function() {
			var visible = (-1 != jQuery.inArray(this.id(), visible_torrent_ids));
			if (!this.isSelected() || !visible)
				s.push( this );
		} );
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
		var tr = this;
		for( var i=0, len=tr._rows.length; i<len; ++i )
			tr.selectElement( tr._rows[i] );
		if( doUpdate )
			tr.selectionChanged();
	},
	deselectAll: function( doUpdate ) {
		var tr = this;
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
		var tr = this;
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
		var tr = this;
		if( tr.isButtonEnabled( event ) ) {
			tr.stopAllTorrents( );
			tr.hideiPhoneAddressbar( );
		}
	},

	stopSelectedClicked: function( event ) {
		var tr = this;
		if( tr.isButtonEnabled( event ) ) {
			tr.stopSelectedTorrents( );
			tr.hideiPhoneAddressbar( );
		}
	},

	startAllClicked: function( event ) {
		var tr = this;
		if( tr.isButtonEnabled( event ) ) {
			tr.startAllTorrents( );
			tr.hideiPhoneAddressbar( );
		}
	},

	startSelectedClicked: function( event ) {
		var tr = this;
		if( tr.isButtonEnabled( event ) ) {
			tr.startSelectedTorrents( );
			tr.hideiPhoneAddressbar( );
		}
	},

	openTorrentClicked: function( event ) {
		var tr = this;
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
		this.updateButtonStates();
	},

	cancelUploadClicked: function(event) {
		this.hideUploadDialog( );
	},

	confirmUploadClicked: function(event) {
		this.uploadTorrentFile( true );
		this.hideUploadDialog( );
	},

	cancelPrefsClicked: function(event) {
		this.hidePrefsDialog( );
	},

	savePrefsClicked: function(event)
	{
		// handle the clutch prefs locally
		var tr = this;
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
		var tr = this;
		if( tr.isButtonEnabled( event ) ) {
			tr.removeSelectedTorrents( );
			tr.hideiPhoneAddressbar( );
		}
	},

	removeDataClicked: function( event ) {	
		var tr = this;
		if( tr.isButtonEnabled( event ) ) {
			tr.removeSelectedTorrentsAndData( );
			tr.hideiPhoneAddressbar( );
		}
	},

	toggleInspectorClicked: function( event ) {
		var tr = this;
		if( tr.isButtonEnabled( event ) )
			tr.toggleInspector( );
	},

	inspectorTabClicked: function(event) {
	
		if (iPhone) event.stopPropagation();
		
		// Select the clicked tab, unselect the others,
		// and display the appropriate info
		var tab_ids = $(this).parent('#inspector_tabs').find('.inspector_tab').map(
			function() { return $(this).attr('id'); } 
		);
		for( var i=0; i<tab_ids.length; ++i ) {
			if (this.id == tab_ids[i]) {
				$('#' + tab_ids[i]).addClass('selected');
				$('#' + tab_ids[i] + '_container').show();
			} else {
				$('#' + tab_ids[i]).removeClass('selected');
				$('#' + tab_ids[i] + '_container').hide();
			}
		}
		this.hideiPhoneAddressbar();
	},
	
	toggleFilterClicked: function(event) {
		if (this.isButtonEnabled(event))
			this.toggleFilter();
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
		this.setFilter( Prefs._FilterAll );
	},
	showDownloadingClicked: function( event ) {
		this.setFilter( Prefs._FilterDownloading );
	},
	showSeedingClicked: function(event) {	
		this.setFilter( Prefs._FilterSeeding );
	},
	showPausedClicked: function(event) {
		this.setFilter( Prefs._FilterPaused );
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
		var tr = this;
		if (state && this._periodic_refresh == null) {
			// sanity check
			if( !this[Prefs._RefreshRate] )
			     this[Prefs._RefreshRate] = 5;
			remote = this.remote;
			this._periodic_refresh = setInterval(function(){ tr.refreshTorrents(); }, this[Prefs._RefreshRate] * 1000 );
		} else {
			clearInterval(this._periodic_refresh);
			this._periodic_refresh = null;
		}
	},
	
	scheduleFileRefresh: function() {
		this._periodicRefreshIterations = 0;
	},

	/*--------------------------------------------
	 * 
	 *  I N T E R F A C E   F U N C T I O N S
	 * 
	 *--------------------------------------------*/
    
	showPrefsDialog: function( ) {
		$('body').addClass('prefs_showing');
		$('#prefs_container').show();
		this.hideiPhoneAddressbar();
		if( Safari3 )
			setTimeout("$('div#prefs_container div.dialog_window').css('top', '0px');",10);
		this.updateButtonStates( );
	},

	hidePrefsDialog: function( )
	{
		$('body.prefs_showing').removeClass('prefs_showing');
		if (iPhone) {
			this.hideiPhoneAddressbar();
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
		
		$('div.download_location input')[0].value = prefs[RPC._DownloadDir];
		$('div.port input')[0].value              = prefs[RPC._PeerPort];
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
		var tr = this;
		var $element = $(event.target);
		
		// Figure out which menu has been clicked
		switch ($element.parent()[0].id) {
			
			// Display the preferences dialog
			case 'footer_super_menu':
				if ($element[0].id == 'preferences') {
					$('div#prefs_container div#pref_error').hide();
					$('div#prefs_container h2.dialog_heading').show();
					tr.showPrefsDialog( );
				}
				break;
			
			// Limit the download rate
			case 'footer_download_rate_menu':
				var args = { };
				var rate = ($element[0].innerHTML).replace(/[^0-9]/ig, '');
				if ($element.is('#unlimited_download_rate')) {
					$element.deselectMenuSiblings().selectMenuItem();
					args[RPC._DownSpeedLimited] = false;
				} else {
					setInnerHTML( $('#limited_download_rate')[0], 'Limit (' + rate + ' KB/s)' );
					$('#limited_download_rate').deselectMenuSiblings().selectMenuItem();
					$('div.preference input#download_rate')[0].value = rate;
					args[RPC._DownSpeedLimit] = parseInt( rate );
					args[RPC._DownSpeedLimited] = true;
				}
				$('div.preference input#limit_download')[0].checked = args[RPC._DownSpeedLimited];
				tr.remote.savePrefs( args );
				break;
			
			// Limit the upload rate
			case 'footer_upload_rate_menu':
				var args = { };
				var rate = ($element[0].innerHTML).replace(/[^0-9]/ig, '');
				if ($element.is('#unlimited_upload_rate')) {
					$element.deselectMenuSiblings().selectMenuItem();
					args[RPC._UpSpeedLimited] = false;
				} else {
					setInnerHTML( $('#limited_upload_rate')[0], 'Limit (' + rate + ' KB/s)' );
					$('#limited_upload_rate').deselectMenuSiblings().selectMenuItem();
					$('div.preference input#upload_rate')[0].value = rate;
					args[RPC._UpSpeedLimit] = parseInt( rate );
					args[RPC._UpSpeedLimited] = true;
				}
				$('div.preference input#limit_upload')[0].checked = args[RPC._UpSpeedLimited];
				tr.remote.savePrefs( args );
				break;
			
			// Sort the torrent list 
			case 'footer_sort_menu':

				// The 'reverse sort' option state can be toggled independently of the other options
				if ($element.is('#reverse_sort_order')) {
					var dir;
					if ($element.menuItemIsSelected()) {
						$element.deselectMenuItem();
						dir = Prefs._SortAscending;
					} else {
						$element.selectMenuItem();
						dir = Prefs._SortDescending;
					}
					tr.setSortDirection( dir );

				// Otherwise, deselect all other options (except reverse-sort) and select this one
				} else {
					$element.parent().find('span.selected').each( function() {
						if (! $element.parent().is('#reverse_sort_order')) {
							$element.parent().deselectMenuItem();
						}
					});
					$element.selectMenuItem();
					var method = $element[0].id.replace(/sort_by_/, '');
					tr.setSortMethod( method );
				}
				break;
		}
		$('#settings_menu').trigger('closemenu');
		return false; // to prevent the event from bubbling up
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
			this.hideInspector();
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
			this.updateVisibleFileLists();
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

		for(i = 0; i < torrents.length; ++i ) {
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
		setInnerHTML( $(ti+'name')[0], name );
		setInnerHTML( $(ti+'size')[0], torrents.length ? Math.formatBytes( total_size ) : 'N/A' );
		setInnerHTML( $(ti+'tracker')[0], total_tracker.replace(/\//g, '/&#8203;') );
		setInnerHTML( $(ti+'hash')[0], hash );
		setInnerHTML( $(ti+'state')[0], total_state );
		setInnerHTML( $(ti+'download_speed')[0], torrents.length ? Math.formatBytes( total_download_speed ) + '/s' : 'N/A' );
		setInnerHTML( $(ti+'upload_speed')[0], torrents.length ? Math.formatBytes( total_upload_speed ) + '/s' : 'N/A' );
		setInnerHTML( $(ti+'uploaded')[0], torrents.length ? Math.formatBytes( total_upload ) : 'N/A' );
		setInnerHTML( $(ti+'downloaded')[0], torrents.length ? Math.formatBytes( total_download ) : 'N/A' );
		setInnerHTML( $(ti+'ratio')[0], torrents.length ? Math.ratio( total_upload, total_download ) : 'N/A' );
		setInnerHTML( $(ti+'total_seeders')[0], torrents.length ? total_seeders : 'N/A' );
		setInnerHTML( $(ti+'total_leechers')[0], torrents.length ? total_leechers : 'N/A' );
		setInnerHTML( $(ti+'swarm_speed')[0], torrents.length ? Math.formatBytes(total_swarm_speed) + '/s' : 'N/A' );
		setInnerHTML( $(ti+'have')[0], torrents.length ? Math.formatBytes(total_completed) + ' (' + Math.formatBytes(total_verified) + ' verified)' : 'N/A' );
		setInnerHTML( $(ti+'upload_to')[0], torrents.length ? total_upload_peers : 'N/A' );
		setInnerHTML( $(ti+'download_from')[0], torrents.length ? total_download_peers : 'N/A' );
		setInnerHTML( $(ti+'secure')[0], private_string );
		setInnerHTML( $(ti+'creator_date')[0], date_created );
		setInnerHTML( $(ti+'progress')[0], torrents.length ? Math.ratio( sizeDone*100, sizeWhenDone ) + '%' : 'N/A' );
		setInnerHTML( $(ti+'comment')[0], comment.replace(/\//g, '/&#8203;') );
		setInnerHTML( $(ti+'creator')[0], creator );
		setInnerHTML( $(ti+'error')[0], error );
		
		$(".inspector_row > div:contains('N/A')").css('color', '#666');
		this.updateVisibleFileLists();
	},
	
	updateVisibleFileLists: function() {
		jQuery.each( this.getSelectedTorrents(), function() {
			this.showFileList();
		} );
		jQuery.each( this.getDeselectedTorrents(), function() {
			this.hideFileList();
		} );
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
			this.hideiPhoneAddressbar();
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
		var container_top = parseInt($('#torrent_container').position().top) + $('#torrent_filter_bar').height() + 1;
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

	refreshTorrents: function() {
		var tr = this;
		this.remote.getUpdatedDataFor('recently-active', function(active, removed){ tr.updateTorrentsData(active, removed); });
	},

	updateTorrentsData: function( active, removed_ids ) {
		var tr = this;
		var new_torrent_ids = [];
		var refresh_files_for = [];
		jQuery.each( active, function() {
			var t = Torrent.lookup(tr._torrents, this.id);
			if (t){
		    t.refresh(this);
		    if(t.isSelected())
		      refresh_files_for.push(t.id());
		  }
		  else
		    new_torrent_ids.push(this.id);
		} );

		if(refresh_files_for.length > 0)
			tr.remote.loadTorrentFiles( refresh_files_for );

		if(new_torrent_ids.length > 0)
			tr.remote.getInitialDataFor(new_torrent_ids, function(torrents){ tr.addTorrents(torrents) } );

		var removedAny = tr.deleteTorrents(removed_ids);

		if( ( new_torrent_ids.length != 0 ) || removedAny ) {
			tr.hideiPhoneAddressbar();
			tr.deselectAll( true );
		}

		this.refilter();
	},

	updateTorrentsFileData: function( torrents ){
		var tr = this;
		jQuery.each( torrents, function() {
			var t = Torrent.lookup(tr._torrents, this.id);
			if (t)
		    t.refreshFileData(this);
		} );
	},

	initalizeAllTorrents: function(){
		var tr = this;
		this.remote.getInitialDataFor( null ,function(torrents) { tr.addTorrents(torrents); } );
	},

	addTorrents: function( new_torrents ){
		var tr = this;

		$.each( new_torrents, function(){
			var torrent = this;
			tr._torrents.push( new Torrent( tr, torrent ) );
		});

		this.refilter();
	},

	deleteTorrents: function(torrent_ids){
		if(typeof torrent_ids == 'undefined')
			return false;
		var tr = this;
		var removedAny = false;
		$.each( torrent_ids, function(index, id){
			var torrent = Torrent.lookup(tr._torrents, id);

			if(torrent) {
				removedAny = true;
				var e = torrent.element();
				if( e ) {
					var row_index = tr.getTorrentIndex(tr._rows, torrent);
					delete e._torrent; //remove circular refernce to help IE garbage collect
					tr._rows.splice(row_index, 1)
					e.remove();
				}

				var pos = Torrent.indexOf( tr._torrents, torrent.id( ) );
				torrent.hideFileList();
				tr._torrents.splice( pos, 1 );
			}
		});

		return removedAny;
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
			if ('' != $('#torrent_upload_url').val()) {
				tr.remote.addTorrentByUrl($('#torrent_upload_url').val(), { paused: !this[Prefs._AutoStart] });
			} else {
				args.url = '/transmission/upload?paused=' + (this[Prefs._AutoStart] ? 'false' : 'true');
				args.type = 'POST';
				args.data = { 'X-Transmission-Session-Id' : tr.remote._token };
				args.dataType = 'xml';
				args.iframe = true;
				args.success = function( data ) {
					tr.refreshTorrents();
					tr.togglePeriodicRefresh( true );
				};
				tr.togglePeriodicRefresh( false );
				$('#torrent_upload_form').ajaxSubmit( args );
			}
		}
	},
   
	removeSelectedTorrents: function() {
		var torrents = this.getSelectedTorrents( );
		if( torrents.length )
			this.promptToRemoveTorrents( torrents );
	},

	removeSelectedTorrentsAndData: function() {
		var torrents = this.getSelectedTorrents( );
		if( torrents.length )
			this.promptToRemoveTorrentsAndData( torrents );
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

	promptToRemoveTorrentsAndData:function( torrents )
	{
		if( torrents.length == 1 )
		{
			var torrent = torrents[0],
				header = 'Remove ' + torrent.name() + ' and delete data?',
				message = 'All data downloaded for this torrent will be deleted. Are you sure you want to remove it?';
			dialog.confirm( header, message, 'Remove', 'transmission.removeTorrentsAndData', torrents );
		}
		else 
		{
			var header = 'Remove ' + torrents.length + ' transfers and delete data?',
				message = 'All data downloaded for these torrents will be deleted. Are you sure you want to remove them?';
			dialog.confirm( header, message, 'Remove', 'transmission.removeTorrentsAndData', torrents );
		}
	},

	removeTorrents: function( torrents ) {
		this.remote.removeTorrents( torrents );
	},

	removeTorrentsAndData: function( torrents ) {
		this.remote.removeTorrentsAndData( torrents );
	},

	verifySelectedTorrents: function() {
		this.verifyTorrents( this.getSelectedTorrents( ) );
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
	verifyTorrent: function( torrent ) {
		this.verifyTorrents( [ torrent ] );
	},
	verifyTorrents: function( torrents ) {
		this.remote.verifyTorrents( torrents );
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
	changeFileCommand: function(command, torrent, file) {
		this.remote.changeFileCommand(command, torrent, file)
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
			this.setEnabled( 'li#removedata', haveSelection );
			this.setEnabled( 'li#pause_all', haveActive );
			this.setEnabled( 'li#resume_all', havePaused );
		}
	}
};
