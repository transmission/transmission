/*
 *	Copyright © Dave Perrett and Malcolm Jarvis
 *	This code is licensed under the GPL version 2.
 *	For more details, see http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 *
 * Common javascript
 */

var transmission;
var dialog;
// Test for a Webkit build that supports box-shadow: 521+ (release Safari 3 is
// actually 523.10.3). We need 3.1 for CSS animation (dialog sheets) but as it
// degrades gracefully let's not worry too much.
var Safari3 = testSafari3();
var iPhone = RegExp("(iPhone|iPod)").test(navigator.userAgent);
if (iPhone) var scroll_timeout;

function testSafari3()
{
    var minimum = new Array(521,0);
    var webKitFields = RegExp("( AppleWebKit/)([^ ]+)").exec(navigator.userAgent);
    if (!webKitFields || webKitFields.length < 3) return false;
    var version = webKitFields[2].split(".");
    for (var i = 0; i < minimum.length; i++) {
        var toInt = parseInt(version[i]);
        var versionField = isNaN(toInt) ? 0 : toInt;
        var minimumField = minimum[i];
        
        if (versionField > minimumField) return true;
        if (versionField < minimumField) return false;
    }
    return true;
};

$(document).ready( function() {
	// Initialise a torrent controller to handle events
	
	// Initialise the dialog controller
	dialog = new Dialog();
	
	// Initialise the main Transmission controller
	transmission = new Transmission();

	if ($.browser.safari) {
		
		// Fix div height problem - causes scrollbar flash in
		// firefox so have to be safari-specific
		$('#torrent_inspector').css('height', '100%');
		
		// Move search field's margin down for the styled input
		$('#torrent_search').css('margin-top', 3);		
	}
	if (!Safari3 && !iPhone) {
		// Fix for non-Safari-3 browsers: dark borders to replace shadows.
		// Opera messes up the menu if we use a border on .trans_menu
		// div.outerbox so use ul instead
		$('.trans_menu ul, div#jqContextMenu, div.dialog_container div.dialog_window').css('border', '1px solid #777');
		// and this kills the border we used to have
		$('.trans_menu div.outerbox').css('border', 'none');
	} else if (!iPhone) {
		// Used for Safari 3.1 CSS animation. Degrades gracefully (so Safari 3
		// test is good enough) but we delay our hide/unhide to wait for the
		// scrolling - no point making other browsers wait.
		$('div#upload_container div.dialog_window').css('top', '-205px');
		$('div#prefs_container div.dialog_window').css('top', '-425px');
		$('div#dialog_container div.dialog_window').css('top', '-425px');
		$('div.dialog_container div.dialog_window').css('-webkit-transition', 'top 0.3s');
		// -webkit-appearance makes some links into buttons, but needs
		// different padding.
		$('div.dialog_container div.dialog_window a').css('padding', '2px 10px 3px');
	}
});

/*
 *   Return a copy of the array
 *
 *   @returns array
 */
Array.prototype.clone = function () {
	return this.concat();
};

/**
 * "innerHTML = html" is pretty slow in FF.  Happily a lot of our innerHTML
 * changes are triggered by periodic refreshes on torrents whose state hasn't
 * changed sine the last update, so even this simple test helps a lot.
 */
function setInnerHTML( e, html )
{
	if( e.innerHTML != html )
		e.innerHTML = html;
};

/*
 *   Converts file & folder byte size values to more  
 *   readable values (bytes, KB, MB, GB or TB).
 *
 *   @param integer bytes
 *   @returns string
 */
Math.formatBytes = function(bytes) {
    var size;
    var unit;
    
    // Terabytes (TB).
    if ( bytes >= 1099511627776 ) { 
        size = bytes / 1099511627776;
		unit = ' TB'; 
    
    // Gigabytes (GB).
    } else if ( bytes >= 1073741824 ) { 
        size = bytes / 1073741824;
		unit = ' GB';

    // Megabytes (MB).
    } else if ( bytes >= 1048576 ) { 
        size = bytes / 1048576;
		unit = ' MB';

    // Kilobytes (KB).
    } else if ( bytes >= 1024 ) { 
        size = bytes / 1024;
		unit = ' KB';

    // The file is less than one KB
    } else {
        size = bytes;
		unit = ' bytes';
    }
	
	// Single-digit numbers have greater precision
	var precision = 1; 
	if (size < 10) {
	    precision = 2;
	}
	size = Math.roundWithPrecision(size, precision);
    
	// Add the decimal if this is an integer
	if ((size % 1) == 0 && unit != ' bytes') {
		size = size + '.0';
	}

    return size + unit;
};


/*
 *   Converts seconds to more readable units (hours, minutes etc).
 *
 *   @param integer seconds
 *   @returns string
 */
