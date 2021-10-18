/*
 * This file Copyright (C) 2008-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <gio/gio.h>

#include <glib/gi18n.h>
#include "conf.h"
#include "notify.h"
#include "tr-prefs.h"
#include "util.h"

#include <libtransmission/rpcimpl.h>

#define NOTIFICATIONS_DBUS_NAME "org.freedesktop.Notifications"
#define NOTIFICATIONS_DBUS_CORE_OBJECT "/org/freedesktop/Notifications"
#define NOTIFICATIONS_DBUS_CORE_INTERFACE "org.freedesktop.Notifications"

static GDBusProxy* proxy = nullptr;
static GHashTable* active_notifications = nullptr;
static gboolean server_supports_actions = FALSE;

typedef struct TrNotification
{
    TrCore* core;
    int torrent_id;
} TrNotification;

static void tr_notification_free(gpointer data)
{
    auto* n = static_cast<TrNotification*>(data);

    if (n->core != nullptr)
    {
        g_object_unref(G_OBJECT(n->core));
    }

    g_free(n);
}

static void torrent_start_now(tr_session* session, int id)
{
    tr_variant top;
    tr_variant* args;
    tr_variant* ids;

    tr_variantInitDict(&top, 2);
    tr_variantDictAddStr(&top, TR_KEY_method, "torrent-start-now");
    args = tr_variantDictAddDict(&top, TR_KEY_arguments, 1);
    ids = tr_variantDictAddList(args, TR_KEY_ids, 0);
    tr_variantListAddInt(ids, id);
    tr_rpc_request_exec_json(session, &top, NULL, NULL);
    tr_variantFree(&top);
}

static void get_capabilities_callback(GObject* source, GAsyncResult* res, gpointer user_data)
{
    TR_UNUSED(user_data);

    char** caps;
    GVariant* result;

    result = g_dbus_proxy_call_finish(G_DBUS_PROXY(source), res, nullptr);

    if (result == nullptr || !g_variant_is_of_type(result, G_VARIANT_TYPE("(as)")))
    {
        if (result != nullptr)
        {
            g_variant_unref(result);
        }

        return;
    }

    g_variant_get(result, "(^a&s)", &caps);

    for (int i = 0; caps[i] != nullptr; i++)
    {
        if (g_strcmp0(caps[i], "actions") == 0)
        {
            server_supports_actions = TRUE;
            break;
        }
    }

    g_free(caps);
    g_variant_unref(result);
}

static void g_signal_callback(
    GDBusProxy const* dbus_proxy,
    char const* sender_name,
    char const* signal_name,
    GVariant* params,
    gconstpointer user_data)
{
    TR_UNUSED(dbus_proxy);
    TR_UNUSED(sender_name);
    TR_UNUSED(user_data);

    g_return_if_fail(g_variant_is_of_type(params, G_VARIANT_TYPE("(u*)")));

    guint id;
    g_variant_get(params, "(u*)", &id, nullptr);
    auto* n = static_cast<TrNotification*>(g_hash_table_lookup(active_notifications, GUINT_TO_POINTER(id)));

    if (n == nullptr)
    {
        return;
    }

    if (g_strcmp0(signal_name, "NotificationClosed") == 0)
    {
        g_hash_table_remove(active_notifications, GUINT_TO_POINTER(id));
    }
    else if (g_strcmp0(signal_name, "ActionInvoked") == 0 && g_variant_is_of_type(params, G_VARIANT_TYPE("(us)")))
    {
        tr_torrent const* tor = gtr_core_find_torrent(n->core, n->torrent_id);
        if (tor == nullptr)
        {
            return;
        }

        char* action = nullptr;
        g_variant_get(params, "(u&s)", nullptr, &action);

        if (g_strcmp0(action, "folder") == 0)
        {
            gtr_core_open_folder(n->core, n->torrent_id);
        }
        else if (g_strcmp0(action, "file") == 0)
        {
            tr_info const* inf = tr_torrentInfo(tor);
            char const* dir = tr_torrentGetDownloadDir(tor);
            char* path = g_build_filename(dir, inf->files[0].name, nullptr);
            gtr_open_file(path);
            g_free(path);
        }
        else if (g_strcmp0(action, "start-now") == 0)
        {
            torrent_start_now(gtr_core_session(n->core), tr_torrentId(tor));
        }
    }
}

static void dbus_proxy_ready_callback(GObject* source, GAsyncResult* res, gpointer user_data)
{
    TR_UNUSED(source);
    TR_UNUSED(user_data);

    proxy = g_dbus_proxy_new_for_bus_finish(res, nullptr);

    if (proxy == nullptr)
    {
        g_warning("Failed to create proxy for %s", NOTIFICATIONS_DBUS_NAME);
        return;
    }

    g_signal_connect(proxy, "g-signal", G_CALLBACK(g_signal_callback), nullptr);
    g_dbus_proxy_call(
        proxy,
        "GetCapabilities",
        g_variant_new("()"),
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        nullptr,
        get_capabilities_callback,
        nullptr);
}

void gtr_notify_init(void)
{
    active_notifications = g_hash_table_new_full(g_direct_hash, g_direct_equal, nullptr, tr_notification_free);
    g_dbus_proxy_new_for_bus(
        G_BUS_TYPE_SESSION,
        G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
        nullptr,
        NOTIFICATIONS_DBUS_NAME,
        NOTIFICATIONS_DBUS_CORE_OBJECT,
        NOTIFICATIONS_DBUS_CORE_INTERFACE,
        nullptr,
        dbus_proxy_ready_callback,
        nullptr);
}

static void notify_callback(GObject* source, GAsyncResult* res, gpointer user_data)
{
    GVariant* result;
    auto* n = static_cast<TrNotification*>(user_data);

    result = g_dbus_proxy_call_finish(G_DBUS_PROXY(source), res, nullptr);

    if (result == nullptr || !g_variant_is_of_type(result, G_VARIANT_TYPE("(u)")))
    {
        if (result != nullptr)
        {
            g_variant_unref(result);
        }

        tr_notification_free(n);
        return;
    }

    guint id;
    g_variant_get(result, "(u)", &id);
    g_hash_table_insert(active_notifications, GUINT_TO_POINTER(id), n);

    g_variant_unref(result);
}

void gtr_notify_torrent_completed(TrCore* core, int torrent_id)
{
    if (gtr_pref_flag_get(TR_KEY_torrent_complete_sound_enabled))
    {
        char** argv = gtr_pref_strv_get(TR_KEY_torrent_complete_sound_command);
        g_spawn_async(
            nullptr /*cwd*/,
            argv,
            nullptr /*envp*/,
            G_SPAWN_SEARCH_PATH,
            nullptr /*GSpawnChildSetupFunc*/,
            nullptr /*user_data*/,
            nullptr /*child_pid*/,
            nullptr);
        g_strfreev(argv);
    }

    if (!gtr_pref_flag_get(TR_KEY_torrent_complete_notification_enabled))
    {
        return;
    }

    g_return_if_fail(G_IS_DBUS_PROXY(proxy));

    tr_torrent const* const tor = gtr_core_find_torrent(core, torrent_id);

    TrNotification* const n = g_new0(TrNotification, 1);
    g_object_ref(G_OBJECT(core));
    n->core = core;
    n->torrent_id = torrent_id;

    GVariantBuilder actions_builder;
    g_variant_builder_init(&actions_builder, G_VARIANT_TYPE("as"));
    if (server_supports_actions)
    {
        tr_info const* inf = tr_torrentInfo(tor);

        if (inf->fileCount == 1)
        {
            g_variant_builder_add(&actions_builder, "s", "file");
            g_variant_builder_add(&actions_builder, "s", _("Open File"));
        }
        else
        {
            g_variant_builder_add(&actions_builder, "s", "folder");
            g_variant_builder_add(&actions_builder, "s", _("Open Folder"));
        }
    }

    GVariantBuilder hints_builder;
    g_variant_builder_init(&hints_builder, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&hints_builder, "{sv}", "category", g_variant_new_string("transfer.complete"));

    g_dbus_proxy_call(
        proxy,
        "Notify",
        g_variant_new(
            "(susssasa{sv}i)",
            "Transmission",
            0,
            "transmission",
            _("Torrent Complete"),
            tr_torrentName(tor),
            &actions_builder,
            &hints_builder,
            -1),
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        nullptr,
        notify_callback,
        n);
}

void gtr_notify_torrent_added(TrCore* core, tr_torrent const* tor)
{
    g_return_if_fail(G_IS_DBUS_PROXY(proxy));

    if (!gtr_pref_flag_get(TR_KEY_torrent_added_notification_enabled))
    {
        return;
    }

    GVariantBuilder actions_builder;
    g_variant_builder_init(&actions_builder, G_VARIANT_TYPE("as"));
    g_variant_builder_add(&actions_builder, "s", "start-now");
    g_variant_builder_add(&actions_builder, "s", _("Start Now"));

    TrNotification* const n = g_new0(TrNotification, 1);
    g_object_ref(G_OBJECT(core));
    n->core = core;
    n->torrent_id = tr_torrentId(tor);
    g_dbus_proxy_call(
        proxy,
        "Notify",
        g_variant_new(
            "(susssasa{sv}i)",
            "Transmission",
            0,
            "transmission",
            _("Torrent Added"),
            tr_torrentName(tor),
            &actions_builder,
            nullptr,
            -1),
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        nullptr,
        notify_callback,
        n);
}
