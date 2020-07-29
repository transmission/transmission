/**
 * Copyright © Charles Kerr, Dave Perrett, Malcolm Jarvis and Bruno Bierbaumer
 *
 * This file is licensed under the GPLv2.
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 */

function Transmission() {
    this.initialize();
}

Transmission.prototype = {
    /****
     *****
     *****  STARTUP
     *****
     ****/

    initialize: function () {
        var e;

        // Initialize the helper classes
        this.remote = new TransmissionRemote(this);
        this.inspector = new Inspector(this, this.remote);
        this.prefsDialog = new PrefsDialog(this.remote);
        $(this.prefsDialog).bind('closed', $.proxy(this.onPrefsDialogClosed, this));

        this.isMenuEnabled = !isMobileDevice;

        // Initialize the implementation fields
        this.filterText = '';
        this._torrents = {};
        this._rows = [];
        this.dirtyTorrents = {};
        this.uriCache = {};

        // Initialize the clutch preferences
        Prefs.getClutchPrefs(this);

        // Set up user events
        $('#toolbar-pause').click($.proxy(this.stopSelectedClicked, this));
        $('#toolbar-start').click($.proxy(this.startSelectedClicked, this));
        $('#toolbar-pause-all').click($.proxy(this.stopAllClicked, this));
        $('#toolbar-start-all').click($.proxy(this.startAllClicked, this));
        $('#toolbar-remove').click($.proxy(this.removeClicked, this));
        $('#toolbar-open').click($.proxy(this.openTorrentClicked, this));

        $('#prefs-button').click($.proxy(this.togglePrefsDialogClicked, this));

        $('#upload_confirm_button').click($.proxy(this.confirmUploadClicked, this));
        $('#upload_cancel_button').click($.proxy(this.hideUploadDialog, this));

        $('#rename_confirm_button').click($.proxy(this.confirmRenameClicked, this));
        $('#rename_cancel_button').click($.proxy(this.hideRenameDialog, this));

        $('#move_confirm_button').click($.proxy(this.confirmMoveClicked, this));
        $('#move_cancel_button').click($.proxy(this.hideMoveDialog, this));

        $('#turtle-button').click($.proxy(this.toggleTurtleClicked, this));
        $('#compact-button').click($.proxy(this.toggleCompactClicked, this));

        // tell jQuery to copy the dataTransfer property from events over if it exists
        jQuery.event.props.push("dataTransfer");

        $('#torrent_upload_form').submit(function () {
            $('#upload_confirm_button').click();
            return false;
        });

        $('#toolbar-inspector').click($.proxy(this.toggleInspector, this));

        e = $('#filter-mode');
        e.val(this[Prefs._FilterMode]);
        e.change($.proxy(this.onFilterModeClicked, this));
        $('#filter-tracker').change($.proxy(this.onFilterTrackerClicked, this));

        if (!isMobileDevice) {
            $(document).bind('keydown', $.proxy(this.keyDown, this));
            $(document).bind('keyup', $.proxy(this.keyUp, this));
            $('#torrent_container').click($.proxy(this.deselectAll, this));
            $('#torrent_container').bind('dragover', $.proxy(this.dragenter, this));
            $('#torrent_container').bind('dragenter', $.proxy(this.dragenter, this));
            $('#torrent_container').bind('drop', $.proxy(this.drop, this));
            $('#inspector_link').click($.proxy(this.toggleInspector, this));

            this.setupSearchBox();
            this.createContextMenu();
        };

        if (this.isMenuEnabled) {
            this.createSettingsMenu();
        };

        e = {};
        e.torrent_list = $('#torrent_list')[0];
        e.toolbar_buttons = $('#toolbar ul li');
        e.toolbar_pause_button = $('#toolbar-pause')[0];
        e.toolbar_start_button = $('#toolbar-start')[0];
        e.toolbar_remove_button = $('#toolbar-remove')[0];
        this.elements = e;

        // Apply the prefs settings to the gui
        this.initializeSettings();

        // Get preferences & torrents from the daemon
        var async = false;
        this.loadDaemonPrefs(async);
        this.loadDaemonStats(async);
        this.initializeTorrents();
        this.refreshTorrents();
        this.togglePeriodicSessionRefresh(true);

        this.updateButtonsSoon();
    },

    loadDaemonPrefs: function (async, callback) {
        this.remote.loadDaemonPrefs(function (data) {
            var o = data['arguments'];
            Prefs.getClutchPrefs(o);
            this.updateGuiFromSession(o);
            this.sessionProperties = o;

            if (callback) {
                callback();
            }
        }, this, async);
    },

    loadImages: function () {
        for (var i = 0, row; row = arguments[i]; ++i) {
            jQuery("<img>").attr("src", row);
        };
    },

    /*
     * Load the clutch prefs and init the GUI according to those prefs
     */
    initializeSettings: function () {
        Prefs.getClutchPrefs(this);

        if (this.isMenuEnabled) {
            $('#sort_by_' + this[Prefs._SortMethod]).selectMenuItem();

            if (this[Prefs._SortDirection] === Prefs._SortDescending) {
                $('#reverse_sort_order').selectMenuItem();
            };
        }

        this.initCompactMode();
    },

    /*
     * Set up the search box
     */
    setupSearchBox: function () {
        var tr = this;
        var search_box = $('#torrent_search');
        search_box.bind('keyup click', function () {
            tr.setFilterText(this.value);
        });
        if (!$.browser.safari) {
            search_box.addClass('blur');
            search_box[0].value = 'Filter';
            search_box.bind('blur', function () {
                if (this.value === '') {
                    $(this).addClass('blur');
                    this.value = 'Filter';
                    tr.setFilterText(null);
                };
            }).bind('focus', function () {
                if ($(this).is('.blur')) {
                    this.value = '';
                    $(this).removeClass('blur');
                }
            });
        }
    },

    /**
     * Create the torrent right-click menu
     */
    createContextMenu: function () {
        var tr = this;
        var bindings = {
            pause_selected: function () {
                tr.stopSelectedTorrents();
            },
            resume_selected: function () {
                tr.startSelectedTorrents(false);
            },
            resume_now_selected: function () {
                tr.startSelectedTorrents(true);
            },
            move: function () {
                tr.moveSelectedTorrents(false);
            },
            remove: function () {
                tr.removeSelectedTorrents();
            },
            remove_data: function () {
                tr.removeSelectedTorrentsAndData();
            },
            verify: function () {
                tr.verifySelectedTorrents();
            },
            rename: function () {
                tr.renameSelectedTorrents();
            },
            reannounce: function () {
                tr.reannounceSelectedTorrents();
            },
            move_top: function () {
                tr.moveTop();
            },
            move_up: function () {
                tr.moveUp();
            },
            move_down: function () {
                tr.moveDown();
            },
            move_bottom: function () {
                tr.moveBottom();
            },
            select_all: function () {
                tr.selectAll();
            },
            deselect_all: function () {
                tr.deselectAll();
            }
        };

        // Set up the context menu
        $("ul#torrent_list").contextmenu({
            delegate: ".torrent",
            menu: "#torrent_context_menu",
            preventSelect: true,
            taphold: true,
            show: {
                effect: "none"
            },
            hide: {
                effect: "none"
            },
            select: function (event, ui) {
                bindings[ui.cmd]();
            },
            beforeOpen: $.proxy(function (event, ui) {
                var element = $(event.currentTarget);
                var i = $('#torrent_list > li').index(element);
                if ((i !== -1) && !this._rows[i].isSelected()) {
                    this.setSelectedRow(this._rows[i]);
                };

                this.calculateTorrentStates(function (s) {
                    var tl = $(event.target);
                    tl.contextmenu("enableEntry", "pause_selected", s.activeSel > 0);
                    tl.contextmenu("enableEntry", "resume_selected", s.pausedSel > 0);
                    tl.contextmenu("enableEntry", "resume_now_selected", s.pausedSel > 0 || s.queuedSel > 0);
                    tl.contextmenu("enableEntry", "rename", s.sel == 1);
                });
            }, this)
        });
    },

    createSettingsMenu: function () {
        $("#footer_super_menu").transMenu({
            open: function () {
                $("#settings_menu").addClass("selected");
            },
            close: function () {
                $("#settings_menu").removeClass("selected");
            },
            select: $.proxy(this.onMenuClicked, this)
        });
        $("#settings_menu").click(function (event) {
            $("#footer_super_menu").transMenu("open");
        });
    },

    /****
     *****
     ****/

    updateFreeSpaceInAddDialog: function () {
        var formdir = $('input#add-dialog-folder-input').val();
        this.remote.getFreeSpace(formdir, this.onFreeSpaceResponse, this);
    },

    onFreeSpaceResponse: function (dir, bytes) {
        var e, str, formdir;

        formdir = $('input#add-dialog-folder-input').val();
        if (formdir == dir) {
            e = $('label#add-dialog-folder-label');
            if (bytes > 0) {
                str = '  <i>(' + Transmission.fmt.size(bytes) + ' Free)</i>';
            } else {
                str = '';
            };
            e.html('Destination folder' + str + ':');
        }
    },

    /****
     *****
     *****  UTILITIES
     *****
     ****/

    getAllTorrents: function () {
        var torrents = [];
        for (var key in this._torrents) {
            torrents.push(this._torrents[key]);
        };
        return torrents;
    },

    getTorrentIds: function (torrents) {
        return $.map(torrents.slice(0), function (t) {
            return t.getId();
        });
    },

    scrollToRow: function (row) {
        if (isMobileDevice) {
            // FIXME: why? return
            var list = $('#torrent_container');
            var scrollTop = list.scrollTop();
            var innerHeight = list.innerHeight();
            var offsetTop = row.getElement().offsetTop;
            var offsetHeight = $(row.getElement()).outerHeight();

            if (offsetTop < scrollTop) {
                list.scrollTop(offsetTop);
            } else if (innerHeight + scrollTop < offsetTop + offsetHeight) {
                list.scrollTop(offsetTop + offsetHeight - innerHeight);
            };
        };
    },

    seedRatioLimit: function () {
        var p = this.sessionProperties;
        if (p && p.seedRatioLimited) {
            return p.seedRatioLimit;
        };
        return -1;
    },

    setPref: function (key, val) {
        this[key] = val;
        Prefs.setValue(key, val);
    },

    /****
     *****
     *****  SELECTION
     *****
     ****/

    getSelectedRows: function () {
        return $.grep(this._rows, function (r) {
            return r.isSelected();
        });
    },

    getSelectedTorrents: function () {
        return $.map(this.getSelectedRows(), function (r) {
            return r.getTorrent();
        });
    },

    getSelectedTorrentIds: function () {
        return this.getTorrentIds(this.getSelectedTorrents());
    },

    setSelectedRow: function (row) {
        $(this.elements.torrent_list).children('.selected').removeClass('selected');
        this.selectRow(row);
    },

    selectRow: function (row) {
        $(row.getElement()).addClass('selected');
        this.callSelectionChangedSoon();
    },

    deselectRow: function (row) {
        $(row.getElement()).removeClass('selected');
        this.callSelectionChangedSoon();
    },

    selectAll: function () {
        $(this.elements.torrent_list).children().addClass('selected');
        this.callSelectionChangedSoon();
    },
    deselectAll: function () {
        $(this.elements.torrent_list).children('.selected').removeClass('selected');
        this.callSelectionChangedSoon();
        delete this._last_torrent_clicked;
    },

    indexOfLastTorrent: function () {
        for (var i = 0, r; r = this._rows[i]; ++i) {
            if (r.getTorrentId() === this._last_torrent_clicked) {
                return i;
            };
        };
        return -1;
    },

    // Select a range from this row to the last clicked torrent
    selectRange: function (row) {
        var last = this.indexOfLastTorrent();

        if (last === -1) {
            this.selectRow(row);
        } else { // select the range between the prevous & current
            var next = this._rows.indexOf(row);
            var min = Math.min(last, next);
            var max = Math.max(last, next);
            for (var i = min; i <= max; ++i) {
                this.selectRow(this._rows[i]);
            };
        }

        this.callSelectionChangedSoon();
    },

    selectionChanged: function () {
        this.updateButtonStates();

        this.inspector.setTorrents(this.inspectorIsVisible() ? this.getSelectedTorrents() : []);

        clearTimeout(this.selectionChangedTimer);
        delete this.selectionChangedTimer;

    },

    callSelectionChangedSoon: function () {
        if (!this.selectionChangedTimer) {
            var callback = $.proxy(this.selectionChanged, this),
                msec = 200;
            this.selectionChangedTimer = setTimeout(callback, msec);
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
    keyDown: function (ev) {
        var handled = false;
        var rows = this._rows;
        var isInputFocused = $(ev.target).is('input');
        var isDialogVisible = ($('.dialog_heading:visible').length > 0 || $('.ui-dialog:visible').length > 0);

        // hotkeys
        var up_key = ev.keyCode === 38; // up key pressed
        var dn_key = ev.keyCode === 40; // down key pressed
        var a_key = ev.keyCode === 65; // a key pressed
        var c_key = ev.keyCode === 67; // c key pressed
        var d_key = ev.keyCode === 68; // d key pressed
        var i_key = ev.keyCode === 73; // i key pressed
        var l_key = ev.keyCode === 76; // l key pressed
        var m_key = ev.keyCode === 77; // m key pressed
        var o_key = ev.keyCode === 79; // o key pressed
        var p_key = ev.keyCode === 80; // p key pressed
        var r_key = ev.keyCode === 82; // r key pressed
        var t_key = ev.keyCode === 84; // t key pressed
        var u_key = ev.keyCode === 85; // u key pressed
        var shift_key = ev.keyCode === 16; // shift key pressed
        var slash_key = ev.keyCode === 191; // slash (/) key pressed
        var backspace_key = ev.keyCode === 8; // backspace key pressed
        var del_key = ev.keyCode === 46; // delete key pressed
        var enter_key = ev.keyCode === 13; // enter key pressed
        var esc_key = ev.keyCode === 27; // esc key pressed
        var comma_key = ev.keyCode === 188; // comma key pressed

        if (enter_key) {
            // handle other dialogs
            if (dialog && dialog.isVisible()) {
                dialog.executeCallback();
                handled = true;
            }

            // handle upload dialog
            if ($('#upload_container').is(':visible')) {
                this.confirmUploadClicked();
                handled = true;
            }

            // handle move dialog
            if ($('#move_container').is(':visible')) {
                this.confirmMoveClicked();
                handled = true;
            }

            // handle rename dialog
            if ($('#rename_container').is(':visible')) {
                this.confirmRenameClicked();
                handled = true;
            }
        }

        if (esc_key) {
            // handle other dialogs
            if (dialog && dialog.isVisible()) {
                dialog.hideDialog();
                handled = true;
            }

            // handle upload dialog
            if ($('#upload_container').is(':visible')) {
                this.hideUploadDialog();
                handled = true;
            }

            // handle move dialog
            if ($('#move_container').is(':visible')) {
                this.hideMoveDialog();
                handled = true;
            }

            // handle rename dialog
            if ($('#rename_container').is(':visible')) {
                this.hideRenameDialog();
                handled = true;
            }
        }

        // Some hotkeys can only be used if the following conditions are met:
        // 1. when no input fields are focused
        // 2. when no other dialogs are visible
        // 3. when the meta or ctrl key isn't pressed (i.e. opening dev tools shouldn't trigger the info panel)
        if (!isInputFocused && !isDialogVisible && !ev.metaKey && !ev.ctrlKey) {
            if (comma_key) {
                this.togglePrefsDialogClicked();
                handled = true;
            }

            if (slash_key) {
                this.showHotkeysDialog();
                handled = true;
            }

            if (a_key) {
                if (ev.shiftKey) {
                    this.deselectAll();
                } else {
                    this.selectAll();
                }
                handled = true;
            }

            if (c_key) {
                this.toggleCompactClicked();
                handled = true;
            }

            if ((backspace_key || del_key || d_key) && rows.length) {
                this.removeSelectedTorrents();
                handled = true;
            }

            if (i_key) {
                this.toggleInspector();
                handled = true;
            }

            if (m_key || l_key) {
                this.moveSelectedTorrents()
                handled = true;
            }

            if (o_key || u_key) {
                this.openTorrentClicked(ev);
                handled = true;
            }

            if (p_key) {
                this.stopSelectedTorrents();
                handled = true;
            }

            if (r_key) {
                this.startSelectedTorrents();
                handled = true;
            }

            if (t_key) {
                this.toggleTurtleClicked();
                handled = true;
            }

            if ((up_key || dn_key) && rows.length) {
                var last = this.indexOfLastTorrent(),
                    i = last,
                    anchor = this._shift_index,
                    r,
                    min = 0,
                    max = rows.length - 1;

                if (dn_key && (i + 1 <= max)) {
                    ++i;
                } else if (up_key && (i - 1 >= min)) {
                    --i;
                };

                var r = rows[i];

                if (anchor >= 0) {
                    // user is extending the selection
                    // with the shift + arrow keys...
                    if (((anchor <= last) && (last < i)) || ((anchor >= last) && (last > i))) {
                        this.selectRow(r);
                    } else if (((anchor >= last) && (i > last)) || ((anchor <= last) && (last > i))) {
                        this.deselectRow(rows[last]);
                    }
                } else {
                    if (ev.shiftKey) {
                        this.selectRange(r);
                    } else {
                        this.setSelectedRow(r);
                    };
                }
                this._last_torrent_clicked = r.getTorrentId();
                this.scrollToRow(r);
                handled = true;
            } else if (shift_key) {
                this._shift_index = this.indexOfLastTorrent();
            }
        }

        return !handled;
    },

    keyUp: function (ev) {
        if (ev.keyCode === 16) { // shift key pressed
            delete this._shift_index;
        };
    },

    isButtonEnabled: function (ev) {
        var p = (ev.target || ev.srcElement).parentNode;
        return p.className !== 'disabled' && p.parentNode.className !== 'disabled';
    },

    stopSelectedClicked: function (ev) {
        if (this.isButtonEnabled(ev)) {
            this.stopSelectedTorrents();
            this.hideMobileAddressbar();
        }
    },

    startSelectedClicked: function (ev) {
        if (this.isButtonEnabled(ev)) {
            this.startSelectedTorrents(false);
            this.hideMobileAddressbar();
        }
    },

    stopAllClicked: function (ev) {
        if (this.isButtonEnabled(ev)) {
            this.stopAllTorrents();
            this.hideMobileAddressbar();
        }
    },

    startAllClicked: function (ev) {
        if (this.isButtonEnabled(ev)) {
            this.startAllTorrents(false);
            this.hideMobileAddressbar();
        }
    },

    openTorrentClicked: function (ev) {
        if (this.isButtonEnabled(ev)) {
            $('body').addClass('open_showing');
            this.uploadTorrentFile();
            this.updateButtonStates();
        }
    },

    dragenter: function (ev) {
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
        } else if (ev.dataTransfer) {
            ev.dataTransfer.dropEffect = "none";
        }
        return true;
    },

    drop: function (ev) {
        var i, uri;
        var uris = null;
        var types = ["text/uri-list", "text/plain"];
        var paused = this.shouldAddedTorrentsStart();

        if (!ev.dataTransfer || !ev.dataTransfer.types) {
            return true;
        };

        for (i = 0; !uris && i < types.length; ++i) {
            if (ev.dataTransfer.types.contains(types[i])) {
                uris = ev.dataTransfer.getData(types[i]).split("\n");
            };
        };

        for (i = 0; uri = uris[i]; ++i) {
            if (/^#/.test(uri)) { // lines which start with "#" are comments
                continue;
            };
            if (/^[a-z-]+:/i.test(uri)) { // close enough to a url
                this.remote.addTorrentByUrl(uri, paused);
            };
        };

        ev.preventDefault();
        return false;
    },

    hideUploadDialog: function () {
        $('body.open_showing').removeClass('open_showing');
        $('#upload_container').hide();
        this.updateButtonStates();
    },

    confirmUploadClicked: function () {
        this.uploadTorrentFile(true);
        this.hideUploadDialog();
    },

    hideMoveDialog: function () {
        $('#move_container').hide();
        this.updateButtonStates();
    },

    confirmMoveClicked: function () {
        this.moveSelectedTorrents(true);
        this.hideUploadDialog();
    },

    hideRenameDialog: function () {
        $('body.open_showing').removeClass('open_showing');
        $('#rename_container').hide();
    },

    confirmRenameClicked: function () {
        var torrents = this.getSelectedTorrents();
        this.renameTorrent(torrents[0], $('input#torrent_rename_name').attr('value'));
        this.hideRenameDialog();
    },

    removeClicked: function (ev) {
        if (this.isButtonEnabled(ev)) {
            this.removeSelectedTorrents();
            this.hideMobileAddressbar();
        };
    },

    // turn the periodic ajax session refresh on & off
    togglePeriodicSessionRefresh: function (enabled) {
        var that = this,
            msec = 8000;

        function callback() {
            that.loadDaemonPrefs(undefined, rescheduleTimeout);
        }

        function rescheduleTimeout() {
            that.sessionTimeout = setTimeout(callback, msec);
        }

        clearTimeout(this.sessionTimeout);
        delete this.sessionTimeout;

        if (enabled) {
            rescheduleTimeout();
        }
    },

    toggleTurtleClicked: function () {
        var o = {};
        o[RPC._TurtleState] = !$('#turtle-button').hasClass('selected');
        this.remote.savePrefs(o);
    },

    /*--------------------------------------------
     *
     *  I N T E R F A C E   F U N C T I O N S
     *
     *--------------------------------------------*/

    onPrefsDialogClosed: function () {
        $('#prefs-button').removeClass('selected');
    },

    togglePrefsDialogClicked: function (ev) {
        var e = $('#prefs-button');

        if (e.hasClass('selected'))
            this.prefsDialog.close();
        else {
            e.addClass('selected');
            this.prefsDialog.show();
        }
    },

    setFilterText: function (search) {
        this.filterText = search ? search.trim() : null;
        this.refilter(true);
    },

    setSortMethod: function (sort_method) {
        this.setPref(Prefs._SortMethod, sort_method);
        this.refilter(true);
    },

    setSortDirection: function (direction) {
        this.setPref(Prefs._SortDirection, direction);
        this.refilter(true);
    },

    onMenuClicked: function (event, ui) {
        var o, dir;
        var id = ui.id;
        var remote = this.remote;
        var element = ui.target;

        if (ui.group == 'sort-mode') {
            element.selectMenuItem();
            this.setSortMethod(id.replace(/sort_by_/, ''));
        } else if (element.hasClass('upload-speed')) {
            o = {};
            o[RPC._UpSpeedLimit] = parseInt(element.text());
            o[RPC._UpSpeedLimited] = true;
            remote.savePrefs(o);
        } else if (element.hasClass('download-speed')) {
            o = {};
            o[RPC._DownSpeedLimit] = parseInt(element.text());
            o[RPC._DownSpeedLimited] = true;
            remote.savePrefs(o);
        } else {
            switch (id) {
            case 'statistics':
                this.showStatsDialog();
                break;

            case 'hotkeys':
                this.showHotkeysDialog();
                break;

            case 'about-button':
                o = 'Transmission ' + this.serverVersion;
                $('#about-dialog #about-title').html(o);
                $('#about-dialog').dialog({
                    title: 'About',
                    show: 'fade',
                    hide: 'fade'
                });
                break;

            case 'homepage':
                window.open('https://transmissionbt.com/');
                break;

            case 'tipjar':
                window.open('https://transmissionbt.com/donate/');
                break;

            case 'unlimited_download_rate':
                o = {};
                o[RPC._DownSpeedLimited] = false;
                remote.savePrefs(o);
                break;

            case 'limited_download_rate':
                o = {};
                o[RPC._DownSpeedLimited] = true;
                remote.savePrefs(o);
                break;

            case 'unlimited_upload_rate':
                o = {};
                o[RPC._UpSpeedLimited] = false;
                remote.savePrefs(o);
                break;

            case 'limited_upload_rate':
                o = {};
                o[RPC._UpSpeedLimited] = true;
                remote.savePrefs(o);
                break;

            case 'reverse_sort_order':
                if (element.menuItemIsSelected()) {
                    dir = Prefs._SortAscending;
                    element.deselectMenuItem();
                } else {
                    dir = Prefs._SortDescending;
                    element.selectMenuItem();
                }
                this.setSortDirection(dir);
                break;

            case 'toggle_notifications':
                Notifications && Notifications.toggle();
                break;

            default:
                console.log('unhandled: ' + id);
                break;
            };
        };
    },

    onTorrentChanged: function (ev, tor) {
        // update our dirty fields
        this.dirtyTorrents[tor.getId()] = true;

        // enqueue ui refreshes
        this.refilterSoon();
        this.updateButtonsSoon();
    },

    updateFromTorrentGet: function (updates, removed_ids) {
        var i, o, t, id, needed, callback, fields;
        var needinfo = [];

        for (i = 0; o = updates[i]; ++i) {
            id = o.id;
            if ((t = this._torrents[id])) {
                needed = t.needsMetaData();
                t.refresh(o);
                if (needed && !t.needsMetaData()) {
                    needinfo.push(id);
                };
            } else {
                t = this._torrents[id] = new Torrent(o);
                this.dirtyTorrents[id] = true;
                callback = $.proxy(this.onTorrentChanged, this);
                $(t).bind('dataChanged', callback);
                // do we need more info for this torrent?
                if (!('name' in t.fields) || !('status' in t.fields))
                    needinfo.push(id);

                t.notifyOnFieldChange('status', $.proxy(function (newValue, oldValue) {
                    if (oldValue === Torrent._StatusDownload && (newValue == Torrent._StatusSeed || newValue == Torrent._StatusSeedWait)) {
                        $(this).trigger('downloadComplete', [t]);
                    } else if (oldValue === Torrent._StatusSeed && newValue === Torrent._StatusStopped && t.isFinished()) {
                        $(this).trigger('seedingComplete', [t]);
                    } else {
                        $(this).trigger('statusChange', [t]);
                    }
                }, this));
            }
        }

        if (needinfo.length) {
            // whee, new torrents! get their initial information.
            fields = ['id'].concat(Torrent.Fields.Metadata,
                Torrent.Fields.Stats);
            this.updateTorrents(needinfo, fields);
            this.refilterSoon();
        }

        if (removed_ids) {
            this.deleteTorrents(removed_ids);
            this.refilterSoon();
        }
    },

    updateTorrents: function (ids, fields, callback) {
        var that = this;

        function f(updates, removedIds) {
            if (callback) {
                callback();
            }

            that.updateFromTorrentGet(updates, removedIds);
        }

        this.remote.updateTorrents(ids, fields, f);
    },

    refreshTorrents: function () {
        var callback = $.proxy(this.refreshTorrents, this);
        var msec = this[Prefs._RefreshRate] * 1000;
        var fields = ['id'].concat(Torrent.Fields.Stats);

        // send a request right now
        this.updateTorrents('recently-active', fields);

        // schedule the next request
        clearTimeout(this.refreshTorrentsTimeout);
        this.refreshTorrentsTimeout = setTimeout(callback, msec);
    },

    initializeTorrents: function () {
        var fields = ['id'].concat(Torrent.Fields.Metadata, Torrent.Fields.Stats);
        this.updateTorrents(null, fields);
    },

    onRowClicked: function (ev) {
        var meta_key = ev.metaKey || ev.ctrlKey,
            row = ev.currentTarget.row;

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

    deleteTorrents: function (ids) {
        var i, id;

        if (ids && ids.length) {
            for (i = 0; id = ids[i]; ++i) {
                this.dirtyTorrents[id] = true;
                delete this._torrents[id];
            };
            this.refilter();
        };
    },

    shouldAddedTorrentsStart: function () {
        return this.prefsDialog.shouldAddedTorrentsStart();
    },

    /*
     * Select a torrent file to upload
     */
    uploadTorrentFile: function (confirmed) {
        var i, file, reader;
        var fileInput = $('input#torrent_upload_file');
        var folderInput = $('input#add-dialog-folder-input');
        var startInput = $('input#torrent_auto_start');
        var urlInput = $('input#torrent_upload_url');

        if (!confirmed) {
            // update the upload dialog's fields
            fileInput.attr('value', '');
            urlInput.attr('value', '');
            startInput.attr('checked', this.shouldAddedTorrentsStart());
            folderInput.attr('value', $("#download-dir").val());
            folderInput.change($.proxy(this.updateFreeSpaceInAddDialog, this));
            this.updateFreeSpaceInAddDialog();

            // show the dialog
            $('#upload_container').show();
            urlInput.focus();
        } else {
            var paused = !startInput.is(':checked');
            var destination = folderInput.val();
            var remote = this.remote;

            jQuery.each(fileInput[0].files, function (i, file) {
                var reader = new FileReader();
                reader.onload = function (e) {
                    var contents = e.target.result;
                    var key = "base64,"
                    var index = contents.indexOf(key);
                    if (index > -1) {
                        var metainfo = contents.substring(index + key.length);
                        var o = {
                            method: 'torrent-add',
                            arguments: {
                                'paused': paused,
                                'download-dir': destination,
                                'metainfo': metainfo
                            }
                        };
                        remote.sendRequest(o, function (response) {
                            if (response.result != 'success')
                                alert('Error adding "' + file.name + '": ' + response.result);
                        });
                    }
                };
                reader.readAsDataURL(file);
            });

            var url = $('#torrent_upload_url').val();
            if (url != '') {
                if (url.match(/^[0-9a-f]{40}$/i)) {
                    url = 'magnet:?xt=urn:btih:' + url;
                };
                var o = {
                    'method': 'torrent-add',
                    arguments: {
                        'paused': paused,
                        'download-dir': destination,
                        'filename': url
                    }
                };
                remote.sendRequest(o, function (response) {
                    if (response.result != 'success') {
                        alert('Error adding "' + url + '": ' + response.result);
                    };
                });
            }
        }
    },

    promptSetLocation: function (confirmed, torrents) {
        if (!confirmed) {
            var path;
            if (torrents.length === 1) {
                path = torrents[0].getDownloadDir();
            } else {
                path = $("#download-dir").val();
            }
            $('input#torrent_path').attr('value', path);
            $('#move_container').show();
            $('#torrent_path').focus();
        } else {
            var ids = this.getTorrentIds(torrents);
            this.remote.moveTorrents(ids, $("input#torrent_path").val(), this.refreshTorrents, this);
            $('#move_container').hide();
        }
    },

    moveSelectedTorrents: function (confirmed) {
        var torrents = this.getSelectedTorrents();
        if (torrents.length) {
            this.promptSetLocation(confirmed, torrents);
        };
    },

    removeSelectedTorrents: function () {
        var torrents = this.getSelectedTorrents();
        if (torrents.length) {
            this.promptToRemoveTorrents(torrents);
        };
    },

    removeSelectedTorrentsAndData: function () {
        var torrents = this.getSelectedTorrents();
        if (torrents.length) {
            this.promptToRemoveTorrentsAndData(torrents);
        };
    },

    promptToRemoveTorrents: function (torrents) {
        if (torrents.length === 1) {
            var torrent = torrents[0];
            var header = 'Remove ' + torrent.getName() + '?';
            var message = 'Once removed, continuing the transfer will require the torrent file. Are you sure you want to remove it?';

            dialog.confirm(header, message, 'Remove', function () {
                transmission.removeTorrents(torrents);
            });
        } else {
            var header = 'Remove ' + torrents.length + ' transfers?';
            var message = 'Once removed, continuing the transfers will require the torrent files. Are you sure you want to remove them?';

            dialog.confirm(header, message, 'Remove', function () {
                transmission.removeTorrents(torrents);
            });
        }
    },

    promptToRemoveTorrentsAndData: function (torrents) {
        if (torrents.length === 1) {
            var torrent = torrents[0];
            var header = 'Remove ' + torrent.getName() + ' and delete data?';
            var message = 'All data downloaded for this torrent will be deleted. Are you sure you want to remove it?';

            dialog.confirm(header, message, 'Remove', function () {
                transmission.removeTorrentsAndData(torrents);
            });
        } else {
            var header = 'Remove ' + torrents.length + ' transfers and delete data?';
            var message = 'All data downloaded for these torrents will be deleted. Are you sure you want to remove them?';

            dialog.confirm(header, message, 'Remove', function () {
                transmission.removeTorrentsAndData(torrents);
            });
        }
    },

    removeTorrents: function (torrents) {
        var ids = this.getTorrentIds(torrents);
        this.remote.removeTorrents(ids, this.refreshTorrents, this);
    },

    removeTorrentsAndData: function (torrents) {
        this.remote.removeTorrentsAndData(torrents);
    },

    promptToRenameTorrent: function (torrent) {
        $('body').addClass('open_showing');
        $('input#torrent_rename_name').attr('value', torrent.getName());
        $('#rename_container').show();
        $('#torrent_rename_name').focus();
    },

    renameSelectedTorrents: function () {
        var torrents = this.getSelectedTorrents();
        if (torrents.length != 1) {
            dialog.alert("Renaming", "You can rename only one torrent at a time.", "Ok");
        } else {
            this.promptToRenameTorrent(torrents[0]);
        };
    },

    onTorrentRenamed: function (response) {
        var torrent;
        if ((response.result === 'success') && (response.arguments) && ((torrent = this._torrents[response.arguments.id]))) {
            torrent.refresh(response.arguments);
        }
    },

    renameTorrent: function (torrent, newname) {
        var oldpath = torrent.getName();
        this.remote.renameTorrent([torrent.getId()], oldpath, newname, this.onTorrentRenamed, this);
    },

    verifySelectedTorrents: function () {
        this.verifyTorrents(this.getSelectedTorrents());
    },

    reannounceSelectedTorrents: function () {
        this.reannounceTorrents(this.getSelectedTorrents());
    },

    startAllTorrents: function (force) {
        this.startTorrents(this.getAllTorrents(), force);
    },
    startSelectedTorrents: function (force) {
        this.startTorrents(this.getSelectedTorrents(), force);
    },
    startTorrent: function (torrent) {
        this.startTorrents([torrent], false);
    },

    startTorrents: function (torrents, force) {
        this.remote.startTorrents(this.getTorrentIds(torrents), force, this.refreshTorrents, this);
    },
    verifyTorrent: function (torrent) {
        this.verifyTorrents([torrent]);
    },
    verifyTorrents: function (torrents) {
        this.remote.verifyTorrents(this.getTorrentIds(torrents), this.refreshTorrents, this);
    },

    reannounceTorrent: function (torrent) {
        this.reannounceTorrents([torrent]);
    },
    reannounceTorrents: function (torrents) {
        this.remote.reannounceTorrents(this.getTorrentIds(torrents), this.refreshTorrents, this);
    },

    stopAllTorrents: function () {
        this.stopTorrents(this.getAllTorrents());
    },
    stopSelectedTorrents: function () {
        this.stopTorrents(this.getSelectedTorrents());
    },
    stopTorrent: function (torrent) {
        this.stopTorrents([torrent]);
    },
    stopTorrents: function (torrents) {
        this.remote.stopTorrents(this.getTorrentIds(torrents), this.refreshTorrents, this);
    },
    changeFileCommand: function (torrentId, rowIndices, command) {
        this.remote.changeFileCommand(torrentId, rowIndices, command);
    },

    hideMobileAddressbar: function (delaySecs) {
        if (isMobileDevice && !scroll_timeout) {
            var callback = $.proxy(this.doToolbarHide, this);
            var msec = delaySecs * 1000 || 150;
            scroll_timeout = setTimeout(callback, msec);
        };
    },
    doToolbarHide: function () {
        window.scrollTo(0, 1);
        scroll_timeout = null;
    },

    // Queue
    moveTop: function () {
        this.remote.moveTorrentsToTop(this.getSelectedTorrentIds(), this.refreshTorrents, this);
    },
    moveUp: function () {
        this.remote.moveTorrentsUp(this.getSelectedTorrentIds(), this.refreshTorrents, this);
    },
    moveDown: function () {
        this.remote.moveTorrentsDown(this.getSelectedTorrentIds(), this.refreshTorrents, this);
    },
    moveBottom: function () {
        this.remote.moveTorrentsToBottom(this.getSelectedTorrentIds(), this.refreshTorrents, this);
    },

    /***
     ****
     ***/

    updateGuiFromSession: function (o) {
        var limit, limited, e, b, text;
        var fmt = Transmission.fmt;
        var menu = $('#footer_super_menu');

        this.serverVersion = o.version;

        this.prefsDialog.set(o);

        if (RPC._TurtleState in o) {
            b = o[RPC._TurtleState];
            e = $('#turtle-button');
            text = ['Click to ', (b ? 'disable' : 'enable'), ' Temporary Speed Limits (', fmt.speed(o[RPC._TurtleUpSpeedLimit]), ' up,', fmt.speed(o[RPC._TurtleDownSpeedLimit]), ' down)'].join('');
            e.toggleClass('selected', b);
            e.attr('title', text);
        }

        if (this.isMenuEnabled && (RPC._DownSpeedLimited in o) && (RPC._DownSpeedLimit in o)) {
            limit = o[RPC._DownSpeedLimit];
            limited = o[RPC._DownSpeedLimited];

            e = menu.find('#limited_download_rate');
            e.html('Limit (' + fmt.speed(limit) + ')');

            if (!limited) {
                e = menu.find('#unlimited_download_rate');
            };
            e.selectMenuItem();
        }

        if (this.isMenuEnabled && (RPC._UpSpeedLimited in o) && (RPC._UpSpeedLimit in o)) {
            limit = o[RPC._UpSpeedLimit];
            limited = o[RPC._UpSpeedLimited];

            e = menu.find('#limited_upload_rate');
            e.html('Limit (' + fmt.speed(limit) + ')');

            if (!limited) {
                e = menu.find('#unlimited_upload_rate');
            };
            e.selectMenuItem();
        }
    },

    updateStatusbar: function () {
        var i, row;
        var u = 0;
        var d = 0;
        var fmt = Transmission.fmt;
        var torrents = this.getAllTorrents();

        // up/down speed
        for (i = 0; row = torrents[i]; ++i) {
            u += row.getUploadSpeed();
            d += row.getDownloadSpeed();
        }

        $('#speed-up-container').toggleClass('active', u > 0);
        $('#speed-up-label').text(fmt.speedBps(u));

        $('#speed-dn-container').toggleClass('active', d > 0);
        $('#speed-dn-label').text(fmt.speedBps(d));

        // visible torrents
        $('#filter-count').text(fmt.countString('Transfer', 'Transfers', this._rows.length));
    },

    setEnabled: function (key, flag) {
        $(key).toggleClass('disabled', !flag);
    },

    updateFilterSelect: function () {
        var i, names, name, str, o;
        var e = $('#filter-tracker');
        var trackers = this.getTrackers();

        // build a sorted list of names
        names = [];
        for (name in trackers) {
            names.push(name);
        };
        names.sort();

        // build the new html
        if (!this.filterTracker) {
            str = '<option value="all" selected="selected">All</option>';
        } else {
            str = '<option value="all">All</option>';
        };
        for (i = 0; name = names[i]; ++i) {
            o = trackers[name];
            str += '<option value="' + o.domain + '"';
            if (trackers[name].domain === this.filterTracker) {
                str += ' selected="selected"';
            };
            str += '>' + name + '</option>';
        }

        if (!this.filterTrackersStr || (this.filterTrackersStr !== str)) {
            this.filterTrackersStr = str;
            $('#filter-tracker').html(str);
        }
    },

    updateButtonsSoon: function () {
        if (!this.buttonRefreshTimer) {
            var callback = $.proxy(this.updateButtonStates, this);
            var msec = 100;

            this.buttonRefreshTimer = setTimeout(callback, msec);
        }
    },

    calculateTorrentStates: function (callback) {
        var stats = {
            total: 0,
            active: 0,
            paused: 0,
            sel: 0,
            activeSel: 0,
            pausedSel: 0,
            queuedSel: 0
        };

        clearTimeout(this.buttonRefreshTimer);
        delete this.buttonRefreshTimer;

        for (var i = 0, row; row = this._rows[i]; ++i) {
            var isStopped = row.getTorrent().isStopped();
            var isSelected = row.isSelected();
            var isQueued = row.getTorrent().isQueued();
            ++stats.total;
            if (!isStopped) {
                ++stats.active;
            };
            if (isStopped) {
                ++stats.paused;
            };
            if (isSelected) {
                ++stats.sel;
            };
            if (isSelected && !isStopped) {
                ++stats.activeSel;
            };
            if (isSelected && isStopped) {
                ++stats.pausedSel;
            };
            if (isSelected && isQueued) {
                ++stats.queuedSel;
            };
        };

        callback(stats);
    },

    updateButtonStates: function () {
        var tr = this;
        var e = this.elements;

        this.calculateTorrentStates(function (s) {
            tr.setEnabled(e.toolbar_pause_button, s.activeSel > 0);
            tr.setEnabled(e.toolbar_start_button, s.pausedSel > 0);
            tr.setEnabled(e.toolbar_remove_button, s.sel > 0);
        });
    },

    /****
     *****
     *****  INSPECTOR
     *****
     ****/

    inspectorIsVisible: function () {
        return $('#torrent_inspector').is(':visible');
    },
    toggleInspector: function () {
        this.setInspectorVisible(!this.inspectorIsVisible());
    },
    setInspectorVisible: function (visible) {
        if (visible) {
            this.inspector.setTorrents(this.getSelectedTorrents());
        };

        // update the ui widgetry
        $('#torrent_inspector').toggle(visible);
        $('#toolbar-inspector').toggleClass('selected', visible);
        this.hideMobileAddressbar();
        if (isMobileDevice) {
            $('body').toggleClass('inspector_showing', visible);
        } else {
            var w = visible ? $('#torrent_inspector').outerWidth() + 1 + 'px' : '0px';
            $('#torrent_container')[0].style.right = w;
        }
    },

    /****
     *****
     *****  FILTER
     *****
     ****/

    refilterSoon: function () {
        if (!this.refilterTimer) {
            var tr = this,
                callback = function () {
                    tr.refilter(false);
                },
                msec = 100;
            this.refilterTimer = setTimeout(callback, msec);
        }
    },

    sortRows: function (rows) {
        var i, tor, row,
            id2row = {},
            torrents = [];

        for (i = 0; row = rows[i]; ++i) {
            tor = row.getTorrent();
            torrents.push(tor);
            id2row[tor.getId()] = row;
        }

        Torrent.sortTorrents(torrents, this[Prefs._SortMethod],
            this[Prefs._SortDirection]);

        for (i = 0; tor = torrents[i]; ++i) {
            rows[i] = id2row[tor.getId()];
        };
    },

    refilter: function (rebuildEverything) {
        var i, e, id, t, row, tmp, rows, clean_rows, dirty_rows, frag;
        var sort_mode = this[Prefs._SortMethod];
        var sort_direction = this[Prefs._SortDirection];
        var filter_mode = this[Prefs._FilterMode];
        var filter_text = this.filterText;
        var filter_tracker = this.filterTracker;
        var renderer = this.torrentRenderer;
        var list = this.elements.torrent_list;

        old_sel_count = $(list).children('.selected').length;

        this.updateFilterSelect();

        clearTimeout(this.refilterTimer);
        delete this.refilterTimer;

        if (rebuildEverything) {
            $(list).empty();
            this._rows = [];
            for (id in this._torrents) {
                this.dirtyTorrents[id] = true;
            };
        }

        // rows that overlap with dirtyTorrents need to be refiltered.
        // those that don't are 'clean' and don't need refiltering.
        clean_rows = [];
        dirty_rows = [];
        for (i = 0; row = this._rows[i]; ++i) {
            if (row.getTorrentId() in this.dirtyTorrents) {
                dirty_rows.push(row);
            } else {
                clean_rows.push(row);
            };
        }

        // remove the dirty rows from the dom
        e = [];
        for (i = 0; row = dirty_rows[i]; ++i) {
            e.push(row.getElement());
        };
        $(e).detach();

        // drop any dirty rows that don't pass the filter test
        tmp = [];
        for (i = 0; row = dirty_rows[i]; ++i) {
            id = row.getTorrentId();
            t = this._torrents[id];
            if (t && t.test(filter_mode, filter_text, filter_tracker)) {
                tmp.push(row);
            };
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
                $(e).click($.proxy(this.onRowClicked, this));
                $(e).dblclick($.proxy(this.toggleInspector, this));
            }
        }

        // sort the dirty rows
        this.sortRows(dirty_rows);

        // now we have two sorted arrays of rows
        // and can do a simple two-way sorted merge.
        rows = [];
        var ci = 0,
            cmax = clean_rows.length;
        var di = 0,
            dmax = dirty_rows.length;
        frag = document.createDocumentFragment();
        while (ci != cmax || di != dmax) {
            var push_clean;

            if (ci == cmax) {
                push_clean = false;
            } else if (di == dmax) {
                push_clean = true;
            } else {
                var c = Torrent.compareTorrents(clean_rows[ci].getTorrent(), dirty_rows[di].getTorrent(), sort_mode, sort_direction);
                push_clean = (c < 0);
            }

            if (push_clean) {
                rows.push(clean_rows[ci++]);
            } else {
                row = dirty_rows[di++];
                e = row.getElement();

                if (ci !== cmax) {
                    list.insertBefore(e, clean_rows[ci].getElement());
                } else {
                    frag.appendChild(e);
                };

                rows.push(row);
            }
        }
        list.appendChild(frag);

        // update our implementation fields
        this._rows = rows;
        this.dirtyTorrents = {};

        // jquery's even/odd starts with 1 not 0, so invert its logic
        e = []
        for (i = 0; row = rows[i]; ++i) {
            e.push(row.getElement());
        };
        $(e).filter(":odd").addClass('even');
        $(e).filter(":even").removeClass('even');

        // sync gui
        this.updateStatusbar();
        if (old_sel_count !== $(list).children('.selected').length) {
            this.selectionChanged();
        };
    },

    setFilterMode: function (mode) {
        // set the state
        this.setPref(Prefs._FilterMode, mode);

        // refilter
        this.refilter(true);
    },

    onFilterModeClicked: function (ev) {
        this.setFilterMode($('#filter-mode').val());
    },

    onFilterTrackerClicked: function (ev) {
        var tracker = $('#filter-tracker').val();
        this.setFilterTracker(tracker === 'all' ? null : tracker);
    },

    setFilterTracker: function (domain) {
        // update which tracker is selected in the popup
        var key = domain ? this.getReadableDomain(domain) : 'all';
        var id = '#show-tracker-' + key;

        $(id).addClass('selected').siblings().removeClass('selected');

        this.filterTracker = domain;
        this.refilter(true);
    },

    // example: "tracker.ubuntu.com" returns "ubuntu.com"
    getDomainName: function (host) {
        var dot = host.indexOf('.');
        if (dot !== host.lastIndexOf('.')) {
            host = host.slice(dot + 1);
        };

        return host;
    },

    // example: "ubuntu.com" returns "Ubuntu"
    getReadableDomain: function (name) {
        if (name.length) {
            name = name.charAt(0).toUpperCase() + name.slice(1);
        };
        var dot = name.indexOf('.');
        if (dot !== -1) {
            name = name.slice(0, dot);
        };
        return name;
    },

    getTrackers: function () {
        var ret = {};

        var torrents = this.getAllTorrents();
        for (var i = 0, torrent; torrent = torrents[i]; ++i) {
            var names = [];
            var trackers = torrent.getTrackers();

            for (var j = 0, tracker; tracker = trackers[j]; ++j) {
                var uri, announce = tracker.announce;

                if (announce in this.uriCache) {
                    uri = this.uriCache[announce];
                } else {
                    uri = this.uriCache[announce] = parseUri(announce);
                    uri.domain = this.getDomainName(uri.host);
                    uri.name = this.getReadableDomain(uri.domain);
                };

                if (!(uri.name in ret)) {
                    ret[uri.name] = {
                        'uri': uri,
                        'domain': uri.domain,
                        'count': 0
                    };
                };

                if (names.indexOf(uri.name) === -1) {
                    names.push(uri.name);
                };
            }

            for (var j = 0, name; name = names[j]; ++j) {
                ret[name].count++;
            };
        }

        return ret;
    },

    /***
     ****
     ****  Compact Mode
     ****
     ***/

    toggleCompactClicked: function () {
        this.setCompactMode(!this[Prefs._CompactDisplayState]);
    },
    setCompactMode: function (is_compact) {
        var key = Prefs._CompactDisplayState;
        var was_compact = this[key];

        if (was_compact !== is_compact) {
            this.setPref(key, is_compact);
            this.onCompactModeChanged();
        };
    },
    initCompactMode: function () {
        this.onCompactModeChanged();
    },
    onCompactModeChanged: function () {
        var compact = this[Prefs._CompactDisplayState];

        // update the ui: footer button
        $("#compact-button").toggleClass('selected', compact);

        // update the ui: torrent list
        this.torrentRenderer = compact ? new TorrentRendererCompact() : new TorrentRendererFull();
        this.refilter(true);
    },

    /***
     ****
     ****  Statistics
     ****
     ***/

    // turn the periodic ajax stats refresh on & off
    togglePeriodicStatsRefresh: function (enabled) {
        var that = this,
            msec = 5000;

        function callback() {
            that.loadDaemonStats(undefined, rescheduleTimeout);
        }

        function rescheduleTimeout() {
            that.statsTimeout = setTimeout(callback, msec);
        }

        clearTimeout(this.statsTimeout);
        delete this.statsTimeout;

        if (enabled) {
            rescheduleTimeout();
        }
    },

    loadDaemonStats: function (async, callback) {
        this.remote.loadDaemonStats(function (data) {
            this.updateStats(data['arguments']);

            if (callback) {
                callback();
            }
        }, this, async);
    },

    // Process new session stats from the server
    updateStats: function (stats) {
        var s, ratio;
        var fmt = Transmission.fmt;

        s = stats["current-stats"];
        ratio = Math.ratio(s.uploadedBytes, s.downloadedBytes);
        $('#stats-session-uploaded').html(fmt.size(s.uploadedBytes));
        $('#stats-session-downloaded').html(fmt.size(s.downloadedBytes));
        $('#stats-session-ratio').html(fmt.ratioString(ratio));
        $('#stats-session-duration').html(fmt.timeInterval(s.secondsActive));

        s = stats["cumulative-stats"];
        ratio = Math.ratio(s.uploadedBytes, s.downloadedBytes);
        $('#stats-total-count').html(s.sessionCount + " times");
        $('#stats-total-uploaded').html(fmt.size(s.uploadedBytes));
        $('#stats-total-downloaded').html(fmt.size(s.downloadedBytes));
        $('#stats-total-ratio').html(fmt.ratioString(ratio));
        $('#stats-total-duration').html(fmt.timeInterval(s.secondsActive));
    },

    showStatsDialog: function () {
        this.loadDaemonStats();
        this.hideMobileAddressbar();
        this.togglePeriodicStatsRefresh(true);
        $('#stats-dialog').dialog({
            close: $.proxy(this.onStatsDialogClosed, this),
            show: 'fade',
            hide: 'fade',
            title: 'Statistics'
        });
    },

    onStatsDialogClosed: function () {
        this.hideMobileAddressbar();
        this.togglePeriodicStatsRefresh(false);
    },

    /***
     ****
     ****  Hotkeys
     ****
     ***/
    showHotkeysDialog: function () {
        $('#hotkeys-dialog').dialog({
            title: 'Hotkeys',
            show: 'fade',
            hide: 'fade'
        });
    }
};
