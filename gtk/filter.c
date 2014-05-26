/*
 * This file Copyright (C) 2012-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <stdlib.h> /* qsort() */

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h>

#include "favicon.h" /* gtr_get_favicon() */
#include "filter.h"
#include "hig.h" /* GUI_PAD */
#include "tr-core.h" /* MC_TORRENT */
#include "util.h" /* gtr_get_host_from_url() */

static GQuark DIRTY_KEY = 0;
static GQuark SESSION_KEY = 0;
static GQuark TEXT_KEY = 0;
static GQuark TORRENT_MODEL_KEY = 0;

/***
****
****  ACTIVITY
****
***/

enum
{
    ACTIVITY_FILTER_ALL,
    ACTIVITY_FILTER_DOWNLOADING,
    ACTIVITY_FILTER_SEEDING,
    ACTIVITY_FILTER_ACTIVE,
    ACTIVITY_FILTER_PAUSED,
    ACTIVITY_FILTER_FINISHED,
    ACTIVITY_FILTER_VERIFYING,
    ACTIVITY_FILTER_ERROR,
    ACTIVITY_FILTER_SEPARATOR
};

enum
{
    ACTIVITY_FILTER_COL_NAME,
    ACTIVITY_FILTER_COL_COUNT,
    ACTIVITY_FILTER_COL_TYPE,
    ACTIVITY_FILTER_COL_STOCK_ID,
    ACTIVITY_FILTER_N_COLS
};

static gboolean activity_is_it_a_separator(GtkTreeModel* m, GtkTreeIter* i, gpointer d UNUSED)
{
    int type;
    gtk_tree_model_get(m, i, ACTIVITY_FILTER_COL_TYPE, &type, -1);
    return type == ACTIVITY_FILTER_SEPARATOR;
}

static gboolean test_torrent_activity(tr_torrent* tor, int type)
{
    tr_stat const* st = tr_torrentStatCached(tor);

    switch (type)
    {
    case ACTIVITY_FILTER_DOWNLOADING:
        return st->activity == TR_STATUS_DOWNLOAD || st->activity == TR_STATUS_DOWNLOAD_WAIT;

    case ACTIVITY_FILTER_SEEDING:
        return st->activity == TR_STATUS_SEED || st->activity == TR_STATUS_SEED_WAIT;

    case ACTIVITY_FILTER_ACTIVE:
        return st->peersSendingToUs > 0 || st->peersGettingFromUs > 0 || st->webseedsSendingToUs > 0 ||
               st->activity == TR_STATUS_CHECK;

    case ACTIVITY_FILTER_PAUSED:
        return st->activity == TR_STATUS_STOPPED;

    case ACTIVITY_FILTER_FINISHED:
        return st->finished == TRUE;

    case ACTIVITY_FILTER_VERIFYING:
        return st->activity == TR_STATUS_CHECK || st->activity == TR_STATUS_CHECK_WAIT;

    case ACTIVITY_FILTER_ERROR:
        return st->error != 0;

    default: /* ACTIVITY_FILTER_ALL */
        return TRUE;
    }
}

static void status_model_update_count(GtkListStore* store, GtkTreeIter* iter, int n)
{
    int count;
    GtkTreeModel* model = GTK_TREE_MODEL(store);
    gtk_tree_model_get(model, iter, ACTIVITY_FILTER_COL_COUNT, &count, -1);

    if (n != count)
    {
        gtk_list_store_set(store, iter, ACTIVITY_FILTER_COL_COUNT, n, -1);
    }
}

