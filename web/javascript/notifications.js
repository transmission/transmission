'use strict';

const Notifications = {};

document.addEventListener('DOMContentLoaded', () => {
  if (!window.webkitNotifications) {
    return;
  }

  let notificationsEnabled = window.webkitNotifications.checkPermission() === 0;
  const toggle = $('#toggle_notifications');

  function updateMenuTitle() {
    toggle.html(`${notificationsEnabled ? 'Disable' : 'Enable'} Notifications`);
  }

  toggle.show();
  updateMenuTitle();
  $(transmission).bind('downloadComplete seedingComplete', (event, torrent) => {
    if (notificationsEnabled) {
      const title = `${event.type === 'downloadComplete' ? 'Download' : 'Seeding'} complete`;
      const content = torrent.getName();
      const notification = window.webkitNotifications.createNotification(
        'style/transmission/images/logo.png',
        title,
        content
      );
      notification.show();
      setTimeout(() => {
        notification.cancel();
      }, 5000);
    }
  });

  Notifications.toggle = function () {
    if (window.webkitNotifications.checkPermission() !== 0) {
      window.webkitNotifications.requestPermission(() => {
        notificationsEnabled = window.webkitNotifications.checkPermission() === 0;
        updateMenuTitle();
      });
    } else {
      notificationsEnabled = !notificationsEnabled;
      updateMenuTitle();
    }
  };
});
