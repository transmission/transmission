/*
 * This file Copyright (C) 2010-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include <string.h>

#include <libtransmission/transmission.h>
#include <libtransmission/file.h> /* tr_sys_path_is_same() */

#include "conf.h"
#include "file-list.h"
#include "hig.h"
#include "open-dialog.h"
#include "tr-prefs.h"
#include "util.h" /* gtr_priority_combo_get_value() */

/****
*****
****/

#define N_RECENT 4

static GSList* get_recent_destinations(void)
{
    GSList* list = NULL;

    for (int i = 0; i < N_RECENT; ++i)
    {
        char key[64];
        char const* val;
        g_snprintf(key, sizeof(key), "recent-download-dir-%d", i + 1);

        if ((val = gtr_pref_string_get(tr_quark_new(key, TR_BAD_SIZE))) != NULL)
        {
            list = g_slist_append(list, (void*)val);
        }
    }

    return list;
}

static void save_recent_destination(TrCore* core, char const* dir)
{
    int i;
    GSList* l;
    GSList* list = get_recent_destinations();

    if (dir == NULL)
    {
        return;
    }

    /* if it was already in the list, remove it */
    if ((l = g_slist_find_custom(list, dir, (GCompareFunc)g_strcmp0)) != NULL)
    {
        list = g_slist_delete_link(list, l);
    }

    /* add it to the front of the list */
    list = g_slist_prepend(list, (void*)dir);

    /* make local copies of the strings that aren't
     * invalidated by gtr_pref_string_set() */
    for (l = list; l != NULL; l = l->next)
    {
        l->data = g_strdup(l->data);
    }

    /* save the first N_RECENT directories */
    for (l = list, i = 0; l != NULL && i < N_RECENT; ++i, l = l->next)
    {
        char key[64];
        g_snprintf(key, sizeof(key), "recent-download-dir-%d", i + 1);
        gtr_pref_string_set(tr_quark_new(key, TR_BAD_SIZE), l->data);
    }

    gtr_pref_save(gtr_core_session(core));

    /* cleanup */
    g_slist_foreach(list, (GFunc)(GCallback)g_free, NULL);
    g_slist_free(list);
}

/****
*****
****/

struct OpenData
{
    TrCore* core;
    GtkWidget* file_list;
    GtkWidget* run_check;
    GtkWidget* trash_check;
    GtkWidget* priority_combo;
    GtkWidget* freespace_label;
    char* filename;
    char* downloadDir;
    tr_torrent* tor;
    tr_ctor* ctor;
};

static void removeOldTorrent(struct OpenData* o)
{
    if (o->tor != NULL)
    {
        gtr_file_list_clear(o->file_list);
        tr_torrentRemove(o->tor, FALSE, NULL);
        o->tor = NULL;
    }
}

static void addResponseCB(GtkDialog* dialog, gint response, gpointer gdata)
{
    struct OpenData* o = gdata;

    if (o->tor != NULL)
    {
        if (response != GTK_RESPONSE_ACCEPT)
        {
            removeOldTorrent(o);
        }
        else
        {
            tr_torrentSetPriority(o->tor, gtr_priority_combo_get_value(GTK_COMBO_BOX(o->priority_combo)));

            if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(o->run_check)))
            {
                tr_torrentStart(o->tor);
            }

            gtr_core_add_torrent(o->core, o->tor, FALSE);

            if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(o->trash_check)))
            {
                gtr_file_trash_or_remove(o->filename, NULL);
            }

            save_recent_destination(o->core, o->downloadDir);
        }
    }

    tr_ctorFree(o->ctor);
    g_free(o->filename);
    g_free(o->downloadDir);
    g_free(o);
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

static void updateTorrent(struct OpenData* o)
{
    gboolean const isLocalFile = tr_ctorGetSourceFile(o->ctor) != NULL;
    gtk_widget_set_sensitive(o->trash_check, isLocalFile);

    if (o->tor == NULL)
    {
        gtr_file_list_clear(o->file_list);
        gtk_widget_set_sensitive(o->file_list, FALSE);
    }
    else
    {
        tr_torrentSetDownloadDir(o->tor, o->downloadDir);
        gtk_widget_set_sensitive(o->file_list, tr_torrentHasMetadata(o->tor));
        gtr_file_list_set_torrent(o->file_list, tr_torrentId(o->tor));
        tr_torrentVerify(o->tor, NULL, NULL);
    }
}

