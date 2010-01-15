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
		$('#filter_downloading_link').parent().bind('click', function(e){ tr.showDownloadingClicked(e); });
		$('#filter_seeding_link').parent().bind('click', function(e){ tr.showSeedingClicked(e); });
		$('#filter_paused_link').parent().bind('click', function(e){ tr.showPausedClicked(e); });
		$('#prefs_save_button').bind('click', function(e) { tr.savePrefsClicked(e); return false;});
		$('#prefs_cancel_button').bind('click', function(e){ tr.cancelPrefsClicked(e); return false; });
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

		if( !iPhone && this[Prefs._ShowInspector] )
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
			boundingBottomPad: 5,
			onContextMenu:     function(e) {
				tr.setSelectedTorrent( $(e.target).closest('.torrent')[0]._torrent, true );
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
		this.scrollToElement( e );
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
		o[RPC._PeerPort]             = parseInt( $('#prefs_form #port')[0].value );
		o[RPC._UpSpeedLimit]         = parseInt( $('#prefs_form #upload_rate')[0].value );
		o[RPC._DownSpeedLimit]       = parseInt( $('#prefs_form #download_rate')[0].value );
		o[RPC._DownloadDir]          = $('#prefs_form #download_location')[0].value;
		o[RPC._UpSpeedLimited]       = $('#prefs_form #limit_upload')[0].checked;
		o[RPC._DownSpeedLimited]     = $('#prefs_form #limit_download')[0].checked;
		o[RPC._Encryption]           = $('#prefs_form #encryption')[0].checked
		                                   ? RPC._EncryptionRequired
		                                   : RPC._EncryptionPreferred;
		o[RPC._TurtleDownSpeedLimit] = parseInt( $('#prefs_form #turtle_download_rate')[0].value );
		o[RPC._TurtleUpSpeedLimit]   = parseInt( $('#prefs_form #turtle_upload_rate')[0].value );
		o[RPC._TurtleTimeEnabled]    = $('#prefs_form #turtle_schedule')[0].checked;
		o[RPC._TurtleTimeBegin]      = parseInt( $('#prefs_form #turtle_start_time').val() );
		o[RPC._TurtleTimeEnd]        = parseInt( $('#prefs_form #turtle_end_time').val() );
		o[RPC._TurtleTimeDay]        = parseInt( $('#prefs_form #turtle_days').val() );

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

	toggleTurtleClicked: function() {
		// Toggle the value
		this[Prefs._TurtleState] = !this[Prefs._TurtleState];
		// Store the result
		var args = { };
		args[RPC._TurtleState] = this[Prefs._TurtleState];
		this.remote.savePrefs( args );
	},

	updateTurtleButton: function() {
		if ( this[Prefs._TurtleState] ) {
			$('#turtle_button').addClass('turtleEnabled');
			$('#turtle_button').removeClass('turtleDisabled');
		} else {
			$('#turtle_button').removeClass('turtleEnabled');
			$('#turtle_button').addClass('turtleDisabled');
		}
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

		var down_limit    = prefs[RPC._DownSpeedLimit];
		var down_limited  = prefs[RPC._DownSpeedLimited];
		var up_limit      = prefs[RPC._UpSpeedLimit];
		var up_limited    = prefs[RPC._UpSpeedLimited];

		$('div.download_location input')[0].value = prefs[RPC._DownloadDir];
		$('div.port input')[0].value              = prefs[RPC._PeerPort];
		$('div.auto_start input')[0].checked      = prefs[Prefs._AutoStart];
		$('input#limit_download')[0].checked      = down_limited;
		$('input#download_rate')[0].value         = down_limit;
		$('input#limit_upload')[0].checked        = up_limited;
		$('input#upload_rate')[0].value           = up_limit;
		$('input#refresh_rate')[0].value          = prefs[Prefs._RefreshRate];
		$('div.encryption input')[0].checked      = prefs[RPC._Encryption] == RPC._EncryptionRequired;
		$('input#turtle_download_rate')[0].value  = prefs[RPC._TurtleDownSpeedLimit];
		$('input#turtle_upload_rate')[0].value    = prefs[RPC._TurtleUpSpeedLimit];
		$('input#turtle_schedule')[0].checked     = prefs[RPC._TurtleTimeEnabled];
		$('select#turtle_start_time').val(          prefs[RPC._TurtleTimeBegin] );
		$('select#turtle_end_time').val(            prefs[RPC._TurtleTimeEnd] );
		$('select#turtle_days').val(                prefs[RPC._TurtleTimeDay] );
		$('#transmission_version').text(            prefs[RPC._DaemonVersion] );

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

		this[Prefs._TurtleState] = prefs[RPC._TurtleState];
		this.updateTurtleButton();
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
		var download_dir = 'N/A';
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
		var total_size = 0;
		var total_state = null;
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
			pieces = t._pieceCount + ', ' + Math.formatBytes(t._pieceSize);
			date_created = Math.formatTimestamp( t._creator_date );
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
			if( total_state == null )
				total_state = t.stateStr();
			else if ( total_state.search ( t.stateStr() ) == -1 )
				total_state += '/' + t.stateStr();
			if( t._is_private )
				have_private = true;
			else
				have_public = true;
		}

		var private_string = '';
		if( have_private && have_public ) private_string = 'Mixed';
		else if( have_private ) private_string = 'Private Torrent';
		else if( have_public ) private_string = 'Public Torrent';	

		setInnerHTML( tab.name, name );
		setInnerHTML( tab.size, torrents.length ? Math.formatBytes( total_size ) : na );
		setInnerHTML( tab.pieces, pieces );
		setInnerHTML( tab.hash, hash );
		setInnerHTML( tab.state, total_state );
		setInnerHTML( tab.download_speed, torrents.length ? Math.formatBytes( total_download_speed ) + '/s' : na );
		setInnerHTML( tab.upload_speed, torrents.length ? Math.formatBytes( total_upload_speed ) + '/s' : na );
		setInnerHTML( tab.uploaded, torrents.length ? Math.formatBytes( total_upload ) : na );
		setInnerHTML( tab.downloaded, torrents.length ? Math.formatBytes( total_download ) : na );
		setInnerHTML( tab.ratio, torrents.length ? Math.ratio( total_upload, total_download ) : na );
		setInnerHTML( tab.have, torrents.length ? Math.formatBytes(total_completed) + ' (' + Math.formatBytes(total_verified) + ' verified)' : na );
		setInnerHTML( tab.upload_to, torrents.length ? total_upload_peers : na );
		setInnerHTML( tab.download_from, torrents.length ? total_download_peers : na );
		setInnerHTML( tab.secure, private_string );
		setInnerHTML( tab.creator_date, date_created );
		setInnerHTML( tab.progress, torrents.length ? Math.ratio( sizeDone*100, sizeWhenDone ) + '%' : na );
		setInnerHTML( tab.comment, comment == na ? comment : comment.replace(/\//g, '/&#8203;') );
		setInnerHTML( tab.creator, creator );
		setInnerHTML( tab.download_dir, download_dir == na ? download_dir : download_dir.replace(/([\/_\.])/g, "$1&#8203;") );
		setInnerHTML( tab.error, error );
		
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
					var row_index = tr.getTorrentIndex(tr._rows, torrent);
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
				$('input#torrent_auto_start').attr('checked', this[Prefs._AutoStart]);
				$('#upload_container').show();
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
				args.url = '/transmission/upload?paused=' + paused;
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
