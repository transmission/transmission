/*
 * Copyright © Dave Perrett and Malcolm Jarvis
 * This code is licensed under the GPL version 2.
 * For details, see http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 *
 * Class TransmissionRemote
 */

function RPC() { }
//Prefs.prototype = { }

// Constants
RPC._Root                   = '/transmission/rpc';
RPC._Encryption             = 'encryption';
RPC._EncryptionPreferred    = 'preferred';
RPC._EncryptionRequired     = 'required';
RPC._UpSpeedLimit           = 'speed-limit-up';
RPC._DownSpeedLimit         = 'speed-limit-down';
RPC._DownloadDir            = 'download-dir';
RPC._PeerPort               = 'peer-port';
RPC._UpSpeedLimited         = 'speed-limit-up-enabled';
RPC._DownSpeedLimited       = 'speed-limit-down-enabled';

function TransmissionRemote( controller )
{
	this.initialize( controller );
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
		if(request.status == 409 && (token = request.getResponseHeader('X-Transmission-Session-Id'))){
			remote._token = token;
			$.ajax(ajaxObject);
			return;
		}

		remote._error = request.responseText
					? request.responseText.trim().replace(/(<([^>]+)>)/ig,"")
					: "";
		if( !remote._error.length )
			remote._error = 'Server not responding';
		
		dialog.confirm('Connection Failed', 
			'Could not connect to the server. You may need to reload the page to reconnect.', 
			'Details',
			'alert(remote._error);',
			null,
			'Dismiss');
		remote._controller.togglePeriodicRefresh(false);
	},

	appendSessionId: function(XHR) {
		XHR.setRequestHeader('X-Transmission-Session-Id', this._token);
	},

	sendRequest: function( data, success ) {
		remote = this;
		$.ajax( {
			url: RPC._Root,
			type: 'POST',
			contentType: 'json',
			dataType: 'json',
			cache: false,
			data: $.toJSON(data),
			beforeSend: function(XHR){ remote.appendSessionId(XHR) },
			error: function(request, error_string, exception){ remote.ajaxError(request, error_string, exception, this) },
			success: success
		} );
	},

	loadDaemonPrefs: function() {
		var tr = this._controller;
		var o = { method: 'session-get' };
		this.sendRequest( o, function(data) {
			var o = data.arguments;
			Prefs.getClutchPrefs( o );
			tr.updatePrefs( o );
		} );
	},

	getInitialDataFor: function(torrent_ids, callback) {
		var o = {
			method: 'torrent-get',
			arguments: {
			fields: [ 'addedDate', 'announceURL', 'comment', 'creator',
				'dateCreated', 'downloadedEver', 'error', 'errorString',
				'eta', 'hashString', 'haveUnchecked', 'haveValid', 'id',
				'isPrivate', 'leechers', 'leftUntilDone', 'name',
				'peersConnected', 'peersGettingFromUs', 'peersSendingToUs',
				'rateDownload', 'rateUpload', 'seeders', 'sizeWhenDone',
				'status', 'swarmSpeed', 'totalSize',
				'uploadedEver', 'uploadRatio', 'seedRatioLimit', 'seedRatioMode',
				'downloadDir', 'files', 'fileStats' ]
			}
		};

		if(torrent_ids)
			o.arguments.ids = torrent_ids;

		this.sendRequest( o, function(data){ callback(data.arguments.torrents)} );
	},

	getUpdatedDataFor: function(torrent_ids, callback) {
		var o = {
			method: 'torrent-get',
			arguments: {
				'ids': torrent_ids,
				fields: [  'id', 'downloadedEver', 'error', 'errorString',
					'eta', 'haveUnchecked', 'haveValid', 'leechers', 'leftUntilDone',
					'peersConnected', 'peersGettingFromUs', 'peersSendingToUs',
					'rateDownload', 'rateUpload', 'recheckProgress', 'seeders',
					'sizeWhenDone', 'status', 'swarmSpeed',
					'uploadedEver', 'uploadRatio', 'seedRatioLimit', 'seedRatioMode',
					'downloadDir' ]
			}
		};

		this.sendRequest( o, function(data){ callback(data.arguments.torrents, data.arguments.removed)} );
	},

	loadTorrentFiles: function( torrent_ids ) {
		var tr = this._controller;
		this.sendRequest( {
			method: 'torrent-get',
			arguments: { fields: [ 'id', 'fileStats'], ids: torrent_ids }
		}, function(data) {
			tr.updateTorrentsFileData( data.arguments.torrents );
		} );
	},
	
	changeFileCommand: function( command, torrent, file ) {
		var remote = this;
		var torrent_ids = [ torrent.id() ];
		var o = {
			method: 'torrent-set',
			arguments: { ids: torrent_ids }
		};
		o.arguments[command] = [ file._index ];
		this.sendRequest( o, function( ) {
			remote._controller.refreshTorrents( torrent_ids );
		} );
	},
	
	sendTorrentCommand: function( method, torrents ) {
		var remote = this;
		var o = {
			method: method,
			arguments: { ids: [ ] }
		};
		if( torrents != null )
			for( var i=0, len=torrents.length; i<len; ++i )
				o.arguments.ids.push( torrents[i].id() );
		this.sendRequest( o, function( ) {
			remote._controller.refreshTorrents();
		} );
	},
	
	startTorrents: function( torrents ) {
		this.sendTorrentCommand( 'torrent-start', torrents );
	},
	stopTorrents: function( torrents ) {
		this.sendTorrentCommand( 'torrent-stop', torrents );
	},
	removeTorrents: function( torrents ) {
		this.sendTorrentCommand( 'torrent-remove', torrents );
	},
	removeTorrentsAndData: function( torrents ) {
		var remote = this;
		var o = {
			method: 'torrent-remove',
			arguments: {
				'delete-local-data': true,
				ids: [ ]
			}
		};

		if( torrents != null )
			for( var i=0, len=torrents.length; i<len; ++i )
				o.arguments.ids.push( torrents[i].id() );
		this.sendRequest( o, function( ) {
			remote._controller.refreshTorrents();
		} );
	},
	verifyTorrents: function( torrents ) {
		this.sendTorrentCommand( 'torrent-verify', torrents );
	},
	addTorrentByUrl: function( url, options ) {
		var remote = this;
		var o = {
			method: 'torrent-add',
			arguments: {
				paused: (options.paused ? 'true' : 'false'),
				filename: url
			}
		};
		
		this.sendRequest(o, function() {
			remote._controller.refreshTorrents();
		} );
	},
	savePrefs: function( args ) {
		var remote = this;
		var o = {
			method: 'session-set',
			arguments: args
		};
		this.sendRequest( o, function() {
			remote.loadDaemonPrefs();
		} );
	}
};
