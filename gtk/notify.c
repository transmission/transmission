/*
 * This file Copyright (C) 2008-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <gio/gio.h>

#include <glib/gi18n.h>
#include "conf.h"
#include "notify.h"
#include "tr-prefs.h"
#include "util.h"

#define NOTIFICATIONS_DBUS_NAME           "org.freedesktop.Notifications"
#define NOTIFICATIONS_DBUS_CORE_OBJECT    "/org/freedesktop/Notifications"
#define NOTIFICATIONS_DBUS_CORE_INTERFACE "org.freedesktop.Notifications"

static GDBusProxy *proxy = NULL;
static GHashTable *active_notifications = NULL;
static gboolean server_supports_actions = FALSE;

typedef struct _TrNotification
{
  guint    id;
  TrCore * core;
  int      torrent_id;
}
TrNotification;

static void
tr_notification_free (gpointer data)
{
  TrNotification * n = data;

  if (n->core)
    g_object_unref (G_OBJECT (n->core));

  g_free (n);
}

static void
get_capabilities_callback (GObject      * source,
                           GAsyncResult * res,
                           gpointer       user_data UNUSED)
{
  int i;
  char ** caps;
  GVariant * result;

  result = g_dbus_proxy_call_finish (G_DBUS_PROXY (source), res, NULL);
  if (!result || !g_variant_is_of_type (result, G_VARIANT_TYPE ("(as)")))
    {
      if (result)
        g_variant_unref (result);
      return;
    }

  g_variant_get (result, "(^a&s)", &caps);
  for (i=0; caps[i]; i++)
    {
      if (g_strcmp0 (caps[i], "actions") == 0)
        {
          server_supports_actions = TRUE;
          break;
        }
    }

  g_free (caps);
  g_variant_unref (result);
}

static void
g_signal_callback (GDBusProxy * proxy UNUSED,
                   char       * sender_name UNUSED,
                   char       * signal_name,
                   GVariant   * params,
                   gpointer     user_data UNUSED)
{
  guint id;
  TrNotification * n;

  g_return_if_fail (g_variant_is_of_type (params, G_VARIANT_TYPE ("(u*)")));

  g_variant_get (params, "(u*)", &id, NULL);
  n = g_hash_table_lookup (active_notifications,
                           GINT_TO_POINTER ((int *) &id));
  if (n == NULL)
    return;

  if (g_strcmp0 (signal_name, "NotificationClosed") == 0)
    {
      g_hash_table_remove (active_notifications,
                           GINT_TO_POINTER ((int *) &n->id));
    }
  else if (g_strcmp0 (signal_name, "ActionInvoked") == 0 &&
           g_variant_is_of_type (params, G_VARIANT_TYPE ("(us)")))
    {
      char * action;
      tr_torrent * tor;

      tor = gtr_core_find_torrent (n->core, n->torrent_id);
      if (tor == NULL)
        return;

      g_variant_get (params, "(u&s)", NULL, &action);
      if (g_strcmp0 (action, "folder") == 0)
        {
          gtr_core_open_folder (n->core, n->torrent_id);
        }
      else if (g_strcmp0 (action, "file") == 0)
        {
          const tr_info * inf = tr_torrentInfo (tor);
          const char * dir = tr_torrentGetDownloadDir (tor);
          char * path = g_build_filename (dir, inf->files[0].name, NULL);
          gtr_open_file (path);
          g_free (path);
        }
    }
}

static void
dbus_proxy_ready_callback (GObject      * source UNUSED,
                           GAsyncResult * res,
                           gpointer       user_data UNUSED)
{
  proxy = g_dbus_proxy_new_for_bus_finish (res, NULL);
  if (proxy == NULL)
    {
      g_warning ("Failed to create proxy for %s", NOTIFICATIONS_DBUS_NAME);
      return;
    }

  g_signal_connect (proxy, "g-signal",
                    G_CALLBACK (g_signal_callback), NULL);
  g_dbus_proxy_call (proxy,
                     "GetCapabilities",
                     g_variant_new ("()"),
                     G_DBUS_CALL_FLAGS_NONE, -1, NULL,
                     get_capabilities_callback, NULL);
}

void
gtr_notify_init (void)
{
  active_notifications = g_hash_table_new_full (g_int_hash, g_int_equal,
                                                NULL, tr_notification_free);
  g_dbus_proxy_new_for_bus (G_BUS_TYPE_SESSION,
                            G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
                            NULL,
                            NOTIFICATIONS_DBUS_NAME,
                            NOTIFICATIONS_DBUS_CORE_OBJECT,
                            NOTIFICATIONS_DBUS_CORE_INTERFACE,
                            NULL, dbus_proxy_ready_callback, NULL);
}

static void
notify_callback (GObject      * source,
                 GAsyncResult * res,
                 gpointer       user_data)
{
  GVariant * result;
  TrNotification * n = user_data;

  result = g_dbus_proxy_call_finish (G_DBUS_PROXY (source), res, NULL);
  if (!result || !g_variant_is_of_type (result, G_VARIANT_TYPE ("(u)")))
    {
      if (result)
          g_variant_unref (result);
      tr_notification_free (n);
      return;
    }

  g_variant_get (result, "(u)", &n->id);
  g_hash_table_insert (active_notifications,
                       GINT_TO_POINTER ((int *)&n->id), n);

  g_variant_unref (result);
}

void
gtr_notify_torrent_completed (TrCore * core, int torrent_id)
{
  GVariantBuilder actions_builder;
  TrNotification * n;
  tr_torrent * tor;
  const char * cmd = gtr_pref_string_get (TR_KEY_torrent_complete_sound_command);

  if (gtr_pref_flag_get (TR_KEY_torrent_complete_sound_enabled))
    g_spawn_command_line_async (cmd, NULL);

  if (!gtr_pref_flag_get (TR_KEY_torrent_complete_notification_enabled))
      return;

  g_return_if_fail (G_IS_DBUS_PROXY (proxy));

  tor = gtr_core_find_torrent (core, torrent_id);

  n = g_new0 (TrNotification, 1);
  n->core = g_object_ref (G_OBJECT (core));
  n->torrent_id = torrent_id;

  g_variant_builder_init (&actions_builder, G_VARIANT_TYPE ("as"));

  if (server_supports_actions)
    {
      const tr_info * inf = tr_torrentInfo (tor);
      if (inf->fileCount == 1)
        {
          g_variant_builder_add (&actions_builder, "s", "file");
          g_variant_builder_add (&actions_builder, "s", _("Open File"));
        }
      else
        {
          g_variant_builder_add (&actions_builder, "s", "folder");
          g_variant_builder_add (&actions_builder, "s", _("Open Folder"));
        }
    }

  g_dbus_proxy_call (proxy,
                     "Notify",
                     g_variant_new ("(susssasa{sv}i)",
                                    "Transmission", n->id, "transmission",
                                    _("Torrent Complete"),
                                    tr_torrentName (tor),
                                    &actions_builder, NULL, -1),
                     G_DBUS_CALL_FLAGS_NONE, -1, NULL,
                     notify_callback, n);
}

void
gtr_notify_torrent_added (const char * name)
{
  TrNotification * n;

  g_return_if_fail (G_IS_DBUS_PROXY (proxy));

  if (!gtr_pref_flag_get (TR_KEY_torrent_added_notification_enabled))
    return;

  n = g_new0 (TrNotification, 1);
  g_dbus_proxy_call (proxy,
                     "Notify",
                     g_variant_new ("(susssasa{sv}i)",
                                    "Transmission", 0, "transmission",
                                    _("Torrent Added"), name,
                                    NULL, NULL, -1),
                     G_DBUS_CALL_FLAGS_NONE, -1, NULL,
                     notify_callback, n);
}
