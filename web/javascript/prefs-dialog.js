/**
 * Copyright © Charles Kerr, Dave Perrett, Malcolm Jarvis and Bruno Bierbaumer
 *
 * This file is licensed under the GPLv2.
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 */

function PrefsDialog(remote) {

    var data = {
        dialog: null,
        remote: null,
        elements: {},

        // all the RPC session keys that we have gui controls for
        keys: [
            'alt-speed-down',
            'alt-speed-time-begin',
            'alt-speed-time-day',
            'alt-speed-time-enabled',
            'alt-speed-time-end',
            'alt-speed-up',
            'blocklist-enabled',
            'blocklist-size',
            'blocklist-url',
            'dht-enabled',
            'download-dir',
            'encryption',
            'idle-seeding-limit',
            'idle-seeding-limit-enabled',
            'lpd-enabled',
            'peer-limit-global',
            'peer-limit-per-torrent',
            'peer-port',
            'peer-port-random-on-start',
            'pex-enabled',
            'port-forwarding-enabled',
            'rename-partial-files',
            'seedRatioLimit',
            'seedRatioLimited',
            'speed-limit-down',
            'speed-limit-down-enabled',
            'speed-limit-up',
            'speed-limit-up-enabled',
            'start-added-torrents',
            'utp-enabled'
        ],

        // map of keys that are enabled only if a 'parent' key is enabled
        groups: {
            'alt-speed-time-enabled': ['alt-speed-time-begin',
                'alt-speed-time-day',
                'alt-speed-time-end'
            ],
            'blocklist-enabled': ['blocklist-url',
                'blocklist-update-button'
            ],
            'idle-seeding-limit-enabled': ['idle-seeding-limit'],
            'seedRatioLimited': ['seedRatioLimit'],
            'speed-limit-down-enabled': ['speed-limit-down'],
            'speed-limit-up-enabled': ['speed-limit-up']
        }
    };

    var initTimeDropDown = function (e) {
        var i, hour, mins, value, content;

        for (i = 0; i < 24 * 4; ++i) {
            hour = parseInt(i / 4, 10);
            mins = ((i % 4) * 15);
            value = i * 15;
            content = hour + ':' + (mins || '00');
            e.options[i] = new Option(content, value);
        }
    };

    var onPortChecked = function (response) {
        var is_open = response['arguments']['port-is-open'];
        var text = 'Port is <b>' + (is_open ? 'Open' : 'Closed') + '</b>';
        var e = data.elements.root.find('#port-label');
        setInnerHTML(e[0], text);
    };

    var setGroupEnabled = function (parent_key, enabled) {
        var i, key, keys, root;

        if (parent_key in data.groups) {
            root = data.elements.root;
            keys = data.groups[parent_key];

            for (i = 0; key = keys[i]; ++i) {
                root.find('#' + key).attr('disabled', !enabled);
            };
        };
    };

    var onBlocklistUpdateClicked = function () {
        data.remote.updateBlocklist();
        setBlocklistButtonEnabled(false);
    };

    var setBlocklistButtonEnabled = function (b) {
        var e = data.elements.blocklist_button;
        e.attr('disabled', !b);
        e.val(b ? 'Update' : 'Updating...');
    };

    var getValue = function (e) {
        var str;

        switch (e[0].type) {
        case 'checkbox':
        case 'radio':
            return e.prop('checked');

        case 'text':
        case 'url':
        case 'email':
        case 'number':
        case 'search':
        case 'select-one':
            str = e.val();
            if (parseInt(str, 10).toString() === str) {
                return parseInt(str, 10);
            };
            if (parseFloat(str).toString() === str) {
                return parseFloat(str);
            };
            return str;

        default:
            return null;
        }
    };

    /* this callback is for controls whose changes can be applied
       immediately, like checkboxs, radioboxes, and selects */
    var onControlChanged = function (ev) {
        var o = {};
        o[ev.target.id] = getValue($(ev.target));
        data.remote.savePrefs(o);
    };

    /* these two callbacks are for controls whose changes can't be applied
       immediately -- like a text entry field -- because it takes many
       change events for the user to get to the desired result */
    var onControlFocused = function (ev) {
        data.oldValue = getValue($(ev.target));
    };

    var onControlBlurred = function (ev) {
        var newValue = getValue($(ev.target));
        if (newValue !== data.oldValue) {
            var o = {};
            o[ev.target.id] = newValue;
            data.remote.savePrefs(o);
            delete data.oldValue;
        }
    };

    var getDefaultMobileOptions = function () {
        return {
            width: $(window).width(),
            height: $(window).height(),
            position: ['left', 'top']
        };
    };

    var initialize = function (remote) {
        var i, key, e, o;

        data.remote = remote;

        e = $('#prefs-dialog');
        data.elements.root = e;

        initTimeDropDown(e.find('#alt-speed-time-begin')[0]);
        initTimeDropDown(e.find('#alt-speed-time-end')[0]);

        o = isMobileDevice ? getDefaultMobileOptions() : {
            width: 350,
            height: 400
        };
        o.autoOpen = false;
        o.show = o.hide = 'fade';
        o.close = onDialogClosed;
        e.tabbedDialog(o);

        e = e.find('#blocklist-update-button');
        data.elements.blocklist_button = e;
        e.click(onBlocklistUpdateClicked);

        // listen for user input
        for (i = 0; key = data.keys[i]; ++i) {
            e = data.elements.root.find('#' + key);
            switch (e[0].type) {
            case 'checkbox':
            case 'radio':
            case 'select-one':
                e.change(onControlChanged);
                break;

            case 'text':
            case 'url':
            case 'email':
            case 'number':
            case 'search':
                e.focus(onControlFocused);
                e.blur(onControlBlurred);

            default:
                break;
            };
        };
    };

    var getValues = function () {
        var i, key, val, o = {},
            keys = data.keys,
            root = data.elements.root;

        for (i = 0; key = keys[i]; ++i) {
            val = getValue(root.find('#' + key));
            if (val !== null) {
                o[key] = val;
            };
        };

        return o;
    };

    var onDialogClosed = function () {
        transmission.hideMobileAddressbar();

        $(data.dialog).trigger('closed', getValues());
    };

    /****
     *****  PUBLIC FUNCTIONS
     ****/

    // update the dialog's controls
    this.set = function (o) {
        var e, i, key, val, option;
        var keys = data.keys;
        var root = data.elements.root;

        setBlocklistButtonEnabled(true);

        for (i = 0; key = keys[i]; ++i) {
            val = o[key];
            e = root.find('#' + key);

            if (key === 'blocklist-size') {
                // special case -- regular text area
                e.text('' + val.toStringWithCommas());
            } else switch (e[0].type) {
            case 'checkbox':
            case 'radio':
                e.prop('checked', val);
                setGroupEnabled(key, val);
                break;
            case 'text':
            case 'url':
            case 'email':
            case 'number':
            case 'search':
                // don't change the text if the user's editing it.
                // it's very annoying when that happens!
                if (e[0] !== document.activeElement) {
                    e.val(val);
                };
                break;
            case 'select-one':
                e.val(val);
                break;
            default:
                break;
            };
        };
    };

    this.show = function () {
        transmission.hideMobileAddressbar();

        setBlocklistButtonEnabled(true);
        data.remote.checkPort(onPortChecked, this);
        data.elements.root.dialog('open');
    };

    this.close = function () {
        transmission.hideMobileAddressbar();
        data.elements.root.dialog('close');
    };

    this.shouldAddedTorrentsStart = function () {
        return data.elements.root.find('#start-added-torrents')[0].checked;
    };

    data.dialog = this;
    initialize(remote);
};
