/******************************************************************************
 * Copyright (c) Transmission authors and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *****************************************************************************/

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include <libtransmission/transmission.h>

#include "dialogs.h"
#include "tr-core.h"

/***
****
***/

struct delete_data
{
    gboolean delete_files;
    GSList* torrent_ids;
    TrCore* core;
};

static void on_remove_dialog_response(GtkDialog* dialog, gint response, gpointer gdd)
{
    struct delete_data* dd = gdd;

    if (response == GTK_RESPONSE_ACCEPT)
    {
        for (GSList* l = dd->torrent_ids; l != NULL; l = l->next)
        {
            gtr_core_remove_torrent(dd->core, GPOINTER_TO_INT(l->data), dd->delete_files);
        }
    }

    gtk_widget_destroy(GTK_WIDGET(dialog));
    g_slist_free(dd->torrent_ids);
    g_free(dd);
}

void gtr_confirm_remove(GtkWindow* parent, TrCore* core, GSList* torrent_ids, gboolean delete_files)
{
    GtkWidget* d;
    GString* primary_text;
    GString* secondary_text;
    struct delete_data* dd;
    int connected = 0;
    int incomplete = 0;
    int const count = g_slist_length(torrent_ids);

    if (count == 0)
    {
        return;
    }

    dd = g_new0(struct delete_data, 1);
    dd->core = core;
    dd->torrent_ids = torrent_ids;
    dd->delete_files = delete_files;

    for (GSList* l = torrent_ids; l != NULL; l = l->next)
    {
        int const id = GPOINTER_TO_INT(l->data);
        tr_torrent* tor = gtr_core_find_torrent(core, id);
        tr_stat const* stat = tr_torrentStat(tor);

        if (stat->leftUntilDone != 0)
        {
            ++incomplete;
        }

        if (stat->peersConnected != 0)
        {
            ++connected;
        }
    }

    primary_text = g_string_new(NULL);

    if (!delete_files)
    {
        g_string_printf(primary_text, ngettext("Remove torrent?", "Remove %d torrents?", count), count);
    }
    else
    {
        g_string_printf(primary_text, ngettext("Delete this torrent's downloaded files?",
            "Delete these %d torrents' downloaded files?", count), count);
    }

    secondary_text = g_string_new(NULL);

    if (incomplete == 0 && connected == 0)
    {
        g_string_assign(secondary_text,
            ngettext("Once removed, continuing the transfer will require the torrent file or magnet link.",
            "Once removed, continuing the transfers will require the torrent files or magnet links.", count));
    }
    else if (count == incomplete)
    {
        g_string_assign(secondary_text, ngettext("This torrent has not finished downloading.",
            "These torrents have not finished downloading.", count));
    }
    else if (count == connected)
    {
        g_string_assign(secondary_text, ngettext("This torrent is connected to peers.",
            "These torrents are connected to peers.", count));
    }
    else
    {
        if (connected != 0)
        {
            g_string_append(secondary_text, ngettext("One of these torrents is connected to peers.",
                "Some of these torrents are connected to peers.", connected));
        }

        if (connected != 0 && incomplete != 0)
        {
            g_string_append(secondary_text, "\n");
        }

        if (incomplete != 0)
        {
            g_string_assign(secondary_text, ngettext("One of these torrents has not finished downloading.",
                "Some of these torrents have not finished downloading.", incomplete));
        }
    }

    d = gtk_message_dialog_new_with_markup(parent, GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE,
        "<big><b>%s</b></big>", primary_text->str);

    if (secondary_text->len != 0)
    {
        gtk_message_dialog_format_secondary_markup(GTK_MESSAGE_DIALOG(d), "%s", secondary_text->str);
    }

    gtk_dialog_add_buttons(GTK_DIALOG(d), GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
        delete_files ? GTK_STOCK_DELETE : GTK_STOCK_REMOVE, GTK_RESPONSE_ACCEPT, NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_CANCEL);
    g_signal_connect(d, "response", G_CALLBACK(on_remove_dialog_response), dd);
    gtk_widget_show_all(d);

    g_string_free(primary_text, TRUE);
    g_string_free(secondary_text, TRUE);
}
