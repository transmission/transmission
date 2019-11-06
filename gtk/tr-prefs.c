/*
 * This file Copyright (C) 2007-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <ctype.h> /* isspace */
#include <limits.h> /* USHRT_MAX, INT_MAX */
#include <unistd.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <libtransmission/transmission.h>
#include <libtransmission/utils.h>
#include <libtransmission/version.h>
#include "conf.h"
#include "hig.h"
#include "tr-core.h"
#include "tr-prefs.h"
#include "util.h"

/**
***
**/

struct prefs_dialog_data
{
    TrCore* core;
    gulong core_prefs_tag;

    GtkWidget* freespace_label;

    GtkWidget* port_label;
    GtkWidget* port_button;
    GtkWidget* port_spin;
};

/**
***
**/

#define PREF_KEY "pref-key"

static void response_cb(GtkDialog* dialog, int response, gpointer unused UNUSED)
{
    if (response == GTK_RESPONSE_HELP)
    {
        char* uri = g_strconcat(gtr_get_help_uri(), "/html/preferences.html", NULL);
        gtr_open_uri(uri);
        g_free(uri);
    }

    if (response == GTK_RESPONSE_CLOSE)
    {
        gtk_widget_destroy(GTK_WIDGET(dialog));
    }
}

static void toggled_cb(GtkToggleButton* w, gpointer core)
{
    tr_quark const key = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), PREF_KEY));
    gboolean const flag = gtk_toggle_button_get_active(w);

    gtr_core_set_pref_bool(TR_CORE(core), key, flag);
}

static GtkWidget* new_check_button(char const* mnemonic, tr_quark const key, gpointer core)
{
    GtkWidget* w = gtk_check_button_new_with_mnemonic(mnemonic);
    g_object_set_data(G_OBJECT(w), PREF_KEY, GINT_TO_POINTER(key));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), gtr_pref_flag_get(key));
    g_signal_connect(w, "toggled", G_CALLBACK(toggled_cb), core);
    return w;
}

#define IDLE_DATA "idle-data"

struct spin_idle_data
{
    gpointer core;
    GTimer* last_change;
    gboolean isDouble;
};

static void spin_idle_data_free(gpointer gdata)
{
    struct spin_idle_data* data = gdata;

    g_timer_destroy(data->last_change);
    g_free(data);
}

static gboolean spun_cb_idle(gpointer spin)
{
    gboolean keep_waiting = TRUE;
    GObject* o = G_OBJECT(spin);
    struct spin_idle_data* data = g_object_get_data(o, IDLE_DATA);

    /* has the user stopped making changes? */
    if (g_timer_elapsed(data->last_change, NULL) > 0.33F)
    {
        /* update the core */
        tr_quark const key = GPOINTER_TO_INT(g_object_get_data(o, PREF_KEY));

        if (data->isDouble)
        {
            double const value = gtk_spin_button_get_value(GTK_SPIN_BUTTON(spin));
            gtr_core_set_pref_double(TR_CORE(data->core), key, value);
        }
        else
        {
            int const value = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin));
            gtr_core_set_pref_int(TR_CORE(data->core), key, value);
        }

        /* cleanup */
        g_object_set_data(o, IDLE_DATA, NULL);
        keep_waiting = FALSE;
        g_object_unref(G_OBJECT(o));
    }

    return keep_waiting;
}

static void spun_cb(GtkSpinButton* w, gpointer core, gboolean isDouble)
{
    /* user may be spinning through many values, so let's hold off
       for a moment to keep from flooding the core with changes */
    GObject* o = G_OBJECT(w);
    struct spin_idle_data* data = g_object_get_data(o, IDLE_DATA);

    if (data == NULL)
    {
        data = g_new(struct spin_idle_data, 1);
        data->core = core;
        data->last_change = g_timer_new();
        data->isDouble = isDouble;
        g_object_set_data_full(o, IDLE_DATA, data, spin_idle_data_free);
        g_object_ref(G_OBJECT(o));
        gdk_threads_add_timeout_seconds(1, spun_cb_idle, w);
    }

    g_timer_start(data->last_change);
}

static void spun_cb_int(GtkSpinButton* w, gpointer core)
{
    spun_cb(w, core, FALSE);
}

static void spun_cb_double(GtkSpinButton* w, gpointer core)
{
    spun_cb(w, core, TRUE);
}

static GtkWidget* new_spin_button(tr_quark const key, gpointer core, int low, int high, int step)
{
    GtkWidget* w = gtk_spin_button_new_with_range(low, high, step);
    g_object_set_data(G_OBJECT(w), PREF_KEY, GINT_TO_POINTER(key));
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(w), 0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(w), gtr_pref_int_get(key));
    g_signal_connect(w, "value-changed", G_CALLBACK(spun_cb_int), core);
    return w;
}

static GtkWidget* new_spin_button_double(tr_quark const key, gpointer core, double low, double high, double step)
{
    GtkWidget* w = gtk_spin_button_new_with_range(low, high, step);
    g_object_set_data(G_OBJECT(w), PREF_KEY, GINT_TO_POINTER(key));
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(w), 2);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(w), gtr_pref_double_get(key));
    g_signal_connect(w, "value-changed", G_CALLBACK(spun_cb_double), core);
    return w;
}

static void entry_changed_cb(GtkEntry* w, gpointer core)
{
    tr_quark const key = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), PREF_KEY));
    char const* value = gtk_entry_get_text(w);

    gtr_core_set_pref(TR_CORE(core), key, value);
}

static GtkWidget* new_entry(tr_quark const key, gpointer core)
{
    GtkWidget* w = gtk_entry_new();
    char const* value = gtr_pref_string_get(key);

    if (value != NULL)
    {
        gtk_entry_set_text(GTK_ENTRY(w), value);
    }

    g_object_set_data(G_OBJECT(w), PREF_KEY, GINT_TO_POINTER(key));
    g_signal_connect(w, "changed", G_CALLBACK(entry_changed_cb), core);
    return w;
}

static void chosen_cb(GtkFileChooser* w, gpointer core)
{
    tr_quark const key = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), PREF_KEY));
    char* value = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(w));
    gtr_core_set_pref(TR_CORE(core), key, value);
    g_free(value);
}

static GtkWidget* new_path_chooser_button(tr_quark const key, gpointer core)
{
    GtkWidget* w = gtk_file_chooser_button_new(NULL, GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);
    char const* path = gtr_pref_string_get(key);
    g_object_set_data(G_OBJECT(w), PREF_KEY, GINT_TO_POINTER(key));

    if (path != NULL)
    {
        gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(w), path);
    }

    g_signal_connect(w, "selection-changed", G_CALLBACK(chosen_cb), core);
    return w;
}

