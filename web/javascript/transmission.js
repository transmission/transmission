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
		this.filterText    = '';
		this._torrents     = { };
		this._rows         = [ ];
		this.dirtyTorrents = { };

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
		$('#prefs_cancel_button').click(function(e) { tr.hidePrefsDialog(); return false; });
		$('#block_update_button').click(function(e) { tr.remote.updateBlocklist(); return false; });
		$('#stats_close_button').click(function(e) { tr.hideStatsDialog(); return false; });
		$('.inspector_tab').click(function(e) { tr.inspectorTabClicked(e, this); });
		$('#files_select_all').live('click', function(e) { tr.filesSelectAllClicked(e, this); });
		$('#files_deselect_all').live('click', function(e) { tr.filesDeselectAllClicked(e, this); });
		$('#open_link').click(function(e) { tr.openTorrentClicked(e); });
		$('#upload_confirm_button').click(function(e) { tr.confirmUploadClicked(e); return false;});
		$('#upload_cancel_button').click(function(e) { tr.hideUploadDialog(); return false; });
		$('#turtle_button').click(function() { tr.toggleTurtleClicked(); return false; });
		$('#compact-button').click(function() { tr.toggleCompactClicked(); return false; });
		$('#prefs-tab-general').click(function() { tr.selectPrefsTab('general'); });
		$('#prefs-tab-speed').click(function() { tr.selectPrefsTab('speed'); });
		$('#prefs-tab-peers').click(function() { tr.selectPrefsTab('peers'); });
		$('#prefs-tab-network').click(function() { tr.selectPrefsTab('network'); });
		$('#torrent_upload_form').submit(function() { $('#upload_confirm_button').click(); return false; });
		$('#torrent_container').bind('dragover', function(e) { return tr.dragenter(e); });
		$('#torrent_container').bind('dragenter', function(e) { return tr.dragenter(e); });
		$('#torrent_container').bind('drop', function(e) { return tr.drop(e); });
		// tell jQuery to copy the dataTransfer property from events over if it exists
		jQuery.event.props.push("dataTransfer");

		$(document).delegate('#torrent_list > li', 'click', function(ev) {tr.setSelectedRow(ev.currentTarget.row);});
		$(document).delegate('#torrent_list > li', 'dblclick', function(e) {tr.toggleInspector();});
	
		$('#torrent_upload_form').submit(function() { $('#upload_confirm_button').click(); return false; });

		if (iPhone) {
			$('#inspector_close').bind('click', function() { tr.setInspectorVisible(false); });
			$('#preferences_link').bind('click', function(e) { tr.releaseClutchPreferencesButton(e); });
		} else {
			$(document).bind('keydown',  function(e) { tr.keyDown(e); });
			$(document).bind('keyup',  function(e) { tr.keyUp(e); });
			$('#torrent_container').click(function() { tr.deselectAll(); });
			$('#inspector_link').click(function(e) { tr.toggleInspector(); });

			this.setupSearchBox();
			this.createContextMenu();
			this.createSettingsMenu();
		}
		this.initTurtleDropDowns();

		this._torrent_list             = $('#torrent_list')[0];
		this._inspector_file_list      = $('#inspector_file_list')[0];
		this._inspector_peers_list     = $('#inspector_peers_list')[0];
		this._inspector_trackers_list  = $('#inspector_trackers_list')[0];
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
		this.initializeTorrents();
		this.refreshTorrents();
		this.togglePeriodicSessionRefresh(true);

		this.filterSetup();
	},

	selectPrefsTab: function(name) {
		$('#prefs-tab-'+name).addClass('selected').siblings('.prefs-tab').removeClass('selected');
		$('#prefs-page-'+name).show().siblings('.prefs-page').hide();
	},

	loadDaemonPrefs: function(async) {
		this.remote.loadDaemonPrefs(function(data) {
			var o = data['arguments'];
			Prefs.getClutchPrefs(o);
			this.updatePrefs(o);
		}, this, async);
	},

	loadDaemonStats: function(async) {
		this.remote.loadDaemonStats(function(data) {
			this.updateStats(data['arguments']);
		}, this, async);
	},
	checkPort: function(async) {
		$('#port_test').text('checking ...');
		this.remote.checkPort(function(data) {
			this.updatePortStatus(data['arguments']);
		}, this, async);
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
		if (!iPhone)
		{
			$('#sort_by_' + this[Prefs._SortMethod]).selectMenuItem();

			if (this[Prefs._SortDirection] === Prefs._SortDescending)
				$('#reverse_sort_order').selectMenuItem();

			if (this[Prefs._ShowInspector])
				this.setInspectorVisible(true);
		}

		this.initCompactMode();
	},

	/*
	 * Set up the search box
	 */
	setupSearchBox: function()
	{
		var tr = this;
		var search_box = $('#torrent_search');
		search_box.bind('keyup click', function() {
			tr.setFilterText(this.value);
		});
		if (!$.browser.safari)
		{
			search_box.addClass('blur');
			search_box[0].value = 'Filter';
			search_box.bind('blur', function() {
				if (this.value == '') {
					$(this).addClass('blur');
					this.value = 'Filter';
					tr.setFilterText(null);
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

	scrollToRow: function(row)
	{
		if (iPhone) // FIXME: why?
			return;

		var list = $('#torrent_container'),
		    scrollTop = list.scrollTop(),
		    innerHeight = list.innerHeight(),
		    offsetTop = row.getElement().offsetTop,
		    offsetHeight = $(row.getElement()).outerHeight();

		if (offsetTop < scrollTop)
			list.scrollTop(offsetTop);
		else if (innerHeight + scrollTop < offsetTop + offsetHeight)
			list.scrollTop(offsetTop + offsetHeight - innerHeight);
	},

	seedRatioLimit: function() {
		if (this._prefs && this._prefs['seedRatioLimited'])
			return this._prefs['seedRatioLimit'];
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
		return $.grep(this._rows, function(r) {return r.isSelected();});
	},

	getSelectedTorrents: function() {
		return $.map(this.getSelectedRows(),function(r) {return r.getTorrent();});
	},

	getSelectedTorrentIds: function() {
		return $.map(this.getSelectedRows(),function(r) {return r.getTorrentId();});
	},

	setSelectedRow: function(row) {
		$.each(this.getSelectedRows(),function(i,r) {r.setSelected(false);});
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
		$.each(this._rows, function(i,r) {r.setSelected(true);});
		this.callSelectionChangedSoon();
	},
	deselectAll: function() {
		$.each(this._rows, function(i,r) {r.setSelected(false);});
		this.callSelectionChangedSoon();
		delete this._last_torrent_clicked;
	},

	indexOfLastTorrent: function() {
		for (var i=0, r; r=this._rows[i]; ++i)
			if (r.getTorrentId() === this._last_torrent_clicked)
				return i;
		return -1;
	},

	/* Select a range from this torrent to the last clicked torrent */
	selectRange: function(row)
	{
		var last = this.indexOfLastTorrent();

		if (last === -1)
		{
			this.selectRow(row);
		}
		else // select the range between the prevous & current
		{
			var next = this._rows.indexOf(row);
			var min = Math.min(last, next);
			var max = Math.max(last, next);
			for (var i=min; i<=max; ++i)
				this.selectRow(this._rows[i]);
		}

		this.callSelectionChangedSoon();
	},

	selectionChanged: function()
	{
		if (this[Prefs._ShowInspector])
			this.refreshInspectorTorrents(true);

		this.updateButtonStates();
		this.updateInspector();

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
		    dn = ev.keyCode === 40, // down key pressed
		    shift = ev.keyCode === 16; // shift key pressed

		if (up || dn)
		{
			var rows = this._rows,
			    last = this.indexOfLastTorrent(),
			    i = last,
			    anchor = this._shift_index;

			if (i === -1) // no selection yet
				i = 0;
			else if (dn)
				i = (i+1) % rows.length;
			else if (up)
				i = (i || rows.length) - 1;
			var r = rows[i];

			if (anchor >= 0)
			{
				// user is extending the selection with the shift + arrow keys...
				if (   ((anchor <= last) && (last < i))
				    || ((anchor >= last) && (last > i)))
				{
					this.selectRow(r);
				}
				else if (((anchor >= last) && (i > last))
				      || ((anchor <= last) && (last > i)))
				{
					this.deselectRow(rows[last]);
				}
			}
			else
			{
				if (ev.shiftKey)
					this.selectRange(r);
				else
					this.setSelectedRow(r);
			}
			this._last_torrent_clicked = r.getTorrentId();
			this.scrollToRow(r);
		}
		else if (shift)
		{
			this._shift_index = this.indexOfLastTorrent();
		}
	},

	keyUp: function(ev)
	{
		if (ev.keyCode === 16) // shift key pressed
			delete this._shift_index;
	},

	isButtonEnabled: function(e) {
		var p = e.target ? e.target.parentNode : e.srcElement.parentNode;
		return p.className!='disabled' && p.parentNode.className!='disabled';
	},

	stopAllClicked: function(ev) {
		if (this.isButtonEnabled(ev)) {
			this.stopAllTorrents();
			this.hideiPhoneAddressbar();
		}
	},

	stopSelectedClicked: function(ev) {
		if (this.isButtonEnabled(ev)) {
			this.stopSelectedTorrents();
			this.hideiPhoneAddressbar();
		}
	},

	startAllClicked: function(ev) {
		if (this.isButtonEnabled(ev)) {
			this.startAllTorrents();
			this.hideiPhoneAddressbar();
		}
	},

	startSelectedClicked: function(ev) {
		if (this.isButtonEnabled(ev)) {
			this.startSelectedTorrents(false);
			this.hideiPhoneAddressbar();
		}
	},

	openTorrentClicked: function(ev) {
		if (this.isButtonEnabled(ev)) {
			$('body').addClass('open_showing');
			this.uploadTorrentFile();
			this.updateButtonStates();
		}
	},

	dragenter: function(ev) {
		if (ev.dataTransfer && ev.dataTransfer.types) {
			var types = ["text/uri-list", "text/plain"];
			for (var i = 0; i < types.length; ++i) {
				// it would be better to look at the links here;
				// sadly, with Firefox, trying would throw.
				if (ev.dataTransfer.types.contains(types[i])) {
					ev.stopPropagation();
					ev.preventDefault();
					ev.dropEffect = "copy";
					return false;
				}
			}
		}
		else if (ev.dataTransfer) {
			ev.dataTransfer.dropEffect = "none";
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

	confirmUploadClicked: function() {
		this.uploadTorrentFile(true);
		this.hideUploadDialog();
	},

	savePrefsClicked: function()
	{
		// handle the clutch prefs locally
		var tr = this;
		var rate = parseInt ($('#prefs_form #refresh_rate')[0].value, 10);
		if (rate != tr[Prefs._RefreshRate])
			tr.setPref (Prefs._RefreshRate, rate);

		var up_bytes        = parseInt($('#prefs_form #upload_rate').val(), 10),
		    dn_bytes        = parseInt($('#prefs_form #download_rate').val(), 10),
		    turtle_up_bytes = parseInt($('#prefs_form #turtle_upload_rate').val(), 10),
		    turtle_dn_bytes = parseInt($('#prefs_form #turtle_download_rate').val(), 10);

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

	removeClicked: function(ev) {
		if (this.isButtonEnabled(ev)) {
			this.removeSelectedTorrents();
			this.hideiPhoneAddressbar();
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

		this[Prefs._TurtleState] = p[RPC._TurtleState];
		this.updateTurtleButton();
		this.setCompactMode(p[Prefs._CompactDisplayState]);
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
		var fmt = Transmission.fmt;

		var s = stats["current-stats"];
		$('#stats_session_uploaded').html(fmt.size(s.uploadedBytes));
		$('#stats_session_downloaded').html(fmt.size(s.downloadedBytes));
		$('#stats_session_ratio').html(fmt.ratioString(Math.ratio(s.uploadedBytes,s.downloadedBytes)));
		$('#stats_session_duration').html(fmt.timeInterval(s.secondsActive));

		var t = stats["cumulative-stats"];
		$('#stats_total_count').html(t.sessionCount + " times");
		$('#stats_total_uploaded').html(fmt.size(t.uploadedBytes));
		$('#stats_total_downloaded').html(fmt.size(t.downloadedBytes));
		$('#stats_total_ratio').html(fmt.ratioString(Math.ratio(t.uploadedBytes,t.downloadedBytes)));
		$('#stats_total_duration').html(fmt.timeInterval(t.secondsActive));
	},

	setFilterText: function(search) {
		this.filterText = search ? search.trim() : null;
		this.refilter(true);
	},

	setSortMethod: function(sort_method) {
		this.setPref(Prefs._SortMethod, sort_method);
		this.refilter(true);
	},

	setSortDirection: function(direction) {
		this.setPref(Prefs._SortDirection, direction);
		this.refilter(true);
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


	onTorrentChanged: function(tor)
	{
		var id = tor.getId();

		// update our dirty fields
		this.dirtyTorrents[id] = true;

		// enqueue a filter refresh
		this.refilterSoon();
	
		// if this torrent is in the inspector, refresh the inspector
		if (this[Prefs._ShowInspector])
			if (this.getSelectedTorrentIds().indexOf(id) !== -1)
				this.updateInspector();
	},

	updateFromTorrentGet: function(updates, removed_ids)
	{
		var needinfo = [];

		for (var i=0, o; o=updates[i]; ++i) {
			var t;
			var id = o.id;
			if ((t = this._torrents[id]))
				t.refresh(o);
			else {
				var tr = this;
				t = tr._torrents[id] = new Torrent(o);
				this.dirtyTorrents[id] = true;
				$(t).bind('dataChanged',function(ev,tor) {tr.onTorrentChanged(tor);});
				if(!('name' in t.fields) || !('status' in t.fields)) // missing some fields...
					needinfo.push(id);
			}
		}

		if (needinfo.length) {
			// whee, new torrents! get their initial information.
			var fields = ['id'].concat(Torrent.Fields.Metadata, Torrent.Fields.Stats);
	        	this.remote.updateTorrents(needinfo, fields, this.updateFromTorrentGet, this);
			this.refilterSoon();
		}

		if (removed_ids) {
			this.deleteTorrents(removed_ids);
			this.refilterSoon();
		}
	},

	refreshTorrents: function()
	{
		// send a request right now
		var fields = ['id'].concat(Torrent.Fields.Stats);
		this.remote.updateTorrents('recently-active', fields, this.updateFromTorrentGet, this);

		// schedule the next request
		clearTimeout(this.refreshTorrentsTimeout);
		var tr = this;
		this.refreshTorrentsTimeout = setTimeout(function(){tr.refreshTorrents();}, tr[Prefs._RefreshRate]*1000);
	},

	initializeTorrents: function()
	{
		// to bootstrap, we only need to ask for the servers's torrents' ids.
		// updateFromTorrentGet() automatically asks for the rest of the info when it gets a new id.
		var fields = ['id'].concat(Torrent.Fields.Metadata, Torrent.Fields.Stats);
	        this.remote.updateTorrents(null, fields, this.updateFromTorrentGet, this);
	},

	refreshInspectorTorrents: function(full)
	{
		// some torrent fields are only used by the inspector, so we defer loading them
		// until the user is viewing the torrent in the inspector.
		if ($('#torrent_inspector').is(':visible')) {
			var ids = this.getSelectedTorrentIds();
			if (ids && ids.length) {
				var fields = ['id'].concat(Torrent.Fields.StatsExtra);
				if (full)
					fields = fields.concat(Torrent.Fields.InfoExtra);
				this.remote.updateTorrents(ids, fields, this.updateFromTorrentGet, this);
			}
		}
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
				this.setInspectorVisible(true);
			this.setSelectedRow(row);

		} else if (ev.shiftKey) {
			this.selectRange(row);
			// Need to deselect any selected text
			window.focus();

		// Apple-Click, not selected
		} else if (!row.isSelected() && meta_key) {
			this.selectRow(row);

		// Regular Click, not selected
		} else if (!row.isSelected()) {
			this.setSelectedRow(row);

		// Apple-Click, selected
		} else if (row.isSelected() && meta_key) {
			this.deselectRow(row);

		// Regular Click, selected
		} else if (row.isSelected()) {
			this.setSelectedRow(row);
		}

		this._last_torrent_clicked = row.getTorrentId();
	},

	deleteTorrents: function(ids)
	{
		if (ids && ids.length)
		{
			for (var i=0, id; id=ids[i]; ++i)
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

		var fmt = Transmission.fmt;
		setInnerHTML($('#statusbar #speed-up-label')[0], u ? '&uarr; ' + fmt.speedBps(u) : '');
		setInnerHTML($('#statusbar #speed-dn-label')[0], d ? '&darr; ' + fmt.speedBps(d) : '');
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
		var ids = $.map(torrents, function(t) { return t.getId(); });
		this.remote.removeTorrents(ids, this.refreshTorrents, this);
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
		this.remote.startTorrents($.map(torrents, function(t) {return t.getId();}),
		                          force, this.refreshTorrents, this);
	},
	verifyTorrent: function(torrent) {
		this.verifyTorrents([ torrent ]);
	},
	verifyTorrents: function(torrents) {
		this.remote.verifyTorrents($.map(torrents, function(t) {return t.getId();}),
		                           this.refreshTorrents, this);
	},

	reannounceTorrent: function(torrent) {
		this.reannounceTorrents([ torrent ]);
	},
	reannounceTorrents: function(torrents) {
		this.remote.reannounceTorrents($.map(torrents, function(t) {return t.getId();}),
		                               this.refreshTorrents, this);
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
		this.remote.stopTorrents($.map(torrents.slice(0), function(t) {return t.getId();}),
		                         this.refreshTorrents, this);
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
		this.remote.moveTorrentsToTop(this.getSelectedTorrentIds(),
		                              this.refreshTorrents, this);
	},
	moveUp: function() {
		this.remote.moveTorrentsUp(this.getSelectedTorrentIds(),
		                           this.refreshTorrents, this);
	},
	moveDown: function() {
		this.remote.moveTorrentsDown(this.getSelectedTorrentIds(),
		                             this.refreshTorrents, this);
	},
	moveBottom: function() {
		this.remote.moveTorrentsToBottom(this.getSelectedTorrentIds(),
		                                 this.refreshTorrents, this);
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
			var haveSelection = false,
			    haveActive = false,
			    haveActiveSelection = false,
			    havePaused = false,
			    havePausedSelection = false;

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
	*****  INSPECTOR
	*****
	****/

	filesSelectAllClicked: function() {
		var t = this._file_torrent;
		if (t)
			this.toggleFilesWantedDisplay(t, true);
	},
	filesDeselectAllClicked: function() {
		var t = this._file_torrent;
		if (t)
			this.toggleFilesWantedDisplay(t, false);
	},
	toggleFilesWantedDisplay: function(torrent, wanted) {
		var rows = [ ];
		for (var i=0, row; row=this._file_rows[i]; ++i)
			if (row.isEditable() && (torrent.getFile(i).wanted !== wanted))
				rows.push(row);
		if (rows.length > 0) {
			var command = wanted ? 'files-wanted' : 'files-unwanted';
			this.changeFileCommand(command, rows);
		}
	},

	inspectorTabClicked: function(ev, tab)
	{
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
	/*
	 * Update the inspector with the latest data for the selected torrents
	 */
	updateInspector: function()
	{
		if (!this[Prefs._ShowInspector])
			return;

		var torrents = this.getSelectedTorrents();
		if (!torrents.length && iPhone) {
			this.setInspectorVisible(false);
			return;
		}

		var creator = 'N/A',
		    comment = 'N/A',
		    download_dir = 'N/A',
		    date_created = 'N/A',
		    error = 'None',
		    hash = 'N/A',
		    have_public = false,
		    have_private = false,
		    name,
		    sizeWhenDone = 0,
		    sizeDone = 0,
		    total_completed = 0,
		    total_download = 0,
		    total_download_peers = 0,
		    total_download_speed = 0,
		    total_availability = 0,
		    total_size = 0,
		    total_state = [ ],
		    pieces = 'N/A',
		    total_upload = 0,
		    total_upload_peers = 0,
		    total_upload_speed = 0,
		    total_verified = 0,
		    na = 'N/A',
		    tab = this._inspector._info_tab,
		    fmt = Transmission.fmt;

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
			pieces = [ t.getPieceCount(), 'pieces @', fmt.mem(t.getPieceSize()) ].join(' ');
			date_created = fmt.timestamp(t.getDateCreated());
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
	clearFileList: function()
	{
		$(this._inspector_file_list).empty();
		delete this._file_torrent;
		delete this._file_rows;
	},
	updateFileList: function()
	{
		if (!$(this._inspector_file_list).is(':visible'))
			return;

		var sel = this.getSelectedTorrents();
		if (sel.length !== 1) {
			this.clearFileList();
			return;
		}

		var torrent = sel[0];
		if (torrent === this._files_torrent)
			if(torrent.getFileCount() === (this._files ? this._files.length: 0))
				return;

		// build the file list
		this.clearFileList();
		this._file_torrent = torrent;
		var n = torrent.getFileCount();
		this._file_rows = [];
		var fragment = document.createDocumentFragment();
		var tr = this;
		for (var i=0; i<n; ++i) {
			var row = this._file_rows[i] = new FileRow(torrent, i);
			fragment.appendChild(row.getElement());
	                $(row).bind('wantedToggled',function(e,row,want) {tr.onFileWantedToggled(row,want);});
	                $(row).bind('priorityToggled',function(e,row,priority) {tr.onFilePriorityToggled(row,priority);});
		}
		this._inspector_file_list.appendChild(fragment);
	},

	updatePeersLists: function()
	{
		if (!$(this._inspector_peers_list).is(':visible'))
			return;

		var html = [ ];
		var fmt = Transmission.fmt;
		var torrents = this.getSelectedTorrents();

		for (var k=0, torrent; torrent=torrents[k]; ++k) {
			var peers = torrent.getPeers();
			html.push('<div class="inspector_group">');
			if (torrents.length > 1) {
				html.push('<div class="inspector_torrent_label">', torrent.getName(), '</div>');
			}
			if (!peers || !peers.length) {
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
					   '<td>', fmt.peerStatus(peer.flagStr), '</td>',
					   '<td>', peer.address, '</td>',
					   '<td class="clientCol">', peer.clientName, '</td>',
					   '</tr>');
			}
			html.push('</table></div>');
		}

		setInnerHTML(this._inspector_peers_list, html.join(''));
	},

	updateTrackersLists: function() {
		if (!$(this._inspector_trackers_list).is(':visible'))
			return;

		var html = [ ];
		var na = 'N/A';
		var torrents = this.getSelectedTorrents();

		// By building up the HTML as as string, then have the browser
		// turn this into a DOM tree, this is a fast operation.
		for (var i=0, torrent; torrent=torrents[i]; ++i)
		{
			html.push ('<div class="inspector_group">');

			if (torrents.length > 1)
				html.push('<div class="inspector_torrent_label">', torrent.getName(), '</div>');

			var tier = -1;
			var trackers = torrent.getTrackers();
			for (var j=0, tracker; tracker=trackers[j]; ++j)
			{
				if (tier != tracker.tier)
				{
					if (tier !== -1) // close previous tier
						html.push('</ul></div>');
	
					tier = tracker.tier;

					html.push('<div class="inspector_group_label">',
						  'Tier ', tier, '</div>',
						  '<ul class="tier_list">');
				}

				var lastAnnounceStatusHash = this.lastAnnounceStatus(tracker);
				var announceState = this.announceState(tracker);
				var lastScrapeStatusHash = this.lastScrapeStatus(tracker);

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
			if (tier !== -1) // close last tier
					html.push('</ul></div>');

			html.push('</div>'); // inspector_group
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

	toggleInspector: function()
	{
		this.setInspectorVisible(!this[Prefs._ShowInspector]);
	},
	setInspectorVisible: function(visible)
	{
		// we collect extra stats on torrents when they're in the inspector...
		clearInterval(this._periodic_inspector_refresh);
		delete this._periodic_inspector_refresh;
		if (visible) {
			var tr = this;
			this._periodic_inspector_refresh = setInterval(function() {tr.refreshInspectorTorrents(false);},2000);
			this.refreshInspectorTorrents(true);
		}

		// update the ui widgetry
		$('#torrent_inspector').toggle(visible);
		if (iPhone) {
			$('body').toggleClass('inspector_showing',visible);
			$('#inspector_close').toggle(visible);
			this.hideiPhoneAddressbar();
		} else {
			var w = visible ? $('#torrent_inspector').width() + 1 + 'px' : '0px';
			$('#torrent_container')[0].style.right = w;
		}

		setInnerHTML($('ul li#context_toggle_inspector')[0], (visible?'Hide':'Show')+' Inspector');
		this.setPref(Prefs._ShowInspector, visible);
		if (visible)
			this.updateInspector();
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
		var text,
		    state = this[Prefs._FilterMode],
		    state_all = state == Prefs._FilterAll,
		    state_string = this.getStateString(state),
		    tracker = this.filterTracker,
		    tracker_all = !tracker,
		    tracker_string = tracker ? this.getReadableDomain(tracker) : '',
		    torrent_count = Object.keys(this._torrents).length,
		    visible_count = this._rows.length;

		text = 'Show <span class="filter-selection">';
		if (state_all && tracker_all)
			text += 'All';
		else if (state_all)
			text += tracker_string;
		else if (tracker_all)
			text += state_string;
		else
			text += state_string + '</span> at <span class="filter-selection">' + tracker_string;
		text += '</span> &mdash; ';

		if (torrent_count === visible_count)
			text += torrent_count + ' Transfers';
		else
			text += visible_count + ' of ' + torrent_count;
		$('#filter-button').html(text);
	},

	refilterSoon: function()
	{
		if (!this.refilterTimer)
		{
			var tr = this;
			this.refilterTimer = setTimeout(function() {tr.refilter();}, 500);
		}
	},

	sortRows: function(rows)
	{
		var i, tor, row,
		    id2row = {},
		    torrents = [];

		for (i=0; row=rows[i]; ++i) {
			tor = row.getTorrent();
			torrents.push(tor);
			id2row[ tor.getId() ] = row;
		}

		Torrent.sortTorrents(torrents, this[Prefs._SortMethod],
		                               this[Prefs._SortDirection]);

		for (i=0; tor=torrents[i]; ++i)
			rows[i] = id2row[ tor.getId() ];
	},

	refilter: function(rebuildEverything)
	{
		var i, e, id, t, row, tmp, sel, rows, clean_rows, dirty_rows,
		    sort_mode = this[Prefs._SortMethod],
		    sort_direction = this[Prefs._SortDirection],
		    filter_mode = this[Prefs._FilterMode],
		    filter_text = this.filterText,
		    filter_tracker = this.filterTracker,
		    renderer = this.torrentRenderer,
		    list = this._torrent_list;

		clearTimeout(this.refilterTimer);
		delete this.refilterTimer;

		// build a temporary lookup table of selected torrent ids
		sel = { };
		for (i=0; row=this._rows[i]; ++i)
			if (row.isSelected())
				sel[row.getTorrentId()] = row;

		if (rebuildEverything) {
			$(list).empty();
			this._rows = [];
			for (id in this._torrents)
				this.dirtyTorrents[id] = true;
		}

		// rows that overlap with dirtyTorrents need to be refiltered.
		// those that don't are 'clean' and don't need refiltering.
		clean_rows = [];
		dirty_rows = [];
		for (i=0; row=this._rows[i]; ++i) {
			if(row.getTorrentId() in this.dirtyTorrents)
				dirty_rows.push(row);
			else
				clean_rows.push(row);
		}

		// remove the dirty rows from the dom
		e = $.map(dirty_rows.slice(0), function(r) {
			return r.getElement();
		});
		$(e).remove();

		// drop any dirty rows that don't pass the filter test
		tmp = [];
		for (i=0; row=dirty_rows[i]; ++i) {
			t = row.getTorrent();
			if (t.test(filter_mode, filter_text, filter_tracker))
				tmp.push(row);
			delete this.dirtyTorrents[t.getId()];
		}
		dirty_rows = tmp;

		// make new rows for dirty torrents that pass the filter test
		// but don't already have a row
		for (id in this.dirtyTorrents) {
			t = this._torrents[id];
			if (t.test(filter_mode, filter_text, filter_tracker)) {
				var s = t.getId() in sel;
				row = new TorrentRow(renderer, this, t, s);
				row.getElement().row = row;
				dirty_rows.push(row);
			}
		}

		// sort the dirty rows
		this.sortRows (dirty_rows);

		// now we have two sorted arrays of rows
		// and can do a simple two-way sorted merge.
		rows = [];
		var ci=0, cmax=clean_rows.length;
		var di=0, dmax=dirty_rows.length;
		while (ci!=cmax || di!=dmax)
		{
			var push_clean;

			if (ci==cmax)
				push_clean = false;
			else if (di==dmax)
				push_clean = true;
			else {
				var c = Torrent.compareTorrents(
				           clean_rows[ci].getTorrent(),
				           dirty_rows[di].getTorrent(),
				           sort_mode, sort_direction);
				push_clean = (c < 0);
			}

			if (push_clean)
				rows.push(clean_rows[ci++]);
			else {
				row = dirty_rows[di++];
				e = row.getElement();
				if (ci !== cmax)
					list.insertBefore(e, clean_rows[ci].getElement());
				else
					list.appendChild(e);
				rows.push(row);
			}
		}

		// update our implementation fields
		this._rows = rows;
		this.dirtyTorrents = { };

		// jquery's even/odd starts with 1 not 0, so invert its logic
		e = $.map(rows.slice(0), function(r){return r.getElement();});
		$(e).filter(":odd").addClass('even');
		$(e).filter(":even").removeClass('even');

		// sync gui
		this.updateStatusbar();
		this.refreshFilterButton();
	},

	setFilterMode: function(mode)
	{
		// set the state
		this.setPref(Prefs._FilterMode, mode);

		// refilter
		this.refilter(true);
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
		               Prefs._FilterSeeding,
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
			$(div).click({'state':s}, function(ev) {
				tr.setFilterMode(ev.data.state);
				$('#filter-popup').dialog('close');
			});
			fragment.appendChild(div);
		}
		$('#filter-by-state .row').remove();
		$('#filter-by-state')[0].appendChild(fragment);

		/***
		****  Trackers
		***/

		var trackers = this.getTrackers();
		var names = Object.keys(trackers).sort();

		var fragment = document.createDocumentFragment();
		var div = document.createElement('div');
		div.id = 'show-tracker-all';
		div.className = 'row' + (tr.filterTracker ? '' : ' selected');
		div.innerHTML = '<span class="filter-img"></span>'
		              + '<span class="filter-name">All</span>'
		              + '<span class="count">' + torrents.length + '</span>';
		$(div).click(function() {
			tr.setFilterTracker(null);
			$('#filter-popup').dialog('close');
		});
		fragment.appendChild(div);
		for (var i=0, name; name=names[i]; ++i) {
			var div = document.createElement('div');
			var o = trackers[name];
			div.id = 'show-tracker-' + name;
			div.className = 'row' + (o.domain === tr.filterTracker  ? ' selected':'');
			div.innerHTML = '<img class="filter-img" src="http://'+o.domain+'/favicon.ico"/>'
			              + '<span class="filter-name">'+ name + '</span>'
			              + '<span class="count">'+ o.count + '</span>';
			$(div).click({domain:o.domain}, function(ev) {
				tr.setFilterTracker(ev.data.domain);
				$('#filter-popup').dialog('close');
			});
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

		this.refilter(true);
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
		var ret = {};

		var torrents = this.getAllTorrents();
		for (var i=0, torrent; torrent=torrents[i]; ++i) {
			var names = [];
			var trackers = torrent.getTrackers();
			for (var j=0, tracker; tracker=trackers[j]; ++j) {
				var uri = parseUri(tracker.announce);
				var domain = this.getDomainName(uri.host);
				var name = this.getReadableDomain(domain);
				if (!(name in ret))
					ret[name] = { 'uri': uri, 'domain': domain, 'count': 0 };
				if (names.indexOf(name) === -1)
					names.push(name);
			}
			for (var j=0, name; name=names[j]; ++j)
				ret[name].count++;
		}

		return ret;
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
		var key = Prefs._CompactDisplayState,
		    was_compact = this[key];

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
		this.refilter(true);
	}
};
