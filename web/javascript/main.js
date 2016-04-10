/**
 * Copyright © Dave Perrett, Malcolm Jarvis and Artem Vorotnikov
 *
 * This file is licensed under the GPLv2.
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 */

$(document).ready(function () {
    // IE8 and below don’t support ES5 Date.now()
    if (!Date.now) {
        Date.now = function () {
            return +new Date();
        };
    };

    // IE specific fixes here
    if ($.browser.msie) {
        try {
            document.execCommand("BackgroundImageCache", false, true);
        } catch (err) {};
        $('.dialog_container').css('height', $(window).height() + 'px');
    };

    if ($.browser.safari) {
        // Move search field's margin down for the styled input
        $('#torrent_search').css('margin-top', 3);
    };

    if (isMobileDevice) {
        window.onload = function () {
            setTimeout(function () {
                window.scrollTo(0, 1);
            }, 500);
        };
        window.onorientationchange = function () {
            setTimeout(function () {
                window.scrollTo(0, 1);
            }, 100);
        };
        if (window.navigator.standalone) {
            // Fix min height for isMobileDevice when run in full screen mode from home screen
            // so the footer appears in the right place
            $('body div#torrent_container').css('min-height', '338px');
        };
        $("label[for=torrent_upload_url]").text("URL: ");
    } else {
        // Fix for non-Safari-3 browsers: dark borders to replace shadows.
        $('div.dialog_container div.dialog_window').css('border', '1px solid #777');
    };

    // Initialise the dialog controller
    dialog = new Dialog();

    // Initialise the main Transmission controller
    transmission = new Transmission();
});