static GtkWidget* new_file_chooser_button(tr_quark const key, gpointer core)
{
    GtkWidget* w = gtk_file_chooser_button_new(NULL, GTK_FILE_CHOOSER_ACTION_OPEN);
    char const* path = gtr_pref_string_get(key);
    g_object_set_data(G_OBJECT(w), PREF_KEY, GINT_TO_POINTER(key));

    if (path != NULL)
    {
        gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(w), path);
    }

    g_signal_connect(w, "selection-changed", G_CALLBACK(chosen_cb), core);
    return w;
}

static void target_cb(GtkWidget* tb, gpointer target)
{
    gboolean const b = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(tb));

    gtk_widget_set_sensitive(GTK_WIDGET(target), b);
}

/****
*****  Download Tab
****/

static GtkWidget* downloadingPage(GObject* core, struct prefs_dialog_data* data)
{
    GtkWidget* t;
    GtkWidget* w;
    GtkWidget* l;
    char const* s;
    guint row = 0;

    t = hig_workarea_create();
    hig_workarea_add_section_title(t, &row, C_("Gerund", "Adding"));

    s = _("Automatically add .torrent files _from:");
    l = new_check_button(s, TR_KEY_watch_dir_enabled, core);
    w = new_path_chooser_button(TR_KEY_watch_dir, core);
    gtk_widget_set_sensitive(GTK_WIDGET(w), gtr_pref_flag_get(TR_KEY_watch_dir_enabled));
    g_signal_connect(l, "toggled", G_CALLBACK(target_cb), w);
    hig_workarea_add_row_w(t, &row, l, w, NULL);

    s = _("Show the Torrent Options _dialog");
    w = new_check_button(s, TR_KEY_show_options_window, core);
    hig_workarea_add_wide_control(t, &row, w);

    s = _("_Start added torrents");
    w = new_check_button(s, TR_KEY_start_added_torrents, core);
    hig_workarea_add_wide_control(t, &row, w);

    s = _("Mo_ve .torrent file to the trash");
    w = new_check_button(s, TR_KEY_trash_original_torrent_files, core);
    hig_workarea_add_wide_control(t, &row, w);

    w = new_path_chooser_button(TR_KEY_download_dir, core);
    hig_workarea_add_row(t, &row, _("Save to _Location:"), w, NULL);

    l = data->freespace_label = gtr_freespace_label_new(TR_CORE(core), NULL);
    g_object_set(l, "halign", GTK_ALIGN_END, "valign", GTK_ALIGN_CENTER, NULL);
    hig_workarea_add_wide_control(t, &row, l);

    hig_workarea_add_section_divider(t, &row);
    hig_workarea_add_section_title(t, &row, _("Download Queue"));

    s = _("Ma_ximum active downloads:");
    w = new_spin_button(TR_KEY_download_queue_size, core, 0, INT_MAX, 1);
    hig_workarea_add_row(t, &row, s, w, NULL);

    s = _("Downloads sharing data in the last _N minutes are active:");
    w = new_spin_button(TR_KEY_queue_stalled_minutes, core, 1, INT_MAX, 15);
    hig_workarea_add_row(t, &row, s, w, NULL);

    hig_workarea_add_section_divider(t, &row);
    hig_workarea_add_section_title(t, &row, _("Incomplete"));

    s = _("Append \"._part\" to incomplete files' names");
    w = new_check_button(s, TR_KEY_rename_partial_files, core);
    hig_workarea_add_wide_control(t, &row, w);

    s = _("Keep _incomplete torrents in:");
    l = new_check_button(s, TR_KEY_incomplete_dir_enabled, core);
    w = new_path_chooser_button(TR_KEY_incomplete_dir, core);
    gtk_widget_set_sensitive(GTK_WIDGET(w), gtr_pref_flag_get(TR_KEY_incomplete_dir_enabled));
    g_signal_connect(l, "toggled", G_CALLBACK(target_cb), w);
    hig_workarea_add_row_w(t, &row, l, w, NULL);

    s = _("Call scrip_t when torrent is completed:");
    l = new_check_button(s, TR_KEY_script_torrent_done_enabled, core);
    w = new_file_chooser_button(TR_KEY_script_torrent_done_filename, core);
    gtk_widget_set_sensitive(GTK_WIDGET(w), gtr_pref_flag_get(TR_KEY_script_torrent_done_enabled));
    g_signal_connect(l, "toggled", G_CALLBACK(target_cb), w);
    hig_workarea_add_row_w(t, &row, l, w, NULL);

    return t;
}

/****
*****  Torrent Tab
****/

static GtkWidget* seedingPage(GObject* core)
{
    GtkWidget* t;
    GtkWidget* w;
    GtkWidget* w2;
    char const* s;
    guint row = 0;

    t = hig_workarea_create();
    hig_workarea_add_section_title(t, &row, _("Limits"));

    s = _("Stop seeding at _ratio:");
    w = new_check_button(s, TR_KEY_ratio_limit_enabled, core);
    w2 = new_spin_button_double(TR_KEY_ratio_limit, core, 0, 1000, .05);
    gtk_widget_set_sensitive(GTK_WIDGET(w2), gtr_pref_flag_get(TR_KEY_ratio_limit_enabled));
    g_signal_connect(w, "toggled", G_CALLBACK(target_cb), w2);
    hig_workarea_add_row_w(t, &row, w, w2, NULL);

    s = _("Stop seeding if idle for _N minutes:");
    w = new_check_button(s, TR_KEY_idle_seeding_limit_enabled, core);
    w2 = new_spin_button(TR_KEY_idle_seeding_limit, core, 1, 40320, 5);
    gtk_widget_set_sensitive(GTK_WIDGET(w2), gtr_pref_flag_get(TR_KEY_idle_seeding_limit_enabled));
    g_signal_connect(w, "toggled", G_CALLBACK(target_cb), w2);
    hig_workarea_add_row_w(t, &row, w, w2, NULL);

    return t;
}

/****
*****  Desktop Tab
****/