/**
 * When the source .torrent file is deleted
 * (such as, if it was a temp file that a web browser passed to us),
 * gtk invokes this callback and `filename' will be NULL.
 * The `filename' tests here are to prevent us from losing the current
 * metadata when that happens.
 */
static void sourceChanged(GtkFileChooserButton* b, gpointer gdata)
{
    struct OpenData* o = gdata;
    char* filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(b));

    /* maybe instantiate a torrent */
    if (filename != NULL || o->tor == NULL)
    {
        int err = 0;
        bool new_file = false;
        int duplicate_id = 0;
        tr_torrent* torrent;

        if (filename != NULL && (o->filename == NULL || !tr_sys_path_is_same(filename, o->filename, NULL)))
        {
            g_free(o->filename);
            o->filename = g_strdup(filename);
            tr_ctorSetMetainfoFromFile(o->ctor, o->filename);
            new_file = true;
        }

        tr_ctorSetDownloadDir(o->ctor, TR_FORCE, o->downloadDir);
        tr_ctorSetPaused(o->ctor, TR_FORCE, TRUE);
        tr_ctorSetDeleteSource(o->ctor, FALSE);

        if ((torrent = tr_torrentNew(o->ctor, &err, &duplicate_id)) != NULL)
        {
            removeOldTorrent(o);
            o->tor = torrent;
        }
        else if (new_file)
        {
            tr_torrent* tor;

            if (duplicate_id != 0)
            {
                tor = gtr_core_find_torrent(o->core, duplicate_id);
            }
            else
            {
                tor = NULL;
            }

            gtr_add_torrent_error_dialog(GTK_WIDGET(b), err, tor, o->filename);
        }

        updateTorrent(o);
    }

    g_free(filename);
}

static void downloadDirChanged(GtkFileChooserButton* b, gpointer gdata)
{
    struct OpenData* data = gdata;
    char* fname = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(b));

    if (fname != NULL && (data->downloadDir == NULL || !tr_sys_path_is_same(fname, data->downloadDir, NULL)))
    {
        g_free(data->downloadDir);
        data->downloadDir = g_strdup(fname);
        updateTorrent(data);

        gtr_freespace_label_set_dir(data->freespace_label, data->downloadDir);
    }

    g_free(fname);
}

static void addTorrentFilters(GtkFileChooser* chooser)
{
    GtkFileFilter* filter;

    filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, _("Torrent files"));
    gtk_file_filter_add_pattern(filter, "*.torrent");
    gtk_file_chooser_add_filter(chooser, filter);

    filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, _("All files"));
    gtk_file_filter_add_pattern(filter, "*");
    gtk_file_chooser_add_filter(chooser, filter);
}

/****
*****
****/

