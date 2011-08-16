/*
 *   Copyright © Jordan Lee
 *   This code is licensed under the GPL version 2.
 *   <http://www.gnu.org/licenses/old-licenses/gpl-2.0.html>
 */

/****
*****
*****
****/

function TorrentRendererHelper()
{
}

TorrentRendererHelper.getProgressInfo = function( controller, t )
{
	var seed_ratio_limit = t.seedRatioLimit( controller );

	var pct = 0;
	if( t.needsMetaData( ) )
		pct = t._metadataPercentComplete * 100;
	else if( !t.isDone( ) )
		pct = Math.round( t.getPercentDone() * 100 );
	else if( seed_ratio_limit > 0 )
		pct = Math.round( t._upload_ratio * 100 / seed_ratio_limit );
	else
		pct = 100;

	var extra;
	if( t.isStopped( ) )
		extra = 'paused';
	else if( t.isSeeding( ) )
		extra = 'seeding';
	else if( t.needsMetaData( ) )
		extra = 'magnet';
	else
		extra = 'leeching';

	return {
		percent: pct,
		complete: [ 'torrent_progress_bar', 'complete', extra ].join(' '),
		incomplete: [ 'torrent_progress_bar', 'incomplete', extra ].join(' ')
	};
}

TorrentRendererHelper.renderProgressbar = function( controller, t, complete, incomplete )
{
	var info = TorrentRendererHelper.getProgressInfo( controller, t );
	var e;
	e = complete;
	e.style.width = '' + info.percent + "%";
	e.className = info.complete;
	e.style.display = info.percent<=0 ? 'none' : 'block';
	e = incomplete;
	e.className = info.incomplete;
	e.style.display = info.percent>=100 ? 'none' : 'block';
}

TorrentRendererHelper.formatUL = function( t )
{
	return 'UL: ' + Transmission.fmt.speedBps( t.uploadSpeed( ) );
}

TorrentRendererHelper.formatDL = function( t )
{
	return 'DL: ' + Transmission.fmt.speedBps( t.downloadSpeed( ) );
}

/****
*****
*****
****/

function TorrentRendererFull()
{
}
TorrentRendererFull.prototype =
{
	createRow: function( )
	{
		var root = document.createElement( 'li' );
		root.className = 'torrent';

		var name = document.createElement( 'div' );
		name.className = 'torrent_name';

		var peers = document.createElement( 'div' );
		peers.className = 'torrent_peer_details';

		var complete = document.createElement( 'div' );
		complete.className = 'torrent_progress_bar complete';
		var incomplete = document.createElement( 'div' );
		incomplete.className = 'torrent_progress_bar incomplete';
		var progressbar = document.createElement( 'div' );
		progressbar.className = 'torrent_progress_bar_container full';
		progressbar.appendChild( complete );
		progressbar.appendChild( incomplete );

		var details = document.createElement( 'div' );
		details.className = 'torrent_progress_details';

		var image = document.createElement( 'div' );
		var button = document.createElement( 'a' );
		button.appendChild( image );

		root.appendChild( name );
		root.appendChild( peers );
		root.appendChild( button );
		root.appendChild( progressbar );
		root.appendChild( details );

		root._name_container = name;
		root._peer_details_container = peers;
		root._progress_details_container = details;
		root._progress_complete_container = complete;
		root._progress_incomplete_container = incomplete;
		root._pause_resume_button_image = image;
		root._toggle_running_button = button;

		return root;
	},

	getPeerDetails: function( t )
	{
		var err;
		if(( err = t.getErrorMessage()))
			return err;

		if( t.isDownloading( ) )
			return [ 'Downloading from',
			         t.peersSendingToUs(),
			         'of',
			         t._peers_connected,
			         'peers',
			         '-',
			         TorrentRendererHelper.formatDL(t),
			         TorrentRendererHelper.formatUL(t) ].join(' ');

		if( t.isSeeding( ) )
			return [ 'Seeding to',
			         t.peersGettingFromUs(),
			         'of',
			         t._peers_connected,
			         'peers',
			         '-',
			         TorrentRendererHelper.formatUL(t) ].join(' ');

		if( t.state() === Torrent._StatusCheck )
			return [ 'Verifying local data (',
			         Transmission.fmt.percentString( 100.0 * t._recheckProgress ),
			         '% tested)' ].join('');

		return t.stateStr( );
	},

	getProgressDetails: function( controller, t )
	{
		if( t.needsMetaData() ) {
			var percent = 100 * t._metadataPercentComplete;
			return [ "Magnetized transfer - retrieving metadata (",
			         Transmission.fmt.percentString( percent ),
			         "%)" ].join('');
		}

		var c;
		var is_done = ( t.isDone( ) )
		           || ( t.state() === Torrent._StatusSeeding );
		if( is_done ) {
			if( t._size == t._sizeWhenDone ) // seed: '698.05 MiB'
				c = [ Transmission.fmt.size( t._size ) ];
			else // partial seed: '127.21 MiB of 698.05 MiB (18.2%)'
				c = [ Transmission.fmt.size( t._sizeWhenDone ),
				      ' of ',
				      Transmission.fmt.size( t._size ),
				      ' (', t.getPercentDoneStr, '%)' ];
			// append UL stats: ', uploaded 8.59 GiB (Ratio: 12.3)'
			c.push( ', uploaded ',
			        Transmission.fmt.size( t._upload_total ),
			        ' (Ratio ',
			        Transmission.fmt.ratioString( t._upload_ratio ),
			        ')' );
		} else { // not done yet
			c = [ Transmission.fmt.size( t._sizeWhenDone - t._leftUntilDone ),
			      ' of ', Transmission.fmt.size( t._sizeWhenDone ),
			      ' (', t.getPercentDoneStr(), '%)' ];
		}

		// maybe append eta
		if( t.isActive() && ( !is_done || t.seedRatioLimit(controller)>0 ) ) {
			c.push(' - ');
			if (t._eta < 0 || this._eta >= Torrent._InfiniteTimeRemaining )
				c.push( 'remaining time unknown' );
			else
				c.push( Transmission.fmt.timeInterval(t._eta),
				        ' remaining' );
		}

		return c.join('');
	},

	render: function( controller, t, root )
	{
		// name
		setInnerHTML( root._name_container, t.name() );

		// progressbar
		TorrentRendererHelper.renderProgressbar(
			controller, t,
			root._progress_complete_container,
			root._progress_incomplete_container );

		// peer details
		var has_error = t._error !== Torrent._ErrNone;
		var e = root._peer_details_container;
		$(e).toggleClass('error',has_error);
		setInnerHTML( e, this.getPeerDetails( t ) );

		// progress details
		e = root._progress_details_container;
		setInnerHTML( e, this.getProgressDetails( controller, t ) );

		// pause/resume button
		var is_stopped = t.state() === Torrent._StatusStopped;
		e = root._pause_resume_button_image;
		e.alt = is_stopped ? 'Resume' : 'Pause';
		e.className = is_stopped ? 'torrent_resume' : 'torrent_pause';
	}
};