static GtkWidget* desktopPage(GObject* core)
{
    GtkWidget* t;
    GtkWidget* w;
    char const* s;
    guint row = 0;

    t = hig_workarea_create();
    hig_workarea_add_section_title(t, &row, _("Desktop"));

    s = _("_Inhibit hibernation when torrents are active");
    w = new_check_button(s, TR_KEY_inhibit_desktop_hibernation, core);
    hig_workarea_add_wide_control(t, &row, w);

    s = _("Show Transmission icon in the _notification area");
    w = new_check_button(s, TR_KEY_show_notification_area_icon, core);
    hig_workarea_add_wide_control(t, &row, w);

    hig_workarea_add_section_divider(t, &row);
    hig_workarea_add_section_title(t, &row, _("Notification"));

    s = _("Show a notification when torrents are a_dded");
    w = new_check_button(s, TR_KEY_torrent_added_notification_enabled, core);
    hig_workarea_add_wide_control(t, &row, w);

    s = _("Show a notification when torrents _finish");
    w = new_check_button(s, TR_KEY_torrent_complete_notification_enabled, core);
    hig_workarea_add_wide_control(t, &row, w);

    s = _("Play a _sound when torrents finish");
    w = new_check_button(s, TR_KEY_torrent_complete_sound_enabled, core);
    hig_workarea_add_wide_control(t, &row, w);

    return t;
}

/****
*****  Peer Tab
****/

struct blocklist_data
{
    gulong updateBlocklistTag;
    GtkWidget* updateBlocklistButton;
    GtkWidget* updateBlocklistDialog;
    GtkWidget* label;
    GtkWidget* check;
    TrCore* core;
};

static void updateBlocklistText(GtkWidget* w, TrCore* core)
{
    char buf1[512];
    char buf2[512];
    int const n = tr_blocklistGetRuleCount(gtr_core_session(core));
    g_snprintf(buf1, sizeof(buf1), ngettext("Blocklist contains %'d rule", "Blocklist contains %'d rules", n), n);
    g_snprintf(buf2, sizeof(buf2), "<i>%s</i>", buf1);
    gtk_label_set_markup(GTK_LABEL(w), buf2);
}

/* prefs dialog is being destroyed, so stop listening to blocklist updates */
static void privacyPageDestroyed(gpointer gdata, GObject* dead UNUSED)
{
    struct blocklist_data* data = gdata;

    if (data->updateBlocklistTag > 0)
    {
        g_signal_handler_disconnect(data->core, data->updateBlocklistTag);
    }

    g_free(data);
}

/* user hit "close" in the blocklist-update dialog */
static void onBlocklistUpdateResponse(GtkDialog* dialog, gint response UNUSED, gpointer gdata)
{
    struct blocklist_data* data = gdata;
    gtk_widget_destroy(GTK_WIDGET(dialog));
    gtk_widget_set_sensitive(data->updateBlocklistButton, TRUE);
    data->updateBlocklistDialog = NULL;
    g_signal_handler_disconnect(data->core, data->updateBlocklistTag);
    data->updateBlocklistTag = 0;
}

/* core says the blocklist was updated */
static void onBlocklistUpdated(TrCore* core, int n, gpointer gdata)
{
    bool const success = n >= 0;
    int const count = n >= 0 ? n : tr_blocklistGetRuleCount(gtr_core_session(core));
    char const* s = ngettext("Blocklist has %'d rule.", "Blocklist has %'d rules.", count);
    struct blocklist_data* data = gdata;
    GtkMessageDialog* d = GTK_MESSAGE_DIALOG(data->updateBlocklistDialog);
    gtk_widget_set_sensitive(data->updateBlocklistButton, TRUE);
    gtk_message_dialog_set_markup(d, success ? _("<b>Update succeeded!</b>") : _("<b>Unable to update.</b>"));
    gtk_message_dialog_format_secondary_text(d, s, count);
    updateBlocklistText(data->label, core);
}

/* user pushed a button to update the blocklist */
static void onBlocklistUpdate(GtkButton* w, gpointer gdata)
{
    GtkWidget* d;
    struct blocklist_data* data = gdata;
    d = gtk_message_dialog_new(GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(w))), GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE, "%s", _("Update Blocklist"));
    gtk_widget_set_sensitive(data->updateBlocklistButton, FALSE);
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(d), "%s", _("Getting new blocklist…"));
    data->updateBlocklistDialog = d;
    g_signal_connect(d, "response", G_CALLBACK(onBlocklistUpdateResponse), data);
    gtk_widget_show(d);
    gtr_core_blocklist_update(data->core);
    data->updateBlocklistTag = g_signal_connect(data->core, "blocklist-updated", G_CALLBACK(onBlocklistUpdated), data);
}

static void on_blocklist_url_changed(GtkEditable* e, gpointer gbutton)
{
    gchar* url = gtk_editable_get_chars(e, 0, -1);
    gboolean const is_url_valid = tr_urlParse(url, TR_BAD_SIZE, NULL, NULL, NULL, NULL);
    gtk_widget_set_sensitive(GTK_WIDGET(gbutton), is_url_valid);
    g_free(url);
}

static void onIntComboChanged(GtkComboBox* combo_box, gpointer core)
{
    int const val = gtr_combo_box_get_active_enum(combo_box);
    tr_quark const key = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(combo_box), PREF_KEY));
    gtr_core_set_pref_int(TR_CORE(core), key, val);
}

static GtkWidget* new_encryption_combo(GObject* core, tr_quark const key)
{
    GtkWidget* w = gtr_combo_box_new_enum(_("Allow encryption"), TR_CLEAR_PREFERRED, _("Prefer encryption"),
        TR_ENCRYPTION_PREFERRED, _("Require encryption"), TR_ENCRYPTION_REQUIRED, NULL);
    gtr_combo_box_set_active_enum(GTK_COMBO_BOX(w), gtr_pref_int_get(key));
    g_object_set_data(G_OBJECT(w), PREF_KEY, GINT_TO_POINTER(key));
    g_signal_connect(w, "changed", G_CALLBACK(onIntComboChanged), core);
    return w;
}

