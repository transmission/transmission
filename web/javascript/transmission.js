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
		this.inspector = new Inspector(this, this.remote);

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
		$('#prefs_cancel_button').click(function() { tr.hidePrefsDialog(); return false; });
		$('#block_update_button').click(function() { tr.remote.updateBlocklist(); return false; });
		$('#stats_close_button').click(function() { tr.hideStatsDialog(); return false; });
		$('#open_link').click(function(e) { tr.openTorrentClicked(e); });
		$('#upload_confirm_button').click(function(e) { tr.confirmUploadClicked(e); return false;});
		$('#upload_cancel_button').click(function() { tr.hideUploadDialog(); return false; });
		$('#turtle-button').click(function() { tr.toggleTurtleClicked(); });
		$('#compact-button').click(function() { tr.toggleCompactClicked(); });
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

		$('#torrent_upload_form').submit(function() { $('#upload_confirm_button').click(); return false; });

		if (isMobileDevice) {
			$('#inspector_close').bind('click', function() { tr.setInspectorVisible(false); });
			$('#preferences_link').bind('click', function(e) { tr.releaseClutchPreferencesButton(e); });
		} else {
			$(document).bind('keydown', function(e) { return tr.keyDown(e); });
			$(document).bind('keyup', function(e) { tr.keyUp(e); });
			$(document).delegate('#torrent_container', 'click', function() { tr.deselectAll(); });
			$('#inspector_link').click(function(e) { tr.toggleInspector(); });

			this.setupSearchBox();
			this.createContextMenu();
			this.createSettingsMenu();
		}
		this.initTurtleDropDowns();

		this._torrent_list             = $('#torrent_list')[0];
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
		if (isMobileDevice) {
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
		// start using prefs on mobile devices we need to weed
		// transmenu refs out of that too.
		if (!isMobileDevice)
		{
			$('#sort_by_' + this[Prefs._SortMethod]).selectMenuItem();

			if (this[Prefs._SortDirection] === Prefs._SortDescending)
				$('#reverse_sort_order').selectMenuItem();
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
			onContextMenu: function(ev) {
				var element = $(ev.target).closest('.torrent')[0];
				var i = $('#torrent_list > li').index(element);
				if ((i!==-1) && !tr._rows[i].isSelected())
					tr.setSelectedRow(tr._rows[i]);
				return true;
			}
		});
	},

	/*
	 * Create the footer settings menu
	 */
	createSettingsMenu: function() {
		$('#settings_menu').transMenu({
			selected_char: '&#x2714;',
			direction: 'up',
			onClick: $.proxy(this.processSettingsMenuEvent,this)
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
		if (isMobileDevice) // FIXME: why?
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
		var p = this._prefs;
		if (p && p.seedRatioLimited)
			return p.seedRatioLimit;
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
		return $.map(this.getSelectedRows(),function(r) {
			return r.getTorrent();
		});
	},

	getSelectedTorrentIds: function() {
		return $.map(this.getSelectedRows(),function(r) {
			return r.getTorrentId();
		});
	},

	setSelectedRow: function(row) {
		$(this._torrent_list).children('.selected').removeClass('selected');
		this.selectRow(row);
	},

	selectRow: function(row) {
		$(row.getElement()).addClass('selected');
		this.callSelectionChangedSoon();
	},

	deselectRow: function(row) {
		$(row.getElement()).removeClass('selected');
		this.callSelectionChangedSoon();
	},

	selectAll: function() {
		$(this._torrent_list).children().addClass('selected');
		this.callSelectionChangedSoon();
	},
	deselectAll: function() {
		$(this._torrent_list).children('.selected').removeClass('selected');
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
		this.updateButtonStates();

		this.inspector.setTorrents(this.inspectorIsVisible() ? this.getSelectedTorrents() : []);

		clearTimeout(this.selectionChangedTimer);
		delete this.selectionChangedTimer;

	},

	callSelectionChangedSoon: function()
	{
		if (!this.selectionChangedTimer)
			this.selectionChangedTimer =
				setTimeout($.proxy(this.selectionChanged,this),200);
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
		var handled = false,
		    rows = this._rows,
		    up = ev.keyCode === 38; // up key pressed
		    dn = ev.keyCode === 40, // down key pressed
		    shift = ev.keyCode === 16; // shift key pressed

		if ((up || dn) && rows.length)
		{
			var last = this.indexOfLastTorrent(),
			    i = last,
			    anchor = this._shift_index,
			    r,
			    min = 0,
			    max = rows.length - 1;

			if (dn && (i+1 <= max))
				++i;
			else if (up && (i-1 >= min))
				--i;

			var r = rows[i];

			if (anchor >= 0)
			{
				// user is extending the selection
				// with the shift + arrow keys...
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
			handled = true;
		}
		else if (shift)
		{
			this._shift_index = this.indexOfLastTorrent();
		}

		return !handled;
	},

	keyUp: function(ev)
	{
		if (ev.keyCode === 16) // shift key pressed
			delete this._shift_index;
	},

	isButtonEnabled: function(ev) {
		var p = (ev.target || ev.srcElement).parentNode;
		return p.className!=='disabled'
		    && p.parentNode.className!=='disabled';
	},

	stopAllClicked: function(ev) {
		if (this.isButtonEnabled(ev)) {
			this.stopAllTorrents();
			this.hideMobileAddressbar();
		}
	},

	stopSelectedClicked: function(ev) {
		if (this.isButtonEnabled(ev)) {
			this.stopSelectedTorrents();
			this.hideMobileAddressbar();
		}
	},

	startAllClicked: function(ev) {
		if (this.isButtonEnabled(ev)) {
			this.startAllTorrents();
			this.hideMobileAddressbar();
		}
	},

	startSelectedClicked: function(ev) {
		if (this.isButtonEnabled(ev)) {
			this.startSelectedTorrents(false);
			this.hideMobileAddressbar();
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
		$('#upload_container').hide();
		this.updateButtonStates();
	},

	confirmUploadClicked: function() {
		this.uploadTorrentFile(true);
		this.hideUploadDialog();
	},

	savePrefsClicked: function()
	{
		// handle the clutch prefs locally
		var rate = parseInt ($('#prefs_form #refresh_rate')[0].value, 10);
		if (rate != this[Prefs._RefreshRate])
			this.setPref (Prefs._RefreshRate, rate);

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

		this.remote.savePrefs(o);

		this.hidePrefsDialog();
	},

	removeClicked: function(ev) {
		if (this.isButtonEnabled(ev)) {
			this.removeSelectedTorrents();
			this.hideMobileAddressbar();
		}
	},

	/*
	 * 'Clutch Preferences' was clicked (isMobileDevice only)
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
		clearInterval(this.sessionInterval);
		delete this.sessionInterval;
		if (enabled) {
			var msec = this.getIntervalMsec(Prefs._SessionRefreshRate, 5);
			this.sessionInterval = setInterval($.proxy(this.loadDaemonPrefs,this), msec);
		}
	},

	/* Turn the periodic ajax stats refresh on & off */
	togglePeriodicStatsRefresh: function(enabled) {
		clearInterval(this.statsInterval);
		delete this.statsInterval;
		if (enabled) {
			var msec = this.getIntervalMsec(Prefs._SessionRefreshRate, 5);
			this.statsInterval = setInterval($.proxy(this.loadDaemonStats,this), msec);
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
		var enabled = this[Prefs._TurtleState],
		    w = $('#turtle-button'),
		    t = [ 'Click to ', (enabled?'disable':'enable'), ' Temporary Speed Limits',
		          '(', Transmission.fmt.speed(this._prefs[RPC._TurtleUpSpeedLimit]), 'up,',
		               Transmission.fmt.speed(this._prefs[RPC._TurtleDownSpeedLimit]), 'down)' ];
		w.toggleClass('enabled',enabled);
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
		$('#prefs_container').fadeIn();
		this.hideMobileAddressbar();
		this.updateButtonStates();
		this.togglePeriodicSessionRefresh(false);
	},

	hidePrefsDialog: function()
	{
		$('body.prefs_showing').removeClass('prefs_showing');
		if (isMobileDevice)
			this.hideMobileAddressbar();
		$('#prefs_container').fadeOut();
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

		if (!isMobileDevice)
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
		$('#stats_container').fadeIn();
		this.hideMobileAddressbar();
		this.updateButtonStates();
		this.togglePeriodicStatsRefresh(true);
	},

	hideStatsDialog: function() {
		$('body.stats_showing').removeClass('stats_showing');
		if (isMobileDevice)
			this.hideMobileAddressbar();
		$('#stats_container').fadeOut();
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
		// update our dirty fields
		this.dirtyTorrents[ tor.getId() ] = true;

		// enqueue ui refreshes
		this.refilterSoon();
		this.updateButtonsSoon();
	},

	updateFromTorrentGet: function(updates, removed_ids)
	{
		var i, o, t, id, needed, needinfo = [];

		for (i=0; o=updates[i]; ++i)
		{
			id = o.id;
			if ((t = this._torrents[id]))
			{
				needed = t.needsMetaData();
				t.refresh(o);
				if (needed && !t.needsMetaData())
					needinfo.push(id);
			}
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
			this.updateTorrents(needinfo, ['id'].concat(Torrent.Fields.Metadata, Torrent.Fields.Stats));
			this.refilterSoon();
		}

		if (removed_ids) {
			this.deleteTorrents(removed_ids);
			this.refilterSoon();
		}
	},

	updateTorrents: function(ids, fields)
	{
		this.remote.updateTorrents(ids, fields, this.updateFromTorrentGet, this);
	},

	refreshTorrents: function()
	{
		// send a request right now
		this.updateTorrents('recently-active', ['id'].concat(Torrent.Fields.Stats));

		// schedule the next request
		clearTimeout(this.refreshTorrentsTimeout);
		this.refreshTorrentsTimeout = setTimeout($.proxy(this.refreshTorrents,this), this[Prefs._RefreshRate]*1000);
	},

	initializeTorrents: function()
	{
		this.updateTorrents(null, ['id'].concat(Torrent.Fields.Metadata, Torrent.Fields.Stats));
	},

	onRowClicked: function(ev, row)
	{
		// handle the per-row "torrent_resume" button
		if (ev.target.className === 'torrent_resume') {
			this.startTorrent(row.getTorrent());
			return;
		}

		// handle the per-row "torrent_pause" button
		if (ev.target.className === 'torrent_pause') {
			this.stopTorrent(row.getTorrent());
			return;
		}

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
		if (isMobileDevice) {
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
			for (var i=0, id; id=ids[i]; ++i) {
				this.dirtyTorrents[id] = true;
				delete this._torrents[id];
			}
			this.refilter();
		}
	},

	updateStatusbar: function()
	{
		var i, row,
		    u=0, d=0,
		    fmt = Transmission.fmt,
		    torrents = this.getAllTorrents();

		this.refreshFilterButton();

		// up/down speed
		for (i=0; row=torrents[i]; ++i) {
			u += row.getUploadSpeed();
			d += row.getDownloadSpeed();
		}

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

		// Submit the upload form
		} else {
			var args = { };
			var remote = this.remote;
			var paused = !$('#torrent_auto_start').is(':checked');
			if ('' != $('#torrent_upload_url').val()) {
				remote.addTorrentByUrl($('#torrent_upload_url').val(), { paused: paused });
			} else {
				args.url = '../upload?paused=' + paused;
				args.type = 'POST';
				args.data = { 'X-Transmission-Session-Id' : remote._token };
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
	changeFileCommand: function(torrentId, rowIndices, command) {
		this.remote.changeFileCommand(torrentId, rowIndices, command);
	},

	hideMobileAddressbar: function(delaySecs) {
		if (isMobileDevice && !scroll_timeout) {
			var delayMsec = delaySecs*1000 || 150;
			scroll_timeout = setTimeout($.proxy(this.doToolbarHide,this), delayMsec);
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

	setEnabled: function(key, flag)
	{
		$(key).toggleClass('disabled', !flag);
	},

	updateButtonsSoon: function()
	{
		if (!this.buttonRefreshTimer)
			this.buttonRefreshTimer = setTimeout($.proxy(this.updateButtonStates,this), 100);
	},

	updateButtonStates: function()
	{
		clearTimeout(this.buttonRefreshTimer);
		delete this.buttonRefreshTimer;

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

	inspectorIsVisible: function()
	{
		return $('#torrent_inspector').is(':visible');
	},
	toggleInspector: function()
	{
		this.setInspectorVisible(!this.inspectorIsVisible());
	},
	setInspectorVisible: function(visible)
	{
		// update the ui widgetry
		$('#torrent_inspector').toggle(visible);
		if (isMobileDevice) {
			$('body').toggleClass('inspector_showing',visible);
			$('#inspector_close').toggle(visible);
			this.hideMobileAddressbar();
		} else {
			var w = visible ? $('#torrent_inspector').width() + 1 + 'px' : '0px';
			$('#torrent_container')[0].style.right = w;
		}
		setInnerHTML($('ul li#context_toggle_inspector')[0], (visible?'Hide':'Show')+' Inspector');

		if (visible)
			this.inspector.setTorrents(this.getSelectedTorrents());
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
			position: isMobileDevice ? [0,0] : [40,80],
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
		var o, tmp, text, torrent_count,
		    state = this[Prefs._FilterMode],
		    state_all = state == Prefs._FilterAll,
		    state_string = this.getStateString(state),
		    tracker = this.filterTracker,
		    tracker_all = !tracker,
		    tracker_string = tracker ? this.getReadableDomain(tracker) : '',
		    visible_count = this._rows.length;

		// count the total number of torrents
		// torrent_count = Object.keys(this._torrents).length; // IE8 doesn't support Object.keys(
		torrent_count = 0;
		o = this._torrents;
		for (tmp in o)
			if (o.hasOwnProperty(tmp))
				++torrent_count;

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
		if (!this.refilterTimer) {
			var tr = this;
			this.refilterTimer = setTimeout(function(){tr.refilter(false);}, 100);
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
		var i, e, id, t, row, tmp, rows, clean_rows, dirty_rows, frag,
		    sort_mode = this[Prefs._SortMethod],
		    sort_direction = this[Prefs._SortDirection],
		    filter_mode = this[Prefs._FilterMode],
		    filter_text = this.filterText,
		    filter_tracker = this.filterTracker,
		    renderer = this.torrentRenderer,
		    list = this._torrent_list,
		    old_sel_count = $(list).children('.selected').length;


		clearTimeout(this.refilterTimer);
		delete this.refilterTimer;

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
		$(e).detach();

		// drop any dirty rows that don't pass the filter test
		tmp = [];
		for (i=0; row=dirty_rows[i]; ++i) {
			id = row.getTorrentId();
			t = this._torrents[ id ];
			if (t && t.test(filter_mode, filter_text, filter_tracker))
				tmp.push(row);
			delete this.dirtyTorrents[id];
		}
		dirty_rows = tmp;

		// make new rows for dirty torrents that pass the filter test
		// but don't already have a row
		for (id in this.dirtyTorrents) {
			t = this._torrents[id];
			if (t && t.test(filter_mode, filter_text, filter_tracker)) {
				row = new TorrentRow(renderer, this, t);
				e = row.getElement();
				e.row = row;
				dirty_rows.push(row);
				var tr = this;
				$(e).click(function(ev){tr.onRowClicked(ev,ev.currentTarget.row);});
				$(e).dblclick(function(ev){tr.toggleInspector();});
			}
		}

		// sort the dirty rows
		this.sortRows (dirty_rows);

		// now we have two sorted arrays of rows
		// and can do a simple two-way sorted merge.
		rows = [];
		var ci=0, cmax=clean_rows.length;
		var di=0, dmax=dirty_rows.length;
		frag = document.createDocumentFragment();
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
					frag.appendChild(e);
				rows.push(row);
			}
		}
		list.appendChild(frag);

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
		if (old_sel_count !== $(list).children('.selected').length)
			this.selectionChanged();
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
		//var names = Object.keys(trackers).sort(); (IE8 doesn't have Object.keys)
		var name, name=[];
		var names = [];
		for  (name in trackers)
			names.push (name);
		names.sort();

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

		// update the ui: footer button
		$("#compact-button").toggleClass('enabled',compact);

		// update the ui: torrent list
		this.torrentRenderer = compact ? new TorrentRendererCompact()
		                               : new TorrentRendererFull();
		this.refilter(true);
	}
};