/****
*****
*****
****/

function TorrentRendererCompact()
{
}
TorrentRendererCompact.prototype =
{
	createRow: function( )
	{
		var complete = document.createElement( 'div' );
		complete.className = 'torrent_progress_bar complete';
		var incomplete = document.createElement( 'div' );
		incomplete.className = 'torrent_progress_bar incomplete';
		var progressbar = document.createElement( 'div' );
		progressbar.className = 'torrent_progress_bar_container compact';
		progressbar.appendChild( complete );
		progressbar.appendChild( incomplete );

		var details = document.createElement( 'div' );
		details.className = 'torrent_peer_details compact';

		var name = document.createElement( 'div' );
		name.className = 'torrent_name';

		var root = document.createElement( 'li' );
		root.appendChild( progressbar );
		root.appendChild( details );
		root.appendChild( name );
		root.className = 'torrent compact';
		root._progress_complete_container = complete;
		root._progress_incomplete_container = incomplete;
		root._details_container = details;
		root._name_container = name;
		return root;
	},

	getPeerDetails: function( t )
	{
		var c;
		if(( c = t.getErrorMessage()))
			return c;
		if( t.isDownloading( ) )
			return [ TorrentRendererHelper.formatDL(t),
			         TorrentRendererHelper.formatUL(t) ].join(' ');
		if( t.isSeeding( ) )
			return TorrentRendererHelper.formatUL(t);
		return t.stateStr( );
	},

	render: function( controller, t, root )
	{
		// name
		var is_stopped = t.state() === Torrent._StatusStopped;
		var e = root._name_container;
		$(e).toggleClass( 'paused', is_stopped );
		setInnerHTML( e, t.name() );

		// peer details
		var has_error = t._error !== Torrent._ErrNone;
		e = root._details_container;
		$(e).toggleClass('error', has_error );
		setInnerHTML( e, this.getPeerDetails( t ) );

		// progressbar
		TorrentRendererHelper.renderProgressbar(
			controller, t,
			root._progress_complete_container,
			root._progress_incomplete_container );
	}
};

/****
*****
*****
****/

function TorrentRow( controller, generator )
{
        this.initialize( controller, generator );
}
TorrentRow.prototype =
{
	initialize: function( controller, generator ) {
		this._generator = generator;
		var root = generator.createRow( );
		this._element = root;
                $(root).bind('dblclick', function(e) { controller.toggleInspector(); });

	},
	getElement: function( ) {
		return this._element;
	},
	render: function( controller ) {
		var tor = this.getTorrent( );
		if( tor !== null )
			this._generator.render( controller, tor, this.getElement( ) );
	},
	isSelected: function( ) {
		return this.getElement().className.indexOf('selected') != -1;
	},
	setSelected: function( flag ) {
		$(this.getElement()).toggleClass( 'selected', flag );
	},

	getToggleRunningButton: function( ) {
		return this.getElement()._toggle_running_button;
	},

	setVisible: function( visible ) {
		this.getElement().style.display = visible ? 'block' : 'none';

		if( !visible )
			this.setSelected( false );
	},
	isVisible: function( visible ) {
		return this.getElement().style.display === 'block';
	},

	setTorrent: function( controller, t ) {
		if( this._torrent !== t ) {
			if( this._torrent )
				$(this).unbind('dataChanged.renderer');
			if(( this._torrent = t ))
				$(this).bind('dataChanged.renderer',this.render(controller));
		}
	},
	getTorrent: function() {
		return this._torrent;
	},
	isEven: function() {
		return this.getElement().className.indexOf('even') != -1;
	},
	setEven: function( even ) {
		if( this.isEven() != even )
			$(this.getElement()).toggleClass('even', even);
	}
};