static GtkWidget* privacyPage(GObject* core)
{
    char const* s;
    GtkWidget* t;
    GtkWidget* w;
    GtkWidget* b;
    GtkWidget* h;
    GtkWidget* e;
    struct blocklist_data* data;
    guint row = 0;

    data = g_new0(struct blocklist_data, 1);
    data->core = TR_CORE(core);

    t = hig_workarea_create();
    hig_workarea_add_section_title(t, &row, _("Privacy"));

    s = _("_Encryption mode:");
    w = new_encryption_combo(core, TR_KEY_encryption);
    hig_workarea_add_row(t, &row, s, w, NULL);

    hig_workarea_add_section_divider(t, &row);
    hig_workarea_add_section_title(t, &row, _("Blocklist"));

    b = new_check_button(_("Enable _blocklist:"), TR_KEY_blocklist_enabled, core);
    e = new_entry(TR_KEY_blocklist_url, core);
    gtk_widget_set_size_request(e, 300, -1);
    hig_workarea_add_row_w(t, &row, b, e, NULL);
    data->check = b;
    g_signal_connect(b, "toggled", G_CALLBACK(target_cb), e);
    target_cb(b, e);

    w = gtk_label_new("");
    g_object_set(w, "halign", GTK_ALIGN_START, "valign", GTK_ALIGN_CENTER, NULL);
    updateBlocklistText(w, TR_CORE(core));
    data->label = w;
    h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, GUI_PAD_BIG);
    gtk_box_pack_start(GTK_BOX(h), w, TRUE, TRUE, 0);
    b = data->updateBlocklistButton = gtk_button_new_with_mnemonic(_("_Update"));
    g_object_set_data(G_OBJECT(b), "session", gtr_core_session(TR_CORE(core)));
    g_signal_connect(b, "clicked", G_CALLBACK(onBlocklistUpdate), data);
    g_signal_connect(data->check, "toggled", G_CALLBACK(target_cb), b);
    target_cb(data->check, b);
    gtk_box_pack_start(GTK_BOX(h), b, FALSE, FALSE, 0);
    g_signal_connect(data->check, "toggled", G_CALLBACK(target_cb), w);
    target_cb(data->check, w);
    hig_workarea_add_wide_control(t, &row, h);
    g_signal_connect(e, "changed", G_CALLBACK(on_blocklist_url_changed), data->updateBlocklistButton);
    on_blocklist_url_changed(GTK_EDITABLE(e), data->updateBlocklistButton);

    s = _("Enable _automatic updates");
    w = new_check_button(s, TR_KEY_blocklist_updates_enabled, core);
    hig_workarea_add_wide_control(t, &row, w);
    g_signal_connect(data->check, "toggled", G_CALLBACK(target_cb), w);
    target_cb(data->check, w);

    g_object_weak_ref(G_OBJECT(t), privacyPageDestroyed, data);
    return t;
}

/****
*****  Remote Tab
****/

enum
{
    COL_ADDRESS,
    N_COLS
};

static GtkTreeModel* whitelist_tree_model_new(char const* whitelist)
{
    char** rules;
    GtkListStore* store = gtk_list_store_new(N_COLS,
        G_TYPE_STRING,
        G_TYPE_STRING);

    rules = g_strsplit(whitelist, ",", 0);

    for (int i = 0; rules != NULL && rules[i] != NULL; ++i)
    {
        GtkTreeIter iter;
        char const* s = rules[i];

        while (isspace(*s))
        {
            ++s;
        }

        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter, COL_ADDRESS, s, -1);
    }

    g_strfreev(rules);
    return GTK_TREE_MODEL(store);
}

struct remote_page
{
    TrCore* core;
    GtkTreeView* view;
    GtkListStore* store;
    GtkWidget* remove_button;
    GSList* widgets;
    GSList* auth_widgets;
    GSList* whitelist_widgets;
    GtkToggleButton* rpc_tb;
    GtkToggleButton* auth_tb;
    GtkToggleButton* whitelist_tb;
};

static void refreshWhitelist(struct remote_page* page)
{
    GtkTreeIter iter;
    GString* gstr = g_string_new(NULL);
    GtkTreeModel* model = GTK_TREE_MODEL(page->store);

    if (gtk_tree_model_iter_nth_child(model, &iter, NULL, 0))
    {
        do
        {
            char* address;
            gtk_tree_model_get(model, &iter, COL_ADDRESS, &address, -1);
            g_string_append(gstr, address);
            g_string_append(gstr, ",");
            g_free(address);
        }
        while (gtk_tree_model_iter_next(model, &iter));
    }

    g_string_truncate(gstr, gstr->len - 1); /* remove the trailing comma */

    gtr_core_set_pref(page->core, TR_KEY_rpc_whitelist, gstr->str);

    g_string_free(gstr, TRUE);
}

static void onAddressEdited(GtkCellRendererText* r UNUSED, gchar* path_string, gchar* address, gpointer gpage)
{
    GtkTreeIter iter;
    struct remote_page* page = gpage;
    GtkTreeModel* model = GTK_TREE_MODEL(page->store);
    GtkTreePath* path = gtk_tree_path_new_from_string(path_string);

    if (gtk_tree_model_get_iter(model, &iter, path))
    {
        gtk_list_store_set(page->store, &iter, COL_ADDRESS, address, -1);
    }

    gtk_tree_path_free(path);
    refreshWhitelist(page);
}

static void onAddWhitelistClicked(GtkButton* b UNUSED, gpointer gpage)
{
    GtkTreeIter iter;
    GtkTreePath* path;
    struct remote_page* page = gpage;

    gtk_list_store_append(page->store, &iter);
    gtk_list_store_set(page->store, &iter, COL_ADDRESS, "0.0.0.0", -1);

    path = gtk_tree_model_get_path(GTK_TREE_MODEL(page->store), &iter);
    gtk_tree_view_set_cursor(page->view, path, gtk_tree_view_get_column(page->view, COL_ADDRESS), TRUE);
    gtk_tree_path_free(path);
}

static void onRemoveWhitelistClicked(GtkButton* b UNUSED, gpointer gpage)
{
    struct remote_page* page = gpage;
    GtkTreeSelection* sel = gtk_tree_view_get_selection(page->view);
    GtkTreeIter iter;

    if (gtk_tree_selection_get_selected(sel, NULL, &iter))
    {
        gtk_list_store_remove(page->store, &iter);
        refreshWhitelist(page);
    }
}

static void refreshRPCSensitivity(struct remote_page* page)
{
    int const rpc_active = gtk_toggle_button_get_active(page->rpc_tb);
    int const auth_active = gtk_toggle_button_get_active(page->auth_tb);
    int const whitelist_active = gtk_toggle_button_get_active(page->whitelist_tb);
    GtkTreeSelection* sel = gtk_tree_view_get_selection(page->view);
    int const have_addr = gtk_tree_selection_get_selected(sel, NULL, NULL);
    int const n_rules = gtk_tree_model_iter_n_children(GTK_TREE_MODEL(page->store), NULL);

    for (GSList* l = page->widgets; l != NULL; l = l->next)
    {
        gtk_widget_set_sensitive(GTK_WIDGET(l->data), rpc_active);
    }

    for (GSList* l = page->auth_widgets; l != NULL; l = l->next)
    {
        gtk_widget_set_sensitive(GTK_WIDGET(l->data), rpc_active && auth_active);
    }

    for (GSList* l = page->whitelist_widgets; l != NULL; l = l->next)
    {
        gtk_widget_set_sensitive(GTK_WIDGET(l->data), rpc_active && whitelist_active);
    }

    gtk_widget_set_sensitive(page->remove_button, rpc_active && have_addr && n_rules > 1);
}

