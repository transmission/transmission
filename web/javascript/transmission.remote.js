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
RPC._PeerPort               = 'port';
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
	},

	/*
	 * Display an error if an ajax request fails, and stop sending requests
	 */
	ajaxError: function(request, error_string, exception) {
		this._error = request.responseText
		            ? request.responseText.trim().replace(/(<([^>]+)>)/ig,"")
		            : "";
		if( !this._error.length )
			this._error = 'Server not responding';
		
		dialog.confirm('Connection Failed', 
			'Could not connect to the server. You may need to reload the page to reconnect.', 
			'Details',
			'alert(transmission.remote._error);',
			null,
			'Dismiss');
		transmission.togglePeriodicRefresh(false);
	},
	
	sendRequest: function( data, success ) {
		$.ajax( {
			url: RPC._Root,
			type: 'POST',
			contentType: 'json',
			dataType: 'json',
			cache: false,
			data: $.toJSON(data),
			error: this.ajaxError,
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

	loadTorrents: function(update_files) {
		var tr = this._controller;
		var o = {
			method: 'torrent-get',
			arguments: { fields: [
				'addedDate', 'announceURL', 'comment', 'creator',
				'dateCreated', 'downloadedEver', 'error', 'errorString',
				'eta', 'hashString', 'haveUnchecked', 'haveValid', 'id',
				'isPrivate', 'leechers', 'leftUntilDone', 'name',
				'peersConnected', 'peersGettingFromUs', 'peersSendingToUs',
				'rateDownload', 'rateUpload', 'seeders', 'sizeWhenDone',
				'status', 'swarmSpeed', 'totalSize', 'uploadedEver' ]
			}
		};
		if (update_files) {
			o.arguments.fields.push('files');
			o.arguments.fields.push('wanted');
			o.arguments.fields.push('priorities');
		}
		this.sendRequest( o, function(data) {
			tr.updateAllTorrents( data.arguments.torrents );
		} );
	},
	
	loadTorrentFiles: function( torrent_ids ) {
		var tr = this._controller;
		this.sendRequest( {
			method: 'torrent-get',
			arguments: { fields: [ 'files', 'wanted', 'priorities'] },
			ids: torrent_ids
		}, function(data) {
			tr.updateTorrentsData( data.arguments.torrents );
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
			remote.loadTorrentFiles( torrent_ids );
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
			remote.loadTorrents();
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
		var remote = this,
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
			remote.loadTorrents();
		} );
	},
	verifyTorrents: function( torrents ) {
		this.sendTorrentCommand( 'torrent-verify', torrents );
	},
	addTorrentByUrl: function( url, options ) {
		this.sendRequest( RPC._Root, $.toJSON({
			method: 'torrent-add',
			arguments: {
				paused: (options.paused ? 'true' : 'false'),
				filename: url
			}
		}) );
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
