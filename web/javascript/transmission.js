/*
 *	Copyright © Dave Perrett, Malcolm Jarvis and Bruno Bierbaumer
 *	This code is licensed under the GPL version 2.
 *	For details, see http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 *
 * Class Transmission
 */

function Transmission()
{
	this.initialize();
}

Transmission.prototype =
{
	/****
	*****
	*****  STARTUP
	*****
	****/

	initialize: function()
	{
		// Initialize the helper classes
		this.remote = new TransmissionRemote(this);

		// Initialize the implementation fields
		this._current_search         = '';
		this._torrents               = { };
		this._rows                   = [ ];

		// Initialize the clutch preferences
		Prefs.getClutchPrefs(this);

		this.preloadImages();

		// Set up user events
		var tr = this;
		$(".numberinput").forceNumeric();
		$('#pause_all_link').click(function(e) { tr.stopAllClicked(e); });
		$('#resume_all_link').click(function(e) { tr.startAllClicked(e); });
		$('#pause_selected_link').click(function(e) { tr.stopSelectedClicked(e); });
		$('#resume_selected_link').click(function(e) { tr.startSelectedClicked(e); });
		$('#remove_link').click(function(e) { tr.removeClicked(e); });
		$('#prefs_save_button').click(function(e) { tr.savePrefsClicked(e); return false;});
		$('#prefs_cancel_button').click(function(e) { tr.cancelPrefsClicked(e); return false; });
		$('#block_update_button').click(function(e) { tr.blocklistUpdateClicked(e); return false; });
		$('#stats_close_button').click(function(e) { tr.closeStatsClicked(e); return false; });
		$('.inspector_tab').click(function(e) { tr.inspectorTabClicked(e, this); });
		$('#files_select_all').live('click', function(e) { tr.filesSelectAllClicked(e, this); });
		$('#files_deselect_all').live('click', function(e) { tr.filesDeselectAllClicked(e, this); });
		$('#open_link').click(function(e) { tr.openTorrentClicked(e); });
		$('#upload_confirm_button').click(function(e) { tr.confirmUploadClicked(e); return false;});
		$('#upload_cancel_button').click(function(e) { tr.cancelUploadClicked(e); return false; });
		$('#turtle_button').click(function() { tr.toggleTurtleClicked(); return false; });
		$('#compact-button').click(function() { tr.toggleCompactClicked(); return false; });
		$('#prefs_tab_general_tab').click(function() { changeTab(this, 'prefs_tab_general'); });
		$('#prefs_tab_speed_tab').click(function() { changeTab(this, 'prefs_tab_speed'); });
		$('#prefs_tab_peers_tab').click(function() { changeTab(this, 'prefs_tab_peers'); });
		$('#prefs_tab_network_tab').click(function() { changeTab(this, 'prefs_tab_network');});
		$('#torrent_upload_form').submit(function() { $('#upload_confirm_button').click(); return false; });
		$('#torrent_container').bind('dragover', function(e) { return tr.dragenter(e); });
		$('#torrent_container').bind('dragenter', function(e) { return tr.dragenter(e); });
		$('#torrent_container').bind('drop', function(e) { return tr.drop(e); });
		// tell jQuery to copy the dataTransfer property from events over if it exists
		jQuery.event.props.push("dataTransfer");

		$('#torrent_upload_form').submit(function() { $('#upload_confirm_button').click(); return false; });

		if (iPhone) {
			$('#inspector_close').bind('click', function() { tr.hideInspector(); });
			$('#preferences_link').bind('click', function(e) { tr.releaseClutchPreferencesButton(e); });
		} else {
			$(document).bind('keydown',  function(e) { tr.keyDown(e); });
			$('#torrent_container').click(function() { tr.deselectAll(true); });
			$('#inspector_link').click(function(e) { tr.toggleInspectorClicked(e); });

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
		this._toolbar_buttons          = $('#toolbar ul li');
		this._toolbar_pause_button     = $('#toolbar #pause_selected')[0];
		this._toolbar_pause_all_button = $('#toolbar #pause_all')[0];
		this._toolbar_start_button     = $('#toolbar #resume_selected')[0];
		this._toolbar_start_all_button = $('#toolbar #resume_all')[0];
		this._toolbar_remove_button    = $('#toolbar #remove')[0];
		this._context_pause_button     = $('li#context_pause_selected')[0];
		this._context_start_button     = $('li#context_resume_selected')[0];
		this._context_start_now_button = $('li#context_resume_now_selected')[0];
		this._context_move_top         = $('li#context_move_top')[0];
		this._context_move_up          = $('li#context_move_up')[0];
		this._context_move_down        = $('li#context_move_down')[0];
		this._context_move_bottom      = $('li#context_move_bottom')[0];

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

		// Setup the prefs gui
		this.initializeSettings();

		// Get preferences & torrents from the daemon
		var async = false;
		this.loadDaemonPrefs(async);
		this.loadDaemonStats(async);
		this.initializeAllTorrents();

		this.togglePeriodicRefresh(true);
		this.togglePeriodicSessionRefresh(true);

		this.filterSetup();
	},

	loadDaemonPrefs: function(async) {
		var tr = this;
		this.remote.loadDaemonPrefs(function(data) {
			var o = data['arguments'];
			Prefs.getClutchPrefs(o);
			tr.updatePrefs(o);
		}, async);
	},

	loadDaemonStats: function(async) {
		var tr = this;
		this.remote.loadDaemonStats(function(data) {
			tr.updateStats(data['arguments']);
		}, async);
	},
	checkPort: function(async) {
		$('#port_test').text('checking ...');
		var tr = this;
		this.remote.checkPort(function(data) {
			tr.updatePortStatus(data['arguments']);
		}, async);
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
		for (var i=0, row; row=arguments[i]; ++i)
			jQuery("<img>").attr("src", row);
	},

	/*
	 * Load the clutch prefs and init the GUI according to those prefs
	 */
	initializeSettings: function()
	{
		Prefs.getClutchPrefs(this);

		// iPhone conditions in the section allow us to not
		// include transmenu js to save some bandwidth; if we
		// start using prefs on iPhone we need to weed
		// transmenu refs out of that too.

		if (!iPhone) $('#sort_by_' + this[Prefs._SortMethod]).selectMenuItem();

		if (!iPhone && (this[Prefs._SortDirection] == Prefs._SortDescending))
			$('#reverse_sort_order').selectMenuItem();

		if (!iPhone && this[Prefs._ShowInspector])
			this.showInspector();

		this.initCompactMode();
	},

	/*
	 * Set up the search box
	 */
	setupSearchBox: function()
	{
		var tr = this;
		var search_box = $('#torrent_search');
		search_box.bind('keyup click', function() {tr.setSearch(this.value);});
		if (!$.browser.safari)
		{
			search_box.addClass('blur');
			search_box[0].value = 'Filter';
			search_box.bind('blur', function() {
				if (this.value == '') {
					$(this).addClass('blur');
					this.value = 'Filter';
					tr.setSearch(null);
				}
			}).bind('focus', function() {
				if ($(this).is('.blur')) {
					this.value = '';
					$(this).removeClass('blur');
				}
			});
		}
	},

	/*
	 * Create the torrent right-click menu
	 */
	createContextMenu: function() {
		var tr = this;
		var bindings = {
			context_pause_selected:       function() { tr.stopSelectedTorrents(); },
			context_resume_selected:      function() { tr.startSelectedTorrents(false); },
			context_resume_now_selected:  function() { tr.startSelectedTorrents(true); },
			context_remove:               function() { tr.removeSelectedTorrents(); },
			context_removedata:           function() { tr.removeSelectedTorrentsAndData(); },
			context_verify:               function() { tr.verifySelectedTorrents(); },
			context_reannounce:           function() { tr.reannounceSelectedTorrents(); },
			context_toggle_inspector:     function() { tr.toggleInspector(); },
			context_select_all:           function() { tr.selectAll(); },
			context_deselect_all:         function() { tr.deselectAll(); },
			context_move_top:             function() { tr.moveTop(); },
			context_move_up:              function() { tr.moveUp(); },
			context_move_down:            function() { tr.moveDown(); },
			context_move_bottom:          function() { tr.moveBottom(); }
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
				var closest_row = $(e.target).closest('.torrent')[0];
				for (var i=0, row; row = tr._rows[i]; ++i) {
					if (row.getElement() === closest_row) {
						tr.setSelectedRow(row);
						break;
					}
				}
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
			onClick: function(e) { return tr.processSettingsMenuEvent(e); }
		});

		$('#unlimited_download_rate').selectMenuItem();
		$('#unlimited_upload_rate').selectMenuItem();
	},


	initTurtleDropDowns: function() {
		var i, hour, mins, start, end, value, content;
		// Build the list of times
		start = $('#turtle_start_time')[0];
		end = $('#turtle_end_time')[0];
		for (i = 0; i < 24 * 4; i++) {
			hour = parseInt(i / 4, 10);
			mins = ((i % 4) * 15);

			value = (i * 15);
			content = hour + ":" + (mins == 0 ? "00" : mins);
			start.options[i] = new Option(content, value);
			end.options[i]  = new Option(content, value);
		}
	},

	/****
	*****
	*****  UTILITIES
	*****
	****/

	getAllTorrents: function()
	{
		var torrents = [];
		for (var key in this._torrents)
		  torrents.push(this._torrents[key]);
		return torrents;
	},

	getVisibleTorrents: function()
	{
		var torrents = [];
		for (var i=0, row; row=this._rows[i]; ++i)
			torrents.push(row.getTorrent());
		return torrents;
	},

	scrollToRow: function(row)
	{
		if (iPhone) // FIXME: why?
			return;

		var list = $('#torrent_container');
		var scrollTop = list.scrollTop();
		var innerHeight = list.innerHeight();

		var e = $(row.getElement());
		var offsetTop = e[0].offsetTop;
		var offsetHeight = e.outerHeight();

		if (offsetTop < scrollTop)
			list.scrollTop(offsetTop);
		else if (innerHeight + scrollTop < offsetTop + offsetHeight)
			list.scrollTop(offsetTop + offsetHeight - innerHeight);
	},

	seedRatioLimit: function() {
		if (this._prefs && this._prefs['seedRatioLimited'])
			return this._prefs['seedRatioLimit'];
		else
			return -1;
	},

	setPref: function(key, val)
	{
		this[key] = val;
		Prefs.setValue(key, val);
	},

	/****
	*****
	*****  SELECTION
	*****
	****/

	getSelectedRows: function() {
		var s = [];
		for (var i=0, row; row=this._rows[i]; ++i)
			if (row.isSelected())
				s.push(row);
		return s;
	},

	getSelectedTorrents: function() {
		var s = this.getSelectedRows();
		for (var i=0, row; row=s[i]; ++i)
			s[i] = s[i].getTorrent();
		return s;
	},

	getSelectedTorrentIds: function() {
		var s = [];
		for (var i=0, row; row=this._rows[i]; ++i)
			if (row.isSelected())
				s.push(row.getTorrent().getId());
		return s;
	},

	setSelectedRow: function(row) {
		var rows = this.getSelectedRows();
		for (var i=0, r; r=rows[i]; ++i)
			this.deselectRow(r);
		this.selectRow(row);
	},

	selectRow: function(row) {
		row.setSelected(true);
		this.callSelectionChangedSoon();
	},

	deselectRow: function(row) {
		row.setSelected(false);
		this.callSelectionChangedSoon();
	},

	selectAll: function() {
		for (var i=0, row; row=this._rows[i]; ++i)
			this.selectRow(row);
		this.callSelectionChangedSoon();
	},
	deselectAll: function() {
		for (var i=0, row; row=this._rows[i]; ++i)
			this.deselectRow(row);
		this.callSelectionChangedSoon();
		delete this._last_torrent_clicked;
	},

	/* Select a range from this torrent to the last clicked torrent */
	selectRange: function(row)
	{
		if (!this._last_torrent_clicked) {
			this.selectRow(row);
		} else { // select the range between the prevous & current

			var prev = null;
			var next = null;
			for (var i=0, r; r=this._rows[i]; ++i) {
				if (r.getTorrent().getId() === this._last_torrent_clicked)
					prev = i;
				if (r === row)
					next = i;
			}
			if ((prev!==null) && (next!==null)) {
				var min = Math.min(prev, next);
				var max = Math.max(prev, next);
				for (i=min; i<=max; ++i)
					this.selectRow(this._rows[i]);
			}
		}

		//this._last_row_clicked = row
		this.callSelectionChangedSoon();
	},

	selectionChanged: function()
	{
		this.updateButtonStates();
		this.updateInspector();
		this.updateSelectedData();

		clearTimeout(this.selectionChangedTimer);
		delete this.selectionChangedTimer;
	},

	callSelectionChangedSoon: function()
	{
		if (!this.selectionChangedTimer)
		{
			var tr = this;
			this.selectionChangedTimer = setTimeout(function() {tr.selectionChanged();},200);
		}
	},

	/*--------------------------------------------
	 *
	 *  E V E N T   F U N C T I O N S
	 *
	 *--------------------------------------------*/

	/*
	 * Process key event
	 */
	keyDown: function(ev)
	{
		var up = ev.keyCode === 38; // up key pressed
		var dn = ev.keyCode === 40; // down key pressed

		if (up || dn)
		{
			var rows = this._rows;

			// find the first selected row
			for (var i=0, row; row=rows[i]; ++i)
				if (row.isSelected())
					break;

			if (i == rows.length) // no selection yet
				i = 0;
			else if (dn)
				i = (i+1) % rows.length;
			else if (up)
				i = (i || rows.length) - 1;

			this.setSelectedRow(rows[i]);
			this.scrollToRow(rows[i]);
		}
	},

	isButtonEnabled: function(e) {
		var p = e.target ? e.target.parentNode : e.srcElement.parentNode;
		return p.className!='disabled' && p.parentNode.className!='disabled';
	},

	stopAllClicked: function(event) {
		if (this.isButtonEnabled(event)) {
			this.stopAllTorrents();
			this.hideiPhoneAddressbar();
		}
	},

	stopSelectedClicked: function(event) {
		if (this.isButtonEnabled(event)) {
			this.stopSelectedTorrents();
			this.hideiPhoneAddressbar();
		}
	},

	startAllClicked: function(event) {
		if (this.isButtonEnabled(event)) {
			this.startAllTorrents();
			this.hideiPhoneAddressbar();
		}
	},

	startSelectedClicked: function(event) {
		if (this.isButtonEnabled(event)) {
			this.startSelectedTorrents(false);
			this.hideiPhoneAddressbar();
		}
	},

	openTorrentClicked: function(event) {
		if (this.isButtonEnabled(event)) {
			$('body').addClass('open_showing');
			this.uploadTorrentFile();
			this.updateButtonStates();
		}
	},

	dragenter: function(event) {
		if (event.dataTransfer && event.dataTransfer.types) {
			var types = ["text/uri-list", "text/plain"];
			for (var i = 0; i < types.length; ++i) {
				if (event.dataTransfer.types.contains(types[i])) {
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

	drop: function(ev) {
		if (!ev.dataTransfer || !ev.dataTransfer.types) {
			return true;
		}
		ev.preventDefault();
		var uris = null;
		var types = ["text/uri-list", "text/plain"];
		for (var i = 0; i < types.length; ++i) {
			if (ev.dataTransfer.types.contains(types[i])) {
				uris = ev.dataTransfer.getData(types[i]).split("\n");
				break;
			}
		}
		var paused = $('#prefs_form #auto_start')[0].checked;
		for (i = 0; i < uris.length; ++i) {
			var uri = uris[i];
			if (/^#/.test(uri)) {
				// lines which start with "#" are comments
				continue;
			}
			if (/^[a-z-]+:/i.test(uri)) {
				// close enough to a url
				this.remote.addTorrentByUrl(uri, paused);
			}
		}
		return false;
	},

	hideUploadDialog: function() {
		$('body.open_showing').removeClass('open_showing');
		if (!iPhone && Safari3) {
			$('div#upload_container div.dialog_window').css('top', '-205px');
			setTimeout("$('#upload_container').hide();",500);
		} else {
			$('#upload_container').hide();
		}
		this.updateButtonStates();
	},

	cancelUploadClicked: function() {
		this.hideUploadDialog();
	},

	confirmUploadClicked: function() {
		this.uploadTorrentFile(true);
		this.hideUploadDialog();
	},

	cancelPrefsClicked: function() {
		this.hidePrefsDialog();
	},

	savePrefsClicked: function()
	{
		// handle the clutch prefs locally
		var tr = this;
		var rate = parseInt ($('#prefs_form #refresh_rate')[0].value, 10);
		if (rate != tr[Prefs._RefreshRate]) {
			tr.setPref (Prefs._RefreshRate, rate);
			tr.togglePeriodicRefresh (true);
		}

		var up_bytes        = parseInt($('#prefs_form #upload_rate').val(), 10);
		var dn_bytes        = parseInt($('#prefs_form #download_rate').val(), 10);
		var turtle_up_bytes = parseInt($('#prefs_form #turtle_upload_rate').val(), 10);
		var turtle_dn_bytes = parseInt($('#prefs_form #turtle_download_rate').val(), 10);

		// pass the new prefs upstream to the RPC server
		var o = { };
		o[RPC._StartAddedTorrent]    = $('#prefs_form #auto_start')[0].checked;
		o[RPC._PeerPort]             = parseInt($('#prefs_form #port').val(), 10);
		o[RPC._UpSpeedLimit]         = up_bytes;
		o[RPC._DownSpeedLimit]       = dn_bytes;
		o[RPC._DownloadDir]          = $('#prefs_form #download_location').val();
		o[RPC._UpSpeedLimited]       = $('#prefs_form #limit_upload').prop('checked');
		o[RPC._DownSpeedLimited]     = $('#prefs_form #limit_download').prop('checked');
		o[RPC._Encryption]           = $('#prefs_form #encryption').prop('checked')
		                                   ? RPC._EncryptionRequired
		                                   : RPC._EncryptionPreferred;
		o[RPC._TurtleDownSpeedLimit] = turtle_dn_bytes;
		o[RPC._TurtleUpSpeedLimit]   = turtle_up_bytes;
		o[RPC._TurtleTimeEnabled]    = $('#prefs_form #turtle_schedule').prop('checked');
		o[RPC._TurtleTimeBegin]      = parseInt($('#prefs_form #turtle_start_time').val(), 10);
		o[RPC._TurtleTimeEnd]        = parseInt($('#prefs_form #turtle_end_time').val(), 10);
		o[RPC._TurtleTimeDay]        = parseInt($('#prefs_form #turtle_days').val(), 10);


		o[RPC._PeerLimitGlobal]      = parseInt($('#prefs_form #conn_global').val(), 10);
		o[RPC._PeerLimitPerTorrent]  = parseInt($('#prefs_form #conn_torrent').val(), 10);
		o[RPC._PexEnabled]           = $('#prefs_form #conn_pex').prop('checked');
		o[RPC._DhtEnabled]           = $('#prefs_form #conn_dht').prop('checked');
		o[RPC._LpdEnabled]           = $('#prefs_form #conn_lpd').prop('checked');
		o[RPC._BlocklistEnabled]     = $('#prefs_form #block_enable').prop('checked');
		o[RPC._BlocklistURL]         = $('#prefs_form #block_url').val();
		o[RPC._UtpEnabled]           = $('#prefs_form #network_utp').prop('checked');
		o[RPC._PeerPortRandom]       = $('#prefs_form #port_rand').prop('checked');
		o[RPC._PortForwardingEnabled]= $('#prefs_form #port_forward').prop('checked');

		tr.remote.savePrefs(o);

		tr.hidePrefsDialog();
	},
	blocklistUpdateClicked: function() {
		var tr = this;
		tr.remote.updateBlocklist();
	},

	closeStatsClicked: function() {
		this.hideStatsDialog();
	},

	removeClicked: function(ev) {
		var tr = this;
		if (tr.isButtonEnabled(ev)) {
			tr.removeSelectedTorrents();
			tr.hideiPhoneAddressbar();
		}
	},

	toggleInspectorClicked: function(ev) {
		var tr = this;
		if (tr.isButtonEnabled(ev))
			tr.toggleInspector();
	},

	inspectorTabClicked: function(ev, tab) {
		if (iPhone) ev.stopPropagation();

		// select this tab and deselect the others
		$(tab).addClass('selected').siblings().removeClass('selected');

		// show this tab and hide the others
		$('#'+tab.id+'_container').show().siblings('.inspector_container').hide();

		this.hideiPhoneAddressbar();
		this.updatePeersLists();
		this.updateTrackersLists();
		this.updateFileList();
	},

	filesSelectAllClicked: function() {
		var t = this._files_torrent;
		if (t)
			this.toggleFilesWantedDisplay(t, true);
	},
	filesDeselectAllClicked: function() {
		var t = this._files_torrent;
		if (t)
			this.toggleFilesWantedDisplay(t, false);
	},
	toggleFilesWantedDisplay: function(torrent, wanted) {
		var rows = [ ];
		for (var i=0, row; row=this._files[i]; ++i)
			if (row.isEditable() && (torrent._files[i].wanted !== wanted))
				rows.push(row);
		if (rows.length > 1) {
			var command = wanted ? 'files-wanted' : 'files-unwanted';
			this.changeFileCommand(command, rows);
		}
	},

	/*
	 * 'Clutch Preferences' was clicked (iPhone only)
	 */
	releaseClutchPreferencesButton: function() {
		$('div#prefs_container div#pref_error').hide();
		$('div#prefs_container h2.dialog_heading').show();
		this.showPrefsDialog();
	},

	getIntervalMsec: function(key, min)
	{
		var interval = this[key];
		if (!interval || (interval < min))
			interval = min;
		return interval * 1000;
	},

	/* Turn the periodic ajax torrents refresh on & off */
	togglePeriodicRefresh: function (enabled) {
		clearInterval (this._periodic_refresh);
		delete this._periodic_refresh;
		if (enabled) {
			var tr = this;
			var msec = this.getIntervalMsec(Prefs._RefreshRate, 3);
			this._periodic_refresh = setInterval(function() {tr.refreshTorrents();}, msec);
		}
	},

	/* Turn the periodic ajax torrents refresh on & off for the selected torrents */
	periodicTorrentUpdate: function(ids) {
		clearInterval (this._metadata_refresh);
		delete this._metadata_refresh;
		delete this._extra_data_ids;
		if (ids && ids.length) {
			this.refreshTorrents(ids);
			this._extra_data_ids = ids;
			var tr = this;
			var msec = this.getIntervalMsec(Prefs._RefreshRate, 3);
			this._metadata_refresh = setInterval(function() { tr.refreshTorrents(ids);}, msec);
		}
	},

	/* Turn the periodic ajax session refresh on & off */
	togglePeriodicSessionRefresh: function(enabled) {
		clearInterval(this._periodic_session_refresh);
		delete this._periodic_session_refresh;
		if (enabled) {
			var tr = this;
			var msec = this.getIntervalMsec(Prefs._SessionRefreshRate, 5);
			this._periodic_session_refresh = setInterval(function() {tr.loadDaemonPrefs();}, msec);
		}
	},

	/* Turn the periodic ajax stats refresh on & off */
	togglePeriodicStatsRefresh: function(enabled) {
		clearInterval(this._periodic_stats_refresh);
		delete this._periodic_stats_refresh;
		if (enabled) {
			var tr = this;
			var msec = this.getIntervalMsec(Prefs._SessionRefreshRate, 5);
			this._periodic_stats_refresh = setInterval(function() {tr.loadDaemonStats();}, msec);
		}
	},

	toggleTurtleClicked: function()
	{
		// toggle it
		var p = Prefs._TurtleState;
		this[p] = !this[p];

		// send it to the session
		var args = { };
		args[RPC._TurtleState] = this[p];
		this.remote.savePrefs(args);
	},

	updateSelectedData: function()
	{
		var ids = this.getSelectedTorrentIds();
		if (ids.length > 0)
			this.periodicTorrentUpdate(ids);
		else
			this.periodicTorrentUpdate(false);
	},

	updateTurtleButton: function() {
		var t;
		var w = $('#turtle_button');
		if (this[Prefs._TurtleState]) {
			w.addClass('turtleEnabled');
			w.removeClass('turtleDisabled');
			t = [ 'Click to disable Temporary Speed Limits' ];
		} else {
			w.removeClass('turtleEnabled');
			w.addClass('turtleDisabled');
			t = [ 'Click to enable Temporary Speed Limits' ];
		}
		t.push('(', Transmission.fmt.speed(this._prefs[RPC._TurtleUpSpeedLimit]), 'up,',
		             Transmission.fmt.speed(this._prefs[RPC._TurtleDownSpeedLimit]), 'down)');
		w.attr('title', t.join(' '));
	},

	/*--------------------------------------------
	 *
	 *  I N T E R F A C E   F U N C T I O N S
	 *
	 *--------------------------------------------*/

	showPrefsDialog: function() {
		this.checkPort(true);
		$('body').addClass('prefs_showing');
		$('#prefs_container').show();
		this.hideiPhoneAddressbar();
		if (Safari3)
			setTimeout("$('div#prefs_container div.dialog_window').css('top', '0px');",10);
		this.updateButtonStates();
		this.togglePeriodicSessionRefresh(false);
	},

	hidePrefsDialog: function()
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
		this.updateButtonStates();
		this.togglePeriodicSessionRefresh(true);
	},

	/*
	 * Process got some new session data from the server
	 */
	updatePrefs: function(p)
	{
		// remember them for later
		this._prefs = p;

		var up_limited        = p[RPC._UpSpeedLimited];
		var dn_limited        = p[RPC._DownSpeedLimited];
		var up_limit_k        = p[RPC._UpSpeedLimit];
		var dn_limit_k        = p[RPC._DownSpeedLimit];
		var turtle_up_limit_k = p[RPC._TurtleUpSpeedLimit];
		var turtle_dn_limit_k = p[RPC._TurtleDownSpeedLimit];

                if (p.units)
                    Transmission.fmt.updateUnits(p.units);

		$('div.download_location input').val(      p[RPC._DownloadDir]);
		$('div.port input').val(                   p[RPC._PeerPort]);
		$('div.auto_start input').prop('checked',  p[RPC._StartAddedTorrent]);
		$('input#limit_download').prop('checked',  dn_limited);
		$('input#download_rate').val(              dn_limit_k);
		$('input#limit_upload').prop('checked',    up_limited);
		$('input#upload_rate').val(                up_limit_k);
		$('input#refresh_rate').val(               p[Prefs._RefreshRate]);
		$('div.encryption input').val(             p[RPC._Encryption] == RPC._EncryptionRequired);
		$('input#turtle_download_rate').val(       turtle_dn_limit_k);
		$('input#turtle_upload_rate').val(         turtle_up_limit_k);
		$('input#turtle_schedule').prop('checked', p[RPC._TurtleTimeEnabled]);
		$('select#turtle_start_time').val(         p[RPC._TurtleTimeBegin]);
		$('select#turtle_end_time').val(           p[RPC._TurtleTimeEnd]);
		$('select#turtle_days').val(               p[RPC._TurtleTimeDay]);
		$('#transmission_version').text(           p[RPC._DaemonVersion]);
		$('#conn_global').val(                     p[RPC._PeerLimitGlobal]);
		$('#conn_torrent').val(                    p[RPC._PeerLimitPerTorrent]);
		$('#conn_pex').prop('checked',             p[RPC._PexEnabled]);
		$('#conn_dht').prop('checked',             p[RPC._DhtEnabled]);
		$('#conn_lpd').prop('checked',             p[RPC._LpdEnabled]);
		$('#block_enable').prop('checked',         p[RPC._BlocklistEnabled]);
		$('#block_url').val(                       p[RPC._BlocklistURL]);
		$('#block_size').text(                     p[RPC._BlocklistSize]+' IP rules in the list');
		$('#network_utp').prop('checked',          p[RPC._UtpEnabled]);
		$('#port_rand').prop('checked',            p[RPC._PeerPortRandom]);
		$('#port_forward').prop('checked',         p[RPC._PortForwardingEnabled]);

		if (!iPhone)
		{
			setInnerHTML($('#limited_download_rate')[0], [ 'Limit (', Transmission.fmt.speed(dn_limit_k), ')' ].join(''));
			var key = dn_limited ? '#limited_download_rate'
			                       : '#unlimited_download_rate';
			$(key).deselectMenuSiblings().selectMenuItem();

			setInnerHTML($('#limited_upload_rate')[0], [ 'Limit (', Transmission.fmt.speed(up_limit_k), ')' ].join(''));
			key = up_limited ? '#limited_upload_rate'
			                 : '#unlimited_upload_rate';
			$(key).deselectMenuSiblings().selectMenuItem();
		}

		this[Prefs._TurtleState] = prefs[RPC._TurtleState];
		this.updateTurtleButton();
		this.setCompactMode(prefs[Prefs._CompactDisplayState]);
	},

	updatePortStatus: function(status) {
		if (status['port-is-open'])
			$('#port_test').text('Port is open');
		else
			$('#port_test').text('Port is closed');
	},

	showStatsDialog: function() {
		this.loadDaemonStats();
		$('body').addClass('stats_showing');
		$('#stats_container').show();
		this.hideiPhoneAddressbar();
		if (Safari3)
			setTimeout("$('div#stats_container div.dialog_window').css('top', '0px');",10);
		this.updateButtonStates();
		this.togglePeriodicStatsRefresh(true);
	},

	hideStatsDialog: function() {
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
		this.updateButtonStates();
		this.togglePeriodicStatsRefresh(false);
	},

	/*
	 * Process got some new session stats from the server
	 */
	updateStats: function(stats)
	{
		// can't think of a reason to remember this
		//this._stats = stats;

		var fmt = Transmission.fmt;
		var session = stats["current-stats"];
		var total = stats["cumulative-stats"];

		setInnerHTML($('#stats_session_uploaded')[0], fmt.size(session["uploadedBytes"]));
		setInnerHTML($('#stats_session_downloaded')[0], fmt.size(session["downloadedBytes"]));
		setInnerHTML($('#stats_session_ratio')[0], fmt.ratioString(Math.ratio(session["uploadedBytes"],session["downloadedBytes"])));
		setInnerHTML($('#stats_session_duration')[0], fmt.timeInterval(session["secondsActive"]));
		setInnerHTML($('#stats_total_count')[0], total["sessionCount"] + " times");
		setInnerHTML($('#stats_total_uploaded')[0], fmt.size(total["uploadedBytes"]));
		setInnerHTML($('#stats_total_downloaded')[0], fmt.size(total["downloadedBytes"]));
		setInnerHTML($('#stats_total_ratio')[0], fmt.ratioString(Math.ratio(total["uploadedBytes"],total["downloadedBytes"])));
		setInnerHTML($('#stats_total_duration')[0], fmt.timeInterval(total["secondsActive"]));
	},

	setSearch: function(search) {
		this._current_search = search ? search.trim() : null;
		this.refilter();
	},

	setSortMethod: function(sort_method) {
		this.setPref(Prefs._SortMethod, sort_method);
		this.refilter();
	},

	setSortDirection: function(direction) {
		this.setPref(Prefs._SortDirection, direction);
		this.refilter();
	},

	/*
	 * Process an event in the footer-menu
	 */
	processSettingsMenuEvent: function(ev) {
		var tr = this;
		var $element = $(ev.target);

		// Figure out which menu has been clicked
		switch ($element.parent()[0].id) {

				// Display the preferences dialog
			case 'footer_super_menu':
				if ($element[0].id == 'preferences') {
					$('div#prefs_container div#pref_error').hide();
					$('div#prefs_container h2.dialog_heading').show();
					tr.showPrefsDialog();
				}
				else if ($element[0].id == 'statistics') {
					$('div#stats_container div#stats_error').hide();
					$('div#stats_container h2.dialog_heading').show();
					tr.showStatsDialog();
				}
				else if ($element[0].id == 'compact_view') {
					this.toggleCompactClicked();
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
					var rate_str = $element[0].innerHTML;
					var rate_val = parseInt(rate_str, 10);
					setInnerHTML($('#limited_download_rate')[0], [ 'Limit (', Transmission.fmt.speed(rate_val), ')' ].join(''));
					$('#limited_download_rate').deselectMenuSiblings().selectMenuItem();
					$('div.preference input#download_rate')[0].value = rate_str;
					args[RPC._DownSpeedLimit] = rate_val;
					args[RPC._DownSpeedLimited] = true;
				}
				$('div.preference input#limit_download')[0].checked = args[RPC._DownSpeedLimited];
				tr.remote.savePrefs(args);
				break;

			// Limit the upload rate
			case 'footer_upload_rate_menu':
				var args = { };
				if ($element.is('#unlimited_upload_rate')) {
					$element.deselectMenuSiblings().selectMenuItem();
					args[RPC._UpSpeedLimited] = false;
				} else {
					var rate_str = $element[0].innerHTML;
					var rate_val = parseInt(rate_str, 10);
					setInnerHTML($('#limited_upload_rate')[0], [ 'Limit (', Transmission.fmt.speed(rate_val), ')' ].join(''));
					$('#limited_upload_rate').deselectMenuSiblings().selectMenuItem();
					$('div.preference input#upload_rate')[0].value = rate_str;
					args[RPC._UpSpeedLimit] = rate_val;
					args[RPC._UpSpeedLimited] = true;
				}
				$('div.preference input#limit_upload')[0].checked = args[RPC._UpSpeedLimited];
				tr.remote.savePrefs(args);
				break;

			// Sort the torrent list
			case 'footer_sort_menu':

				// The 'reverse sort' option state can be toggled independently of the other options
				if ($element.is('#reverse_sort_order')) {
					if (!$element.is('#reverse_sort_order.active')) break;
					var dir;
					if ($element.menuItemIsSelected()) {
						$element.deselectMenuItem();
						dir = Prefs._SortAscending;
					} else {
						$element.selectMenuItem();
						dir = Prefs._SortDescending;
					}
					tr.setSortDirection(dir);

				// Otherwise, deselect all other options (except reverse-sort) and select this one
				} else {
					$element.parent().find('span.selected').each(function() {
						if (! $element.parent().is('#reverse_sort_order')) {
							$element.parent().deselectMenuItem();
						}
					});
					$element.selectMenuItem();
					var method = $element[0].id.replace(/sort_by_/, '');
					tr.setSortMethod(method);
				}
				break;
		}
		$('#settings_menu').trigger('closemenu');
		return false; // to prevent the event from bubbling up
	},

	/*
	 * Update the inspector with the latest data for the selected torrents
	 */
	updateInspector: function()
	{
		if (!this[Prefs._ShowInspector])
			return;

		var torrents = this.getSelectedTorrents();
		if (!torrents.length && iPhone) {
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

		if (torrents.length == 0)
		{
			setInnerHTML(tab.name, 'No Selection');
			setInnerHTML(tab.size, na);
			setInnerHTML(tab.pieces, na);
			setInnerHTML(tab.hash, na);
			setInnerHTML(tab.state, na);
			setInnerHTML(tab.download_speed, na);
			setInnerHTML(tab.upload_speed, na);
			setInnerHTML(tab.uploaded, na);
			setInnerHTML(tab.downloaded, na);
			setInnerHTML(tab.availability, na);
			setInnerHTML(tab.ratio, na);
			setInnerHTML(tab.have, na);
			setInnerHTML(tab.upload_to, na);
			setInnerHTML(tab.download_from, na);
			setInnerHTML(tab.secure, na);
			setInnerHTML(tab.creator_date, na);
			setInnerHTML(tab.progress, na);
			setInnerHTML(tab.comment, na);
			setInnerHTML(tab.creator, na);
			setInnerHTML(tab.download_dir, na);
			setInnerHTML(tab.error, na);
			this.updateFileList();
			this.updatePeersLists();
			this.updateTrackersLists();
			$("#torrent_inspector_size, .inspector_row > div:contains('N/A')").css('color', '#666');
			return;
		}

		name = torrents.length == 1
			? torrents[0].getName()
			: torrents.length+' Transfers Selected';

		if (torrents.length == 1)
		{
			var text;
			var t = torrents[0];
			var err = t.getErrorMessage();
			if (err)
				error = err;
			if ((text = t.getComment()))
				comment = text;
			if ((text = t.getCreator()))
				creator = text;
			if ((text = t.getDownloadDir()))
				download_dir = text;

			hash = t.getHashString();
			pieces = [ t.getPieceCount(), 'pieces @', Transmission.fmt.mem(t.getPieceSize()) ].join(' ');
			date_created = Transmission.fmt.timestamp(t.getDateCreated());
		}

		for (var i=0, t; t=torrents[i]; ++i) {
			var l = t.getLeftUntilDone();
			var d = t.getSizeWhenDone();
			sizeWhenDone         += d;
			sizeDone             += d - l;
			total_completed      += t.getHave();
			total_verified       += t.getHaveValid();
			total_size           += t.getTotalSize();
			total_upload         += t.getUploadedEver();
			total_download       += t.getDownloadedEver();
			total_upload_speed   += t.getUploadSpeed();
			total_download_speed += t.getDownloadSpeed();
			total_upload_peers   += t.getPeersGettingFromUs();
			total_download_peers += t.getPeersSendingToUs();
			total_availability   += sizeWhenDone - l + t.getDesiredAvailable();

			var s = t.getStateString();
			if (total_state.indexOf(s) == -1)
				total_state.push(s);

			if (t.getPrivateFlag())
				have_private = true;
			else
				have_public = true;
		}

		var private_string = '';
		var fmt = Transmission.fmt;
		if (have_private && have_public) private_string = 'Mixed';
		else if (have_private) private_string = 'Private Torrent';
		else if (have_public) private_string = 'Public Torrent';

		setInnerHTML(tab.name, name);
		setInnerHTML(tab.size, torrents.length ? fmt.size(total_size) : na);
		setInnerHTML(tab.pieces, pieces);
		setInnerHTML(tab.hash, hash);
		setInnerHTML(tab.state, total_state.join('/'));
		setInnerHTML(tab.download_speed, torrents.length ? fmt.speedBps(total_download_speed) : na);
		setInnerHTML(tab.upload_speed, torrents.length ? fmt.speedBps(total_upload_speed) : na);
		setInnerHTML(tab.uploaded, torrents.length ? fmt.size(total_upload) : na);
		setInnerHTML(tab.downloaded, torrents.length ? fmt.size(total_download) : na);
		setInnerHTML(tab.availability, torrents.length ? fmt.percentString(Math.ratio(total_availability*100, sizeWhenDone)) + '%' : na);
		setInnerHTML(tab.ratio, torrents.length ? fmt.ratioString(Math.ratio(total_upload, total_download)) : na);
		setInnerHTML(tab.have, torrents.length ? fmt.size(total_completed) + ' (' + fmt.size(total_verified) + ' verified)' : na);
		setInnerHTML(tab.upload_to, torrents.length ? total_upload_peers : na);
		setInnerHTML(tab.download_from, torrents.length ? total_download_peers : na);
		setInnerHTML(tab.secure, private_string);
		setInnerHTML(tab.creator_date, date_created);
		setInnerHTML(tab.progress, torrents.length ? fmt.percentString(Math.ratio(sizeDone*100, sizeWhenDone)) + '%' : na);
		setInnerHTML(tab.comment, comment);
		setInnerHTML(tab.creator, creator);
		setInnerHTML(tab.download_dir, download_dir);
		setInnerHTML(tab.error, error);

		this.updatePeersLists();
		this.updateTrackersLists();
		$(".inspector_row > div:contains('N/A')").css('color', '#666');
		this.updateFileList();
	},

	onFileWantedToggled: function(row, want) {
		var command = want ? 'files-wanted' : 'files-unwanted';
		this.changeFileCommand(command, [ row ]);
	},
	onFilePriorityToggled: function(row, priority) {
		var command;
		switch(priority) {
			case -1: command = 'priority-low'; break;
			case  1: command = 'priority-high'; break;
			default: command = 'priority-normal'; break;
		}
		this.changeFileCommand(command, [ row ]);
	},
	clearFileList: function() {
		$(this._inspector_file_list).empty();
		delete this._files_torrent;
		delete this._files;
	},
	updateFileList: function() {

		// if the file list is hidden, clear the list
		if (this._inspector_tab_files.className.indexOf('selected') == -1) {
			this.clearFileList();
			return;
		}

		// if not torrent is selected, clear the list
		var selected_torrents = this.getSelectedTorrents();
		if (selected_torrents.length != 1) {
			this.clearFileList();
			return;
		}

		// if the active torrent hasn't changed, noop
		var torrent = selected_torrents[0];
		if (this._files_torrent === torrent)
			return;

		// build the file list
		this.clearFileList();
		this._files_torrent = torrent;
		var n = torrent._files.length;
		this._files = new Array(n);
		var fragment = document.createDocumentFragment();
		var tr = this;
		for (var i=0; i<n; ++i) {
			var row = new FileRow(torrent, i);
			fragment.appendChild(row.getElement());
			this._files[i] = row;
	                $(row).bind('wantedToggled',function(e,row,want) {tr.onFileWantedToggled(row,want);});
	                $(row).bind('priorityToggled',function(e,row,priority) {tr.onFilePriorityToggled(row,priority);});
		}
		this._inspector_file_list.appendChild(fragment);
	},

	refreshFileView: function() {
		for (var i=0, row; row=this._files[i]; ++i)
			row.refresh();
	},

	updatePeersLists: function() {
		var html = [ ];
		var fmt = Transmission.fmt;
		var torrents = this.getSelectedTorrents();
		if ($(this._inspector_peers_list).is(':visible')) {
			for (var k=0, torrent; torrent=torrents[k]; ++k) {
				var peers = torrent.getPeers();
				html.push('<div class="inspector_group">');
				if (torrents.length > 1) {
					html.push('<div class="inspector_torrent_label">', torrent.getName(), '</div>');
				}
				if (peers.length == 0) {
					html.push('<br></div>'); // firefox won't paint the top border if the div is empty
					continue;
				}
				html.push('<table class="peer_list">',
				           '<tr class="inspector_peer_entry even">',
				           '<th class="encryptedCol"></th>',
				           '<th class="upCol">Up</th>',
				           '<th class="downCol">Down</th>',
				           '<th class="percentCol">%</th>',
				           '<th class="statusCol">Status</th>',
				           '<th class="addressCol">Address</th>',
				           '<th class="clientCol">Client</th>',
				           '</tr>');
				for (var i=0, peer; peer=peers[i]; ++i) {
					var parity = ((i+1) % 2 == 0 ? 'even' : 'odd');
					html.push('<tr class="inspector_peer_entry ', parity, '">',
					           '<td>', (peer.isEncrypted ? '<img src="images/graphics/lock_icon.png" alt="Encrypted"/>' : ''), '</td>',
					           '<td>', (peer.rateToPeer ? fmt.speedBps(peer.rateToPeer) : ''), '</td>',
					           '<td>', (peer.rateToClient ? fmt.speedBps(peer.rateToClient) : ''), '</td>',
					           '<td class="percentCol">', Math.floor(peer.progress*100), '%', '</td>',
					           '<td>', peer.flagStr, '</td>',
					           '<td>', peer.address, '</td>',
					           '<td class="clientCol">', peer.clientName, '</td>',
					           '</tr>');
				}
				html.push('</table></div>');
			}
		}
		setInnerHTML(this._inspector_peers_list, html.join(''));
	},

	updateTrackersLists: function() {
		// By building up the HTML as as string, then have the browser
		// turn this into a DOM tree, this is a fast operation.
		var tr = this;
		var html = [ ];
		var na = 'N/A';
		var torrents = this.getSelectedTorrents();
		if ($(this._inspector_trackers_list).is(':visible')) {
			for (var k=0, torrent; torrent = torrents[k]; ++k) {
				html.push ('<div class="inspector_group">');
				if (torrents.length > 1) {
					html.push('<div class="inspector_torrent_label">', torrent.getName(), '</div>');
				}
				for (var i=0, tier; tier=torrent._trackerStats[i]; ++i) {
					html.push('<div class="inspector_group_label">',
					          'Tier ', (i + 1), '</div>',
					          '<ul class="tier_list">');
					for (var j=0, tracker; tracker=tier[j]; ++j) {
						var lastAnnounceStatusHash = tr.lastAnnounceStatus(tracker);
						var announceState = tr.announceState(tracker);
						var lastScrapeStatusHash = tr.lastScrapeStatus(tracker);

						// Display construction
						var parity = ((j+1) % 2 == 0 ? 'even' : 'odd');
						html.push('<li class="inspector_tracker_entry ', parity, '"><div class="tracker_host" title="', tracker.announce, '">',
						          tracker.host, '</div>',
						          '<div class="tracker_activity">',
						          '<div>', lastAnnounceStatusHash['label'], ': ', lastAnnounceStatusHash['value'], '</div>',
						          '<div>', announceState, '</div>',
						          '<div>', lastScrapeStatusHash['label'], ': ', lastScrapeStatusHash['value'], '</div>',
						          '</div><table class="tracker_stats">',
						          '<tr><th>Seeders:</th><td>', (tracker.seederCount > -1 ? tracker.seederCount : na), '</td></tr>',
						          '<tr><th>Leechers:</th><td>', (tracker.leecherCount > -1 ? tracker.leecherCount : na), '</td></tr>',
						          '<tr><th>Downloads:</th><td>', (tracker.downloadCount > -1 ? tracker.downloadCount : na), '</td></tr>',
						          '</table></li>');
					}
					html.push('</ul>');
				}
				html.push('</div>');
			}
		}
		setInnerHTML(this._inspector_trackers_list, html.join(''));
	},

	lastAnnounceStatus: function(tracker) {
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

	announceState: function(tracker) {
		var announceState = '';
		switch (tracker.announceState) {
			case Torrent._TrackerActive:
				announceState = 'Announce in progress';
				break;
			case Torrent._TrackerWaiting:
				var timeUntilAnnounce = tracker.nextAnnounceTime - ((new Date()).getTime() / 1000);
				if (timeUntilAnnounce < 0) {
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

	lastScrapeStatus: function(tracker) {
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
		return {'label':lastScrapeLabel, 'value':lastScrape};
	},

	/*
	 * Toggle the visibility of the inspector (used by the context menu)
	 */
	toggleInspector: function() {
		if (this[Prefs._ShowInspector])
			this.hideInspector();
		else
			this.showInspector();
	},

	showInspector: function() {
		$('#torrent_inspector').show();
		if (iPhone) {
			$('body').addClass('inspector_showing');
			$('#inspector_close').show();
			this.hideiPhoneAddressbar();
		} else {
			var w = $('#torrent_inspector').width() + 1 + 'px';
			$('#torrent_container')[0].style.right = w;
		}

		setInnerHTML($('ul li#context_toggle_inspector')[0], 'Hide Inspector');

		this.setPref(Prefs._ShowInspector, true);
		this.updateInspector();
	},

	/*
	 * Hide the inspector
	 */
	hideInspector: function() {

		$('#torrent_inspector').hide();

		if (iPhone) {
			this.deselectAll();
			$('body.inspector_showing').removeClass('inspector_showing');
			$('#inspector_close').hide();
			this.hideiPhoneAddressbar();
		} else {
			$('#torrent_container')[0].style.right = '0px';
			setInnerHTML($('ul li#context_toggle_inspector')[0], 'Show Inspector');
		}

		this.setPref(Prefs._ShowInspector, false);
	},

	refreshMetaData: function(ids) {
		var tr = this;
		this.remote.getMetaDataFor(ids, function(active) { tr.updateMetaData(active); });
	},

	updateMetaData: function(torrents)
	{
		var tr = this;
		var refresh_files_for = [ ];
		var selected_torrents = this.getSelectedTorrents();
		jQuery.each(torrents, function() {
			var t = tr._torrents[ this.id ];
			if (t) {
				t.refreshMetaData(this);
				if (selected_torrents.indexOf(t) != -1)
					refresh_files_for.push(t.getId());
			}
		});
		if (refresh_files_for.length > 0)
			tr.remote.loadTorrentFiles(refresh_files_for);
	},

	refreshTorrents: function(ids) {
		var tr = this;
		if (!ids)
			ids = 'recently-active';

		this.remote.getUpdatedDataFor(ids, function(active, removed) { tr.updateTorrentsData(active, removed); });
	},

	updateTorrentsData: function(updated, removed_ids) {
		var tr = this;
		var new_torrent_ids = [];
		var refresh_files_for = [];
		var selected_torrents = this.getSelectedTorrents();

		for (var i=0, o; o=updated[i]; ++i) {
			var t = tr._torrents[o.id];
			if (t == null)
				new_torrent_ids.push(o.id);
			else {
				t.refresh(o);
				if (selected_torrents.indexOf(t) != -1)
					refresh_files_for.push(t.getId());
			}
		}

		if (refresh_files_for.length > 0)
			tr.remote.loadTorrentFiles(refresh_files_for);

		if (new_torrent_ids.length > 0)
			tr.remote.getInitialDataFor(new_torrent_ids, function(torrents) {tr.addTorrents(torrents);});

		tr.deleteTorrents(removed_ids);

		if (new_torrent_ids.length != 0) {
			tr.hideiPhoneAddressbar();
			tr.deselectAll(true);
		}
	},

	updateTorrentsFileData: function(torrents) {
		for (var i=0, o; o=torrents[i]; ++i) {
			var t = this._torrents[o.id];
			if (t) {
				t.refreshFiles(o);
				if (t === this._files_torrent)
					this.refreshFileView();
			}
		}
	},

	initializeAllTorrents: function() {
		var tr = this;
		this.remote.getInitialDataFor(null ,function(torrents) { tr.addTorrents(torrents); });
	},

        onRowClicked: function(ev, row)
        {
                // Prevents click carrying to parent element
                // which deselects all on click
                ev.stopPropagation();
                // but still hide the context menu if it is showing
                $('#jqContextMenu').hide();

                // 'Apple' button emulation on PC :
                // Need settable meta-key and ctrl-key variables for mac emulation
                var meta_key = ev.metaKey;
                if (ev.ctrlKey && navigator.appVersion.toLowerCase().indexOf("mac") == -1)
                        meta_key = true;

                // Shift-Click - selects a range from the last-clicked row to this one
                if (iPhone) {
                        if (row.isSelected())
                                this.showInspector();
                        this.setSelectedRow(row, true);

                } else if (ev.shiftKey) {
                        this.selectRange(row, true);
                        // Need to deselect any selected text
                        window.focus();

                // Apple-Click, not selected
                } else if (!row.isSelected() && meta_key) {
                        this.selectRow(row, true);

                // Regular Click, not selected
                } else if (!row.isSelected()) {
                        this.setSelectedRow(row, true);

                // Apple-Click, selected
                } else if (row.isSelected() && meta_key) {
                        this.deselectRow(row);

                // Regular Click, selected
                } else if (row.isSelected()) {
                        this.setSelectedRow(row, true);
                }

		this._last_torrent_clicked = row.getTorrent().getId();
        },

	addTorrents: function(new_torrents)
	{
		var tr = this;
		var key = 'dataChanged';

		for (var i=0, row; row=new_torrents[i]; ++i) {
			var t = new Torrent(row);
			$(t).bind(key,function() {tr.refilterSoon();});
			this._torrents[t.getId()] = t;
		}

		this.refilterSoon();
	},

	deleteTorrents: function(torrent_ids)
	{
		if (torrent_ids && torrent_ids.length)
		{
			for (var i=0, id; i=torrent_ids[i]; ++i)
				delete this._torrents[id];
			this.refilter();
		}
	},

	updateStatusbar: function()
	{
		this.refreshFilterButton();

		// up/down speed
		var u=0, d=0;
		var torrents = this.getAllTorrents();
		for (var i=0, row; row=torrents[i]; ++i) {
			u += row.getUploadSpeed();
			d += row.getDownloadSpeed();
		}
		setInnerHTML($('#statusbar #speed-up-label')[0], '&uarr; ' + Transmission.fmt.speedBps(u));
		setInnerHTML($('#statusbar #speed-dn-label')[0], '&darr; ' + Transmission.fmt.speedBps(d));
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
				args.success = function() {
					tr.refreshTorrents();
					tr.togglePeriodicRefresh(true);
				};
				tr.togglePeriodicRefresh(false);
				$('#torrent_upload_form').ajaxSubmit(args);
			}
		}
	},

	removeSelectedTorrents: function() {
		var torrents = this.getSelectedTorrents();
		if (torrents.length)
			this.promptToRemoveTorrents(torrents);
	},

	removeSelectedTorrentsAndData: function() {
		var torrents = this.getSelectedTorrents();
		if (torrents.length)
			this.promptToRemoveTorrentsAndData(torrents);
	},

	promptToRemoveTorrents:function(torrents)
	{
		if (torrents.length == 1)
		{
			var torrent = torrents[0];
			var header = 'Remove ' + torrent.getName() + '?';
			var message = 'Once removed, continuing the transfer will require the torrent file. Are you sure you want to remove it?';
			dialog.confirm(header, message, 'Remove', 'transmission.removeTorrents', torrents);
		}
		else
		{
			var header = 'Remove ' + torrents.length + ' transfers?';
			var message = 'Once removed, continuing the transfers will require the torrent files. Are you sure you want to remove them?';
			dialog.confirm(header, message, 'Remove', 'transmission.removeTorrents', torrents);
		}
	},

	promptToRemoveTorrentsAndData:function(torrents)
	{
		if (torrents.length == 1)
		{
			var torrent = torrents[0],
				header = 'Remove ' + torrent.getName() + ' and delete data?',
				message = 'All data downloaded for this torrent will be deleted. Are you sure you want to remove it?';
			dialog.confirm(header, message, 'Remove', 'transmission.removeTorrentsAndData', torrents);
		}
		else
		{
			var header = 'Remove ' + torrents.length + ' transfers and delete data?',
				message = 'All data downloaded for these torrents will be deleted. Are you sure you want to remove them?';
			dialog.confirm(header, message, 'Remove', 'transmission.removeTorrentsAndData', torrents);
		}
	},

	removeTorrents: function(torrents) {
		var torrent_ids = jQuery.map(torrents, function(t) { return t.getId(); });
		var tr = this;
		this.remote.removeTorrents(torrent_ids, function() { tr.refreshTorrents();});
	},

	removeTorrentsAndData: function(torrents) {
		this.remote.removeTorrentsAndData(torrents);
	},

	verifySelectedTorrents: function() {
		this.verifyTorrents(this.getSelectedTorrents());
	},

	reannounceSelectedTorrents: function() {
		this.reannounceTorrents(this.getSelectedTorrents());
	},

	startSelectedTorrents: function(force) {
		this.startTorrents(this.getSelectedTorrents(), force);
	},
	startAllTorrents: function() {
		this.startTorrents(this.getAllTorrents(), false);
	},
	startTorrent: function(torrent) {
		this.startTorrents([ torrent ], false);
	},
	startTorrents: function(torrents, force) {
		var torrent_ids = jQuery.map(torrents, function(t) { return t.getId(); });
		var tr = this;
		this.remote.startTorrents(torrent_ids, force, function() { tr.refreshTorrents(torrent_ids); });
	},
	verifyTorrent: function(torrent) {
		this.verifyTorrents([ torrent ]);
	},
	verifyTorrents: function(torrents) {
		var tr = this;
		var torrent_ids = jQuery.map(torrents, function(t) { return t.getId(); });
		this.remote.verifyTorrents(torrent_ids, function() { tr.refreshTorrents(torrent_ids); });
	},

	reannounceTorrent: function(torrent) {
		this.reannounceTorrents([ torrent ]);
	},
	reannounceTorrents: function(torrents) {
		var tr = this;
		var torrent_ids = jQuery.map(torrents, function(t) { return t.getId(); });
		this.remote.reannounceTorrents(torrent_ids, function() { tr.refreshTorrents(torrent_ids); });
	},

	stopSelectedTorrents: function() {
		this.stopTorrents(this.getSelectedTorrents());
	},
	stopAllTorrents: function() {
		this.stopTorrents(this.getAllTorrents());
	},
	stopTorrent: function(torrent) {
		this.stopTorrents([ torrent ]);
	},
	stopTorrents: function(torrents) {
		var torrent_ids = jQuery.map(torrents, function(t) { return t.getId(); });
		var tr = this;
		this.remote.stopTorrents(torrent_ids,	function() { tr.refreshTorrents(torrent_ids);});
	},
	changeFileCommand: function(command, rows) {
		this.remote.changeFileCommand(command, rows);
	},

	hideiPhoneAddressbar: function(timeInSeconds) {
		if (iPhone) {
			var delayLength = timeInSeconds ? timeInSeconds*1000 : 150;
			// not currently supported on iPhone
			if (/*document.body.scrollTop!=1 && */scroll_timeout==null) {
				var tr = this;
				scroll_timeout = setTimeout(function() {tr.doToolbarHide();}, delayLength);
			}
		}
	},
	doToolbarHide: function() {
		window.scrollTo(0,1);
		scroll_timeout=null;
	},

	// Queue
	moveTop: function() {
		var tr = this;
		var torrent_ids = this.getSelectedTorrentIds();
		this.remote.moveTorrentsToTop(torrent_ids, function() { tr.refreshTorrents(torrent_ids);});
	},
	moveUp: function() {
		var tr = this;
		var torrent_ids = this.getSelectedTorrentIds();
		this.remote.moveTorrentsUp(torrent_ids, function() { tr.refreshTorrents(torrent_ids);});
	},
	moveDown: function() {
		var tr = this;
		var torrent_ids = this.getSelectedTorrentIds();
		this.remote.moveTorrentsDown(torrent_ids, function() { tr.refreshTorrents(torrent_ids);});
	},
	moveBottom: function() {
		var tr = this;
		var torrent_ids = this.getSelectedTorrentIds();
		this.remote.moveTorrentsToBottom(torrent_ids, function() { tr.refreshTorrents(torrent_ids);});
	},


	/***
	****
	***/

	onToggleRunningClicked: function(ev)
	{
		var torrent = ev.data.r.getTorrent();

		if (torrent.isStopped())
			this.startTorrent(torrent);
		else
			this.stopTorrent(torrent);
	},

	setEnabled: function(key, flag)
	{
		$(key).toggleClass('disabled', !flag);
	},

	updateButtonStates: function()
	{
		var showing_dialog = new RegExp("(prefs_showing|dialog_showing|open_showing)").test(document.body.className);
		this._toolbar_buttons.toggleClass('disabled', showing_dialog);

		if (!showing_dialog)
		{
			var haveSelection = false;
			var haveActive = false;
			var haveActiveSelection = false;
			var havePaused = false;
			var havePausedSelection = false;

			for (var i=0, row; row=this._rows[i]; ++i) {
				var isStopped = row.getTorrent().isStopped();
				var isSelected = row.isSelected();
				if (!isStopped) haveActive = true;
				if (isStopped) havePaused = true;
				if (isSelected) haveSelection = true;
				if (isSelected && !isStopped) haveActiveSelection = true;
				if (isSelected && isStopped) havePausedSelection = true;
			}

			this.setEnabled(this._toolbar_pause_button,       haveActiveSelection);
			this.setEnabled(this._context_pause_button,       haveActiveSelection);
			this.setEnabled(this._toolbar_start_button,       havePausedSelection);
			this.setEnabled(this._context_start_button,       havePausedSelection);
			this.setEnabled(this._context_move_top_button,    haveSelection);
			this.setEnabled(this._context_move_up_button,     haveSelection);
			this.setEnabled(this._context_move_down_button,   haveSelection);
			this.setEnabled(this._context_move_bottom_button, haveSelection);
			this.setEnabled(this._context_start_now_button,   havePausedSelection);
			this.setEnabled(this._toolbar_remove_button,      haveSelection);
			this.setEnabled(this._toolbar_pause_all_button,   haveActive);
			this.setEnabled(this._toolbar_start_all_button,   havePaused);
		}
	},

	/****
	*****
	*****  FILTER
	*****
	****/

	filterSetup: function()
	{
		var popup = $('#filter-popup');
		popup.dialog({
			autoOpen: false,
			position: iPhone ? [0,0] : [40,80],
			show: 'blind',
			hide: 'blind',
			title: 'Show',
			width: 315
		});
		var tr = this;
		$('#filter-button').click(function() {
			if (popup.is(":visible"))
				popup.dialog('close');
			else {
				tr.refreshFilterPopup();
				popup.dialog('open');
			}
		});
		this.refreshFilterButton();
	},

	refreshFilterButton: function()
	{
		var state = this[Prefs._FilterMode];
		var state_all = state == Prefs._FilterAll;
		var state_string = this.getStateString(state);
		var tracker = this.filterTracker;
		var tracker_all = !tracker;
		var tracker_string = tracker ? this.getReadableDomain(tracker) : '';

		var text;
		if (state_all && tracker_all)
			text = 'Show <span class="filter-selection">All</span>';
		else if (state_all)
			text = 'Show <span class="filter-selection">' + tracker_string + '</span>';
		else if (tracker_all)
			text = 'Show <span class="filter-selection">' + state_string + '</span>';
		else
			text = 'Show <span class="filter-selection">' + state_string + '</span> at <span class="filter-selection">' + tracker_string + '</span>';

		var torrent_count = this.getAllTorrents().length;
		var visible_count = this.getVisibleTorrents().length;
		if (torrent_count === visible_count)
			text += ' &mdash; ' + torrent_count + ' Transfers';
		else
			text += ' &mdash; ' + visible_count + ' of ' + torrent_count;
		$('#filter-button')[0].innerHTML = text;
	},

	refilterSoon: function()
	{
		if (!this.refilterTimer)
		{
			var tr = this;
			this.refilterTimer = setTimeout(function() {tr.refilter();}, 500);
		}
	},

	refilter: function()
	{
		clearTimeout(this.refilterTimer);
		delete this.refilterTimer;

		// decide which torrents to show
		var keep = [];
		var all_torrents = this.getAllTorrents();
		for (var i=0, t; t=all_torrents[i]; ++i)
			if (t.test(this[Prefs._FilterMode], this._current_search, this.filterTracker))
				keep.push(t);

		// sort the torrents we're going to show
		Torrent.sortTorrents(keep, this[Prefs._SortMethod],
		                           this[Prefs._SortDirection]);

		// make a temporary backup of the selection
		var sel = this.getSelectedTorrents();
		var new_sel_count = 0;

		// make the new rows
		var tr = this;
		var rows = [ ];
		var fragment = document.createDocumentFragment();
		for (var i=0, tor; tor=keep[i]; ++i)
		{
			var is_selected = sel.indexOf(tor) !== -1;
			var row = new TorrentRow(this.torrentRenderer, this, tor, is_selected);
			row.setEven((i+1) % 2 == 0);
			if (is_selected)
				new_sel_count++;
			if (!iPhone) {
				var b = row.getToggleRunningButton();
				if (b)
					$(b).click({r:row}, function(ev) {tr.onToggleRunningClicked(ev);});
			}
			$(row.getElement()).click({r: row}, function(ev) {tr.onRowClicked(ev,ev.data.r);});
			$(row.getElement()).dblclick(function() { tr.toggleInspector();});
			fragment.appendChild(row.getElement());
			rows.push(row);
		}
		$('ul.torrent_list').empty();
		delete this._rows;
		this._rows = rows;
		this._torrent_list.appendChild(fragment);

		// sync gui
		this.updateStatusbar();
		if (sel.length !== new_sel_count)
			this.selectionChanged();
		this.refreshFilterButton();
	},

	setFilter: function(mode)
	{
		// set the state
		this.setPref(Prefs._FilterMode, mode);

		// refilter
		this.refilter();
	},

	refreshFilterPopup: function()
	{
		var tr = this;

		/***
		****  States
		***/

		var counts = { };
		var states = [ Prefs._FilterAll,
		               Prefs._FilterActive,
		               Prefs._FilterDownloading,
		               Prefs._FilterPaused,
		               Prefs._FilterFinished ];
		for (var i=0, state; state=states[i]; ++i)
			counts[state] = 0;
		var torrents = this.getAllTorrents();
		for (var i=0, tor; tor=torrents[i]; ++i)
			for (var j=0, s; s=states[j]; ++j)
				if (tor.testState(s))
					counts[s]++;
		var sel_state = tr[Prefs._FilterMode];
		var fragment = document.createDocumentFragment();
		for (var i=0, s; s=states[i]; ++i)
		{
			var div = document.createElement('div');
			div.id = 'show-state-' + s;
			div.className = 'row' + (s === sel_state ? ' selected':'');
			div.innerHTML = '<span class="filter-img"></span>'
			              + '<span class="filter-name">' + tr.getStateString(s) + '</span>'
			              + '<span class="count">' + counts[s] + '</span>';
			$(div).click({'state':s}, function(ev) { tr.setFilter(ev.data.state); $('#filter-popup').dialog('close');});
			fragment.appendChild(div);
		}
		$('#filter-by-state .row').remove();
		$('#filter-by-state')[0].appendChild(fragment);

		/***
		****  Trackers
		***/

		var trackers = this.getTrackers();
		var names = [];
		for (var name in trackers)
			names.push(name);
		names.sort();

		var fragment = document.createDocumentFragment();
		var div = document.createElement('div');
		div.id = 'show-tracker-all';
		div.className = 'row' + (tr.filterTracker ? '' : ' selected');
		div.innerHTML = '<span class="filter-img"></span>'
		              + '<span class="filter-name">All</span>'
		              + '<span class="count">' + torrents.length + '</span>';
		$(div).click(function() {tr.setFilterTracker(null); $('#filter-popup').dialog('close');})
		fragment.appendChild(div);
		for (var i=0, name; name=names[i]; ++i) {
			var div = document.createElement('div');
			var o = trackers[name];
			div.id = 'show-tracker-' + name;
			div.className = 'row' + (o.domain === tr.filterTracker  ? ' selected':'');
			div.innerHTML = '<img class="filter-img" src="http://'+o.domain+'/favicon.ico"/>'
			              + '<span class="filter-name">'+ name + '</span>'
			              + '<span class="count">'+ o.count + '</span>';
			$(div).click({domain:o.domain}, function(ev) { tr.setFilterTracker(ev.data.domain); $('#filter-popup').dialog('close');});
			fragment.appendChild(div);
		}
		$('#filter-by-tracker .row').remove();
		$('#filter-by-tracker')[0].appendChild(fragment);
	},

	getStateString: function(mode)
	{
		switch (mode)
		{
			case Prefs._FilterActive:      return 'Active';
			case Prefs._FilterSeeding:     return 'Seeding';
			case Prefs._FilterDownloading: return 'Downloading';
			case Prefs._FilterPaused:      return 'Paused';
			case Prefs._FilterFinished:    return 'Finished';
			default:                       return 'All';
		}
	},

	setFilterTracker: function(domain)
	{
		this.filterTracker = domain;

		// update which tracker is selected in the popup
		var key = domain ? this.getReadableDomain(domain) : 'all';
		var id = '#show-tracker-' + key;
		$(id).addClass('selected').siblings().removeClass('selected');

		this.refilterSoon();
	},

	/* example: "tracker.ubuntu.com" returns "ubuntu.com" */
	getDomainName: function(host)
	{
		var dot = host.indexOf('.');
		if (dot !== host.lastIndexOf('.'))
			host = host.slice(dot+1);
		return host;
	},

	/* example: "ubuntu.com" returns "Ubuntu" */
	getReadableDomain: function(name)
	{
		if (name.length)
			name = name.charAt(0).toUpperCase() + name.slice(1);
		var dot = name.indexOf('.');
		if (dot !== -1)
			name = name.slice(0, dot);
		return name;
	},

	getTrackers: function()
	{
		var trackers = {};

		var torrents = this.getAllTorrents();
		for (var i=0, torrent; torrent=torrents[i]; ++i) {
			var names = [];
			for (var j=0, tier; tier=torrent._trackerStats[j]; ++j) {
				for (var k=0, tracker; tracker=tier[k]; ++k) {
					var uri = parseUri(tracker.announce);
					var domain = this.getDomainName(uri.host);
					var name = this.getReadableDomain(domain);
					if (!(name in trackers))
						trackers[name] = { 'uri': uri, 'domain': domain, 'count': 0 };
					if (names.indexOf(name) === -1)
						names.push(name);
				}
			}
			for (var j=0, name; name=names[j]; ++j)
				trackers[name].count++;
		}

		return trackers;
	},

	/***
	****
	****  Compact Mode
	****
	***/

	toggleCompactClicked: function()
	{
		this.setCompactMode(!this[Prefs._CompactDisplayState]);
	},
	setCompactMode: function(is_compact)
	{
		var key = Prefs._CompactDisplayState;
		var was_compact = this[key];
		if (was_compact !== is_compact) {
			this.setPref(key, is_compact);
			this.onCompactModeChanged();
		}
	},
	initCompactMode: function()
	{
		this.onCompactModeChanged();
	},
	onCompactModeChanged: function()
	{
		var compact = this[Prefs._CompactDisplayState];

		// update the ui: context menu
		// (disabled in iphone mode...)
		if (!iPhone) {
			var e = $('#settings_menu #compact_view');
			if (compact)
				e.selectMenuItem();
			else
				e.deselectMenuItem();
		}

		// update the ui: footer button
		$("#compact-button").toggleClass('enabled',compact);

		// update the ui: torrent list
		this.torrentRenderer = compact ? new TorrentRendererCompact()
		                               : new TorrentRendererFull();
		this.refilter();
	}
};