static void onRPCToggled(GtkToggleButton* tb UNUSED, gpointer page)
{
    refreshRPCSensitivity(page);
}

static void onWhitelistSelectionChanged(GtkTreeSelection* sel UNUSED, gpointer page)
{
    refreshRPCSensitivity(page);
}

static void onLaunchClutchCB(GtkButton* w UNUSED, gpointer data UNUSED)
{
    char* uri;
    int const port = gtr_pref_int_get(TR_KEY_rpc_port);

    uri = g_strdup_printf("http://localhost:%d/", port);
    gtr_open_uri(uri);
    g_free(uri);
}

static void remotePageFree(gpointer gpage)
{
    struct remote_page* page = gpage;

    g_slist_free(page->widgets);
    g_slist_free(page->auth_widgets);
    g_slist_free(page->whitelist_widgets);
    g_free(page);
}

static GtkWidget* remotePage(GObject* core)
{
    GtkWidget* t;
    GtkWidget* w;
    GtkWidget* h;
    char const* s;
    guint row = 0;
    struct remote_page* page = g_new0(struct remote_page, 1);

    page->core = TR_CORE(core);

    t = hig_workarea_create();
    g_object_set_data_full(G_OBJECT(t), "page", page, remotePageFree);

    hig_workarea_add_section_title(t, &row, _("Remote Control"));

    /* "enabled" checkbutton */
    s = _("Allow _remote access");
    w = new_check_button(s, TR_KEY_rpc_enabled, core);
    page->rpc_tb = GTK_TOGGLE_BUTTON(w);
    g_signal_connect(w, "clicked", G_CALLBACK(onRPCToggled), page);
    h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, GUI_PAD_BIG);
    gtk_box_pack_start(GTK_BOX(h), w, TRUE, TRUE, 0);
    w = gtk_button_new_with_mnemonic(_("_Open web client"));
    page->widgets = g_slist_prepend(page->widgets, w);
    g_signal_connect(w, "clicked", G_CALLBACK(onLaunchClutchCB), NULL);
    gtk_box_pack_start(GTK_BOX(h), w, FALSE, FALSE, 0);
    hig_workarea_add_wide_control(t, &row, h);

    /* port */
    w = new_spin_button(TR_KEY_rpc_port, core, 0, USHRT_MAX, 1);
    page->widgets = g_slist_prepend(page->widgets, w);
    w = hig_workarea_add_row(t, &row, _("HTTP _port:"), w, NULL);
    page->widgets = g_slist_prepend(page->widgets, w);

    /* require authentication */
    s = _("Use _authentication");
    w = new_check_button(s, TR_KEY_rpc_authentication_required, core);
    hig_workarea_add_wide_control(t, &row, w);
    page->auth_tb = GTK_TOGGLE_BUTTON(w);
    page->widgets = g_slist_prepend(page->widgets, w);
    g_signal_connect(w, "clicked", G_CALLBACK(onRPCToggled), page);

    /* username */
    s = _("_Username:");
    w = new_entry(TR_KEY_rpc_username, core);
    page->auth_widgets = g_slist_prepend(page->auth_widgets, w);
    w = hig_workarea_add_row(t, &row, s, w, NULL);
    page->auth_widgets = g_slist_prepend(page->auth_widgets, w);

    /* password */
    s = _("Pass_word:");
    w = new_entry(TR_KEY_rpc_password, core);
    gtk_entry_set_visibility(GTK_ENTRY(w), FALSE);
    page->auth_widgets = g_slist_prepend(page->auth_widgets, w);
    w = hig_workarea_add_row(t, &row, s, w, NULL);
    page->auth_widgets = g_slist_prepend(page->auth_widgets, w);

    /* require authentication */
    s = _("Only allow these IP a_ddresses:");
    w = new_check_button(s, TR_KEY_rpc_whitelist_enabled, core);
    hig_workarea_add_wide_control(t, &row, w);
    page->whitelist_tb = GTK_TOGGLE_BUTTON(w);
    page->widgets = g_slist_prepend(page->widgets, w);
    g_signal_connect(w, "clicked", G_CALLBACK(onRPCToggled), page);

    /* access control list */
    {
        char const* val = gtr_pref_string_get(TR_KEY_rpc_whitelist);
        GtkTreeModel* m = whitelist_tree_model_new(val);
        GtkTreeViewColumn* c;
        GtkCellRenderer* r;
        GtkTreeSelection* sel;
        GtkTreeView* v;
        GtkWidget* w;
        GtkWidget* h;

        page->store = GTK_LIST_STORE(m);
        w = gtk_tree_view_new_with_model(m);
        g_signal_connect(w, "button-release-event", G_CALLBACK(on_tree_view_button_released), NULL);

        page->whitelist_widgets = g_slist_prepend(page->whitelist_widgets, w);
        v = page->view = GTK_TREE_VIEW(w);
        gtk_widget_set_tooltip_text(w, _("IP addresses may use wildcards, such as 192.168.*.*"));
        sel = gtk_tree_view_get_selection(v);
        g_signal_connect(sel, "changed",
            G_CALLBACK(onWhitelistSelectionChanged), page);
        g_object_unref(G_OBJECT(m));
        gtk_tree_view_set_headers_visible(v, TRUE);
        w = gtk_frame_new(NULL);
        gtk_frame_set_shadow_type(GTK_FRAME(w), GTK_SHADOW_IN);
        gtk_container_add(GTK_CONTAINER(w), GTK_WIDGET(v));

        /* ip address column */
        r = gtk_cell_renderer_text_new();
        g_signal_connect(r, "edited", G_CALLBACK(onAddressEdited), page);
        g_object_set(G_OBJECT(r), "editable", TRUE, NULL);
        c = gtk_tree_view_column_new_with_attributes(NULL, r, "text", COL_ADDRESS, NULL);
        gtk_tree_view_column_set_expand(c, TRUE);
        gtk_tree_view_append_column(v, c);
        gtk_tree_view_set_headers_visible(v, FALSE);

        s = _("Addresses:");
        w = hig_workarea_add_row(t, &row, s, w, NULL);
        g_object_set(w, "halign", GTK_ALIGN_START, "valign", GTK_ALIGN_START, "margin-top", GUI_PAD, "margin-bottom", GUI_PAD,
            NULL);
        page->whitelist_widgets = g_slist_prepend(page->whitelist_widgets, w);

        h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, GUI_PAD);
        w = gtk_button_new_from_stock(GTK_STOCK_REMOVE);
        g_signal_connect(w, "clicked", G_CALLBACK(onRemoveWhitelistClicked), page);
        page->remove_button = w;
        onWhitelistSelectionChanged(sel, page);
        gtk_box_pack_start(GTK_BOX(h), w, TRUE, TRUE, 0);
        w = gtk_button_new_from_stock(GTK_STOCK_ADD);
        page->whitelist_widgets = g_slist_prepend(page->whitelist_widgets, w);
        g_signal_connect(w, "clicked", G_CALLBACK(onAddWhitelistClicked), page);
        g_object_set(h, "halign", GTK_ALIGN_END, "valign", GTK_ALIGN_CENTER, NULL);
        gtk_box_pack_start(GTK_BOX(h), w, TRUE, TRUE, 0);
        hig_workarea_add_wide_control(t, &row, h);
    }

    refreshRPCSensitivity(page);
    return t;
}