static gboolean activity_filter_model_update(gpointer gstore)
{
    GtkTreeIter iter;
    GObject* o = G_OBJECT(gstore);
    GtkListStore* store = GTK_LIST_STORE(gstore);
    GtkTreeModel* model = GTK_TREE_MODEL(store);
    GtkTreeModel* tmodel = GTK_TREE_MODEL(g_object_get_qdata(o, TORRENT_MODEL_KEY));

    g_object_steal_qdata(o, DIRTY_KEY);

    if (gtk_tree_model_iter_nth_child(model, &iter, NULL, 0))
    {
        do
        {
            int hits;
            int type;
            GtkTreeIter torrent_iter;

            gtk_tree_model_get(model, &iter, ACTIVITY_FILTER_COL_TYPE, &type, -1);

            hits = 0;

            if (gtk_tree_model_iter_nth_child(tmodel, &torrent_iter, NULL, 0))
            {
                do
                {
                    tr_torrent* tor;
                    gtk_tree_model_get(tmodel, &torrent_iter, MC_TORRENT, &tor, -1);

                    if (test_torrent_activity(tor, type))
                    {
                        ++hits;
                    }
                }
                while (gtk_tree_model_iter_next(tmodel, &torrent_iter));
            }

            status_model_update_count(store, &iter, hits);
        }
        while (gtk_tree_model_iter_next(model, &iter));
    }

    return G_SOURCE_REMOVE;
}

static GtkTreeModel* activity_filter_model_new(GtkTreeModel* tmodel)
{
    struct
    {
        int type;
        char const* context;
        char const* name;
        char const* stock_id;
    }
    types[] =
    {
        { ACTIVITY_FILTER_ALL, NULL, N_("All"), NULL },
        { ACTIVITY_FILTER_SEPARATOR, NULL, NULL, NULL },
        { ACTIVITY_FILTER_ACTIVE, NULL, N_("Active"), NULL },
        { ACTIVITY_FILTER_DOWNLOADING, "Verb", NC_("Verb", "Downloading"), NULL },
        { ACTIVITY_FILTER_SEEDING, "Verb", NC_("Verb", "Seeding"), NULL },
        { ACTIVITY_FILTER_PAUSED, NULL, N_("Paused"), NULL },
        { ACTIVITY_FILTER_FINISHED, NULL, N_("Finished"), NULL },
        { ACTIVITY_FILTER_VERIFYING, "Verb", NC_("Verb", "Verifying"), NULL },
        { ACTIVITY_FILTER_ERROR, NULL, N_("Error"), NULL }
    };

    GtkListStore* store = gtk_list_store_new(ACTIVITY_FILTER_N_COLS,
        G_TYPE_STRING,
        G_TYPE_INT,
        G_TYPE_INT,
        G_TYPE_STRING);

    for (size_t i = 0; i < G_N_ELEMENTS(types); ++i)
    {
        char const* name = types[i].context != NULL ? g_dpgettext2(NULL, types[i].context, types[i].name) : _(types[i].name);
        gtk_list_store_insert_with_values(store, NULL, -1,
            ACTIVITY_FILTER_COL_NAME, name,
            ACTIVITY_FILTER_COL_TYPE, types[i].type,
            ACTIVITY_FILTER_COL_STOCK_ID, types[i].stock_id,
            -1);
    }

    g_object_set_qdata(G_OBJECT(store), TORRENT_MODEL_KEY, tmodel);
    activity_filter_model_update(store);
    return GTK_TREE_MODEL(store);
}

static void render_activity_pixbuf_func(GtkCellLayout* cell_layout UNUSED, GtkCellRenderer* cell_renderer,
    GtkTreeModel* tree_model, GtkTreeIter* iter, gpointer data UNUSED)
{
    int type;
    int width;
    int ypad;

    gtk_tree_model_get(tree_model, iter, ACTIVITY_FILTER_COL_TYPE, &type, -1);
    width = type == ACTIVITY_FILTER_ALL ? 0 : 20;
    ypad = type == ACTIVITY_FILTER_ALL ? 0 : 2;

    g_object_set(cell_renderer, "width", width, "ypad", ypad, NULL);
}

