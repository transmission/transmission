var Notifications = {};

$(document).ready(function () {
    if (!window.webkitNotifications) {
        return;
    };

    var notificationsEnabled = (window.webkitNotifications.checkPermission() === 0)
    var toggle = $('#toggle_notifications');

    toggle.show();
    updateMenuTitle();
    $(transmission).bind('downloadComplete seedingComplete', function (event, torrent) {
        if (notificationsEnabled) {
            var title = (event.type == 'downloadComplete' ? 'Download' : 'Seeding') + ' complete',
                content = torrent.getName(),
                notification;

            notification = window.webkitNotifications.createNotification('style/transmission/images/logo.png', title, content);
            notification.show();
            setTimeout(function () {
                notification.cancel();
            }, 5000);
        };
    });

    function updateMenuTitle() {
        toggle.html((notificationsEnabled ? 'Disable' : 'Enable') + ' Notifications');
    };

    Notifications.toggle = function () {
        if (window.webkitNotifications.checkPermission() !== 0) {
            window.webkitNotifications.requestPermission(function () {
                notificationsEnabled = (window.webkitNotifications.checkPermission() === 0);
                updateMenuTitle();
            });
        } else {
            notificationsEnabled = !notificationsEnabled;
            updateMenuTitle();
        };
    };
});