/****
*****  Bandwidth Tab
****/

struct BandwidthPage
{
    TrCore* core;
    GSList* sched_widgets;
};

static void refreshSchedSensitivity(struct BandwidthPage* p)
{
    gboolean const sched_enabled = gtr_pref_flag_get(TR_KEY_alt_speed_time_enabled);

    for (GSList* l = p->sched_widgets; l != NULL; l = l->next)
    {
        gtk_widget_set_sensitive(GTK_WIDGET(l->data), sched_enabled);
    }
}

static void onSchedToggled(GtkToggleButton* tb UNUSED, gpointer user_data)
{
    refreshSchedSensitivity(user_data);
}

static void onTimeComboChanged(GtkComboBox* w, gpointer core)
{
    GtkTreeIter iter;

    if (gtk_combo_box_get_active_iter(w, &iter))
    {
        int val = 0;
        tr_quark const key = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), PREF_KEY));
        gtk_tree_model_get(gtk_combo_box_get_model(w), &iter, 0, &val, -1);
        gtr_core_set_pref_int(TR_CORE(core), key, val);
    }
}

static GtkWidget* new_time_combo(GObject* core, tr_quark const key)
{
    int val;
    GtkWidget* w;
    GtkCellRenderer* r;
    GtkListStore* store;

    /* build a store at 15 minute intervals */
    store = gtk_list_store_new(2, G_TYPE_INT, G_TYPE_STRING);

    for (int i = 0; i < 60 * 24; i += 15)
    {
        char buf[128];
        GtkTreeIter iter;
        g_snprintf(buf, sizeof(buf), "%02d:%02d", i / 60, i % 60);
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter, 0, i, 1, buf, -1);
    }

    /* build the widget */
    w = gtk_combo_box_new_with_model(GTK_TREE_MODEL(store));
    gtk_combo_box_set_wrap_width(GTK_COMBO_BOX(w), 4);
    r = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(w), r, TRUE);
    gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(w), r, "text", 1, NULL);
    g_object_set_data(G_OBJECT(w), PREF_KEY, GINT_TO_POINTER(key));
    val = gtr_pref_int_get(key);
    gtk_combo_box_set_active(GTK_COMBO_BOX(w), val / (15));
    g_signal_connect(w, "changed", G_CALLBACK(onTimeComboChanged), core);

    /* cleanup */
    g_object_unref(G_OBJECT(store));
    return w;
}

static GtkWidget* new_week_combo(GObject* core, tr_quark const key)
{
    GtkWidget* w = gtr_combo_box_new_enum(
        _("Every Day"), TR_SCHED_ALL,
        _("Weekdays"), TR_SCHED_WEEKDAY,
        _("Weekends"), TR_SCHED_WEEKEND,
        _("Sunday"), TR_SCHED_SUN,
        _("Monday"), TR_SCHED_MON,
        _("Tuesday"), TR_SCHED_TUES,
        _("Wednesday"), TR_SCHED_WED,
        _("Thursday"), TR_SCHED_THURS,
        _("Friday"), TR_SCHED_FRI,
        _("Saturday"), TR_SCHED_SAT,
        NULL);
    gtr_combo_box_set_active_enum(GTK_COMBO_BOX(w), gtr_pref_int_get(key));
    g_object_set_data(G_OBJECT(w), PREF_KEY, GINT_TO_POINTER(key));
    g_signal_connect(w, "changed", G_CALLBACK(onIntComboChanged), core);
    return w;
}

static void speedPageFree(gpointer gpage)
{
    struct BandwidthPage* page = gpage;

    g_slist_free(page->sched_widgets);
    g_free(page);
}