static void activity_model_update_idle(gpointer activity_model)
{
    GObject* o = G_OBJECT(activity_model);
    gboolean const pending = g_object_get_qdata(o, DIRTY_KEY) != NULL;

    if (!pending)
    {
        GSourceFunc func = activity_filter_model_update;
        g_object_set_qdata(o, DIRTY_KEY, GINT_TO_POINTER(1));
        gdk_threads_add_idle(func, activity_model);
    }
}

static void activity_torrent_model_row_changed(GtkTreeModel* tmodel UNUSED, GtkTreePath* path UNUSED, GtkTreeIter* iter UNUSED,
    gpointer activity_model)
{
    activity_model_update_idle(activity_model);
}

static void activity_torrent_model_row_deleted_cb(GtkTreeModel* tmodel UNUSED, GtkTreePath* path UNUSED,
    gpointer activity_model)
{
    activity_model_update_idle(activity_model);
}

static void disconnect_activity_model_callbacks(gpointer tmodel, GObject* cat_model)
{
    g_signal_handlers_disconnect_by_func(tmodel, activity_torrent_model_row_changed, cat_model);
    g_signal_handlers_disconnect_by_func(tmodel, activity_torrent_model_row_deleted_cb, cat_model);
}

static GtkWidget* activity_combo_box_new(GtkTreeModel* tmodel)
{
    GtkWidget* c;
    GtkCellRenderer* r;
    GtkTreeModel* activity_model;
    GtkComboBox* c_combo_box;
    GtkCellLayout* c_cell_layout;

    activity_model = activity_filter_model_new(tmodel);
    c = gtk_combo_box_new_with_model(activity_model);
    c_combo_box = GTK_COMBO_BOX(c);
    c_cell_layout = GTK_CELL_LAYOUT(c);
    gtk_combo_box_set_row_separator_func(c_combo_box, activity_is_it_a_separator, NULL, NULL);
    gtk_combo_box_set_active(c_combo_box, 0);

    r = gtk_cell_renderer_pixbuf_new();
    gtk_cell_layout_pack_start(c_cell_layout, r, FALSE);
    gtk_cell_layout_set_attributes(c_cell_layout, r, "stock-id", ACTIVITY_FILTER_COL_STOCK_ID, NULL);
    gtk_cell_layout_set_cell_data_func(c_cell_layout, r, render_activity_pixbuf_func, NULL, NULL);

    r = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start(c_cell_layout, r, TRUE);
    gtk_cell_layout_set_attributes(c_cell_layout, r, "text", ACTIVITY_FILTER_COL_NAME, NULL);

    g_object_weak_ref(G_OBJECT(activity_model), disconnect_activity_model_callbacks, tmodel);
    g_signal_connect(tmodel, "row-changed", G_CALLBACK(activity_torrent_model_row_changed), activity_model);
    g_signal_connect(tmodel, "row-inserted", G_CALLBACK(activity_torrent_model_row_changed), activity_model);
    g_signal_connect(tmodel, "row-deleted", G_CALLBACK(activity_torrent_model_row_deleted_cb), activity_model);

    return c;
}

/****
*****
*****  ENTRY FIELD
*****
****/

static gboolean testText(tr_torrent const* tor, char const* key)
{
    gboolean ret = FALSE;

    if (key == NULL || *key == '\0')
    {
        ret = TRUE;
    }
    else
    {
        tr_info const* inf = tr_torrentInfo(tor);

        /* test the torrent name... */
        {
            char* pch = g_utf8_casefold(tr_torrentName(tor), -1);
            ret = key == NULL || strstr(pch, key) != NULL;
            g_free(pch);
        }

        /* test the files... */
        for (tr_file_index_t i = 0; i < inf->fileCount && !ret; ++i)
        {
            char* pch = g_utf8_casefold(inf->files[i].name, -1);
            ret = key == NULL || strstr(pch, key) != NULL;
            g_free(pch);
        }
    }

    return ret;
}

static void entry_clear(GtkEntry* e)
{
    gtk_entry_set_text(e, "");
}

