/**
 * Copyright © Jordan Lee, Dave Perrett, Malcolm Jarvis and Bruno Bierbaumer
 *
 * This file is licensed under the GPLv2.
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 */

function Inspector(controller) {

    var data = {
        controller: null,
        elements: { },
        torrents: [ ]
    },

    needsExtraInfo = function (torrents) {
        var i, id, tor;

        for (i = 0; tor = torrents[i]; i++)
            if (!tor.hasExtraInfo())
                return true;

        return false;
    },

    refreshTorrents = function () {
        var fields,
            ids = $.map(data.torrents.slice(0), function (t) {return t.getId();});

        if (ids && ids.length)
        {
            fields = ['id'].concat(Torrent.Fields.StatsExtra);

            if (needsExtraInfo(data.torrents))
                $.merge(fields, Torrent.Fields.InfoExtra);

            data.controller.updateTorrents(ids, fields);
        }
    },

    onTabClicked = function (ev) {
        var tab = ev.currentTarget;

        if (isMobileDevice)
            ev.stopPropagation();

        // select this tab and deselect the others
        $(tab).addClass('selected').siblings().removeClass('selected');

        // show this tab and hide the others
        $('#'+tab.id+'_container').show().siblings('.inspector_container').hide();

        updateInspector();
    },

    updateInspector = function () {
        var e = data.elements,
            torrents = data.torrents,
            name;

        // update the name, which is shown on all the pages
        if (!torrents || !torrents.length)
            name = 'No Selection';
        else if(torrents.length === 1)
            name = torrents[0].getName();
        else
            name = '' + torrents.length+' Transfers Selected';
        setInnerHTML(e.name, name || na);

        // update the visible page
        if ($(e.info_page).is(':visible'))
            updateInfoPage();
        else if ($(e.activity_page).is(':visible'))
            updateActivityPage();
        else if ($(e.peers_page).is(':visible'))
            updatePeersPage();
        else if ($(e.trackers_page).is(':visible'))
            updateTrackersPage();
        else if ($(e.files_page).is(':visible'))
            updateFilesPage();
    },

    /****
    *****  GENERAL INFO PAGE
    ****/

    accumulateString = function (oldVal, newVal) {
        if (!oldVal || !oldVal.length)
            return newVal;
        if (oldVal === newVal)
            return newVal;
        return 'Mixed';
    },

    updateInfoPage = function () {
        var torrents = data.torrents,
            e = data.elements,
            fmt = Transmission.fmt,
            na = 'N/A',
            name = '',
            pieceCount = 0,
            pieceSize = '',
            hash = '',
            secure = '',
            comment = '',
            creator = '',
            date = '',
            directory = '',
            s, i, t;

        for (i=0; t=torrents[i]; ++i) {
            name        = accumulateString(name, t.getName());
            pieceCount += t.getPieceCount();
            pieceSize   = accumulateString(pieceSize, fmt.mem(t.getPieceSize()));
            hash        = accumulateString(hash, t.getHashString());
            secure      = accumulateString(secure, (t.getPrivateFlag() ? 'Private' : 'Public')+' Torrent');
            comment     = accumulateString(comment, t.getComment() || 'N/A');
            creator     = accumulateString(creator, t.getCreator());
            date        = accumulateString(date, fmt.timestamp(t.getDateCreated()));
            directory   = accumulateString(directory, t.getDownloadDir());
        }

        if (!pieceCount)
            setInnerHTML(e.pieces, na);
        else if (pieceSize == 'Mixed')
            setInnerHTML(e.pieces, 'Mixed');
        else
            setInnerHTML(e.pieces, pieceCount + ' pieces @ ' + pieceSize);

        setInnerHTML(e.hash, hash || na);
        setInnerHTML(e.secure, secure || na);
        setInnerHTML(e.comment, comment.replace(/(https?|ftp):\/\/([\w\-]+(\.[\w\-]+)*(\.[a-z]{2,4})?)(\d{1,5})?(\/([^<>\s]*))?/g, '<a target="_blank" href="$&">$&</a>') || na);
        setInnerHTML(e.creator, creator || na);
        setInnerHTML(e.date, date || na);
        setInnerHTML(e.directory, directory || na);

        $("#inspector_tab_info_container .inspector_row > div").css('color', '#222');
        $("#inspector_tab_info_container .inspector_row > div:contains('N/A')").css('color', '#666');
    },

    /****
    *****  ACTIVITY PAGE
    ****/

    updateActivityPage = function() {

        var i, t, l, d, na='N/A',
            fmt = Transmission.fmt,
            e = data.elements,
            torrents = data.torrents,
            state = '',
            have = 0,
            sizeWhenDone = 0,
            progress = '',
            have = 0,
            verified = 0,
            availability = 0,
            downloaded = 0,
            uploaded = 0,
            error = '',
            uploadSpeed = 0,
            uploadPeers = 0,
            downloadSpeed = 0,
            downloadPeers = 0,
            availability = 0;

        for (i=0; t=torrents[i]; ++i)
        {
            l = t.getLeftUntilDone();
            d = t.getSizeWhenDone();

            state = accumulateString(state, t.getStateString());
            error = accumulateString(error, t.getErrorString());

            have           += t.getHave();
            verified       += t.getHaveValid();
            sizeWhenDone   += d;
            availability   += (d - l + t.getDesiredAvailable());

            uploaded       += t.getUploadedEver();
            uploadSpeed    += t.getUploadSpeed();
            uploadPeers    += t.getPeersGettingFromUs();

            downloaded     += t.getDownloadedEver();
            downloadSpeed  += t.getDownloadSpeed();
            downloadPeers  += t.getPeersSendingToUs();
        }

        setInnerHTML(e.state, state);
        setInnerHTML(e.progress, torrents.length ? fmt.percentString(Math.ratio(have*100, sizeWhenDone)) + '%' : na);
        setInnerHTML(e.have, torrents.length ? fmt.size(have) + ' (' + fmt.size(verified) + ' verified)' : na);
        setInnerHTML(e.availability, torrents.length ? fmt.percentString(Math.ratio(availability*100, sizeWhenDone)) + '%' : na);
        setInnerHTML(e.uploaded, torrents.length ? fmt.size(uploaded) : na);
        setInnerHTML(e.downloaded, torrents.length ? fmt.size(downloaded) : na);
        setInnerHTML(e.ratio, torrents.length ? fmt.ratioString(Math.ratio(uploaded, downloaded)) : na);
        setInnerHTML(e.error, error);
        setInnerHTML(e.upload_speed, torrents.length ? fmt.speedBps(uploadSpeed) : na);
        setInnerHTML(e.download_speed, torrents.length ? fmt.speedBps(downloadSpeed) : na);
        setInnerHTML(e.upload_to, torrents.length ? uploadPeers : na);
        setInnerHTML(e.download_from, torrents.length ? downloadPeers : na);

        $("#inspector_tab_activity_container .inspector_row > div").css('color', '#222');
        $("#inspector_tab_activity_container .inspector_row > div:contains('N/A')").css('color', '#666');
    },

    /****
    *****  FILES PAGE
    ****/

    filesSelectAllClicked = function() { filesAllClicked(true); },
    filesDeselectAllClicked = function() { filesAllClicked(false); },
    filesAllClicked = function(s) {
        var i, row, rows=[], t=data.file_torrent;
        if (!t)
            return;
        for (i=0; row=data.file_rows[i]; ++i)
            if (row.isEditable() && (t.getFile(i).wanted !== s))
                rows.push(row);
        if (rows.length > 0)
            changeFileCommand(rows, s?'files-wanted':'files-unwanted');
    },

    changeFileCommand = function(rows, command) {
        var torrentId = data.file_torrent.getId();
        var rowIndices = $.map(rows.slice(0),function (row) {return row.getIndex();});
        data.controller.changeFileCommand(torrentId, rowIndices, command);
    },

    onFileWantedToggled = function(ev, row, want) {
        changeFileCommand([row], want?'files-wanted':'files-unwanted');
    },

    onFilePriorityToggled = function(ev, row, priority) {
        var command;
        switch(priority) {
            case -1: command = 'priority-low'; break;
            case  1: command = 'priority-high'; break;
            default: command = 'priority-normal'; break;
        }
        changeFileCommand([row], command);
    },

    clearFileList = function() {
        $(data.elements.file_list).empty();
        delete data.file_torrent;
        delete data.file_rows;
    },

    updateFilesPage = function() {
        var i, n, sel, row, tor, fragment,
            file_list = data.elements.file_list,
            torrents = data.torrents;

        if (torrents.length !== 1) {
            clearFileList();
            return;
        }

        // build the file list
        tor = torrents[0];

        clearFileList();
        data.file_torrent = tor;
        n = tor.getFileCount();
        data.file_rows = [];
        fragment = document.createDocumentFragment();

        for (i=0; i<n; ++i) {
            row = data.file_rows[i] = new FileRow(tor, i);
            fragment.appendChild(row.getElement());
                    $(row).bind('wantedToggled',onFileWantedToggled);
                    $(row).bind('priorityToggled',onFilePriorityToggled);
        }

        file_list.appendChild(fragment);
    },

    /****
    *****  PEERS PAGE
    ****/

    updatePeersPage = function() {
        var i, k, tor, peers, peer, parity,
            html = [],
            fmt = Transmission.fmt,
            peers_list = data.elements.peers_list,
            torrents = data.torrents;

        for (k=0; tor=torrents[k]; ++k)
        {
            peers = tor.getPeers();
            html.push('<div class="inspector_group">');
            if (torrents.length > 1) {
                html.push('<div class="inspector_torrent_label">', tor.getName(), '</div>');
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
            for (i=0; peer=peers[i]; ++i) {
                parity = (i%2) ? 'odd' : 'even';
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

        setInnerHTML(peers_list, html.join(''));
    },

    /****
    *****  TRACKERS PAGE
    ****/

    getAnnounceState = function(tracker) {
        var timeUntilAnnounce, s = '';
        switch (tracker.announceState) {
            case Torrent._TrackerActive:
                s = 'Announce in progress';
                break;
            case Torrent._TrackerWaiting:
                timeUntilAnnounce = tracker.nextAnnounceTime - ((new Date()).getTime() / 1000);
                if (timeUntilAnnounce < 0) {
                    timeUntilAnnounce = 0;
                }
                s = 'Next announce in ' + Transmission.fmt.timeInterval(timeUntilAnnounce);
                break;
            case Torrent._TrackerQueued:
                s = 'Announce is queued';
                break;
            case Torrent._TrackerInactive:
                s = tracker.isBackup ?
                    'Tracker will be used as a backup' :
                    'Announce not scheduled';
                break;
            default:
                s = 'unknown announce state: ' + tracker.announceState;
        }
        return s;
    },

    lastAnnounceStatus = function(tracker) {

        var lastAnnounceLabel = 'Last Announce',
            lastAnnounce = [ 'N/A' ],
        lastAnnounceTime;

        if (tracker.hasAnnounced) {
            lastAnnounceTime = Transmission.fmt.timestamp(tracker.lastAnnounceTime);
            if (tracker.lastAnnounceSucceeded) {
                lastAnnounce = [ lastAnnounceTime, ' (got ',  Transmission.fmt.plural(tracker.lastAnnouncePeerCount, 'peer'), ')' ];
            } else {
                lastAnnounceLabel = 'Announce error';
                lastAnnounce = [ (tracker.lastAnnounceResult ? (tracker.lastAnnounceResult + ' - ') : ''), lastAnnounceTime ];
            }
        }
        return { 'label':lastAnnounceLabel, 'value':lastAnnounce.join('') };
    },

    lastScrapeStatus = function(tracker) {

        var lastScrapeLabel = 'Last Scrape',
            lastScrape = 'N/A',
        lastScrapeTime;

        if (tracker.hasScraped) {
            lastScrapeTime = Transmission.fmt.timestamp(tracker.lastScrapeTime);
            if (tracker.lastScrapeSucceeded) {
                lastScrape = lastScrapeTime;
            } else {
                lastScrapeLabel = 'Scrape error';
                lastScrape = (tracker.lastScrapeResult ? tracker.lastScrapeResult + ' - ' : '') + lastScrapeTime;
            }
        }
        return {'label':lastScrapeLabel, 'value':lastScrape};
    },

    updateTrackersPage = function() {
        var i, j, tier, tracker, trackers, tor,
            html, parity, lastAnnounceStatusHash,
            announceState, lastScrapeStatusHash,
            na = 'N/A',
            trackers_list = data.elements.trackers_list,
            torrents = data.torrents;

        // By building up the HTML as as string, then have the browser
        // turn this into a DOM tree, this is a fast operation.
        html = [];
        for (i=0; tor=torrents[i]; ++i)
        {
            html.push ('<div class="inspector_group">');

            if (torrents.length > 1)
                html.push('<div class="inspector_torrent_label">', tor.getName(), '</div>');

            tier = -1;
            trackers = tor.getTrackers();
            for (j=0; tracker=trackers[j]; ++j)
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

                // Display construction
                lastAnnounceStatusHash = lastAnnounceStatus(tracker);
                announceState = getAnnounceState(tracker);
                lastScrapeStatusHash = lastScrapeStatus(tracker);
                parity = (j%2) ? 'odd' : 'even';
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

        setInnerHTML(trackers_list, html.join(''));
    },

    initialize = function (controller) {

        var ti = '#torrent_inspector_';

        data.controller = controller;

        $('.inspector_tab').click(onTabClicked);
        $('#files_select_all').click(filesSelectAllClicked);
        $('#files_deselect_all').click(filesDeselectAllClicked);

        data.elements.info_page      = $('#inspector_tab_info_container')[0];
        data.elements.files_page     = $('#inspector_tab_files_container')[0];
        data.elements.peers_page     = $('#inspector_tab_peers_container')[0];
        data.elements.trackers_page  = $('#inspector_tab_trackers_container')[0];
        data.elements.activity_page  = $('#inspector_tab_activity_container')[0];

        data.elements.file_list      = $('#inspector_file_list')[0];
        data.elements.peers_list     = $('#inspector_peers_list')[0];
        data.elements.trackers_list  = $('#inspector_trackers_list')[0];

        data.elements.availability   = $(ti+'availability')[0];
        data.elements.comment        = $(ti+'comment')[0];
        data.elements.date           = $(ti+'creator_date')[0];
        data.elements.creator        = $(ti+'creator')[0];
        data.elements.directory      = $(ti+'download_dir')[0];
        data.elements.downloaded     = $(ti+'downloaded')[0];
        data.elements.download_from  = $(ti+'download_from')[0];
        data.elements.download_speed = $(ti+'download_speed')[0];
        data.elements.error          = $(ti+'error')[0];
        data.elements.hash           = $(ti+'hash')[0];
        data.elements.have           = $(ti+'have')[0];
        data.elements.name           = $(ti+'name')[0];
        data.elements.progress       = $(ti+'progress')[0];
        data.elements.ratio          = $(ti+'ratio')[0];
        data.elements.secure         = $(ti+'secure')[0];
        data.elements.size           = $(ti+'size')[0];
        data.elements.state          = $(ti+'state')[0];
        data.elements.pieces         = $(ti+'pieces')[0];
        data.elements.uploaded       = $(ti+'uploaded')[0];
        data.elements.upload_speed   = $(ti+'upload_speed')[0];
        data.elements.upload_to      = $(ti+'upload_to')[0];

        // force initial 'N/A' updates on all the pages
        updateInspector();
        updateInfoPage();
        updateActivityPage();
        updatePeersPage();
        updateTrackersPage();
        updateFilesPage();
    };

    /****
    *****  PUBLIC FUNCTIONS
    ****/

    this.setTorrents = function (torrents) {
        var d = data;

        // update the inspector when a selected torrent's data changes.
        $(d.torrents).unbind('dataChanged.inspector');
        $(torrents).bind('dataChanged.inspector', $.proxy(updateInspector,this));
        d.torrents = torrents;

        // periodically ask for updates to the inspector's torrents
        clearInterval(d.refreshInterval);
        d.refreshInterval = setInterval($.proxy(refreshTorrents,this), 2000);
        refreshTorrents();

        // refresh the inspector's UI
        updateInspector();
    };

    initialize (controller);
};