static GtkWidget* speedPage(GObject* core)
{
    char const* s;
    GtkWidget* t;
    GtkWidget* l;
    GtkWidget* w;
    GtkWidget* w2;
    GtkWidget* h;
    char buf[512];
    guint row = 0;
    struct BandwidthPage* page = tr_new0(struct BandwidthPage, 1);

    page->core = TR_CORE(core);

    t = hig_workarea_create();
    hig_workarea_add_section_title(t, &row, _("Speed Limits"));

    g_snprintf(buf, sizeof(buf), _("_Upload (%s):"), _(speed_K_str));
    w = new_check_button(buf, TR_KEY_speed_limit_up_enabled, core);
    w2 = new_spin_button(TR_KEY_speed_limit_up, core, 0, INT_MAX, 5);
    gtk_widget_set_sensitive(GTK_WIDGET(w2), gtr_pref_flag_get(TR_KEY_speed_limit_up_enabled));
    g_signal_connect(w, "toggled", G_CALLBACK(target_cb), w2);
    hig_workarea_add_row_w(t, &row, w, w2, NULL);

    g_snprintf(buf, sizeof(buf), _("_Download (%s):"), _(speed_K_str));
    w = new_check_button(buf, TR_KEY_speed_limit_down_enabled, core);
    w2 = new_spin_button(TR_KEY_speed_limit_down, core, 0, INT_MAX, 5);
    gtk_widget_set_sensitive(GTK_WIDGET(w2), gtr_pref_flag_get(TR_KEY_speed_limit_down_enabled));
    g_signal_connect(w, "toggled", G_CALLBACK(target_cb), w2);
    hig_workarea_add_row_w(t, &row, w, w2, NULL);

    hig_workarea_add_section_divider(t, &row);
    h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, GUI_PAD);
    g_snprintf(buf, sizeof(buf), "<b>%s</b>", _("Alternative Speed Limits"));
    w = gtk_label_new(buf);
    g_object_set(w, "halign", GTK_ALIGN_START, "valign", GTK_ALIGN_CENTER, NULL);
    gtk_label_set_use_markup(GTK_LABEL(w), TRUE);
    gtk_box_pack_start(GTK_BOX(h), w, FALSE, FALSE, 0);
    w = gtk_image_new_from_icon_name("alt-speed-on", GTK_ICON_SIZE_MENU);
    gtk_box_pack_start(GTK_BOX(h), w, FALSE, FALSE, 0);
    hig_workarea_add_section_title_widget(t, &row, h);

    s = _("Override normal speed limits manually or at scheduled times");
    g_snprintf(buf, sizeof(buf), "<small>%s</small>", s);
    w = gtk_label_new(buf);
    gtk_label_set_use_markup(GTK_LABEL(w), TRUE);
    g_object_set(w, "halign", GTK_ALIGN_START, "valign", GTK_ALIGN_CENTER, NULL);
    hig_workarea_add_wide_control(t, &row, w);

    g_snprintf(buf, sizeof(buf), _("U_pload (%s):"), _(speed_K_str));
    w = new_spin_button(TR_KEY_alt_speed_up, core, 0, INT_MAX, 5);
    hig_workarea_add_row(t, &row, buf, w, NULL);

    g_snprintf(buf, sizeof(buf), _("Do_wnload (%s):"), _(speed_K_str));
    w = new_spin_button(TR_KEY_alt_speed_down, core, 0, INT_MAX, 5);
    hig_workarea_add_row(t, &row, buf, w, NULL);

    s = _("_Scheduled times:");
    h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    w2 = new_time_combo(core, TR_KEY_alt_speed_time_begin);
    page->sched_widgets = g_slist_prepend(page->sched_widgets, w2);
    gtk_box_pack_start(GTK_BOX(h), w2, TRUE, TRUE, 0);
    w2 = l = gtk_label_new_with_mnemonic(_(" _to "));
    page->sched_widgets = g_slist_prepend(page->sched_widgets, w2);
    gtk_box_pack_start(GTK_BOX(h), w2, FALSE, FALSE, 0);
    w2 = new_time_combo(core, TR_KEY_alt_speed_time_end);
    gtk_label_set_mnemonic_widget(GTK_LABEL(l), w2);
    page->sched_widgets = g_slist_prepend(page->sched_widgets, w2);
    gtk_box_pack_start(GTK_BOX(h), w2, TRUE, TRUE, 0);
    w = new_check_button(s, TR_KEY_alt_speed_time_enabled, core);
    g_signal_connect(w, "toggled", G_CALLBACK(onSchedToggled), page);
    hig_workarea_add_row_w(t, &row, w, h, NULL);

    s = _("_On days:");
    w = new_week_combo(core, TR_KEY_alt_speed_time_day);
    page->sched_widgets = g_slist_prepend(page->sched_widgets, w);
    w = hig_workarea_add_row(t, &row, s, w, NULL);
    page->sched_widgets = g_slist_prepend(page->sched_widgets, w);

    g_object_set_data_full(G_OBJECT(t), "page", page, speedPageFree);

    refreshSchedSensitivity(page);
    return t;
}

/****
*****  Network Tab
****/

struct network_page_data
{
    TrCore* core;
    GtkWidget* portLabel;
    GtkWidget* portButton;
    GtkWidget* portSpin;
    gulong portTag;
    gulong prefsTag;
};

static void onCorePrefsChanged(TrCore* core UNUSED, tr_quark const key, gpointer gdata)
{
    if (key == TR_KEY_peer_port)
    {
        struct network_page_data* data = gdata;
        gtr_label_set_text(GTK_LABEL(data->portLabel), _("Status unknown"));
        gtk_widget_set_sensitive(data->portButton, TRUE);
        gtk_widget_set_sensitive(data->portSpin, TRUE);
    }
}

static void networkPageDestroyed(gpointer gdata, GObject* dead UNUSED)
{
    struct network_page_data* data = gdata;

    if (data->prefsTag > 0)
    {
        g_signal_handler_disconnect(data->core, data->prefsTag);
    }

    if (data->portTag > 0)
    {
        g_signal_handler_disconnect(data->core, data->portTag);
    }

    g_free(data);
}

static void onPortTested(TrCore* core UNUSED, gboolean isOpen, gpointer vdata)
{
    struct network_page_data* data = vdata;
    char const* markup = isOpen ? _("Port is <b>open</b>") : _("Port is <b>closed</b>");

    // gdk_threads_enter();
    gtk_label_set_markup(GTK_LABEL(data->portLabel), markup);
    gtk_widget_set_sensitive(data->portButton, TRUE);
    gtk_widget_set_sensitive(data->portSpin, TRUE);
    // gdk_threads_leave();
}

static void onPortTest(GtkButton* button UNUSED, gpointer vdata)
{
    struct network_page_data* data = vdata;
    gtk_widget_set_sensitive(data->portButton, FALSE);
    gtk_widget_set_sensitive(data->portSpin, FALSE);
    gtk_label_set_markup(GTK_LABEL(data->portLabel), _("<i>Testing TCP port…</i>"));

    if (data->portTag == 0)
    {
        data->portTag = g_signal_connect(data->core, "port-tested", G_CALLBACK(onPortTested), data);
    }

    gtr_core_port_test(data->core);
}