static void filter_entry_changed(GtkEditable* e, gpointer filter_model)
{
    char* pch;
    char* folded;

    pch = gtk_editable_get_chars(e, 0, -1);
    folded = g_utf8_casefold(pch, -1);
    g_strstrip(folded);
    g_object_set_qdata_full(filter_model, TEXT_KEY, folded, g_free);
    g_free(pch);

    gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(filter_model));
}

/*****
******
******
******
*****/

struct filter_data
{
    GtkWidget* activity;
    GtkWidget* entry;
    GtkWidget* show_lb;
    GtkWidget* header_bar;
    GtkTreeModel* filter_model;
    int active_activity_type;
};

static gboolean is_row_visible(GtkTreeModel* model, GtkTreeIter* iter, gpointer vdata)
{
    char const* text;
    tr_torrent* tor;
    struct filter_data* data = vdata;
    GObject* o = G_OBJECT(data->filter_model);

    gtk_tree_model_get(model, iter, MC_TORRENT, &tor, -1);

    text = (char const*)g_object_get_qdata(o, TEXT_KEY);

    return tor != NULL && test_torrent_activity(tor, data->active_activity_type) && testText(tor, text);
}

static void selection_changed_cb(GtkComboBox* combo, gpointer vdata)
{
    int type;
    char* host;
    char* name;
    GtkTreeIter iter;
    GtkTreeModel* model;
    struct filter_data* data = vdata;
    gchar bar_title[255];

    /* set data->active_activity_type from the activity combobox */
    combo = GTK_COMBO_BOX(data->activity);
    model = gtk_combo_box_get_model(combo);

    if (gtk_combo_box_get_active_iter(combo, &iter))
    {
        gtk_tree_model_get(model, &iter,
            ACTIVITY_FILTER_COL_TYPE, &type,
            ACTIVITY_FILTER_COL_NAME, &name,
            -1);
        g_snprintf(bar_title, sizeof(bar_title), _("%s torrents"), name);
        gtk_header_bar_set_subtitle(GTK_HEADER_BAR(data->header_bar), bar_title);
    }
    else
    {
        type = ACTIVITY_FILTER_ALL;
    }

    data->active_activity_type = type;

    if (gtk_combo_box_get_active_iter(combo, &iter))
    {
        gtk_tree_model_get(model, &iter, ACTIVITY_FILTER_COL_TYPE, &type, -1);
    }
    else
    {
        type = ACTIVITY_FILTER_ALL;
    }

    data->active_activity_type = type;

    /* refilter */
    gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(data->filter_model));
}

/***
****
***/

static gboolean update_count_label(gpointer gdata)
{
    char buf[512];
    int visibleCount;
    int activityCount;
    GtkTreeModel* model;
    GtkComboBox* combo;
    GtkTreeIter iter;
    struct filter_data* data = gdata;

    /* get the visible count */
    visibleCount = gtk_tree_model_iter_n_children(data->filter_model, NULL);

    /* get the activity count */
    combo = GTK_COMBO_BOX(data->activity);
    model = gtk_combo_box_get_model(combo);

    if (gtk_combo_box_get_active_iter(combo, &iter))
    {
        gtk_tree_model_get(model, &iter, ACTIVITY_FILTER_COL_COUNT, &activityCount, -1);
    }
    else
    {
        activityCount = 0;
    }

    /* set the text */
    g_snprintf(buf, sizeof(buf), _("_Show %'d of:"), visibleCount);

    gtk_label_set_markup_with_mnemonic(GTK_LABEL(data->show_lb), buf);

    g_object_steal_qdata(G_OBJECT(data->show_lb), DIRTY_KEY);
    return G_SOURCE_REMOVE;
}

static void update_count_label_idle(struct filter_data* data)
{
    GObject* o = G_OBJECT(data->show_lb);
    gboolean const pending = g_object_get_qdata(o, DIRTY_KEY) != NULL;

    if (!pending)
    {
        g_object_set_qdata(o, DIRTY_KEY, GINT_TO_POINTER(1));
        gdk_threads_add_idle(update_count_label, data);
    }
}

