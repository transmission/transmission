/**
 * Copyright © Mnemosyne LLC
 *
 * This file is licensed under the GPLv2.
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 */

function TorrentRendererHelper() {}

TorrentRendererHelper.getProgressInfo = function (controller, t) {
    var pct, extra;
    var s = t.getStatus();
    var seed_ratio_limit = t.seedRatioLimit(controller);

    if (t.needsMetaData()) {
        pct = t.getMetadataPercentComplete() * 100;
    } else if (!t.isDone()) {
        pct = Math.round(t.getPercentDone() * 100);
    } else if (seed_ratio_limit > 0 && t.isSeeding()) { // don't split up the bar if paused or queued
        pct = Math.round(t.getUploadRatio() * 100 / seed_ratio_limit);
    } else {
        pct = 100;
    };

    if (s === Torrent._StatusStopped) {
        extra = 'paused';
    } else if (s === Torrent._StatusDownloadWait) {
        extra = 'leeching queued';
    } else if (t.needsMetaData()) {
        extra = 'magnet';
    } else if (s === Torrent._StatusDownload) {
        extra = 'leeching';
    } else if (s === Torrent._StatusSeedWait) {
        extra = 'seeding queued';
    } else if (s === Torrent._StatusSeed) {
        extra = 'seeding';
    } else {
        extra = '';
    };

    return {
        percent: pct,
        complete: ['torrent_progress_bar', 'complete', extra].join(' '),
        incomplete: ['torrent_progress_bar', 'incomplete', extra].join(' ')
    };
};

TorrentRendererHelper.createProgressbar = function (classes) {
    var complete, incomplete, progressbar;

    complete = document.createElement('div');
    complete.className = 'torrent_progress_bar complete';

    incomplete = document.createElement('div');
    incomplete.className = 'torrent_progress_bar incomplete';

    progressbar = document.createElement('div');
    progressbar.className = 'torrent_progress_bar_container ' + classes;
    progressbar.appendChild(complete);
    progressbar.appendChild(incomplete);

    return {
        'element': progressbar,
        'complete': complete,
        'incomplete': incomplete
    };
};

TorrentRendererHelper.renderProgressbar = function (controller, t, progressbar) {
    var e, style, width, display
    var info = TorrentRendererHelper.getProgressInfo(controller, t);

    // update the complete progressbar
    e = progressbar.complete;
    style = e.style;
    width = '' + info.percent + '%';
    display = info.percent > 0 ? 'block' : 'none';
    if (style.width !== width || style.display !== display) {
        $(e).css({
            width: '' + info.percent + '%',
            display: display
        });
    };

    if (e.className !== info.complete) {
        e.className = info.complete;
    };

    // update the incomplete progressbar
    e = progressbar.incomplete;
    display = (info.percent < 100) ? 'block' : 'none';

    if (e.style.display !== display) {
        e.style.display = display;
    };

    if (e.className !== info.incomplete) {
        e.className = info.incomplete;
    };
};

TorrentRendererHelper.formatUL = function (t) {
    return '▲' + Transmission.fmt.speedBps(t.getUploadSpeed());
};

TorrentRendererHelper.formatDL = function (t) {
    return '▼' + Transmission.fmt.speedBps(t.getDownloadSpeed());
};

TorrentRendererHelper.formatETA = function (t) {
    var eta = t.getETA();
    if (eta < 0 || eta >= (999 * 60 * 60)) {
        return "";
    };
    return "ETA: " + Transmission.fmt.timeInterval(eta);
};

/****
 *****
 *****
 ****/

