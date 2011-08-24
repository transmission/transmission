/*
 * Copyright © Dave Perrett, Malcolm Jarvis and Bruno Bierbaumer
 * This code is licensed under the GPL version 2.
 * For details, see http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 *
 * Class TransmissionRemote
 */

function RPC() { }
//Prefs.prototype = { }

// Constants
RPC._Root                   = '../rpc';
RPC._DaemonVersion          = 'version';
RPC._Encryption             = 'encryption';
RPC._EncryptionPreferred    = 'preferred';
RPC._EncryptionRequired     = 'required';
RPC._UpSpeedLimit           = 'speed-limit-up';
RPC._DownSpeedLimit         = 'speed-limit-down';
RPC._DownloadDir            = 'download-dir';
RPC._PeerPort               = 'peer-port';
RPC._UpSpeedLimited         = 'speed-limit-up-enabled';
RPC._DownSpeedLimited       = 'speed-limit-down-enabled';
RPC._TurtleState            = 'alt-speed-enabled';
RPC._TurtleUpSpeedLimit     = 'alt-speed-up';
RPC._TurtleDownSpeedLimit   = 'alt-speed-down';
RPC._TurtleTimeEnabled      = 'alt-speed-time-enabled';
RPC._TurtleTimeBegin        = 'alt-speed-time-begin';
RPC._TurtleTimeEnd          = 'alt-speed-time-end';
RPC._TurtleTimeDay          = 'alt-speed-time-day';
RPC._PeerLimitGlobal        = 'peer-limit-global';
RPC._PeerLimitPerTorrent    = 'peer-limit-per-torrent';
RPC._PexEnabled	            = 'pex-enabled';
RPC._DhtEnabled             = 'dht-enabled';
RPC._LpdEnabled             = 'lpd-enabled';
RPC._BlocklistEnabled       = 'blocklist-enabled';
RPC._BlocklistURL           = 'blocklist-url';
RPC._BlocklistSize          = 'blocklist-size';
RPC._UtpEnabled	            = 'utp-enabled';
RPC._PeerPortRandom         = 'peer-port-random-on-start';
RPC._PortForwardingEnabled  = 'port-forwarding-enabled';
RPC._StartAddedTorrent      = 'start-added-torrents';
RPC._QueueMoveTop			= 'queue-move-top';
RPC._QueueMoveBottom		= 'queue-move-bottom';
RPC._QueueMoveUp			= 'queue-move-up';
RPC._QueueMoveDown			= 'queue-move-down';

function TransmissionRemote(controller)
{
	this.initialize(controller);
	return this;
}

