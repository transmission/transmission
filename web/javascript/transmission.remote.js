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

RPC._TorrentActivity        = (1<<0);
RPC._TorrentAnnounce        = (1<<1);
RPC._TorrentError           = (1<<2);
RPC._TorrentFiles           = (1<<3);
RPC._TorrentHistory         = (1<<4);
RPC._TorrentId              = (1<<5);
RPC._TorrentInfo            = (1<<6);
RPC._TorrentLimits          = (1<<7);
RPC._TorrentPeers           = (1<<8);
RPC._TorrentPriorities      = (1<<9);
RPC._TorrentScrape          = (1<<10);
RPC._TorrentSize            = (1<<11);
RPC._TorrentTrackerStats    = (1<<12);
RPC._TorrentTrackers        = (1<<13);
RPC._TorrentWebseeds        = (1<<14);

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

	sendRequest: function( url, data, success, contentType )
	{
		var o = { };
		o.cache = false;
		o.contentType = contentType;
		o.data = data;
		o.dataType = 'json';
		o.error = this.ajaxError;
		o.success = success;
		o.type = 'POST';
		o.url = url;
		$.ajax( o );
	},

	loadDaemonPrefs: function() {
		var tr = this._controller;
		var o = { };
		o.method = 'session-get';
		this.sendRequest( RPC._Root, $.toJSON(o), function(data) {
			var o = data.arguments;
			Prefs.getClutchPrefs( o );
			tr.updatePrefs( o );
		}, "json" );
	},

	loadTorrents: function() {
		var tr = this._controller;
		var o = { };
		o.method = 'torrent-get'
		o.arguments = { };
		o.arguments.fields = RPC._TorrentActivity
                                   + RPC._TorrentAnnounce
                                   + RPC._TorrentError
                                   + RPC._TorrentHistory
                                   + RPC._TorrentId
                                   + RPC._TorrentInfo
                                   + RPC._TorrentLimits
                                   + RPC._TorrentScrape
                                   + RPC._TorrentSize
                                   + RPC._TorrentTrackerStats;
		this.sendRequest( RPC._Root, $.toJSON(o), function(data) {
			tr.updateTorrents( data.arguments.torrents );
		}, "json" );
	},

	sendTorrentCommand: function( method, torrents ) {
		var remote = this;
		var o = { };
		o.method = method;
		o.arguments = { };
		o.arguments.ids = [ ];
		if( torrents != null )
			for( var i=0, len=torrents.length; i<len; ++i )
				o.arguments.ids.push( torrents[i].id() );
		this.sendRequest( RPC._Root, $.toJSON(o), function( ) {
			remote.loadTorrents();
		}, "json" );
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

	savePrefs: function( args ) {
		var remote = this;
		var o = { };
		o.method = 'session-set';
		o.arguments = args;
		this.sendRequest( RPC._Root, $.toJSON(o), function(){
			remote.loadDaemonPrefs();
		}, "json" );
	},

	/*
	 * Upload Torrent by URL
	addTorrentByURL: function() {
		$('#torrent_upload_form')[0].action = 'remote/index.php?action=addTorrentByURL&param=[]';
		$('#torrent_upload_form').ajaxSubmit({dataType: 'script', type: 'POST'});
	},
	 */
};
