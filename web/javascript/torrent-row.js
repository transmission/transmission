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

TorrentRendererHelper.getProgressInfo = function(controller, t)
{
	var seed_ratio_limit = t.seedRatioLimit(controller);

	var pct = 0;
	if (t.needsMetaData())
		pct = t.getMetadataPercentComplete() * 100;
	else if (!t.isDone())
		pct = Math.round(t.getPercentDone() * 100);
	else if (seed_ratio_limit > 0)
		pct = Math.round(t.getUploadRatio() * 100 / seed_ratio_limit);
	else
		pct = 100;

	var extra;
	if (t.isStopped())
		extra = 'paused';
	else if (t.isSeeding())
		extra = 'seeding';
	else if (t.needsMetaData())
		extra = 'magnet';
	else
		extra = 'leeching';

	return {
		percent: pct,
		complete: [ 'torrent_progress_bar', 'complete', extra ].join(' '),
		incomplete: [ 'torrent_progress_bar', 'incomplete', extra ].join(' ')
	};
};

TorrentRendererHelper.createProgressbar = function(classes)
{
	var complete = document.createElement('div');
	complete.className = 'torrent_progress_bar complete';
	var incomplete = document.createElement('div');
	incomplete.className = 'torrent_progress_bar incomplete';
	var progressbar = document.createElement('div');
	progressbar.className = 'torrent_progress_bar_container ' + classes;
	progressbar.appendChild(complete);
	progressbar.appendChild(incomplete);
	return { 'element': progressbar, 'complete': complete, 'incomplete': incomplete };
};

TorrentRendererHelper.renderProgressbar = function(controller, t, progressbar)
{
	var info = TorrentRendererHelper.getProgressInfo(controller, t);
	var e;
	e = progressbar.complete;
	e.style.width = '' + info.percent + "%";
	e.className = info.complete;
	e.style.display = info.percent<=0 ? 'none' : 'block';
	e = progressbar.incomplete;
	e.className = info.incomplete;
	e.style.display = info.percent>=100 ? 'none' : 'block';
};

TorrentRendererHelper.formatUL = function(t)
{
	return '&uarr; ' + Transmission.fmt.speedBps(t.getUploadSpeed());
};

TorrentRendererHelper.formatDL = function(t)
{
	return '&darr; ' + Transmission.fmt.speedBps(t.getDownloadSpeed());
};

/****
*****
*****
****/