Math.formatSeconds = function(seconds)
{
	var result;
	var days = Math.floor(seconds / 86400);
	var hours = Math.floor((seconds % 86400) / 3600);
	var minutes = Math.floor((seconds % 3600) / 60);
	var seconds = Math.floor((seconds % 3600) % 60);    

	if (days > 0 && hours == 0)
		result = days + ' days';
	else if (days > 0 && hours > 0)
		result = days + ' days ' + hours + ' hr';
	else if (hours > 0 && minutes == 0)
		result = hours + ' hr';   
	else if (hours > 0 && minutes > 0)
		result = hours + ' hr ' + minutes + ' min';
	else if (minutes > 0 && seconds == 0)
		result = minutes + ' min';
	else if (minutes > 0 && seconds > 0)
		result = minutes + ' min ' + seconds + ' seconds';
	else
		result = seconds + ' seconds';  

	return result;
};


/*
 *   Converts a unix timestamp to a human readable value
 *
 *   @param integer seconds
 *   @returns string
 */
Math.formatTimestamp = function(seconds) {
	var myDate = new Date(seconds*1000);
	return myDate.toGMTString();
};

/*
 *   Round a float to a specified number of decimal 
 *   places, stripping trailing zeroes
 *
 *   @param float floatnum
 *   @param integer precision
 *   @returns float
 */
Math.roundWithPrecision = function(floatnum, precision) {
    return Math.round ( floatnum * Math.pow ( 10, precision ) ) / Math.pow ( 10, precision );
};


/*
 *   Given a numerator and denominator, return a ratio string
 */
Math.ratio = function( numerator, denominator )
{
	var result = Math.roundWithPrecision((numerator / denominator), 2);

	// check for special cases
	if (isNaN(result)) result = 0;
	if (result=="Infinity") result = "&infin;";

	// Add the decimals if this is an integer
	if ((result % 1) == 0)
		result = result + '.00';

	return result;
};

/*
 * Trim whitespace from a string
 */
String.prototype.trim = function () {
	return this.replace(/^\s*/, "").replace(/\s*$/, "");
}

/**
 * @brief strcmp()-style compare useful for sorting
 */
String.prototype.compareTo = function( that ) {
	// FIXME: how to fold these two comparisons together?
        if( this < that ) return -1;
        if( this > that ) return 1;
        return 0;
}


/***
****  Preferences
***/

function Prefs() { }
Prefs.prototype = { };

Prefs._AutoStart          = 'auto-start-torrents';

Prefs._RefreshRate        = 'refresh_rate';

Prefs._ShowFilter         = 'show_filter';

Prefs._ShowInspector      = 'show_inspector';

Prefs._FilterMode         = 'filter';
Prefs._FilterAll          = 'all';
Prefs._FilterSeeding      = 'seeding';
Prefs._FilterDownloading  = 'downloading';
Prefs._FilterPaused       = 'paused';

Prefs._SortDirection      = 'sort_direction';
Prefs._SortAscending      = 'ascending';
Prefs._SortDescending     = 'descending';

Prefs._SortMethod         = 'sort_method';
Prefs._SortByAge          = 'age';
Prefs._SortByActivity     = 'activity';
Prefs._SortByQueue        = 'queue_order';
Prefs._SortByName         = 'name';
Prefs._SortByProgress     = 'percent_completed';
Prefs._SortByState        = 'state';
Prefs._SortByTracker      = 'tracker';


Prefs._Defaults =
{
	'auto-start-torrents': true,
	'filter': 'all',
	'refresh_rate' : 5,
	'show_filter': true,
	'show_inspector': false,
	'sort_direction': 'ascending',
	'sort_method': 'name'
};

/*
 * Set a preference option
 */
Prefs.setValue = function( key, val )
{
	if( Prefs._Defaults[key] == undefined )
		console.warn( "unrecognized preference key '%s'", key );

	var days = 30;
	var date = new Date();
	date.setTime(date.getTime()+(days*24*60*60*1000));
	document.cookie = key+"="+val+"; expires="+date.toGMTString()+"; path=/";
};

/**
 * Get a preference option
 *
 * @param key the preference's key
 * @param fallback if the option isn't set, return this instead
 */
Prefs.getValue = function( key, fallback )
{
	var val;

	if( Prefs._Defaults[key] == undefined )
		console.warn( "unrecognized preference key '%s'", key );

        var lines = document.cookie.split( ';' );
        for( var i=0, len=lines.length; !val && i<len; ++i ) {
		var line = lines[i].trim( );
		var delim = line.indexOf( '=' );
		if( ( delim == key.length ) && line.indexOf( key ) == 0 )
			val = line.substring( delim + 1 );
	}

	// FIXME: we support strings and booleans... add number support too?
	if( !val ) val = fallback;
	else if( val == 'true' ) val = true;
	else if( val == 'false' ) val = false;
	return val;
};

/**
 * Get an object with all the Clutch preferences set
 *
 * @pararm o object to be populated (optional)
 */
Prefs.getClutchPrefs = function( o )
{
	if( !o )
		o = { };
	for( var key in Prefs._Defaults )
		o[key] = Prefs.getValue( key, Prefs._Defaults[key] );
	return o;
};