static GtkWidget* networkPage(GObject* core)
{
    GtkWidget* t;
    GtkWidget* w;
    GtkWidget* h;
    GtkWidget* l;
    char const* s;
    struct network_page_data* data;
    guint row = 0;

    /* register to stop listening to core prefs changes when the page is destroyed */
    data = g_new0(struct network_page_data, 1);
    data->core = TR_CORE(core);

    /* build the page */
    t = hig_workarea_create();
    hig_workarea_add_section_title(t, &row, _("Listening Port"));

    s = _("_Port used for incoming connections:");
    w = data->portSpin = new_spin_button(TR_KEY_peer_port, core, 1, USHRT_MAX, 1);
    hig_workarea_add_row(t, &row, s, w, NULL);

    h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, GUI_PAD_BIG);
    l = data->portLabel = gtk_label_new(_("Status unknown"));
    g_object_set(l, "halign", GTK_ALIGN_START, "valign", GTK_ALIGN_CENTER, NULL);
    gtk_box_pack_start(GTK_BOX(h), l, TRUE, TRUE, 0);
    w = data->portButton = gtk_button_new_with_mnemonic(_("Te_st Port"));
    gtk_box_pack_end(GTK_BOX(h), w, FALSE, FALSE, 0);
    g_signal_connect(w, "clicked", G_CALLBACK(onPortTest), data);
    hig_workarea_add_row(t, &row, NULL, h, NULL);
    data->prefsTag = g_signal_connect(TR_CORE(core), "prefs-changed", G_CALLBACK(onCorePrefsChanged), data);
    g_object_weak_ref(G_OBJECT(t), networkPageDestroyed, data);

    s = _("Pick a _random port every time Transmission is started");
    w = new_check_button(s, TR_KEY_peer_port_random_on_start, core);
    hig_workarea_add_wide_control(t, &row, w);

    s = _("Use UPnP or NAT-PMP port _forwarding from my router");
    w = new_check_button(s, TR_KEY_port_forwarding_enabled, core);
    hig_workarea_add_wide_control(t, &row, w);

    hig_workarea_add_section_divider(t, &row);
    hig_workarea_add_section_title(t, &row, _("Peer Limits"));

    w = new_spin_button(TR_KEY_peer_limit_per_torrent, core, 1, FD_SETSIZE, 5);
    hig_workarea_add_row(t, &row, _("Maximum peers per _torrent:"), w, NULL);
    w = new_spin_button(TR_KEY_peer_limit_global, core, 1, FD_SETSIZE, 5);
    hig_workarea_add_row(t, &row, _("Maximum peers _overall:"), w, NULL);

    hig_workarea_add_section_divider(t, &row);
    hig_workarea_add_section_title(t, &row, _("Options"));

#ifdef WITH_UTP
    s = _("Enable _uTP for peer communication");
    w = new_check_button(s, TR_KEY_utp_enabled, core);
    s = _("uTP is a tool for reducing network congestion.");
    gtk_widget_set_tooltip_text(w, s);
    hig_workarea_add_wide_control(t, &row, w);
#endif

    s = _("Use PE_X to find more peers");
    w = new_check_button(s, TR_KEY_pex_enabled, core);
    s = _("PEX is a tool for exchanging peer lists with the peers you're connected to.");
    gtk_widget_set_tooltip_text(w, s);
    hig_workarea_add_wide_control(t, &row, w);

    s = _("Use _DHT to find more peers");
    w = new_check_button(s, TR_KEY_dht_enabled, core);
    s = _("DHT is a tool for finding peers without a tracker.");
    gtk_widget_set_tooltip_text(w, s);
    hig_workarea_add_wide_control(t, &row, w);

    s = _("Use _Local Peer Discovery to find more peers");
    w = new_check_button(s, TR_KEY_lpd_enabled, core);
    s = _("LPD is a tool for finding peers on your local network.");
    gtk_widget_set_tooltip_text(w, s);
    hig_workarea_add_wide_control(t, &row, w);

    return t;
}

/****
*****
****/

static void on_prefs_dialog_destroyed(gpointer gdata, GObject* dead_dialog G_GNUC_UNUSED)
{
    struct prefs_dialog_data* data = gdata;

    if (data->core_prefs_tag > 0)
    {
        g_signal_handler_disconnect(data->core, data->core_prefs_tag);
    }

    g_free(data);
}

static void on_core_prefs_changed(TrCore* core, tr_quark const key, gpointer gdata)
{
    struct prefs_dialog_data* data = gdata;

#if 0

    if (key == TR_KEY_peer_port)
    {
        gtr_label_set_text(GTK_LABEL(data->port_label), _("Status unknown"));
        gtk_widget_set_sensitive(data->port_button, TRUE);
        gtk_widget_set_sensitive(data->port_spin, TRUE);
    }

#endif

    if (key == TR_KEY_download_dir)
    {
        char const* downloadDir = tr_sessionGetDownloadDir(gtr_core_session(core));
        gtr_freespace_label_set_dir(data->freespace_label, downloadDir);
    }
}

GtkWidget* gtr_prefs_dialog_new(GtkWindow* parent, GObject* core)
{
    GtkWidget* d;
    GtkWidget* n;
    struct prefs_dialog_data* data;
    tr_quark const prefs_quarks[] = { TR_KEY_peer_port, TR_KEY_download_dir };

    data = g_new0(struct prefs_dialog_data, 1);
    data->core = TR_CORE(core);
    data->core_prefs_tag = g_signal_connect(TR_CORE(core), "prefs-changed", G_CALLBACK(on_core_prefs_changed), data);

    d = gtk_dialog_new_with_buttons(_("Transmission Preferences"), parent, GTK_DIALOG_DESTROY_WITH_PARENT, GTK_STOCK_HELP,
        GTK_RESPONSE_HELP, GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE, NULL);
    g_object_weak_ref(G_OBJECT(d), on_prefs_dialog_destroyed, data);
    gtk_window_set_role(GTK_WINDOW(d), "transmission-preferences-dialog");
    gtk_container_set_border_width(GTK_CONTAINER(d), GUI_PAD);

    n = gtk_notebook_new();
    gtk_container_set_border_width(GTK_CONTAINER(n), GUI_PAD);

    gtk_notebook_append_page(GTK_NOTEBOOK(n), speedPage(core), gtk_label_new(_("Speed")));
    gtk_notebook_append_page(GTK_NOTEBOOK(n), downloadingPage(core, data), gtk_label_new(C_("Gerund", "Downloading")));
    gtk_notebook_append_page(GTK_NOTEBOOK(n), seedingPage(core), gtk_label_new(C_("Gerund", "Seeding")));
    gtk_notebook_append_page(GTK_NOTEBOOK(n), privacyPage(core), gtk_label_new(_("Privacy")));
    gtk_notebook_append_page(GTK_NOTEBOOK(n), networkPage(core), gtk_label_new(_("Network")));
    gtk_notebook_append_page(GTK_NOTEBOOK(n), desktopPage(core), gtk_label_new(_("Desktop")));
    gtk_notebook_append_page(GTK_NOTEBOOK(n), remotePage(core), gtk_label_new(_("Remote")));

    /* init from prefs keys */
    for (size_t i = 0; i < G_N_ELEMENTS(prefs_quarks); ++i)
    {
        on_core_prefs_changed(TR_CORE(core), prefs_quarks[i], data);
    }

    g_signal_connect(d, "response", G_CALLBACK(response_cb), core);
    gtr_dialog_set_content(GTK_DIALOG(d), n);
    return d;
}