function TorrentRendererFull() {};
TorrentRendererFull.prototype = {
    createRow: function () {
        var root, name, peers, progressbar, details, image, button;

        root = document.createElement('li');
        root.className = 'torrent';

        name = document.createElement('div');
        name.className = 'torrent_name';

        peers = document.createElement('div');
        peers.className = 'torrent_peer_details';

        progressbar = TorrentRendererHelper.createProgressbar('full');

        details = document.createElement('div');
        details.className = 'torrent_progress_details';

        image = document.createElement('div');
        button = document.createElement('a');
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

    getPeerDetails: function (t) {
        var err,
            peer_count,
            webseed_count,
            fmt = Transmission.fmt;

        if ((err = t.getErrorMessage())) {
            return err;
        };

        if (t.isDownloading()) {
            peer_count = t.getPeersConnected();
            webseed_count = t.getWebseedsSendingToUs();

            if (webseed_count && peer_count) {
                // Downloading from 2 of 3 peer(s) and 2 webseed(s)
                return ['Downloading from',
                    t.getPeersSendingToUs(),
                    'of',
                    fmt.countString('peer', 'peers', peer_count),
                    'and',
                    fmt.countString('web seed', 'web seeds', webseed_count),
                    '–',
                    TorrentRendererHelper.formatDL(t),
                    TorrentRendererHelper.formatUL(t)
                ].join(' ');
            } else if (webseed_count) {
                // Downloading from 2 webseed(s)
                return ['Downloading from',
                    fmt.countString('web seed', 'web seeds', webseed_count),
                    '–',
                    TorrentRendererHelper.formatDL(t),
                    TorrentRendererHelper.formatUL(t)
                ].join(' ');
            } else {
                // Downloading from 2 of 3 peer(s)
                return ['Downloading from',
                    t.getPeersSendingToUs(),
                    'of',
                    fmt.countString('peer', 'peers', peer_count),
                    '–',
                    TorrentRendererHelper.formatDL(t),
                    TorrentRendererHelper.formatUL(t)
                ].join(' ');
            };
        };

        if (t.isSeeding()) {
            return ['Seeding to', t.getPeersGettingFromUs(), 'of', fmt.countString('peer', 'peers', t.getPeersConnected()), '-', TorrentRendererHelper.formatUL(t)].join(' ');
        };

        if (t.isChecking()) {
            return ['Verifying local data (', Transmission.fmt.percentString(100.0 * t.getRecheckProgress()), '% tested)'].join('');
        }

        return t.getStateString();
    },

    getProgressDetails: function (controller, t) {
        if (t.needsMetaData()) {
            var MetaDataStatus = "retrieving";
            if (t.isStopped()) {
                MetaDataStatus = "needs";
            };
            var percent = 100 * t.getMetadataPercentComplete();
            return ["Magnetized transfer - " + MetaDataStatus + " metadata (",
                Transmission.fmt.percentString(percent),
                "%)"
            ].join('');
        }

        var c;
        var sizeWhenDone = t.getSizeWhenDone();
        var totalSize = t.getTotalSize();
        var is_done = t.isDone() || t.isSeeding();

        if (is_done) {
            if (totalSize === sizeWhenDone) {
                // seed: '698.05 MiB'
                c = [Transmission.fmt.size(totalSize)];
            } else { // partial seed: '127.21 MiB of 698.05 MiB (18.2%)'
                c = [Transmission.fmt.size(sizeWhenDone), ' of ', Transmission.fmt.size(t.getTotalSize()), ' (', t.getPercentDoneStr(), '%)'];
            };
            // append UL stats: ', uploaded 8.59 GiB (Ratio: 12.3)'
            c.push(', uploaded ',
                Transmission.fmt.size(t.getUploadedEver()),
                ' (Ratio ',
                Transmission.fmt.ratioString(t.getUploadRatio()),
                ')');
        } else { // not done yet
            c = [Transmission.fmt.size(sizeWhenDone - t.getLeftUntilDone()),
                ' of ', Transmission.fmt.size(sizeWhenDone),
                ' (', t.getPercentDoneStr(), '%)'
            ];
        };

        // maybe append eta
        if (!t.isStopped() && (!is_done || t.seedRatioLimit(controller) > 0)) {
            c.push(' - ');
            var eta = t.getETA();
            if (eta < 0 || eta >= (999 * 60 * 60) /* arbitrary */ ) {
                c.push('remaining time unknown');
            } else {
                c.push(Transmission.fmt.timeInterval(t.getETA()), ' remaining');
            };
        };

        return c.join('');
    },

    render: function (controller, t, root) {
        // name
        setTextContent(root._name_container, t.getName());

        // progressbar
        TorrentRendererHelper.renderProgressbar(controller, t, root._progressbar);

        // peer details
        var has_error = t.getError() !== Torrent._ErrNone;
        var e = root._peer_details_container;
        $(e).toggleClass('error', has_error);
        setTextContent(e, this.getPeerDetails(t));

        // progress details
        e = root._progress_details_container;
        setTextContent(e, this.getProgressDetails(controller, t));

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

function TorrentRendererCompact() {};
TorrentRendererCompact.prototype = {
    createRow: function () {
        var progressbar, details, name, root;

        progressbar = TorrentRendererHelper.createProgressbar('compact');

        details = document.createElement('div');
        details.className = 'torrent_peer_details compact';

        name = document.createElement('div');
        name.className = 'torrent_name compact';

        root = document.createElement('li');
        root.appendChild(progressbar.element);
        root.appendChild(details);
        root.appendChild(name);
        root.className = 'torrent compact';
        root._progressbar = progressbar;
        root._details_container = details;
        root._name_container = name;
        return root;
    },

    getPeerDetails: function (t) {
        var c;
        if ((c = t.getErrorMessage())) {
            return c;
        };
        if (t.isDownloading()) {
            var have_dn = t.getDownloadSpeed() > 0;
            var have_up = t.getUploadSpeed() > 0;

            if (!have_up && !have_dn) {
                return 'Idle';
            };
            var s = '';
            if (!isMobileDevice) {
                s = TorrentRendererHelper.formatETA(t) + ' ';
            };
            if (have_dn) {
                s += TorrentRendererHelper.formatDL(t);
            };
            if (have_dn && have_up) {
                s += ' ';
            };
            if (have_up) {
                s += TorrentRendererHelper.formatUL(t);
            };
            return s;
        };
        if (t.isSeeding()) {
            return ['Ratio: ', Transmission.fmt.ratioString(t.getUploadRatio()), ', ', TorrentRendererHelper.formatUL(t)].join('');
        };
        return t.getStateString();
    },

    render: function (controller, t, root) {
        // name
        var is_stopped = t.isStopped();
        var e = root._name_container;
        $(e).toggleClass('paused', is_stopped);
        setTextContent(e, t.getName());

        // peer details
        var has_error = t.getError() !== Torrent._ErrNone;
        e = root._details_container;
        $(e).toggleClass('error', has_error);
        setTextContent(e, this.getPeerDetails(t));

        // progressbar
        TorrentRendererHelper.renderProgressbar(controller, t, root._progressbar);
    }
};

/****
 *****
 *****
 ****/

function TorrentRow(view, controller, torrent) {
    this.initialize(view, controller, torrent);
};
TorrentRow.prototype = {
    initialize: function (view, controller, torrent) {
        var row = this;
        this._view = view;
        this._torrent = torrent;
        this._element = view.createRow();
        this.render(controller);
        $(this._torrent).bind('dataChanged.torrentRowListener', function () {
            row.render(controller);
        });

    },
    getElement: function () {
        return this._element;
    },
    render: function (controller) {
        var tor = this.getTorrent();
        if (tor) {
            this._view.render(controller, tor, this.getElement());
        };
    },
    isSelected: function () {
        return this.getElement().className.indexOf('selected') !== -1;
    },

    getTorrent: function () {
        return this._torrent;
    },
    getTorrentId: function () {
        return this.getTorrent().getId();
    }
};
