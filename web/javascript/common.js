/**
 * Copyright © Dave Perrett, Malcolm Jarvis and Artem Vorotnikov
 *
 * This file is licensed under the GPLv2.
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 */

var transmission,
    dialog,
    isMobileDevice = RegExp("(iPhone|iPod|Android)").test(navigator.userAgent),
    scroll_timeout;

// http://forum.jquery.com/topic/combining-ui-dialog-and-tabs
$.fn.tabbedDialog = function (dialog_opts) {
    this.tabs({
        selected: 0
    });
    this.dialog(dialog_opts);
    this.find('.ui-tab-dialog-close').append(this.parent().find('.ui-dialog-titlebar-close'));
    this.find('.ui-tab-dialog-close').css({
        'position': 'absolute',
        'right': '0',
        'top': '16px'
    });
    this.find('.ui-tab-dialog-close > a').css({
        'float': 'none',
        'padding': '0'
    });
    var tabul = this.find('ul:first');
    this.parent().addClass('ui-tabs').prepend(tabul).draggable('option', 'handle', tabul);
    this.siblings('.ui-dialog-titlebar').remove();
    tabul.addClass('ui-dialog-titlebar');
}

/**
 * Checks to see if the content actually changed before poking the DOM.
 */
function setInnerHTML(e, html) {
    if (!e) {
        return;
    };

    /* innerHTML is listed as a string, but the browser seems to change it.
     * For example, "&infin;" gets changed to "∞" somewhere down the line.
     * So, let's use an arbitrary  different field to test our state... */
    if (e.currentHTML != html) {
        e.currentHTML = html;
        e.innerHTML = html;
    };
};

function sanitizeText(text) {
    return text.replace(/</g, "&lt;").replace(/>/g, "&gt;");
};

/**
 * Many of our text changes are triggered by periodic refreshes
 * on torrents whose state hasn't changed since the last update,
 * so see if the text actually changed before poking the DOM.
 */
function setTextContent(e, text) {
    if (e && (e.textContent != text)) {
        e.textContent = text;
    };
};

/*
 *   Given a numerator and denominator, return a ratio string
 */
Math.ratio = function (numerator, denominator) {
    var result = Math.floor(100 * numerator / denominator) / 100;

    // check for special cases
    if (result == Number.POSITIVE_INFINITY || result == Number.NEGATIVE_INFINITY) {
        result = -2;
    } else if (isNaN(result)) {
        result = -1;
    };

    return result;
};

/**
 * Round a string of a number to a specified number of decimal places
 */
Number.prototype.toTruncFixed = function (place) {
    var ret = Math.floor(this * Math.pow(10, place)) / Math.pow(10, place);
    return ret.toFixed(place);
};

Number.prototype.toStringWithCommas = function () {
    return this.toString().replace(/\B(?=(?:\d{3})+(?!\d))/g, ",");
};

/*
 * Trim whitespace from a string
 */
String.prototype.trim = function () {
    return this.replace(/^\s*/, "").replace(/\s*$/, "");
};

/***
 ****  Preferences
 ***/

function Prefs() {};
Prefs.prototype = {};

Prefs._RefreshRate = 'refresh_rate';

Prefs._FilterMode = 'filter';
Prefs._FilterAll = 'all';
Prefs._FilterActive = 'active';
Prefs._FilterSeeding = 'seeding';
Prefs._FilterDownloading = 'downloading';
Prefs._FilterPaused = 'paused';
Prefs._FilterFinished = 'finished';

Prefs._SortDirection = 'sort_direction';
Prefs._SortAscending = 'ascending';
Prefs._SortDescending = 'descending';

Prefs._SortMethod = 'sort_method';
Prefs._SortByAge = 'age';
Prefs._SortByActivity = 'activity';
Prefs._SortByName = 'name';
Prefs._SortByQueue = 'queue_order';
Prefs._SortBySize = 'size';
Prefs._SortByProgress = 'percent_completed';
Prefs._SortByRatio = 'ratio';
Prefs._SortByState = 'state';

Prefs._CompactDisplayState = 'compact_display_state';

Prefs._Defaults = {
    'filter': 'all',
    'refresh_rate': 5,
    'sort_direction': 'ascending',
    'sort_method': 'name',
    'turtle-state': false,
    'compact_display_state': false
};