function TorrentRendererFull()
{
}
TorrentRendererFull.prototype =
{
	createRow: function()
	{
		var root = document.createElement('li');
		root.className = 'torrent';

		var name = document.createElement('div');
		name.className = 'torrent_name';

		var peers = document.createElement('div');
		peers.className = 'torrent_peer_details';

		var progressbar = TorrentRendererHelper.createProgressbar('full');

		var details = document.createElement('div');
		details.className = 'torrent_progress_details';

		var image = document.createElement('div');
		var button = document.createElement('a');
		button.appendChild(image);

		root.appendChild(name);
		root.appendChild(peers);
		root.appendChild(button);
		root.appendChild(progressbar.element);
		root.appendChild(details);

		root._name_container = name;
		root._peer_details_container = peers;
		root._progress_details_container = details;
		root._progressbar = progressbar;
		root._pause_resume_button_image = image;
		root._toggle_running_button = button;

		return root;
	},

	getPeerDetails: function(t)
	{
		var err;
		if ((err = t.getErrorMessage()))
			return err;

		if (t.isDownloading())
			return [ 'Downloading from',
			         t.getPeersSendingToUs(),
			         'of',
			         t.getPeersConnected(),
			         'peers',
			         '-',
			         TorrentRendererHelper.formatDL(t),
			         TorrentRendererHelper.formatUL(t) ].join(' ');

		if (t.isSeeding())
			return [ 'Seeding to',
			         t.getPeersGettingFromUs(),
			         'of',
			         t.getPeersConnected(),
			         'peers',
			         '-',
			         TorrentRendererHelper.formatUL(t) ].join(' ');

		if (t.isChecking())
			return [ 'Verifying local data (',
			         Transmission.fmt.percentString(100.0 * t.getRecheckProgress()),
			         '% tested)' ].join('');

		return t.getStateString();
	},

	getProgressDetails: function(controller, t)
	{
		if (t.needsMetaData()) {
			var percent = 100 * t.getMetadataPercentComplete();
			return [ "Magnetized transfer - retrieving metadata (",
			         Transmission.fmt.percentString(percent),
			         "%)" ].join('');
		}

		var c;
		var sizeWhenDone = t.getSizeWhenDone();
		var totalSize = t.getTotalSize();
		var is_done = t.isDone() || t.isSeeding();

		if (is_done) {
			if (totalSize == sizeWhenDone) // seed: '698.05 MiB'
				c = [ Transmission.fmt.size(totalSize) ];
			else // partial seed: '127.21 MiB of 698.05 MiB (18.2%)'
				c = [ Transmission.fmt.size(sizeWhenDone),
				      ' of ',
				      Transmission.fmt.size(t.getTotalSize()),
				      ' (', t.getPercentDoneStr(), '%)' ];
			// append UL stats: ', uploaded 8.59 GiB (Ratio: 12.3)'
			c.push(', uploaded ',
			        Transmission.fmt.size(t.getUploadedEver()),
			        ' (Ratio ',
			        Transmission.fmt.ratioString(t.getUploadRatio()),
			        ')');
		} else { // not done yet
			c = [ Transmission.fmt.size(sizeWhenDone - t.getLeftUntilDone()),
			      ' of ', Transmission.fmt.size(sizeWhenDone),
			      ' (', t.getPercentDoneStr(), '%)' ];
		}

		// maybe append eta
		if (!t.isStopped() && (!is_done || t.seedRatioLimit(controller)>0)) {
			c.push(' - ');
			var eta = t.getETA();
			if (eta < 0 || eta >= (999*60*60) /* arbitrary */)
				c.push('remaining time unknown');
			else
				c.push(Transmission.fmt.timeInterval(t.getETA()),
				        ' remaining');
		}

		return c.join('');
	},

	render: function(controller, t, root)
	{
		// name
		setInnerHTML(root._name_container, t.getName());

		// progressbar
		TorrentRendererHelper.renderProgressbar(controller, t, root._progressbar);

		// peer details
		var has_error = t.getError() !== Torrent._ErrNone;
		var e = root._peer_details_container;
		$(e).toggleClass('error',has_error);
		setInnerHTML(e, this.getPeerDetails(t));

		// progress details
		e = root._progress_details_container;
		setInnerHTML(e, this.getProgressDetails(controller, t));

		// pause/resume button
		var is_stopped = t.isStopped();
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
	createRow: function()
	{
		var progressbar = TorrentRendererHelper.createProgressbar('compact');

		var details = document.createElement('div');
		details.className = 'torrent_peer_details compact';

		var name = document.createElement('div');
		name.className = 'torrent_name compact';

		var root = document.createElement('li');
		root.appendChild(progressbar.element);
		root.appendChild(details);
		root.appendChild(name);
		root.className = 'torrent compact';
		root._progressbar = progressbar;
		root._details_container = details;
		root._name_container = name;
		return root;
	},

	getPeerDetails: function(t)
	{
		var c;
		if ((c = t.getErrorMessage()))
			return c;
		if (t.isDownloading())
			return [ TorrentRendererHelper.formatDL(t),
			         TorrentRendererHelper.formatUL(t) ].join(' ');
		if (t.isSeeding())
			return TorrentRendererHelper.formatUL(t);
		return t.getStateString();
	},

	render: function(controller, t, root)
	{
		// name
		var is_stopped = t.isStopped();
		var e = root._name_container;
		$(e).toggleClass('paused', is_stopped);
		setInnerHTML(e, t.getName());

		// peer details
		var has_error = t.getError() !== Torrent._ErrNone;
		e = root._details_container;
		$(e).toggleClass('error', has_error);
		setInnerHTML(e, this.getPeerDetails(t));

		// progressbar
		TorrentRendererHelper.renderProgressbar(controller, t, root._progressbar);
	}
};

/****
*****
*****
****/

function TorrentRow(view, controller, torrent, selected)
{
        this.initialize(view, controller, torrent, selected);
}
TorrentRow.prototype =
{
	initialize: function(view, controller, torrent, selected) {
		this._view = view;
		this._element = view.createRow();
		this.setTorrent(controller, torrent);
		if (selected)
			this.setSelected(selected);
		this.render(controller);

	},
	getElement: function() {
		return this._element;
	},
	render: function(controller) {
		var tor = this.getTorrent();
		if (tor)
			this._view.render(controller, tor, this.getElement());
	},
	isSelected: function() {
		return this.getElement().className.indexOf('selected') !== -1;
	},
	setSelected: function(flag) {
		$(this.getElement()).toggleClass('selected', flag);
	},

	getToggleRunningButton: function() {
		return this.getElement()._toggle_running_button;
	},

	setTorrent: function(controller, t) {
		if (this._torrent !== t) {
			var row = this;
			var key = 'dataChanged.torrentRowListener';
			if (this._torrent)
				$(this._torrent).unbind(key);
			if ((this._torrent = t))
				$(this._torrent).bind(key,function(){row.render(controller);});
		}
	},
	getTorrent: function() {
		return this._torrent;
	},
	isEven: function() {
		return this.getElement().className.indexOf('even') != -1;
	},
	setEven: function(even) {
		if (this.isEven() != even)
			$(this.getElement()).toggleClass('even', even);
	}
};
