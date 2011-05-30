/*
 *	Copyright © Dave Perrett, Malcolm Jarvis and Bruno Bierbaumer
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
		// Initialize the helper classes
		this.remote = new TransmissionRemote(this);

		// Initialize the implementation fields
		this._current_search         = '';
		this._torrents               = { };
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
		$('#filter_all_link').parent().bind('click', function(e){ tr.showAllClicked(e); });
		$('#filter_active_link').parent().bind('click', function(e){ tr.showActiveClicked(e); });
		$('#filter_downloading_link').parent().bind('click', function(e){ tr.showDownloadingClicked(e); });
		$('#filter_seeding_link').parent().bind('click', function(e){ tr.showSeedingClicked(e); });
		$('#filter_paused_link').parent().bind('click', function(e){ tr.showPausedClicked(e); });
                $('#filter_finished_link').parent().bind('click', function(e){ tr.showFinishedClicked(e); });
		$('#prefs_save_button').bind('click', function(e) { tr.savePrefsClicked(e); return false;});
		$('#prefs_cancel_button').bind('click', function(e){ tr.cancelPrefsClicked(e); return false; });
		$('#block_update_button').bind('click', function(e){ tr.blocklistUpdateClicked(e); return false; });
		$('#stats_close_button').bind('click', function(e){ tr.closeStatsClicked(e); return false; });
		$('.inspector_tab').bind('click', function(e){ tr.inspectorTabClicked(e, this); });
		$('.file_wanted_control').live('click', function(e){ tr.fileWantedClicked(e, this); });
		$('.file_priority_control').live('click', function(e){ tr.filePriorityClicked(e, this); });
		$('#files_select_all').live('click', function(e){ tr.filesSelectAllClicked(e, this); });
		$('#files_deselect_all').live('click', function(e){ tr.filesDeselectAllClicked(e, this); });
		$('#open_link').bind('click', function(e){ tr.openTorrentClicked(e); });
		$('#upload_confirm_button').bind('click', function(e){ tr.confirmUploadClicked(e); return false;});
		$('#upload_cancel_button').bind('click', function(e){ tr.cancelUploadClicked(e); return false; });
		$('#turtle_button').bind('click', function(e){ tr.toggleTurtleClicked(e); return false; });
		$('#prefs_tab_general_tab').click(function(e){ changeTab(this, 'prefs_tab_general') });
		$('#prefs_tab_speed_tab').click(function(e){ changeTab(this, 'prefs_tab_speed') });
		$('#prefs_tab_peers_tab').click(function(e){ changeTab(this, 'prefs_tab_peers') });
		$('#prefs_tab_network_tab').click(function(e){ changeTab(this, 'prefs_tab_network');});
        $('#torrent_upload_form').submit(function(){ $('#upload_confirm_button').click(); return false; });
		$('#torrent_container').bind('dragover', function(e){ return tr.dragenter(e); });
		$('#torrent_container').bind('dragenter', function(e){ return tr.dragenter(e); });
		$('#torrent_container').bind('drop', function(e){ return tr.drop(e); });
		// tell jQuery to copy the dataTransfer property from events over if it exists
		jQuery.event.props.push("dataTransfer");

		$('#torrent_upload_form').submit(function(){ $('#upload_confirm_button').click(); return false; });

		if (iPhone) {
			$('#inspector_close').bind('click', function(e){ tr.hideInspector(); });
			$('#preferences_link').bind('click', function(e){ tr.releaseClutchPreferencesButton(e); });
		} else {
			$(document).bind('keydown',  function(e){ tr.keyDown(e); });
			$('#torrent_container').bind('click', function(e){ tr.deselectAll( true ); });
			$('#filter_toggle_link').bind('click', function(e){ tr.toggleFilterClicked(e); });
			$('#inspector_link').bind('click', function(e){ tr.toggleInspectorClicked(e); });

			this.setupSearchBox();
			this.createContextMenu();
			this.createSettingsMenu();
		}
		this.initTurtleDropDowns();

		this._torrent_list             = $('#torrent_list')[0];
		this._inspector_file_list      = $('#inspector_file_list')[0];
		this._inspector_peers_list     = $('#inspector_peers_list')[0];
		this._inspector_trackers_list  = $('#inspector_trackers_list')[0];
		this._inspector_tab_files      = $('#inspector_tab_files')[0];
		this._toolbar_buttons          = $('#torrent_global_menu ul li');
		this._toolbar_pause_button     = $('li#pause_selected')[0];
		this._toolbar_pause_all_button = $('li#pause_all')[0];
		this._toolbar_start_button     = $('li#resume_selected')[0];
		this._toolbar_start_all_button = $('li#resume_all')[0];
		this._toolbar_remove_button    = $('li#remove')[0];
		this._context_pause_button     = $('li#context_pause_selected')[0];
		this._context_start_button     = $('li#context_resume_selected')[0];

		var ti = '#torrent_inspector_';
		this._inspector = { };
		this._inspector._info_tab = { };
		this._inspector._info_tab.availability = $(ti+'availability')[0];
		this._inspector._info_tab.comment = $(ti+'comment')[0];
		this._inspector._info_tab.creator_date = $(ti+'creator_date')[0];
		this._inspector._info_tab.creator = $(ti+'creator')[0];
		this._inspector._info_tab.download_dir = $(ti+'download_dir')[0];
		this._inspector._info_tab.downloaded = $(ti+'downloaded')[0];
		this._inspector._info_tab.download_from = $(ti+'download_from')[0];
		this._inspector._info_tab.download_speed = $(ti+'download_speed')[0];
		this._inspector._info_tab.error = $(ti+'error')[0];
		this._inspector._info_tab.hash = $(ti+'hash')[0];
		this._inspector._info_tab.have = $(ti+'have')[0];
		this._inspector._info_tab.name = $(ti+'name')[0];
		this._inspector._info_tab.progress = $(ti+'progress')[0];
		this._inspector._info_tab.ratio = $(ti+'ratio')[0];
		this._inspector._info_tab.secure = $(ti+'secure')[0];
		this._inspector._info_tab.size = $(ti+'size')[0];
		this._inspector._info_tab.state = $(ti+'state')[0];
		this._inspector._info_tab.pieces = $(ti+'pieces')[0];
		this._inspector._info_tab.uploaded = $(ti+'uploaded')[0];
		this._inspector._info_tab.upload_speed = $(ti+'upload_speed')[0];
		this._inspector._info_tab.upload_to = $(ti+'upload_to')[0];

		// Setup the preference box
		this.setupPrefConstraints();

		// Setup the prefs gui
		this.initializeSettings( );

		// Get preferences & torrents from the daemon
		var tr = this;
		var async = false;
		this.loadDaemonPrefs( async );
		this.loadDaemonStats( async );
		this.initializeAllTorrents();

		this.togglePeriodicRefresh( true );
		this.togglePeriodicSessionRefresh( true );
	},

	loadDaemonPrefs: function( async ){
		var tr = this;
		this.remote.loadDaemonPrefs( function(data){
			var o = data.arguments;
			Prefs.getClutchPrefs( o );
			tr.updatePrefs( o );
		}, async );
	},

	loadDaemonStats: function( async ){
		var tr = this;
		this.remote.loadDaemonStats( function(data){
			var o = data.arguments;
			tr.updateStats( o );
		}, async );
	},
	checkPort: function( async ){
		$('#port_test').text('checking ...')
		var tr = this;
		this.remote.checkPort( function(data){
			var o = data.arguments;
			tr.updatePortStatus( o );
		}, async );		
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
		for( var i=0, row; row=arguments[i]; ++i )
			jQuery("<img>").attr("src", row);
	},

	/*
	 * Set up the preference validation
	 */
	setupPrefConstraints: function() {
		// only allow integers for speed limit & port options
		$('div.preference input[@type=text]:not(#download_location,#block_url)').blur( function() {
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

		if( !iPhone && this[Prefs._ShowInspector] )
			this.showInspector( );

		if( !iPhone && this[Prefs._CompactDisplayState] )
			$('#compact_view').selectMenuItem();
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
	contextReannounceSelected: function( ) {
		this.reannounceSelectedTorrents( );
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
			context_reannounce:        function(e){ tr.contextReannounceSelected(e); },
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
			boundingBottomPad: 5,
			onContextMenu:     function(e) {
				var closestRow = $(e.target).closest('.torrent')[0]._torrent;
				if(!closestRow.isSelected())
					tr.setSelectedTorrent( closestRow, true );
				return true;
			}
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


	initTurtleDropDowns: function() {
		var i, out, hour, mins, start, end, value, content;
		// Build the list of times
		out = "";
		start = $('#turtle_start_time')[0];
		end = $('#turtle_end_time')[0];
		for (i = 0; i < 24 * 4; i++) {
			hour = parseInt(i / 4);
			mins = ((i % 4) * 15);

			value = (i * 15);
			content = hour + ":" + (mins == 0 ? "00" : mins);
			start.options[i] = new Option(content, value);
			end.options[i]  = new Option(content, value);
		}
	},

	/*--------------------------------------------
	 *
	 *  U T I L I T I E S
	 *
	 *--------------------------------------------*/

	getAllTorrents: function()
	{
		var torrents = [];
		for(var key in this._torrents)
		  torrents.push(this._torrents[key]);
		return torrents;
	},

	getVisibleTorrents: function()
	{
		var torrents = [ ];
		for( var i=0, row; row=this._rows[i]; ++i )
			if( row._torrent && ( row[0].style.display != 'none' ) )
				torrents.push( row._torrent );
		return torrents;
	},

	getSelectedTorrents: function()
	{
		var v = this.getVisibleTorrents( );
		var s = [ ];
		for( var i=0, row; row=v[i]; ++i )
			if( row.isSelected( ) )
				s.push( row );
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
		for( var i=0, row; row=this._rows[i]; ++i )
			if( row[0].style.display != 'none' )
				rows.push( row );
		return rows;
	},

	getTorrentIndex: function( rows, torrent )
	{
		for( var i=0, row; row=rows[i]; ++i )
			if( row._torrent == torrent )
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

	seedRatioLimit: function(){
		if(this._prefs && this._prefs['seedRatioLimited'])
			return this._prefs['seedRatioLimit'];
		else
			return -1;
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
		e.addClass('selected');
		if( doUpdate )
			this.selectionChanged( );
	},
	selectRow: function( rowIndex, doUpdate ) {
		this.selectElement( this._rows[rowIndex], doUpdate );
	},
	selectTorrent: function( torrent, doUpdate ) {
		if( torrent._element )
			this.selectElement( torrent._element, doUpdate );
	},

	deselectElement: function( e, doUpdate ) {
		e.removeClass('selected');
		if( doUpdate )
			this.selectionChanged( );
	},
	deselectTorrent: function( torrent, doUpdate ) {
		if( torrent._element )
			this.deselectElement( torrent._element, doUpdate );
	},

	selectAll: function( doUpdate ) {
		var tr = this;
		for( var i=0, row; row=tr._rows[i]; ++i )
			tr.selectElement( row );
		if( doUpdate )
			tr.selectionChanged();
	},
	deselectAll: function( doUpdate ) {
		var tr = this;
		for( var i=0, row; row=tr._rows[i]; ++i )
			tr.deselectElement( row );
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
		this.updateSelectedData();
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
			tr.scrollToElement( tr._rows[i] );
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

	dragenter: function( event ) {
		if( event.dataTransfer && event.dataTransfer.types ) {
			var types = ["text/uri-list", "text/plain"];
			for( var i = 0; i < types.length; ++i ) {
				if( event.dataTransfer.types.contains(types[i]) ) {
					// it would be better to actually look at the links here;
					// sadly, (at least with Firefox,) trying would throw.
					event.stopPropagation();
					event.preventDefault();
					event.dropEffect = "copy";
					return false;
				}
			}
		}
		else if (event.dataTransfer) {
			event.dataTransfer.dropEffect = "none";
		}
		return true;
	},

	drop: function( event ) {
		if( !event.dataTransfer || !event.dataTransfer.types ) {
			return true;
		}
		event.preventDefault();
		var uris = null;
		var types = ["text/uri-list", "text/plain"];
		for( var i = 0; i < types.length; ++i ) {
			if( event.dataTransfer.types.contains(types[i]) ) {
				uris = event.dataTransfer.getData( types[i] ).split("\n");
				break;
			}
		}
		var paused = $('#prefs_form #auto_start')[0].checked;
		for( i = 0; i < uris.length; ++i ) {
			var uri = uris[i];
			if( /^#/.test(uri) ) {
				// lines which start with "#" are comments
				continue;
			}
			if( /^[a-z-]+:/i.test(uri) ) {
				// close enough to a url
				this.remote.addTorrentByUrl( uri, paused );
			}
		}
		return false;
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
		var rate = parseInt( $('#prefs_form #refresh_rate')[0].value );
		if( rate != tr[Prefs._RefreshRate] ) {
			tr.setPref( Prefs._RefreshRate, rate );
			tr.togglePeriodicRefresh( false );
			tr.togglePeriodicRefresh( true );
		}

		var up_bytes        = parseInt( $('#prefs_form #upload_rate'  )[0].value );
		var dn_bytes        = parseInt( $('#prefs_form #download_rate')[0].value );
		var turtle_up_bytes = parseInt( $('#prefs_form #turtle_upload_rate'  )[0].value );
		var turtle_dn_bytes = parseInt( $('#prefs_form #turtle_download_rate')[0].value );

		// pass the new prefs upstream to the RPC server
		var o = { };
		o[RPC._StartAddedTorrent]    = $('#prefs_form #auto_start')[0].checked;
		o[RPC._PeerPort]             = parseInt( $('#prefs_form #port')[0].value );
		o[RPC._UpSpeedLimit]         = up_bytes;
		o[RPC._DownSpeedLimit]       = dn_bytes;
		o[RPC._DownloadDir]          = $('#prefs_form #download_location')[0].value;
		o[RPC._UpSpeedLimited]       = $('#prefs_form #limit_upload'  )[0].checked;
		o[RPC._DownSpeedLimited]     = $('#prefs_form #limit_download')[0].checked;
		o[RPC._Encryption]           = $('#prefs_form #encryption')[0].checked
		                                   ? RPC._EncryptionRequired
		                                   : RPC._EncryptionPreferred;
		o[RPC._TurtleDownSpeedLimit] = turtle_dn_bytes;
		o[RPC._TurtleUpSpeedLimit]   = turtle_up_bytes;
		o[RPC._TurtleTimeEnabled]    = $('#prefs_form #turtle_schedule')[0].checked;
		o[RPC._TurtleTimeBegin]      = parseInt( $('#prefs_form #turtle_start_time').val() );
		o[RPC._TurtleTimeEnd]        = parseInt( $('#prefs_form #turtle_end_time').val() );
		o[RPC._TurtleTimeDay]        = parseInt( $('#prefs_form #turtle_days').val() );


		o[RPC._PeerLimitGlobal]      = parseInt( $('#prefs_form #conn_global').val() );
		o[RPC._PeerLimitPerTorrent]  = parseInt( $('#prefs_form #conn_torrent').val() );
		o[RPC._PexEnabled]           = $('#prefs_form #conn_pex')[0].checked;
		o[RPC._DhtEnabled]           = $('#prefs_form #conn_dht')[0].checked;
		o[RPC._LpdEnabled]           = $('#prefs_form #conn_lpd')[0].checked;
		o[RPC._BlocklistEnabled]     = $('#prefs_form #block_enable')[0].checked;
		o[RPC._BlocklistURL]         = $('#prefs_form #block_url').val();
		o[RPC._UtpEnabled]			 = $('#prefs_form #network_utp')[0].checked;
		o[RPC._PeerPortRandom]		 = $('#prefs_form #port_rand')[0].checked;
		o[RPC._PortForwardingEnabled]= $('#prefs_form #port_forward')[0].checked;

	

		tr.remote.savePrefs( o );

		tr.hidePrefsDialog( );
	},
	blocklistUpdateClicked: function(event){
		var tr = this;
		tr.remote.updateBlocklist();	
	},

	closeStatsClicked: function(event) {
		this.hideStatsDialog( );
	},

	removeClicked: function( event ) {
		var tr = this;
		if( tr.isButtonEnabled( event ) ) {
			tr.removeSelectedTorrents( );
			tr.hideiPhoneAddressbar( );
		}
	},

	toggleInspectorClicked: function( event ) {
		var tr = this;
		if( tr.isButtonEnabled( event ) )
			tr.toggleInspector( );
	},

	inspectorTabClicked: function(event, tab) {
		if (iPhone) event.stopPropagation();

		// Select the clicked tab, unselect the others,
		// and display the appropriate info
		var tab_ids = $(tab).parent('#inspector_tabs').find('.inspector_tab').map(
			function() { return $(this).attr('id'); }
		);
		for( var i=0, row; row=tab_ids[i]; ++i ) {
			if (tab.id == row) {
				$('#'+row).addClass('selected');
				$('#'+row+'_container').show();
			} else {
				$('#'+row).removeClass('selected');
				$('#'+row+'_container').hide();
			}
		}
		this.hideiPhoneAddressbar();

		this.updateVisibleFileLists();
		this.updatePeersLists();
		this.updateTrackersLists();
	},

	fileWantedClicked: function(event, element){
		this.extractFileFromElement(element).fileWantedControlClicked(event);
	},

	filePriorityClicked: function(event, element){
		this.extractFileFromElement(element).filePriorityControlClicked(event, element);
	},

	filesSelectAllClicked: function(event) {
		var tr = this;
		var ids = jQuery.map(this.getSelectedTorrents( ), function(t) { return t.id(); } );
		var files_list = this.toggleFilesWantedDisplay(ids, true);
		for (i = 0; i < ids.length; ++i) {
			if (files_list[i].length)
				this.remote.filesSelectAll( [ ids[i] ], files_list[i], function() { tr.refreshTorrents( ids ); } );
		}
	},

	filesDeselectAllClicked: function(event) {
		var tr = this;
		var ids = jQuery.map(this.getSelectedTorrents( ), function(t) { return t.id(); } );
		var files_list = this.toggleFilesWantedDisplay(ids, false);
		for (i = 0; i < ids.length; ++i) {
			if (files_list[i].length)
				this.remote.filesDeselectAll( [ ids[i] ], files_list[i], function() { tr.refreshTorrents( ids ); } );
		}
	},

	extractFileFromElement: function(element) {
		var match = $(element).closest('.inspector_torrent_file_list_entry').attr('id').match(/^t(\d+)f(\d+)$/);
		var torrent_id = match[1];
		var file_id = match[2];
		var torrent = this._torrents[torrent_id];
		return torrent._file_view[file_id];
	},

	toggleFilesWantedDisplay: function(ids, wanted) {
		var i, j, k, torrent, files_list = [ ];
		for (i = 0; i < ids.length; ++i) {
			torrent = this._torrents[ids[i]];
			files_list[i] = [ ];
			for (j = k = 0; j < torrent._file_view.length; ++j) {
				if (torrent._file_view[j].isEditable() && torrent._file_view[j]._wanted != wanted) {
					torrent._file_view[j].setWanted(wanted, false);
					files_list[i][k++] = j;
				}
			}
			torrent.refreshFileView;
		}
		return files_list;
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
			case Prefs._FilterActive:      c = '#filter_active_link'; break;
			case Prefs._FilterSeeding:     c = '#filter_seeding_link'; break;
			case Prefs._FilterDownloading: c = '#filter_downloading_link'; break;
			case Prefs._FilterPaused:      c = '#filter_paused_link'; break;
			case Prefs._FilterFinished:    c = '#filter_finished_link'; break;
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
	showActiveClicked: function( event ) {
		this.setFilter( Prefs._FilterActive );
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
	showFinishedClicked: function(event) {
		this.setFilter( Prefs._FilterFinished );
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
	 * Turn the periodic ajax torrents refresh on & off
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

	/*
	 * Turn the periodic ajax torrents refresh on & off for the selected torrents
	 */
	periodicTorrentUpdate: function( ids ) {
		var tr = this;
		if( ids ) {
			var curIds = this._extra_data_ids;
			if( curIds == null )
				curIds = [ ];
			if( ids.length == curIds.length ) {
				var duplicate = true;
				for(var i = 0; i < ids.length; i++ ) {
					if( ids[i] != curIds[i] ) {
						duplicate = false;
						break;
					}
				}
				if( duplicate ) return;
			}
			tr.refreshTorrents(ids);
			clearInterval(this._metadata_refresh);
			// sanity check
			if( !this[Prefs._RefreshRate] ) this[Prefs._RefreshRate] = 5;
			this._metadata_refresh = setInterval(function(){ tr.refreshTorrents(ids); }, this[Prefs._RefreshRate] * 1000 );
			this._extra_data_ids = ids;
		} else {
			clearInterval(this._metadata_refresh);
			this._metadata_refresh = null;
			this._extra_data_ids = null;
		}
	},

	/*
	 * Turn the periodic ajax session refresh on & off
	 */
	togglePeriodicSessionRefresh: function(state) {
		var tr = this;
		if (state && this._periodic_session_refresh == null) {
			// sanity check
			if( !this[Prefs._SessionRefreshRate] )
			     this[Prefs._SessionRefreshRate] = 5;
			remote = this.remote;
			this._periodic_session_refresh = setInterval(
				function(){ tr.loadDaemonPrefs(); }, this[Prefs._SessionRefreshRate] * 1000
			);
		} else {
			clearInterval(this._periodic_session_refresh);
			this._periodic_session_refresh = null;
		}
	},

	/*
	 * Turn the periodic ajax stats refresh on & off
	 */
	togglePeriodicStatsRefresh: function(state) {
		var tr = this;
		if (state && this._periodic_stats_refresh == null) {
			// sanity check
			if( !this[Prefs._SessionRefreshRate] )
			     this[Prefs._SessionRefreshRate] = 5;
			remote = this.remote;
			this._periodic_stats_refresh = setInterval(
				function(){ tr.loadDaemonStats(); }, this[Prefs._SessionRefreshRate] * 1000
			);
		} else {
			clearInterval(this._periodic_stats_refresh);
			this._periodic_stats_refresh = null;
		}
	},

	toggleTurtleClicked: function() {
		// Toggle the value
		this[Prefs._TurtleState] = !this[Prefs._TurtleState];
		// Store the result
		var args = { };
		args[RPC._TurtleState] = this[Prefs._TurtleState];
		this.remote.savePrefs( args );
	},

	updateSelectedData: function()
	{
		var ids = jQuery.map(this.getSelectedTorrents( ), function(t) { return t.id(); } );
		if( ids.length > 0 )
			this.periodicTorrentUpdate( ids );
		else
			this.periodicTorrentUpdate( false );
	},

	updateTurtleButton: function() {
		var t;
		var w = $('#turtle_button');
		if ( this[Prefs._TurtleState] ) {
			w.addClass('turtleEnabled');
			w.removeClass('turtleDisabled');
			t = [ 'Click to disable Temporary Speed Limits' ];
		} else {
			w.removeClass('turtleEnabled');
			w.addClass('turtleDisabled');
			t = [ 'Click to enable Temporary Speed Limits' ];
		}
		t.push( '(', Transmission.fmt.speed(this._prefs[RPC._TurtleUpSpeedLimit]), 'up,',
		             Transmission.fmt.speed(this._prefs[RPC._TurtleDownSpeedLimit]), 'down)' );
		w.attr( 'title', t.join(' ') );
	},

	/*--------------------------------------------
	 *
	 *  I N T E R F A C E   F U N C T I O N S
	 *
	 *--------------------------------------------*/

	showPrefsDialog: function( ) {
		this.checkPort(true);
		$('body').addClass('prefs_showing');
		$('#prefs_container').show();
		this.hideiPhoneAddressbar();
		if( Safari3 )
			setTimeout("$('div#prefs_container div.dialog_window').css('top', '0px');",10);
		this.updateButtonStates( );
		this.togglePeriodicSessionRefresh(false);
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
		this.togglePeriodicSessionRefresh(true);
	},

	/*
	 * Process got some new session data from the server
	 */
	updatePrefs: function( prefs )
	{
		// remember them for later
		this._prefs = prefs;

		var up_limited        = prefs[RPC._UpSpeedLimited];
		var dn_limited        = prefs[RPC._DownSpeedLimited];
		var up_limit_k        = prefs[RPC._UpSpeedLimit];
		var dn_limit_k        = prefs[RPC._DownSpeedLimit];
		var turtle_up_limit_k = prefs[RPC._TurtleUpSpeedLimit];
		var turtle_dn_limit_k = prefs[RPC._TurtleDownSpeedLimit];

                if( prefs.units )
                    Transmission.fmt.updateUnits( prefs.units );

		$('div.download_location input')[0].value = prefs[RPC._DownloadDir];
		$('div.port input')[0].value              = prefs[RPC._PeerPort];
		$('div.auto_start input')[0].checked      = prefs[RPC._StartAddedTorrent];
		$('input#limit_download')[0].checked      = dn_limited;
		$('input#download_rate')[0].value         = dn_limit_k;
		$('input#limit_upload')[0].checked        = up_limited;
		$('input#upload_rate')[0].value           = up_limit_k;
		$('input#refresh_rate')[0].value          = prefs[Prefs._RefreshRate];
		$('div.encryption input')[0].checked      = prefs[RPC._Encryption] == RPC._EncryptionRequired;
		$('input#turtle_download_rate')[0].value  = turtle_dn_limit_k;
		$('input#turtle_upload_rate')[0].value    = turtle_up_limit_k;
		$('input#turtle_schedule')[0].checked     = prefs[RPC._TurtleTimeEnabled];
		$('select#turtle_start_time').val(          prefs[RPC._TurtleTimeBegin] );
		$('select#turtle_end_time').val(            prefs[RPC._TurtleTimeEnd] );
		$('select#turtle_days').val(                prefs[RPC._TurtleTimeDay] );
		$('#transmission_version').text(            prefs[RPC._DaemonVersion] );

		$('#conn_global').val(						prefs[RPC._PeerLimitGlobal] );
		$('#conn_torrent').val(						prefs[RPC._PeerLimitPerTorrent] );
		$('#conn_pex')[0].checked				  = prefs[RPC._PexEnabled];
		$('#conn_dht')[0].checked				  = prefs[RPC._DhtEnabled];
		$('#conn_lpd')[0].checked				  = prefs[RPC._LpdEnabled];
		$('#block_enable')[0].checked			  = prefs[RPC._BlocklistEnabled];
		$('#block_url').val(					    prefs[RPC._BlocklistURL]);
		$('#block_size').text(					    prefs[RPC._BlocklistSize] + ' IP rules in the list' );
		$('#network_utp')[0].checked			  = prefs[RPC._UtpEnabled];
		$('#port_rand')[0].checked				  = prefs[RPC._PeerPortRandom];
		$('#port_forward')[0].checked			  = prefs[RPC._PortForwardingEnabled];


		if (!iPhone)
		{
			setInnerHTML( $('#limited_download_rate')[0], [ 'Limit (', Transmission.fmt.speed(dn_limit_k), ')' ].join('') );
			var key = dn_limited ? '#limited_download_rate'
			                       : '#unlimited_download_rate';
			$(key).deselectMenuSiblings().selectMenuItem();

			setInnerHTML( $('#limited_upload_rate')[0], [ 'Limit (', Transmission.fmt.speed(up_limit_k), ')' ].join('') );
			key = up_limited ? '#limited_upload_rate'
			                 : '#unlimited_upload_rate';
			$(key).deselectMenuSiblings().selectMenuItem();
		}

		this[Prefs._TurtleState] = prefs[RPC._TurtleState];
		this.updateTurtleButton();
	},

	updatePortStatus: function( status ){
		if(status['port-is-open'])
			$('#port_test').text('Port is open');
		else
			$('#port_test').text('Port is closed');
	},

	showStatsDialog: function( ) {
		this.loadDaemonStats();
		$('body').addClass('stats_showing');
		$('#stats_container').show();
		this.hideiPhoneAddressbar();
		if( Safari3 )
			setTimeout("$('div#stats_container div.dialog_window').css('top', '0px');",10);
		this.updateButtonStates( );
		this.togglePeriodicStatsRefresh(true);
	},

	hideStatsDialog: function( ){
		$('body.stats_showing').removeClass('stats_showing');
		if (iPhone) {
			this.hideiPhoneAddressbar();
			$('#stats_container').hide();
		} else if (Safari3) {
			$('div#stats_container div.dialog_window').css('top', '-425px');
			setTimeout("$('#stats_container').hide();",500);
		} else {
			$('#stats_container').hide();
		}
		this.updateButtonStates( );
		this.togglePeriodicStatsRefresh(false);
	},

	/*
	 * Process got some new session stats from the server
	 */
	updateStats: function( stats )
	{
		// can't think of a reason to remember this
		//this._stats = stats;

		var fmt = Transmission.fmt;
		var session = stats["current-stats"];
		var total = stats["cumulative-stats"];

		setInnerHTML( $('#stats_session_uploaded')[0], fmt.size(session["uploadedBytes"]) );
		setInnerHTML( $('#stats_session_downloaded')[0], fmt.size(session["downloadedBytes"]) );
		setInnerHTML( $('#stats_session_ratio')[0], fmt.ratioString(Math.ratio(session["uploadedBytes"],session["downloadedBytes"])));
		setInnerHTML( $('#stats_session_duration')[0], fmt.timeInterval(session["secondsActive"]) );
		setInnerHTML( $('#stats_total_count')[0], total["sessionCount"] + " times" );
		setInnerHTML( $('#stats_total_uploaded')[0], fmt.size(total["uploadedBytes"]) );
		setInnerHTML( $('#stats_total_downloaded')[0], fmt.size(total["downloadedBytes"]) );
		setInnerHTML( $('#stats_total_ratio')[0], fmt.ratioString(Math.ratio(total["uploadedBytes"],total["downloadedBytes"])));
		setInnerHTML( $('#stats_total_duration')[0], fmt.timeInterval(total["secondsActive"]) );
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
				else if ($element[0].id == 'statistics') {
					$('div#stats_container div#stats_error').hide();
					$('div#stats_container h2.dialog_heading').show();
					tr.showStatsDialog( );
				}
				else if ($element[0].id == 'compact_view') {
					this.setPref( Prefs._CompactDisplayState, !this[Prefs._CompactDisplayState])
					if(this[Prefs._CompactDisplayState])
						$element.selectMenuItem();
					else
						$element.deselectMenuItem();
					this.refreshDisplay( );
				}
				else if ($element[0].id == 'homepage') {
					window.open('http://www.transmissionbt.com/');
				}
				else if ($element[0].id == 'tipjar') {
					window.open('http://www.transmissionbt.com/donate.php');
				}
				break;

			// Limit the download rate
			case 'footer_download_rate_menu':
				var args = { };
				if ($element.is('#unlimited_download_rate')) {
					$element.deselectMenuSiblings().selectMenuItem();
					args[RPC._DownSpeedLimited] = false;
				} else {
					var rate_str = ($element[0].innerHTML).replace(/[^0-9]/ig, '');
					var rate_val = parseInt( rate_str );
					setInnerHTML( $('#limited_download_rate')[0], [ 'Limit (', Transmission.fmt.speed(rate_val), ')' ].join('') );
					$('#limited_download_rate').deselectMenuSiblings().selectMenuItem();
					$('div.preference input#download_rate')[0].value = rate_str;
					args[RPC._DownSpeedLimit] = rate_val;
					args[RPC._DownSpeedLimited] = true;
				}
				$('div.preference input#limit_download')[0].checked = args[RPC._DownSpeedLimited];
				tr.remote.savePrefs( args );
				break;

			// Limit the upload rate
			case 'footer_upload_rate_menu':
				var args = { };
				if ($element.is('#unlimited_upload_rate')) {
					$element.deselectMenuSiblings().selectMenuItem();
					args[RPC._UpSpeedLimited] = false;
				} else {
					var rate_str = ($element[0].innerHTML).replace(/[^0-9]/ig, '');
					var rate_val = parseInt( rate_str );
					setInnerHTML( $('#limited_upload_rate')[0], [ 'Limit (', Transmission.fmt.speed(rate_val), ')' ].join('')  );
					$('#limited_upload_rate').deselectMenuSiblings().selectMenuItem();
					$('div.preference input#upload_rate')[0].value = rate_str;
					args[RPC._UpSpeedLimit] = rate_val;
					args[RPC._UpSpeedLimited] = true;
				}
				$('div.preference input#limit_upload')[0].checked = args[RPC._UpSpeedLimited];
				tr.remote.savePrefs( args );
				break;

			// Sort the torrent list
			case 'footer_sort_menu':

				// The 'reverse sort' option state can be toggled independently of the other options
				if ($element.is('#reverse_sort_order')) {
					if(!$element.is('#reverse_sort_order.active')) break;
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
		var download_dir = 'N/A';
		var date_created = 'N/A';
		var error = 'None';
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
		var total_availability = 0;
		var total_have = 0;
		var total_size = 0;
		var total_state = [ ];
		var pieces = 'N/A';
		var total_upload = 0;
		var total_upload_peers = 0;
		var total_upload_speed = 0;
		var total_verified = 0;
		var na = 'N/A';
		var tab = this._inspector._info_tab;

		$("#torrent_inspector_size, .inspector_row div").css('color', '#222');

		if( torrents.length == 0 )
		{
			setInnerHTML( tab.name, 'No Selection' );
			setInnerHTML( tab.size, na );
			setInnerHTML( tab.pieces, na );
			setInnerHTML( tab.hash, na );
			setInnerHTML( tab.state, na );
			setInnerHTML( tab.download_speed, na );
			setInnerHTML( tab.upload_speed, na );
			setInnerHTML( tab.uploaded, na );
			setInnerHTML( tab.downloaded, na );
			setInnerHTML( tab.availability, na );
			setInnerHTML( tab.ratio, na );
			setInnerHTML( tab.have, na );
			setInnerHTML( tab.upload_to, na );
			setInnerHTML( tab.download_from, na );
			setInnerHTML( tab.secure, na );
			setInnerHTML( tab.creator_date, na );
			setInnerHTML( tab.progress, na );
			setInnerHTML( tab.comment, na );
			setInnerHTML( tab.creator, na );
			setInnerHTML( tab.download_dir, na );
			setInnerHTML( tab.error, na );
			this.updateVisibleFileLists();
			this.updatePeersLists();
			this.updateTrackersLists();
			$("#torrent_inspector_size, .inspector_row > div:contains('N/A')").css('color', '#666');
			return;
		}

		name = torrents.length == 1
			? torrents[0].name()
			: torrents.length+' Transfers Selected';

		if( torrents.length == 1 )
		{
			var t = torrents[0];
			var err = t.getErrorMessage( );
			if( err )
				error = err;
			if( t._comment)
				comment = t._comment ;
			if( t._creator )
				creator = t._creator ;
			if( t._download_dir)
				download_dir = t._download_dir;

			hash = t.hash();
			pieces = [ t._pieceCount, 'pieces @', Transmission.fmt.mem(t._pieceSize) ].join(' ');
			date_created = Transmission.fmt.timestamp( t._creator_date );
		}

		for( var i=0, t; t=torrents[i]; ++i ) {
			sizeWhenDone         += t._sizeWhenDone;
			sizeDone             += t._sizeWhenDone - t._leftUntilDone;
			total_completed      += t.completed();
			total_verified       += t._verified;
			total_size           += t.size();
			total_upload         += t.uploadTotal();
			total_download       += t.downloadTotal();
			total_upload_speed   += t.uploadSpeed();
			total_download_speed += t.downloadSpeed();
			total_upload_peers   += t.peersGettingFromUs();
			total_download_peers += t.peersSendingToUs();
			total_availability   += t._sizeWhenDone - t._leftUntilDone + t._desiredAvailable;

			var s = t.stateStr();
			if( total_state.indexOf( s ) == -1 )
				total_state.push( s );

			if( t._is_private )
				have_private = true;
			else
				have_public = true;
		}

		var private_string = '';
		var fmt = Transmission.fmt;
		if( have_private && have_public ) private_string = 'Mixed';
		else if( have_private ) private_string = 'Private Torrent';
		else if( have_public ) private_string = 'Public Torrent';

		setInnerHTML( tab.name, name );
		setInnerHTML( tab.size, torrents.length ? fmt.size( total_size ) : na );
		setInnerHTML( tab.pieces, pieces );
		setInnerHTML( tab.hash, hash );
		setInnerHTML( tab.state, total_state.join('/') );
		setInnerHTML( tab.download_speed, torrents.length ? fmt.speedBps( total_download_speed ) : na );
		setInnerHTML( tab.upload_speed, torrents.length ? fmt.speedBps( total_upload_speed ) : na );
		setInnerHTML( tab.uploaded, torrents.length ? fmt.size( total_upload ) : na );
		setInnerHTML( tab.downloaded, torrents.length ? fmt.size( total_download ) : na );
		setInnerHTML( tab.availability, torrents.length ? fmt.percentString(Math.ratio( total_availability*100, sizeWhenDone )) + '%' : na );
		setInnerHTML( tab.ratio, torrents.length ? fmt.ratioString(Math.ratio( total_upload, total_download )) : na );
		setInnerHTML( tab.have, torrents.length ? fmt.size(total_completed) + ' (' + fmt.size(total_verified) + ' verified)' : na );
		setInnerHTML( tab.upload_to, torrents.length ? total_upload_peers : na );
		setInnerHTML( tab.download_from, torrents.length ? total_download_peers : na );
		setInnerHTML( tab.secure, private_string );
		setInnerHTML( tab.creator_date, date_created );
		setInnerHTML( tab.progress, torrents.length ? fmt.percentString(Math.ratio( sizeDone*100, sizeWhenDone )) + '%' : na );
		setInnerHTML( tab.comment, comment == na ? comment : comment.replace(/\//g, '/&#8203;') );
		setInnerHTML( tab.creator, creator );
		setInnerHTML( tab.download_dir, download_dir == na ? download_dir : download_dir.replace(/([\/_\.])/g, "$1&#8203;") );
		setInnerHTML( tab.error, error );

		this.updatePeersLists();
		this.updateTrackersLists();
		$(".inspector_row > div:contains('N/A')").css('color', '#666');
		this.updateVisibleFileLists();
	},

	fileListIsVisible: function() {
		return this._inspector_tab_files.className.indexOf('selected') != -1;
	},

	updateVisibleFileLists: function() {
		if( this.fileListIsVisible( ) === true ) {
			var selected = this.getSelectedTorrents();
			jQuery.each( selected, function() { this.showFileList(); } );
			jQuery.each( this.getDeselectedTorrents(), function() { this.hideFileList(); } );
			// Check if we need to display the select all buttions
			if ( !selected.length ) {
				if ( $("#select_all_button_container").is(':visible') )
					$("#select_all_button_container").hide();
			} else {
				if ( !$("#select_all_button_container").is(':visible') )
					$("#select_all_button_container").show();
			}
		}
	},

	updatePeersLists: function() {
		var tr = this;
		var html = [ ];
		var fmt = Transmission.fmt;
		var torrents = this.getSelectedTorrents( );
		if( $(this._inspector_peers_list).is(':visible') ) {
			for( var k=0, torrent; torrent=torrents[k]; ++k ) {
				html.push( '<div class="inspector_group">' );
				if( torrents.length > 1 ) {
					html.push( '<div class="inspector_torrent_label">', torrent._name, '</div>' );
				}
				if( torrent._peers.length == 0 ) {
					html.push( '<br></div>' ); // firefox won't paint the top border if the div is empty
					continue;
				}
				html.push( '<table class="peer_list">',
				           '<tr class="inspector_peer_entry even">',
				           '<th class="encryptedCol"></th>',
				           '<th class="upCol">Up</th>',
				           '<th class="downCol">Down</th>',
				           '<th class="percentCol">%</th>',
				           '<th class="statusCol">Status</th>',
				           '<th class="addressCol">Address</th>',
				           '<th class="clientCol">Client</th>',
				           '</tr>' );
				for( var i=0, peer; peer=torrent._peers[i]; ++i ) {
					var parity = ((i+1) % 2 == 0 ? 'even' : 'odd');
					html.push( '<tr class="inspector_peer_entry ', parity, '">',
					           '<td>', (peer.isEncrypted ? '<img src="images/graphics/lock_icon.png" alt="Encrypted"/>' : ''), '</td>',
					           '<td>', ( peer.rateToPeer ? fmt.speedBps(peer.rateToPeer) : '' ), '</td>',
					           '<td>', ( peer.rateToClient ? fmt.speedBps(peer.rateToClient) : '' ), '</td>',
					           '<td class="percentCol">', Math.floor(peer.progress*100), '%', '</td>',
					           '<td>', peer.flagStr, '</td>',
					           '<td>', peer.address, '</td>',
					           '<td class="clientCol">', peer.clientName, '</td>',
					           '</tr>' );
				}
				html.push( '</table></div>' );
			}
		}
		setInnerHTML(this._inspector_peers_list, html.join('') );
	},

	updateTrackersLists: function() {
		// By building up the HTML as as string, then have the browser
		// turn this into a DOM tree, this is a fast operation.
		var tr = this;
		var html = [ ];
		var na = 'N/A';
		var torrents = this.getSelectedTorrents( );
		if( $(this._inspector_trackers_list).is(':visible') ) {
			for( var k=0, torrent; torrent = torrents[k]; ++k ) {
				html.push( '<div class="inspector_group">' );
				if( torrents.length > 1 ) {
					html.push( '<div class="inspector_torrent_label">', torrent._name, '</div>' );
				}
				for( var i=0, tier; tier=torrent._trackerStats[i]; ++i ) {
					html.push( '<div class="inspector_group_label">',
					           'Tier ', (i + 1), '</div>',
					           '<ul class="tier_list">' );
					for( var j=0, tracker; tracker=tier[j]; ++j ) {
						var lastAnnounceStatusHash = tr.lastAnnounceStatus(tracker);
						var announceState = tr.announceState(tracker);
						var lastScrapeStatusHash = tr.lastScrapeStatus(tracker);

						// Display construction
						var parity = ((j+1) % 2 == 0 ? 'even' : 'odd');
						html.push( '<li class="inspector_tracker_entry ', parity, '"><div class="tracker_host" title="', tracker.announce, '">',
						           tracker.host, '</div>',
						           '<div class="tracker_activity">',
						           '<div>', lastAnnounceStatusHash['label'], ': ', lastAnnounceStatusHash['value'], '</div>',
						           '<div>', announceState, '</div>',
						           '<div>', lastScrapeStatusHash['label'], ': ', lastScrapeStatusHash['value'], '</div>',
						           '</div><table class="tracker_stats">',
						           '<tr><th>Seeders:</th><td>', (tracker.seederCount > -1 ? tracker.seederCount : na), '</td></tr>',
						           '<tr><th>Leechers:</th><td>', (tracker.leecherCount > -1 ? tracker.leecherCount : na), '</td></tr>',
						           '<tr><th>Downloads:</th><td>', (tracker.downloadCount > -1 ? tracker.downloadCount : na), '</td></tr>',
						           '</table></li>' );
					}
					html.push( '</ul>' );
				}
				html.push( '</div>' );
			}
		}
		setInnerHTML(this._inspector_trackers_list, html.join(''));
	},

	lastAnnounceStatus: function(tracker){
		var lastAnnounceLabel = 'Last Announce';
		var lastAnnounce = [ 'N/A' ];
		if (tracker.hasAnnounced) {
			var lastAnnounceTime = Transmission.fmt.timestamp(tracker.lastAnnounceTime);
			if (tracker.lastAnnounceSucceeded) {
				lastAnnounce = [ lastAnnounceTime, ' (got ',  Transmission.fmt.plural(tracker.lastAnnouncePeerCount, 'peer'), ')' ];
			} else {
				lastAnnounceLabel = 'Announce error';
				lastAnnounce = [ (tracker.lastAnnounceResult ? (tracker.lastAnnounceResult + ' - ') : ''), lastAnnounceTime ];
			}
		}
		return { 'label':lastAnnounceLabel, 'value':lastAnnounce.join('') };
	},

	announceState: function(tracker){
		var announceState = '';
		switch (tracker.announceState) {
			case Torrent._TrackerActive:
				announceState = 'Announce in progress';
				break;
			case Torrent._TrackerWaiting:
				var timeUntilAnnounce = tracker.nextAnnounceTime - ((new Date()).getTime() / 1000);
				if(timeUntilAnnounce < 0){
					timeUntilAnnounce = 0;
				}
				announceState = 'Next announce in ' + Transmission.fmt.timeInterval(timeUntilAnnounce);
				break;
			case Torrent._TrackerQueued:
				announceState = 'Announce is queued';
				break;
			case Torrent._TrackerInactive:
				announceState = tracker.isBackup ?
					'Tracker will be used as a backup' :
					'Announce not scheduled';
				break;
			default:
				announceState = 'unknown announce state: ' + tracker.announceState;
		}
		return announceState;
	},

	lastScrapeStatus: function(tracker){
		var lastScrapeLabel = 'Last Scrape';
		var lastScrape = 'N/A';
		if (tracker.hasScraped) {
			var lastScrapeTime = Transmission.fmt.timestamp(tracker.lastScrapeTime);
			if (tracker.lastScrapeSucceeded) {
				lastScrape = lastScrapeTime;
			} else {
				lastScrapeLabel = 'Scrape error';
				lastScrape = (tracker.lastScrapeResult ? tracker.lastScrapeResult + ' - ' : '') + lastScrapeTime;
			}
		}
		return {'label':lastScrapeLabel, 'value':lastScrape}
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
			$('#inspector_close').show();
			this.hideiPhoneAddressbar();
		} else {
			var w = $('#torrent_inspector').width() + 1 + 'px';
			$('#torrent_filter_bar')[0].style.right = w;
			$('#torrent_container')[0].style.right = w;
		}

		setInnerHTML( $('ul li#context_toggle_inspector')[0], 'Hide Inspector' );

		this.setPref( Prefs._ShowInspector, true );
		this.updateInspector( );
		this.refreshDisplay( );
	},

	/*
	 * Hide the inspector
	 */
	hideInspector: function() {

		$('#torrent_inspector').hide();

		if (iPhone) {
			this.deselectAll( );
			$('body.inspector_showing').removeClass('inspector_showing');
			$('#inspector_close').hide();
			this.hideiPhoneAddressbar();
		} else {
			$('#torrent_filter_bar')[0].style.right = '0px';
			$('#torrent_container')[0].style.right = '0px';
			setInnerHTML( $('ul li#context_toggle_inspector')[0], 'Show Inspector' );
		}

		this.setPref( Prefs._ShowInspector, false );
		this.refreshDisplay( );
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

	refreshMetaData: function(ids) {
		var tr = this;
		this.remote.getMetaDataFor(ids, function(active, removed){ tr.updateMetaData(active); });
	},

	updateMetaData: function( torrents )
	{
		var tr = this;
		var refresh_files_for = [ ];
		jQuery.each( torrents, function( ) {
			var t = tr._torrents[ this.id ];
			if( t ) {
				t.refreshMetaData( this );
				if( t.isSelected( ) )
					refresh_files_for.push( t.id( ) );
			}
		} );
		if( refresh_files_for.length > 0 )
			tr.remote.loadTorrentFiles( refresh_files_for );
	},

	refreshTorrents: function(ids) {
		var tr = this;
		if (!ids)
			ids = 'recently-active';

		this.remote.getUpdatedDataFor(ids, function(active, removed){ tr.updateTorrentsData(active, removed); });
	},

	updateTorrentsData: function( updated, removed_ids ) {
		var tr = this;
		var new_torrent_ids = [];
		var refresh_files_for = [];
		jQuery.each( updated, function() {
			var t = tr._torrents[this.id];
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
		var listIsVisible = tr.fileListIsVisible( );
		jQuery.each( torrents, function() {
			var t = tr._torrents[this.id];
			if (t) {
				t.refreshFileModel(this);
				if( listIsVisible && t.isSelected())
					t.refreshFileView();
			}
		} );
	},

	initializeAllTorrents: function(){
		var tr = this;
		this.remote.getInitialDataFor( null ,function(torrents) { tr.addTorrents(torrents); } );
	},

	addTorrents: function( new_torrents )
	{
		var transferFragment = document.createDocumentFragment( );
		var fileFragment = document.createDocumentFragment( );

		for( var i=0, row; row=new_torrents[i]; ++i ) {
			var new_torrent = new Torrent( transferFragment, fileFragment, this, row );
			this._torrents[new_torrent.id()] = new_torrent;
		}

		this._inspector_file_list.appendChild( fileFragment );
		this._torrent_list.appendChild( transferFragment );

		this.refilter( );
	},

	deleteTorrents: function(torrent_ids){
		if(typeof torrent_ids == 'undefined')
			return false;
		var tr = this;
		var removedAny = false;
		$.each( torrent_ids, function(index, id){
			var torrent = tr._torrents[id];

			if(torrent) {
				removedAny = true;
				var e = torrent.element();
				if( e ) {
					var row_index;
					for( var i=0, row; row = tr._rows[i]; ++i ) {
						if( row._id == torrent._id )
						{
							row_index = i;
							e = tr._rows[row_index];
							break;
						}
					}
					delete e._torrent; //remove circular refernce to help IE garbage collect
					tr._rows.splice(row_index, 1)
					e.remove();
				}

				torrent.hideFileList();
				torrent.deleteFiles();
				delete tr._torrents[torrent.id()];
			}
		});

		return removedAny;
	},

	refreshDisplay: function( )
	{
		var torrents = this.getVisibleTorrents();
		for( var i=0; torrents[i]; ++i )
			torrents[i].refreshHTML();
	},

	/*
	 * Set the alternating background colors for torrents
	 */
	setTorrentBgColors: function( )
	{
		var rows = this.getVisibleRows( );
		for( var i=0, row; row=rows[i]; ++i ) {
			var wasEven = row[0].className.indexOf('even') != -1;
			var isEven = ((i+1) % 2 == 0);
			if( wasEven != isEven )
				row.toggleClass('even', isEven);
		}
	},

	updateStatusbar: function()
	{
		var torrents = this.getAllTorrents();
		var torrentCount = torrents.length;
		var visibleCount = this.getVisibleTorrents().length;

		// calculate the overall speed
		var upSpeed = 0;
		var downSpeed = 0;
		for( var i=0, row; row=torrents[i]; ++i ) {
			upSpeed += row.uploadSpeed( );
			downSpeed += row.downloadSpeed( );
		}

		// update torrent count label
		var s;
		if( torrentCount == visibleCount )
			s = torrentCount + ' Transfers';
		else
			s = visibleCount + ' of ' + torrentCount + ' Transfers';
		setInnerHTML( $('#torrent_global_transfer')[0], s );

		// update the speeds
		s = Transmission.fmt.speedBps( upSpeed );
		if( iPhone ) s = 'UL: ' + s;
		setInnerHTML( $('#torrent_global_upload')[0], s );

		// download speeds
		s = Transmission.fmt.speedBps( downSpeed );
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
				$('input#torrent_auto_start').attr('checked', $('#prefs_form #auto_start')[0].checked);
				$('#upload_container').show();
                $('#torrent_upload_url').focus();
			if (!iPhone && Safari3) {
				setTimeout("$('div#upload_container div.dialog_window').css('top', '0px');",10);
			}

		// Submit the upload form
		} else {
			var tr = this;
			var args = { };
			var paused = !$('#torrent_auto_start').is(':checked');
			if ('' != $('#torrent_upload_url').val()) {
				tr.remote.addTorrentByUrl($('#torrent_upload_url').val(), { paused: paused });
			} else {
				args.url = '../upload?paused=' + paused;
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
		var torrent_ids = jQuery.map(torrents, function(t) { return t.id(); } );
		var tr = this;
		this.remote.removeTorrents( torrent_ids, function(){ tr.refreshTorrents() } );
	},

	removeTorrentsAndData: function( torrents ) {
		this.remote.removeTorrentsAndData( torrents );
	},

	verifySelectedTorrents: function() {
		this.verifyTorrents( this.getSelectedTorrents( ) );
	},

	reannounceSelectedTorrents: function() {
		this.reannounceTorrents( this.getSelectedTorrents( ) );
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
		var torrent_ids = jQuery.map(torrents, function(t) { return t.id(); } );
		var tr = this;
		this.remote.startTorrents( torrent_ids, function(){ tr.refreshTorrents(torrent_ids) } );
	},
	verifyTorrent: function( torrent ) {
		this.verifyTorrents( [ torrent ] );
	},
	verifyTorrents: function( torrents ) {
		var torrent_ids = jQuery.map(torrents, function(t) { return t.id(); } );
		var tr = this;
		this.remote.verifyTorrents( torrent_ids, function(){ tr.refreshTorrents(torrent_ids) } );
	},

	reannounceTorrent: function( torrent ) {
		this.reannounceTorrents( [ torrent ] );
	},
	reannounceTorrents: function( torrents ) {
		var torrent_ids = jQuery.map(torrents, function(t) { return t.id(); } );
		var tr = this;
		this.remote.reannounceTorrents( torrent_ids, function(){ tr.refreshTorrents(torrent_ids) } );
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
		var torrent_ids = jQuery.map(torrents, function(t) { return t.id(); } );
		var tr = this;
		this.remote.stopTorrents( torrent_ids,	function(){ tr.refreshTorrents(torrent_ids )} );
	},
	changeFileCommand: function(command, torrent, file) {
		this.remote.changeFileCommand(command, torrent, file)
	},

	hideiPhoneAddressbar: function(timeInSeconds) {
		var tr = this;
		if( iPhone ) {
			var delayLength = timeInSeconds ? timeInSeconds*1000 : 150;
			// not currently supported on iPhone
			if(/*document.body.scrollTop!=1 && */scroll_timeout==null) {
				scroll_timeout = setTimeout(function(){ tr.doToolbarHide(); }, delayLength);
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
		for( var i=0, t; t=allTorrents[i]; ++i )
			if( t.test( this[Prefs._FilterMode], this._current_search ) )
				keep.push( t );

		// sort the keepers
		Torrent.sortTorrents( keep, this[Prefs._SortMethod],
		                            this[Prefs._SortDirection] );

		// make a backup of the selection
		var sel = this.getSelectedTorrents( );
		this.deselectAll( );

		// hide the ones we're not keeping
		for( var i=keep.length, e; e=this._rows[i]; ++i ) {
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
		this.refreshDisplay( );
	},

	setEnabled: function( key, flag )
	{
		$(key).toggleClass( 'disabled', !flag );
	},

	updateButtonStates: function()
	{
		var showing_dialog = new RegExp("(prefs_showing|dialog_showing|open_showing)").test(document.body.className);
		this._toolbar_buttons.toggleClass( 'disabled', showing_dialog );

		if (!showing_dialog)
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

			this.setEnabled( this._toolbar_pause_button, haveActiveSelection );
			this.setEnabled( this._context_pause_button, haveActiveSelection );
			this.setEnabled( this._toolbar_start_button, havePausedSelection );
			this.setEnabled( this._context_start_button, havePausedSelection );
			this.setEnabled( this._toolbar_remove_button, haveSelection );
			this.setEnabled( this._toolbar_pause_all_button, haveActive );
			this.setEnabled( this._toolbar_start_all_button, havePaused );
		}
	}
};