/*
 * Set a preference option
 */
Prefs.setValue = function (key, val) {
    if (!(key in Prefs._Defaults)) {
        console.warn("unrecognized preference key '%s'", key);
    };

    var date = new Date();
    date.setFullYear(date.getFullYear() + 1);
    document.cookie = key + "=" + val + "; expires=" + date.toGMTString() + "; path=/";
};

/**
 * Get a preference option
 *
 * @param key the preference's key
 * @param fallback if the option isn't set, return this instead
 */
Prefs.getValue = function (key, fallback) {
    var val;

    if (!(key in Prefs._Defaults)) {
        console.warn("unrecognized preference key '%s'", key);
    };

    var lines = document.cookie.split(';');
    for (var i = 0, len = lines.length; !val && i < len; ++i) {
        var line = lines[i].trim();
        var delim = line.indexOf('=');
        if ((delim === key.length) && line.indexOf(key) === 0) {
            val = line.substring(delim + 1);
        };
    };

    // FIXME: we support strings and booleans... add number support too?
    if (!val) {
        val = fallback;
    } else if (val === 'true') {
        val = true;
    } else if (val === 'false') {
        val = false;
    };
    return val;
};

/**
 * Get an object with all the Clutch preferences set
 *
 * @pararm o object to be populated (optional)
 */
Prefs.getClutchPrefs = function (o) {
    if (!o) {
        o = {};
    };
    for (var key in Prefs._Defaults) {
        o[key] = Prefs.getValue(key, Prefs._Defaults[key]);
    };
    return o;
};

// forceNumeric() plug-in implementation
jQuery.fn.forceNumeric = function () {
    return this.each(function () {
        $(this).keydown(function (e) {
            var key = e.which || e.keyCode;
            return !e.shiftKey && !e.altKey && !e.ctrlKey &&
                // numbers
                key >= 48 && key <= 57 ||
                // Numeric keypad
                key >= 96 && key <= 105 ||
                // comma, period and minus, . on keypad
                key === 190 || key === 188 || key === 109 || key === 110 ||
                // Backspace and Tab and Enter
                key === 8 || key === 9 || key === 13 ||
                // Home and End
                key === 35 || key === 36 ||
                // left and right arrows
                key === 37 || key === 39 ||
                // Del and Ins
                key === 46 || key === 45;
        });
    });
}

/**
 * http://blog.stevenlevithan.com/archives/parseuri
 *
 * parseUri 1.2.2
 * (c) Steven Levithan <stevenlevithan.com>
 * MIT License
 */
function parseUri(str) {
    var o = parseUri.options;
    var m = o.parser[o.strictMode ? "strict" : "loose"].exec(str);
    var uri = {};
    var i = 14;

    while (i--) {
        uri[o.key[i]] = m[i] || "";
    };

    uri[o.q.name] = {};
    uri[o.key[12]].replace(o.q.parser, function ($0, $1, $2) {
        if ($1) {
            uri[o.q.name][$1] = $2;
        };
    });

    return uri;
};

parseUri.options = {
    strictMode: false,
    key: ["source", "protocol", "authority", "userInfo", "user", "password", "host", "port", "relative", "path", "directory", "file", "query", "anchor"],
    q: {
        name: "queryKey",
        parser: /(?:^|&)([^&=]*)=?([^&]*)/g
    },
    parser: {
        strict: /^(?:([^:\/?#]+):)?(?:\/\/((?:(([^:@]*)(?::([^:@]*))?)?@)?([^:\/?#]*)(?::(\d*))?))?((((?:[^?#\/]*\/)*)([^?#]*))(?:\?([^#]*))?(?:#(.*))?)/,
        loose: /^(?:(?![^:@]+:[^:@\/]*@)([^:\/?#.]+):)?(?:\/\/)?((?:(([^:@]*)(?::([^:@]*))?)?@)?([^:\/?#]*)(?::(\d*))?)(((\/(?:[^?#](?![^?#\/]*\.[^?#\/.]+(?:[?#]|$)))*\/?)?([^?#\/]*))(?:\?([^#]*))?(?:#(.*))?)/
    }
};