GtkWidget* gtr_torrent_options_dialog_new(GtkWindow* parent, TrCore* core, tr_ctor* ctor)
{
    char const* str;
    GtkWidget* w;
    GtkWidget* d;
    GtkGrid* grid;
    int row;
    GtkWidget* l;
    GtkWidget* source_chooser;
    struct OpenData* data;
    bool flag;
    GSList* list;

    /* make the dialog */
    d = gtk_dialog_new_with_buttons(_("Torrent Options"), parent, GTK_DIALOG_DESTROY_WITH_PARENT, GTK_STOCK_CANCEL,
        GTK_RESPONSE_CANCEL, GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT, NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_ACCEPT);

    if (!tr_ctorGetDownloadDir(ctor, TR_FORCE, &str))
    {
        g_assert_not_reached();
    }

    g_assert(str);

    data = g_new0(struct OpenData, 1);
    data->core = core;
    data->ctor = ctor;
    data->filename = g_strdup(tr_ctorGetSourceFile(ctor));
    data->downloadDir = g_strdup(str);
    data->file_list = gtr_file_list_new(core, 0);
    str = _("Mo_ve .torrent file to the trash");
    data->trash_check = gtk_check_button_new_with_mnemonic(str);
    str = _("_Start when added");
    data->run_check = gtk_check_button_new_with_mnemonic(str);

    w = data->priority_combo = gtr_priority_combo_new();
    gtr_priority_combo_set_value(GTK_COMBO_BOX(w), TR_PRI_NORMAL);

    g_signal_connect(G_OBJECT(d), "response", G_CALLBACK(addResponseCB), data);

    row = 0;
    grid = GTK_GRID(gtk_grid_new());
    gtk_container_set_border_width(GTK_CONTAINER(grid), GUI_PAD_BIG);
    gtk_grid_set_row_spacing(grid, GUI_PAD);
    gtk_grid_set_column_spacing(grid, GUI_PAD_BIG);

    /* "torrent file" row */
    l = gtk_label_new_with_mnemonic(_("_Torrent file:"));
    g_object_set(l, "halign", GTK_ALIGN_START, "valign", GTK_ALIGN_CENTER, NULL);
    gtk_grid_attach(grid, l, 0, row, 1, 1);
    w = gtk_file_chooser_button_new(_("Select Source File"), GTK_FILE_CHOOSER_ACTION_OPEN);
    source_chooser = w;
    gtk_widget_set_hexpand(w, TRUE);
    gtk_grid_attach_next_to(grid, w, l, GTK_POS_RIGHT, 1, 1);
    gtk_label_set_mnemonic_widget(GTK_LABEL(l), w);
    addTorrentFilters(GTK_FILE_CHOOSER(w));
    g_signal_connect(w, "selection-changed", G_CALLBACK(sourceChanged), data);

    /* "destination folder" row */
    row++;
    l = gtk_label_new_with_mnemonic(_("_Destination folder:"));
    g_object_set(l, "halign", GTK_ALIGN_START, "valign", GTK_ALIGN_CENTER, NULL);
    gtk_grid_attach(grid, l, 0, row, 1, 1);
    w = gtk_file_chooser_button_new(_("Select Destination Folder"), GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);

    if (!gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(w), data->downloadDir))
    {
        g_warning("couldn't select '%s'", data->downloadDir);
    }

    list = get_recent_destinations();

    for (GSList* walk = list; walk != NULL; walk = walk->next)
    {
        gtk_file_chooser_add_shortcut_folder(GTK_FILE_CHOOSER(w), walk->data, NULL);
    }

    g_slist_free(list);
    gtk_grid_attach_next_to(grid, w, l, GTK_POS_RIGHT, 1, 1);
    gtk_label_set_mnemonic_widget(GTK_LABEL(l), w);
    g_signal_connect(w, "selection-changed", G_CALLBACK(downloadDirChanged), data);

    row++;
    l = data->freespace_label = gtr_freespace_label_new(core, data->downloadDir);
    gtk_widget_set_margin_bottom(l, GUI_PAD_BIG);
    g_object_set(l, "halign", GTK_ALIGN_END, "valign", GTK_ALIGN_CENTER, NULL);
    gtk_grid_attach(grid, l, 0, row, 2, 1);

    /* file list row */
    row++;
    w = data->file_list;
    gtk_widget_set_vexpand(w, TRUE);
    gtk_widget_set_size_request(w, 466U, 300U);
    gtk_grid_attach(grid, w, 0, row, 2, 1);

    /* torrent priority row */
    row++;
    l = gtk_label_new_with_mnemonic(_("Torrent _priority:"));
    g_object_set(l, "halign", GTK_ALIGN_START, "valign", GTK_ALIGN_CENTER, NULL);
    gtk_grid_attach(grid, l, 0, row, 1, 1);
    w = data->priority_combo;
    gtk_label_set_mnemonic_widget(GTK_LABEL(l), w);
    gtk_grid_attach_next_to(grid, w, l, GTK_POS_RIGHT, 1, 1);

    /* torrent priority row */
    row++;
    w = data->run_check;

    if (!tr_ctorGetPaused(ctor, TR_FORCE, &flag))
    {
        g_assert_not_reached();
    }

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), !flag);
    gtk_grid_attach(grid, w, 0, row, 2, 1);

    /* "trash .torrent file" row */
    row++;
    w = data->trash_check;

    if (!tr_ctorGetDeleteSource(ctor, &flag))
    {
        g_assert_not_reached();
    }

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), flag);
    gtk_grid_attach(grid, w, 0, row, 2, 1);

    /* trigger sourceChanged, either directly or indirectly,
     * so that it creates the tor/gtor objects */
    w = source_chooser;

    if (data->filename != NULL)
    {
        gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(w), data->filename);
    }
    else
    {
        sourceChanged(GTK_FILE_CHOOSER_BUTTON(w), data);
    }

    gtr_dialog_set_content(GTK_DIALOG(d), GTK_WIDGET(grid));
    w = gtk_dialog_get_widget_for_response(GTK_DIALOG(d), GTK_RESPONSE_ACCEPT);
    gtk_widget_grab_focus(w);
    return d;
}

/****
*****
****/