TransmissionRemote.prototype =
{
	/*
	 * Constructor
	 */
	initialize: function(controller) {
		this._controller = controller;
		this._error = '';
		this._token = '';
	},

	/*
	 * Display an error if an ajax request fails, and stop sending requests
	 * or on a 409, globally set the X-Transmission-Session-Id and resend
	 */
	ajaxError: function(request, error_string, exception, ajaxObject) {
		var token;
		remote = this;

		// set the Transmission-Session-Id on a 409
		if (request.status == 409 && (token = request.getResponseHeader('X-Transmission-Session-Id'))){
			remote._token = token;
			$.ajax(ajaxObject);
			return;
		}

		remote._error = request.responseText
					? request.responseText.trim().replace(/(<([^>]+)>)/ig,"")
					: "";
		if (!remote._error.length)
			remote._error = 'Server not responding';

		dialog.confirm('Connection Failed',
			'Could not connect to the server. You may need to reload the page to reconnect.',
			'Details',
			'alert(remote._error);',
			null,
			'Dismiss');
		remote._controller.togglePeriodicRefresh(false);
		remote._controller.togglePeriodicSessionRefresh(false);
	},

	appendSessionId: function(XHR) {
		XHR.setRequestHeader('X-Transmission-Session-Id', this._token);
	},

	sendRequest: function(data, success, async) {
		remote = this;
		if (typeof async != 'boolean')
			async = true;

		var ajaxSettings = {
			url: RPC._Root,
			type: 'POST',
			contentType: 'json',
			dataType: 'json',
			cache: false,
			data: $.toJSON(data),
			beforeSend: function(XHR){ remote.appendSessionId(XHR); },
			error: function(request, error_string, exception){ remote.ajaxError(request, error_string, exception, ajaxSettings); },
			success: success,
			async: async
		};

		$.ajax(ajaxSettings);
	},

	loadDaemonPrefs: function(callback, async) {
		var o = { method: 'session-get' };
		this.sendRequest(o, callback, async);
	},
	
	checkPort: function(callback, async) {
		var o = { method: 'port-test' };
		this.sendRequest(o, callback, async);
	},
	
	loadDaemonStats: function(callback, async) {
		var o = { method: 'session-stats' };
		this.sendRequest(o, callback, async);
	},

	getInitialDataFor: function(torrent_ids, callback) {
		var o = {
			method: 'torrent-get',
			arguments: {
			fields: Torrent._StaticFields.concat(Torrent._MetaDataFields,
			                                     Torrent._DynamicFields,
			                                     [ 'files', 'fileStats' ])
			}
		};

		if (torrent_ids)
			o.arguments.ids = torrent_ids;

		this.sendRequest(o, function(data){ callback(data.arguments.torrents);});
	},

	getMetaDataFor: function(torrent_ids, callback) {
		var o = {
			method: 'torrent-get',
			arguments: {
			fields: Torrent._StaticFields.concat(Torrent._MetaDataFields,
			                                     ['files', 'fileStats'])
			}
		};

		if (torrent_ids)
			o.arguments.ids = torrent_ids;

		this.sendRequest(o, function(data) {callback(data.arguments.torrents)});
	},

	getUpdatedDataFor: function(torrent_ids, callback) {
		var o = {
			method: 'torrent-get',
			arguments: {
				'ids': torrent_ids,
				fields: [ 'id' ].concat(Torrent._DynamicFields)
			}
		};

		this.sendRequest(o, function(data) {callback(data.arguments.torrents, data.arguments.removed);});
	},

	loadTorrentFiles: function(torrent_ids) {
		var tr = this._controller;
		this.sendRequest({
			method: 'torrent-get',
			arguments: { fields: [ 'id', 'fileStats'], ids: torrent_ids }
		}, function(data) {
			tr.updateTorrentsFileData(data.arguments.torrents);
		});
	},

	changeFileCommand: function(command, rows) {
		var remote = this;
		var torrent_ids = [ rows[0].getTorrent().getId() ];
		var files = [];
		for (var i=0, row; row=rows[i]; ++i)
			files.push(row.getIndex());
		var o = {
			method: 'torrent-set',
			arguments: { ids: torrent_ids }
		};
		o.arguments[command] = files;
		this.sendRequest(o, function() {
			remote._controller.refreshTorrents(torrent_ids);
		});
	},

	sendTorrentSetRequests: function(method, torrent_ids, args, callback) {
		if (!args) args = { };
		args['ids'] = torrent_ids;
		var o = {
			method: method,
			arguments: args
		};

		this.sendRequest(o, function(data) {
			callback();
		});
	},

	sendTorrentActionRequests: function(method, torrent_ids, callback) {
		this.sendTorrentSetRequests(method, torrent_ids, null, callback);
	},

	startTorrents: function(torrent_ids, noqueue, callback) {
		var name = noqueue ? 'torrent-start-now' : 'torrent-start';
		this.sendTorrentActionRequests(name, torrent_ids, callback);
	},
	stopTorrents: function(torrent_ids, callback) {
		this.sendTorrentActionRequests('torrent-stop', torrent_ids, callback);
	},
	removeTorrents: function(torrent_ids, callback) {
		this.sendTorrentActionRequests('torrent-remove', torrent_ids, callback);
	},
	removeTorrentsAndData: function(torrents) {
		var remote = this;
		var o = {
			method: 'torrent-remove',
			arguments: {
				'delete-local-data': true,
				ids: [ ]
			}
		};

		if (torrents) {
			for (var i=0, len=torrents.length; i<len; ++i) {
				o.arguments.ids.push(torrents[i].getId());
			}
		}
		this.sendRequest(o, function() {
			remote._controller.refreshTorrents();
		});
	},
	verifyTorrents: function(torrent_ids, callback) {
		this.sendTorrentActionRequests('torrent-verify', torrent_ids, callback);
	},
	reannounceTorrents: function(torrent_ids, callback) {
		this.sendTorrentActionRequests('torrent-reannounce', torrent_ids, callback);
	},
	addTorrentByUrl: function(url, options) {
		var remote = this;
        if (url.match(/^[0-9a-f]{40}$/i)) {
            url = 'magnet:?xt=urn:btih:'+url;
        }
		var o = {
			method: 'torrent-add',
			arguments: {
				paused: (options.paused),
				filename: url
			}
		};
		this.sendRequest(o, function() {
			remote._controller.refreshTorrents();
		});
	},
	savePrefs: function(args) {
		var remote = this;
		var o = {
			method: 'session-set',
			arguments: args
		};
		this.sendRequest(o, function() {
			remote._controller.loadDaemonPrefs();
		});
	},
	updateBlocklist: function() {
		var remote = this;
		var o = {
			method: 'blocklist-update'
		};
		this.sendRequest(o, function() {
			remote._controller.loadDaemonPrefs();
		});
	},

	// Added queue calls
	moveTorrentsToTop: function(torrent_ids, callback) {
		this.sendTorrentActionRequests(RPC._QueueMoveTop, torrent_ids, callback);
	},
	moveTorrentsToBottom: function(torrent_ids, callback) {
		this.sendTorrentActionRequests(RPC._QueueMoveBottom, torrent_ids, callback);
	},
	moveTorrentsUp: function(torrent_ids, callback) {
		this.sendTorrentActionRequests(RPC._QueueMoveUp, torrent_ids, callback);
	},
	moveTorrentsDown: function(torrent_ids, callback) {
		this.sendTorrentActionRequests(RPC._QueueMoveDown, torrent_ids, callback);
	}
};
