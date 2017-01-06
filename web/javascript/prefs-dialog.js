/**
 * Copyright © Jordan Lee, Dave Perrett, Malcolm Jarvis and Bruno Bierbaumer
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
            'utp-enabled',
            'download-group-default'
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
            'speed-limit-up-enabled': ['speed-limit-up'],
            dGroups: [],
            dGroupsDirty: false,
            spectrumOpt: {
                className: 'full-spectrum',
                showInput: true,
                showInitial: false,
                showPalette: true,
                preferredFormat: "hex",
                showInput: true,
                showButtons: false,
                hide: function (color) {
                    onControlChangedGroupColor(this, color);
                },
                palette: [
                    ["#000","#444","#666","#999","#ccc","#eee","#f3f3f3","#fff"],
                    ["#f00","#f90","#ff0","#0f0","#0ff","#00f","#90f","#f0f"],
                    ["#f4cccc","#fce5cd","#fff2cc","#d9ead3","#d0e0e3","#cfe2f3","#d9d2e9","#ead1dc"],
                    ["#ea9999","#f9cb9c","#ffe599","#b6d7a8","#a2c4c9","#9fc5e8","#b4a7d6","#d5a6bd"],
                    ["#e06666","#f6b26b","#ffd966","#93c47d","#76a5af","#6fa8dc","#8e7cc3","#c27ba0"],
                    ["#c00","#e69138","#f1c232","#6aa84f","#45818e","#3d85c6","#674ea7","#a64d79"],
                    ["#900","#b45f06","#bf9000","#38761d","#134f5c","#0b5394","#351c75","#741b47"],
                    ["#600","#783f04","#7f6000","#274e13","#0c343d","#073763","#20124d","#4c1130"]
                ]

            }
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
    
    var onControlChangedGroupColor  = function(obj, color) {
        var curidx = obj.id.replace('color-', '');
        $('#group-colors div[rel="' + curidx + '"] input.color').val(color.toHexString());
        data.dGroupsDirty = true;
    };
    
    var onControlClickGroupUp  = function(ev)
    {
        // bail when changes have been made but something is wrong
        if(data.dGroupsDirty && !checkGroupValues()) return;

        var curidx = +$(ev.target).parents('div[rel]').attr('rel');
          if(curidx < 1) return;

          var tmp = data.dGroups[curidx];
          data.dGroups[curidx] = data.dGroups[curidx-1];
          data.dGroups[curidx-1] = tmp;
          getGroupsTable();
    };
    
    var onControlClickGroupDown  = function(ev)
    {
        // bail when changes have been made but something is wrong
        if(data.dGroupsDirty && !checkGroupValues()) return;

          var curidx = +$(ev.target).parents('div[rel]').attr('rel');
          if(curidx + 1 >= data.dGroups.length) return;

          var tmp = data.dGroups[curidx];
          data.dGroups[curidx] = data.dGroups[curidx+1];
          data.dGroups[curidx+1] = tmp;
          getGroupsTable();
    };
    
    var onControlClickGroupAdd  = function(ev)
    {
        // bail when changes have been made but something is wrong
        if(data.dGroupsDirty && !checkGroupValues()) return;

        var curidx = +$(ev.target).parents('div[rel]').attr('rel');
          data.dGroups.splice(curidx + 1, 0, ['','','', 2]);
          getGroupsTable();
        data.dGroupsDirty = true;
    };
    
   var onControlClickGroupDelete  = function(ev)
    {
          var curidx = +$(ev.target).parents('div[rel]').attr('rel');
          if(data.dGroups[curidx][3] == 2) { // just added
                  data.dGroups.splice(curidx,1); // no pity to remove
        } else {
            // bail when changes have been made but something is wrong
            if(data.dGroupsDirty && !checkGroupValues()) return;

                  data.dGroups[curidx][3] = !data.dGroups[curidx][3]; // mark as removed
        }
          getGroupsTable();
        data.dGroupsDirty = true;
    };
    
    var getGroupsTable = function(e)
    {
          var optSnip = '',
            tabSnip = '',
            dglen = data.dGroups.length;
        // remove old color-pickers
        $('#group-colors .color-row .color').spectrum('destroy');
        // create snippet text
          for (var idx = 0; idx < dglen; idx++) {
                  var addGroup = data.dGroups[idx];
            optSnip += '<option value="' + addGroup[0].toLowerCase() + '">' + addGroup[0] + '</option>';

            var isDel = (addGroup[3] == 1);
            tabSnip += '<div rel="' + idx + '" class="color-row' + (isDel ? ' removed' : '') + '" name="' + addGroup[0].toLowerCase() + '">';
            tabSnip += '<input type="text" class="group" value="' + addGroup[0] + '" ' + (isDel ? ' disabled="disabled"' : '') + '/>';
            tabSnip += '<input type="text" class="color" id="color-' + idx + '" value="' + addGroup[1] + '" style="display:none;" ' + (isDel ? ' disabled="disabled"' : '') + '/>';
            //tabSnip += '<input type="text" class="code" value="' + addGroup[2] + '" style="dislay:none;" ' + (isDel ? ' disabled="disabled"' : '') + '/>';
            tabSnip += '<span class="controls">';
            tabSnip += '<span class="ui-icon up ui-icon-triangle-1-n' + (idx == 0 ? ' disabled' : '') + '"></span>';
            tabSnip += '<span class="ui-icon down ui-icon-triangle-1-s' + (idx + 1 == dglen ? ' disabled' : '') + '"></span>';
            tabSnip += '<span class="ui-icon add ui-icon-plus"></span>';
            tabSnip += '<span class="ui-icon delete ui-icon-' + (isDel ? 'cancel' : 'close') + (dglen == 1 ? ' disabled' : '') + '"></span>';
            tabSnip += '</span>';
            tabSnip += '</div>';
        }
        // apply snippets
        if(e && optSnip.length > 0) {
            e.empty().append($(optSnip));
        }
        gtab = $('#group-colors').empty().append($(tabSnip));
        // apply color pickers
        gtab.find('.color-row .color').spectrum(data.groups.spectrumOpt);
        // events
        gtab.find('.controls span.up').click('', onControlClickGroupUp);
        gtab.find('.controls span.down').click('', onControlClickGroupDown);
        gtab.find('.controls span.add').click('', onControlClickGroupAdd);
        gtab.find('.controls span.delete').click('', onControlClickGroupDelete);
        // handle changes in inputs
        gtab.find('input').change(function() {
            var curidx = +$(this).parents('div[rel]').attr('rel');
            var fldclass = $(this).attr('class');
            var fldidx = fldclass == 'group' ? 0 : 
                         fldclass == 'color' ? 1 : 2;
            data.dGroups[curidx][fldidx] = $(this).val();
            data.dGroupsDirty = true;
        });
    };
    
    var setGroupButtonEnabled = function(b)
    {
        var e = $('#groups-update-button');
        e.attr('disabled', !b);
        e.val(b ? 'Update' : 'Updating...');
    };
    
    var onControlClickGroupUpdate = function()
    {
        // notinhg to change
        if(!data.dGroupsDirty) return;
        // changes have errors
        if(!checkGroupValues()) return;

        // push new data to server
          setGroupButtonEnabled(false);
        // save prefs
          var o = {};
          o['download-groups'] = data.newGroups;
          data.remote.savePrefs(o, function () {
            data.remote._controller.loadDaemonPrefs();
            data.remote._controller.refilter(true);
            getGroupsTable();
        });
        data.dGroupsDirty = false;
          data.dGroups = data.newGroups;
    };
    
    var checkGroupValues = function()
    {
        var newValue = [],
            dupGroup = []
            dupCode = [],
            errorCount = 0;

        $('#prefs-page-groups .message').html('');
        $('#group-colors').find('.color-row').each(function() {
            $(this).removeClass('error');
            idx = $(this).attr('rel');
            if(data.dGroups[idx][3] != 1) // not removed
            {
                var rowVal = [$.trim($(this).find('input.group').val()),
                              $(this).find('input.color').val()];
                // keep track of dups
                if($.inArray(rowVal[0], dupGroup) == -1) {
                    dupGroup.push(rowVal[0]);
                }
                // check if all values are correct
                if(rowVal[0] != '') {
                    newValue.push(rowVal);
                    return;
                }
                // something must be wrong
                $(this).addClass('error');
                if(rowVal[0] == '') errorCount++;
            }
        });
        // text for content errors
        var errMsg = '';
        if(errorCount != 0) {
            errMsg += 'One or more \'Title\' fields are empty. See marked row(s).';
            $('#prefs-page-groups .message').html(errMsg);
            return false;
        }
        // any duplicates
        if(dupGroup.length != newValue.length)
        {
            errMsg += '\'Title\' fields must be unique.';
            $('#prefs-page-groups .message').html(errMsg);
            return false;
        }
        if(newValue.length == 0)
        {
            $('#prefs-page-groups .message').html('At least one group must be defined.');
            return false;
        }

        data.newGroups = newValue;
        return true;
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
        $('#group-colors .color-row .color').spectrum('hide');


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
        setGroupButtonEnabled(true);


        for (i = 0; key = keys[i]; ++i) {
            val = o[key];
            e = root.find('#' + key);

            if (key === 'blocklist-size') {
                // special case -- regular text area
                e.text('' + val.toStringWithCommas());
            } else if (key === 'download-group-default') {
            // defaults are new or  more/less items (TODO: check if there are differences)
                if (e.children().length == 0 || e.children().length != o['download-groups'].length)
                {
                    data.dGroups = o['download-groups'];
                    data.dGroupsDirty = false;
                          getGroupsTable(e);
                          root.find('input#groups-update-button').click(onControlClickGroupUpdate);
                }

                var currDefGroup = $('#download-group-default');
                if(currDefGroup.val() != val)                          // default has changed
                {
                    currDefGroup.val(val);
                }
            }            
            else switch (e[0].type) {
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
        setGroupButtonEnabled(true);
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