static void onOpenDialogResponse(GtkDialog* dialog, int response, gpointer core)
{
    char* folder;

    /* remember this folder the next time we use this dialog */
    folder = gtk_file_chooser_get_current_folder(GTK_FILE_CHOOSER(dialog));
    gtr_pref_string_set(TR_KEY_open_dialog_dir, folder);
    g_free(folder);

    if (response == GTK_RESPONSE_ACCEPT)
    {
        GtkFileChooser* chooser = GTK_FILE_CHOOSER(dialog);
        GtkWidget* w = gtk_file_chooser_get_extra_widget(chooser);
        GtkToggleButton* tb = GTK_TOGGLE_BUTTON(w);
        gboolean const do_start = gtr_pref_flag_get(TR_KEY_start_added_torrents);
        gboolean const do_prompt = gtk_toggle_button_get_active(tb);
        gboolean const do_notify = FALSE;
        GSList* files = gtk_file_chooser_get_files(chooser);

        gtr_core_add_files(core, files, do_start, do_prompt, do_notify);
        g_slist_foreach(files, (GFunc)(GCallback)g_object_unref, NULL);
        g_slist_free(files);
    }

    gtk_widget_destroy(GTK_WIDGET(dialog));
}

GtkWidget* gtr_torrent_open_from_file_dialog_new(GtkWindow* parent, TrCore* core)
{
    GtkWidget* w;
    GtkWidget* c;
    char const* folder;

    w = gtk_file_chooser_dialog_new(_("Open a Torrent"), parent, GTK_FILE_CHOOSER_ACTION_OPEN, GTK_STOCK_CANCEL,
        GTK_RESPONSE_CANCEL, GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT, NULL);
    gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(w), TRUE);
    addTorrentFilters(GTK_FILE_CHOOSER(w));
    g_signal_connect(w, "response", G_CALLBACK(onOpenDialogResponse), core);

    if ((folder = gtr_pref_string_get(TR_KEY_open_dialog_dir)) != NULL)
    {
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(w), folder);
    }

    c = gtk_check_button_new_with_mnemonic(_("Show _options dialog"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(c), gtr_pref_flag_get(TR_KEY_show_options_window));
    gtk_file_chooser_set_extra_widget(GTK_FILE_CHOOSER(w), c);
    gtk_widget_show(c);

    return w;
}

/***
****
***/

static void onOpenURLResponse(GtkDialog* dialog, int response, gpointer user_data)
{
    bool handled = false;

    if (response == GTK_RESPONSE_ACCEPT)
    {
        GtkWidget* e = GTK_WIDGET(g_object_get_data(G_OBJECT(dialog), "url-entry"));
        char* url = g_strdup(gtk_entry_get_text(GTK_ENTRY(e)));
        g_strstrip(url);

        if (url != NULL)
        {
            handled = gtr_core_add_from_url(user_data, url);

            if (!handled)
            {
                gtr_unrecognized_url_dialog(GTK_WIDGET(dialog), url);
            }

            g_free(url);
        }
    }
    else if (response == GTK_RESPONSE_CANCEL)
    {
        handled = true;
    }

    if (handled)
    {
        gtk_widget_destroy(GTK_WIDGET(dialog));
    }
}

GtkWidget* gtr_torrent_open_from_url_dialog_new(GtkWindow* parent, TrCore* core)
{
    guint row;
    GtkWidget* e;
    GtkWidget* t;
    GtkWidget* w;

    w = gtk_dialog_new_with_buttons(_("Open URL"), parent, GTK_DIALOG_DESTROY_WITH_PARENT, GTK_STOCK_CANCEL,
        GTK_RESPONSE_CANCEL, GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT, NULL);
    g_signal_connect(w, "response", G_CALLBACK(onOpenURLResponse), core);

    row = 0;
    t = hig_workarea_create();
    hig_workarea_add_section_title(t, &row, _("Open torrent from URL"));
    e = gtk_entry_new();
    gtk_widget_set_size_request(e, 400, -1);
    gtr_paste_clipboard_url_into_entry(e);
    g_object_set_data(G_OBJECT(w), "url-entry", e);
    hig_workarea_add_row(t, &row, _("_URL"), e, NULL);

    gtr_dialog_set_content(GTK_DIALOG(w), t);

    if (gtk_entry_get_text_length(GTK_ENTRY(e)) == 0)
    {
        gtk_widget_grab_focus(e);
    }
    else
    {
        gtk_widget_grab_focus(gtk_dialog_get_widget_for_response(GTK_DIALOG(w), GTK_RESPONSE_ACCEPT));
    }

    return w;
}