static void on_filter_model_row_inserted(GtkTreeModel* tree_model UNUSED, GtkTreePath* path UNUSED, GtkTreeIter* iter UNUSED,
    gpointer data)
{
    update_count_label_idle(data);
}

static void on_filter_model_row_deleted(GtkTreeModel* tree_model UNUSED, GtkTreePath* path UNUSED, gpointer data)
{
    update_count_label_idle(data);
}

/***
****
***/

GtkWidget* gtr_filter_bar_new(tr_session* session, GtkTreeModel* tmodel, GtkTreeModel** filter_model, GtkHeaderBar* header_bar)
{
    GtkWidget* l;
    GtkWidget* w;
    GtkWidget* s;
    GtkWidget* b;
    GtkWidget* reveal;
    GtkWidget* toolbar;
    GtkWidget* activity;
    GtkToolItem* ti;
    struct filter_data* data;

    g_assert(DIRTY_KEY == 0);
    TEXT_KEY = g_quark_from_static_string("tr-filter-text-key");
    DIRTY_KEY = g_quark_from_static_string("tr-filter-dirty-key");
    SESSION_KEY = g_quark_from_static_string("tr-session-key");
    TORRENT_MODEL_KEY = g_quark_from_static_string("tr-filter-torrent-model-key");

    data = g_new0(struct filter_data, 1);
    data->show_lb = gtk_label_new(NULL);
    data->activity = activity = activity_combo_box_new(tmodel);
    data->filter_model = gtk_tree_model_filter_new(tmodel, NULL);
    data->header_bar = GTK_WIDGET(header_bar);
    g_signal_connect(data->filter_model, "row-deleted", G_CALLBACK(on_filter_model_row_deleted), data);
    g_signal_connect(data->filter_model, "row-inserted", G_CALLBACK(on_filter_model_row_inserted), data);

    gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(data->filter_model), is_row_visible, data, g_free);

    g_signal_connect(data->activity, "changed", G_CALLBACK(selection_changed_cb), data);

    toolbar = gtk_toolbar_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(toolbar), GTK_STYLE_CLASS_PRIMARY_TOOLBAR);

    /* add the activity combobox */
    w = activity;
    l = data->show_lb;
    gtk_label_set_mnemonic_widget(GTK_LABEL(l), w);

    b = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

    gtk_box_pack_start(GTK_BOX(b), l, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(b), w, FALSE, FALSE, 0);

    /* create a tool item for activity and label. */
    ti = gtk_tool_item_new();
    gtk_container_add(GTK_CONTAINER(ti), b);

    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), GTK_TOOL_ITEM(ti), 0);

    /* add a spacer */
    // w = gtk_alignment_new(0.0f, 0.0f, 0.0f, 0.0f);
    // gtk_widget_set_size_request(w, 0u, GUI_PAD_BIG);
    // gtk_box_pack_start(h_box, w, FALSE, FALSE, 0);

    /* add the entry field */
    s = gtk_entry_new();
    gtk_entry_set_icon_from_icon_name(GTK_ENTRY(s), GTK_ENTRY_ICON_SECONDARY, "edit-clear-symbolic");
    g_signal_connect(s, "icon-release", G_CALLBACK(entry_clear), NULL);

    ti = gtk_tool_item_new();
    gtk_tool_item_set_expand(GTK_TOOL_ITEM(ti), TRUE);
    gtk_container_add(GTK_CONTAINER(ti), s);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), GTK_TOOL_ITEM(ti), 1);

    g_signal_connect(s, "changed", G_CALLBACK(filter_entry_changed), data->filter_model);

    *filter_model = data->filter_model;
    update_count_label(data);

    reveal = gtk_revealer_new();
    gtk_container_add(GTK_CONTAINER(reveal), toolbar);
    return reveal;
}
