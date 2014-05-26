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
    ACTIVITY_FILTER_LAST
};

enum
{
    ACTIVITY_FILTER_COL_NAME,
    ACTIVITY_FILTER_COL_COUNT,
    ACTIVITY_FILTER_COL_TYPE,
    ACTIVITY_FILTER_COL_STOCK_ID,
    ACTIVITY_FILTER_N_COLS
};

struct filter
{
    int type;
    char const* name;
    GtkWidget* widget;
};

#define MAX_FILTERS_ON_TOOLBAR 4

struct filter filters[] =
{
    { ACTIVITY_FILTER_ALL, N_("All"), NULL },
    { ACTIVITY_FILTER_DOWNLOADING, NC_("Verb", "Downloading"), NULL },
    { ACTIVITY_FILTER_SEEDING, NC_("Verb", "Seeding"), NULL },
    { ACTIVITY_FILTER_ACTIVE, N_("Active"), NULL },
    { ACTIVITY_FILTER_FINISHED, N_("Finished"), NULL },
    { ACTIVITY_FILTER_PAUSED, N_("Paused"), NULL },
    { ACTIVITY_FILTER_VERIFYING, NC_("Verb", "Verifying"), NULL },
    { ACTIVITY_FILTER_ERROR, N_("Error"), NULL }
};

struct filter_data
{
    struct filter* filters;
    GtkWidget* entry;
    GtkWidget* show_lb;
    GtkWidget* header_bar;
    GtkTreeModel* filter_model;
    GtkTreeModel* tree_model;
    int active_activity_type;
};

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

static void selection_changed_cb(GtkButton* button, gpointer vdata)
{
    struct filter_data* data = vdata;
    gchar bar_title[255];

    // If the button isn't active, we are not interested in it.
    if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button)))
    {
        return;
    }

    for (int i = 0; i < MAX_FILTERS_ON_TOOLBAR; i++)
    {
        if (GTK_WIDGET(button) == data->filters[i].widget)
        {
            data->active_activity_type = data->filters[i].type;
            g_snprintf(bar_title, sizeof(bar_title), _("%s torrents"), data->filters[i].name);
            gtk_header_bar_set_subtitle(GTK_HEADER_BAR(data->header_bar), bar_title);
        }
    }

    /* refilter */
    gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(data->filter_model));
}

/***
****
***/

GtkWidget* gtr_filter_bar_new(tr_session* session, GtkTreeModel* tmodel, GtkTreeModel** filter_model, GtkHeaderBar* header_bar)
{
    GtkWidget* w;
    GtkWidget* b;
    GtkWidget* reveal;
    GtkWidget* toolbar;
    GtkWidget* wrapper;
    GtkWidget* sibling;
    GtkToolItem* ti;
    struct filter_data* data;

    g_assert(DIRTY_KEY == 0);
    TEXT_KEY = g_quark_from_static_string("tr-filter-text-key");
    DIRTY_KEY = g_quark_from_static_string("tr-filter-dirty-key");
    SESSION_KEY = g_quark_from_static_string("tr-session-key");
    TORRENT_MODEL_KEY = g_quark_from_static_string("tr-filter-torrent-model-key");

    data = g_new0(struct filter_data, 1);
    data->show_lb = gtk_label_new(NULL);
    data->filter_model = gtk_tree_model_filter_new(tmodel, NULL);
    data->tree_model = tmodel;
    data->header_bar = GTK_WIDGET(header_bar);
    data->filters = filters;

    gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(data->filter_model), is_row_visible, data, g_free);

    toolbar = gtk_toolbar_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(toolbar), GTK_STYLE_CLASS_TOOLBAR);
    gtk_container_set_border_width(GTK_CONTAINER(toolbar), 0);

    ti = gtk_tool_item_new();
    gtk_tool_item_set_expand(GTK_TOOL_ITEM(ti), TRUE);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), GTK_TOOL_ITEM(ti), 0);

    wrapper = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_widget_set_valign(wrapper, GTK_ALIGN_CENTER);
    gtk_container_add(GTK_CONTAINER(ti), wrapper);

    /* Buttons */
    b = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

    for (int i = 0; i < MAX_FILTERS_ON_TOOLBAR; i++)
    {
        printf("Adding type %d, name %s\n", filters[i].type, filters[i].name);

        if (sibling != NULL)
        {
            w = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(sibling), filters[i].name);
        }
        else
        {
            w = gtk_radio_button_new_with_label(NULL, filters[i].name);
        }

        ti = gtk_tool_item_new();
        gtk_container_add(GTK_CONTAINER(ti), b);
        gtk_style_context_add_class(gtk_widget_get_style_context(w), "button");
        gtk_style_context_add_class(gtk_widget_get_style_context(w), "text-button");
        g_object_set(G_OBJECT(w), "draw-indicator", FALSE, NULL);
        gtk_box_pack_start(GTK_BOX(b), w, FALSE, FALSE, 0);
        g_signal_connect(w, "toggled", G_CALLBACK(selection_changed_cb), data);
        data->filters[i].widget = sibling = w;
    }

    gtk_style_context_add_class(gtk_widget_get_style_context(b), GTK_STYLE_CLASS_LINKED);

    gtk_box_pack_start(GTK_BOX(wrapper), b, FALSE, FALSE, 0);

    /* add the entry field */
    w = gtk_search_entry_new();
    g_signal_connect(w, "icon-release", G_CALLBACK(entry_clear), NULL);

    gtk_box_pack_start(GTK_BOX(wrapper), w, TRUE, TRUE, 0);

    g_signal_connect(w, "changed", G_CALLBACK(filter_entry_changed), data->filter_model);

    *filter_model = data->filter_model;

    reveal = gtk_revealer_new();
    gtk_container_add(GTK_CONTAINER(reveal), toolbar);
    return reveal;
}
