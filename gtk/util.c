/*
 * This file Copyright (C) 2008-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <ctype.h> /* isxdigit() */
#include <errno.h>
#include <limits.h> /* INT_MAX */
#include <stdarg.h>
#include <string.h> /* strchr(), strrchr(), strlen(), strstr() */

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <gio/gio.h> /* g_file_trash() */

#include <libtransmission/transmission.h> /* TR_RATIO_NA, TR_RATIO_INF */
#include <libtransmission/error.h>
#include <libtransmission/utils.h> /* tr_strratio() */
#include <libtransmission/web.h> /* tr_webResponseStr() */
#include <libtransmission/version.h> /* SHORT_VERSION_STRING */

#include "conf.h"
#include "hig.h"
#include "tr-core.h"
#include "tr-prefs.h"
#include "util.h"

/***
****  UNITS
***/

int const mem_K = 1024;
char const* mem_K_str = N_("KiB");
char const* mem_M_str = N_("MiB");
char const* mem_G_str = N_("GiB");
char const* mem_T_str = N_("TiB");

int const disk_K = 1000;
char const* disk_K_str = N_("kB");
char const* disk_M_str = N_("MB");
char const* disk_G_str = N_("GB");
char const* disk_T_str = N_("TB");

int const speed_K = 1000;
char const* speed_K_str = N_("kB/s");
char const* speed_M_str = N_("MB/s");
char const* speed_G_str = N_("GB/s");
char const* speed_T_str = N_("TB/s");

/***
****
***/

char const* gtr_get_unicode_string(int i)
{
    switch (i)
    {
    case GTR_UNICODE_UP:
        return "\xE2\x96\xB4";

    case GTR_UNICODE_DOWN:
        return "\xE2\x96\xBE";

    case GTR_UNICODE_INF:
        return "\xE2\x88\x9E";

    case GTR_UNICODE_BULLET:
        return "\xE2\x88\x99";

    default:
        return "err";
    }
}

char* tr_strlratio(char* buf, double ratio, size_t buflen)
{
    return tr_strratio(buf, buflen, ratio, gtr_get_unicode_string(GTR_UNICODE_INF));
}

char* tr_strlpercent(char* buf, double x, size_t buflen)
{
    return tr_strpercent(buf, x, buflen);
}

char* tr_strlsize(char* buf, guint64 bytes, size_t buflen)
{
    if (bytes == 0)
    {
        g_strlcpy(buf, Q_("None"), buflen);
    }
    else
    {
        tr_formatter_size_B(buf, bytes, buflen);
    }

    return buf;
}

char* tr_strltime(char* buf, int seconds, size_t buflen)
{
    int days;
    int hours;
    int minutes;
    char d[128];
    char h[128];
    char m[128];
    char s[128];

    if (seconds < 0)
    {
        seconds = 0;
    }

    days = seconds / 86400;
    hours = (seconds % 86400) / 3600;
    minutes = (seconds % 3600) / 60;
    seconds = (seconds % 3600) % 60;

    g_snprintf(d, sizeof(d), ngettext("%'d day", "%'d days", days), days);
    g_snprintf(h, sizeof(h), ngettext("%'d hour", "%'d hours", hours), hours);
    g_snprintf(m, sizeof(m), ngettext("%'d minute", "%'d minutes", minutes), minutes);
    g_snprintf(s, sizeof(s), ngettext("%'d second", "%'d seconds", seconds), seconds);

    if (days != 0)
    {
        if (days >= 4 || hours == 0)
        {
            g_strlcpy(buf, d, buflen);
        }
        else
        {
            g_snprintf(buf, buflen, "%s, %s", d, h);
        }
    }
    else if (hours != 0)
    {
        if (hours >= 4 || minutes == 0)
        {
            g_strlcpy(buf, h, buflen);
        }
        else
        {
            g_snprintf(buf, buflen, "%s, %s", h, m);
        }
    }
    else if (minutes != 0)
    {
        if (minutes >= 4 || seconds == 0)
        {
            g_strlcpy(buf, m, buflen);
        }
        else
        {
            g_snprintf(buf, buflen, "%s, %s", m, s);
        }
    }
    else
    {
        g_strlcpy(buf, s, buflen);
    }

    return buf;
}

/* pattern-matching text; ie, legaltorrents.com */
void gtr_get_host_from_url(char* buf, size_t buflen, char const* url)
{
    char host[1024];
    char const* pch;

    if ((pch = strstr(url, "://")) != NULL)
    {
        size_t const hostlen = strcspn(pch + 3, ":/");
        size_t const copylen = MIN(hostlen, sizeof(host) - 1);
        memcpy(host, pch + 3, copylen);
        host[copylen] = '\0';
    }
    else
    {
        *host = '\0';
    }

    if (tr_addressIsIP(host))
    {
        g_strlcpy(buf, url, buflen);
    }
    else
    {
        char const* first_dot = strchr(host, '.');
        char const* last_dot = strrchr(host, '.');

        if (first_dot != NULL && last_dot != NULL && first_dot != last_dot)
        {
            g_strlcpy(buf, first_dot + 1, buflen);
        }
        else
        {
            g_strlcpy(buf, host, buflen);
        }
    }
}

static gboolean gtr_is_supported_url(char const* str)
{
    return str != NULL && (g_str_has_prefix(str, "ftp://") || g_str_has_prefix(str, "http://") ||
        g_str_has_prefix(str, "https://"));
}

gboolean gtr_is_magnet_link(char const* str)
{
    return str != NULL && g_str_has_prefix(str, "magnet:?");
}

gboolean gtr_is_hex_hashcode(char const* str)
{
    if (str == NULL || strlen(str) != 40)
    {
        return FALSE;
    }

    for (int i = 0; i < 40; ++i)
    {
        if (!isxdigit(str[i]))
        {
            return FALSE;
        }
    }

    return TRUE;
}

static GtkWindow* getWindow(GtkWidget* w)
{
    if (w == NULL)
    {
        return NULL;
    }

    if (GTK_IS_WINDOW(w))
    {
        return GTK_WINDOW(w);
    }

    return GTK_WINDOW(gtk_widget_get_ancestor(w, GTK_TYPE_WINDOW));
}

void gtr_add_torrent_error_dialog(GtkWidget* child, int err, tr_torrent* duplicate_torrent, char const* filename)
{
    char* secondary;
    GtkWidget* w;
    GtkWindow* win = getWindow(child);

    if (err == TR_PARSE_ERR)
    {
        secondary = g_strdup_printf(_("The torrent file \"%s\" contains invalid data."), filename);
    }
    else if (err == TR_PARSE_DUPLICATE)
    {
        secondary = g_strdup_printf(_("The torrent file \"%s\" is already in use by \"%s.\""), filename,
            tr_torrentName(duplicate_torrent));
    }
    else
    {
        secondary = g_strdup_printf(_("The torrent file \"%s\" encountered an unknown error."), filename);
    }

    w = gtk_message_dialog_new(win, GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s",
        _("Error opening torrent"));
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(w), "%s", secondary);
    g_signal_connect_swapped(w, "response", G_CALLBACK(gtk_widget_destroy), w);
    gtk_widget_show_all(w);
    g_free(secondary);
}

typedef void (* PopupFunc)(GtkWidget*, GdkEventButton*);

/* pop up the context menu if a user right-clicks.
   if the row they right-click on isn't selected, select it. */

gboolean on_tree_view_button_pressed(GtkWidget* view, GdkEventButton* event, gpointer func)
{
    GtkTreeView* tv = GTK_TREE_VIEW(view);

    if (event->type == GDK_BUTTON_PRESS && event->button == 3)
    {
        GtkTreePath* path;
        GtkTreeSelection* selection = gtk_tree_view_get_selection(tv);

        if (gtk_tree_view_get_path_at_pos(tv, (gint)event->x, (gint)event->y, &path, NULL, NULL, NULL))
        {
            if (!gtk_tree_selection_path_is_selected(selection, path))
            {
                gtk_tree_selection_unselect_all(selection);
                gtk_tree_selection_select_path(selection, path);
            }

            gtk_tree_path_free(path);
        }

        if (func != NULL)
        {
            (*(PopupFunc)func)(view, event);
        }

        return TRUE;
    }

    return FALSE;
}

/* if the user clicked in an empty area of the list,
 * clear all the selections. */
gboolean on_tree_view_button_released(GtkWidget* view, GdkEventButton* event, gpointer unused UNUSED)
{
    GtkTreeView* tv = GTK_TREE_VIEW(view);

    if (!gtk_tree_view_get_path_at_pos(tv, (gint)event->x, (gint)event->y, NULL, NULL, NULL, NULL))
    {
        GtkTreeSelection* selection = gtk_tree_view_get_selection(tv);
        gtk_tree_selection_unselect_all(selection);
    }

    return FALSE;
}

bool gtr_file_trash_or_remove(char const* filename, tr_error** error)
{
    GFile* file;
    gboolean trashed = FALSE;
    bool result = true;

    g_return_val_if_fail(filename && *filename, false);

    file = g_file_new_for_path(filename);

    if (gtr_pref_flag_get(TR_KEY_trash_can_enabled))
    {
        GError* err = NULL;
        trashed = g_file_trash(file, NULL, &err);

        if (err != NULL)
        {
            g_message("Unable to trash file \"%s\": %s", filename, err->message);
            tr_error_set_literal(error, err->code, err->message);
            g_clear_error(&err);
        }
    }

    if (!trashed)
    {
        GError* err = NULL;
        g_file_delete(file, NULL, &err);

        if (err != NULL)
        {
            g_message("Unable to delete file \"%s\": %s", filename, err->message);
            tr_error_clear(error);
            tr_error_set_literal(error, err->code, err->message);
            g_clear_error(&err);
            result = false;
        }
    }

    g_object_unref(G_OBJECT(file));
    return result;
}

char const* gtr_get_help_uri(void)
{
    static char* uri = NULL;

    if (uri == NULL)
    {
        char const* fmt = "https://transmissionbt.com/help/gtk/%d.%dx";
        uri = g_strdup_printf(fmt, MAJOR_VERSION, MINOR_VERSION / 10);
    }

    return uri;
}

void gtr_open_file(char const* path)
{
    GFile* file = g_file_new_for_path(path);
    gchar* uri = g_file_get_uri(file);
    gtr_open_uri(uri);
    g_free(uri);
    g_object_unref(file);
}

void gtr_open_uri(char const* uri)
{
    if (uri != NULL)
    {
        gboolean opened = FALSE;

        if (!opened)
        {
#if GTK_CHECK_VERSION(3, 22, 0)
            opened = gtk_show_uri_on_window(NULL, uri, GDK_CURRENT_TIME, NULL);
#else
            opened = gtk_show_uri(NULL, uri, GDK_CURRENT_TIME, NULL);
#endif
        }

        if (!opened)
        {
            opened = g_app_info_launch_default_for_uri(uri, NULL, NULL);
        }

        if (!opened)
        {
            char* argv[] = { (char*)"xdg-open", (char*)uri, NULL };
            opened = g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);
        }

        if (!opened)
        {
            g_message("Unable to open \"%s\"", uri);
        }
    }
}

/***
****
***/

void gtr_combo_box_set_active_enum(GtkComboBox* combo_box, int value)
{
    int i;
    int currentValue;
    int const column = 0;
    GtkTreeIter iter;
    GtkTreeModel* model = gtk_combo_box_get_model(combo_box);

    /* do the value and current value match? */
    if (gtk_combo_box_get_active_iter(combo_box, &iter))
    {
        gtk_tree_model_get(model, &iter, column, &currentValue, -1);

        if (currentValue == value)
        {
            return;
        }
    }

    /* find the one to select */
    i = 0;

    while (gtk_tree_model_iter_nth_child(model, &iter, NULL, i))
    {
        gtk_tree_model_get(model, &iter, column, &currentValue, -1);

        if (currentValue == value)
        {
            gtk_combo_box_set_active_iter(combo_box, &iter);
            return;
        }

        ++i;
    }
}

GtkWidget* gtr_combo_box_new_enum(char const* text_1, ...)
{
    GtkWidget* w;
    GtkCellRenderer* r;
    GtkListStore* store;
    char const* text;

    store = gtk_list_store_new(2, G_TYPE_INT, G_TYPE_STRING);

    text = text_1;

    if (text != NULL)
    {
        va_list vl;

        va_start(vl, text_1);

        do
        {
            int const val = va_arg(vl, int);
            gtk_list_store_insert_with_values(store, NULL, INT_MAX, 0, val, 1, text, -1);
            text = va_arg(vl, char const*);
        }
        while (text != NULL);

        va_end(vl);
    }

    w = gtk_combo_box_new_with_model(GTK_TREE_MODEL(store));
    r = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(w), r, TRUE);
    gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(w), r, "text", 1, NULL);

    /* cleanup */
    g_object_unref(store);
    return w;
}

int gtr_combo_box_get_active_enum(GtkComboBox* combo_box)
{
    int value = 0;
    GtkTreeIter iter;

    if (gtk_combo_box_get_active_iter(combo_box, &iter))
    {
        gtk_tree_model_get(gtk_combo_box_get_model(combo_box), &iter, 0, &value, -1);
    }

    return value;
}

GtkWidget* gtr_priority_combo_new(void)
{
    return gtr_combo_box_new_enum(
        _("High"), TR_PRI_HIGH,
        _("Normal"), TR_PRI_NORMAL,
        _("Low"), TR_PRI_LOW,
        NULL);
}

/***
****
***/

#define GTR_CHILD_HIDDEN "gtr-child-hidden"

void gtr_widget_set_visible(GtkWidget* w, gboolean b)
{
    /* toggle the transient children, too */
    if (GTK_IS_WINDOW(w))
    {
        GList* windows = gtk_window_list_toplevels();
        GtkWindow* window = GTK_WINDOW(w);

        for (GList* l = windows; l != NULL; l = l->next)
        {
            if (!GTK_IS_WINDOW(l->data))
            {
                continue;
            }

            if (gtk_window_get_transient_for(GTK_WINDOW(l->data)) != window)
            {
                continue;
            }

            if (gtk_widget_get_visible(GTK_WIDGET(l->data)) == b)
            {
                continue;
            }

            if (b && g_object_get_data(G_OBJECT(l->data), GTR_CHILD_HIDDEN) != NULL)
            {
                g_object_steal_data(G_OBJECT(l->data), GTR_CHILD_HIDDEN);
                gtr_widget_set_visible(GTK_WIDGET(l->data), TRUE);
            }
            else if (!b)
            {
                g_object_set_data(G_OBJECT(l->data), GTR_CHILD_HIDDEN, GINT_TO_POINTER(1));
                gtr_widget_set_visible(GTK_WIDGET(l->data), FALSE);
            }
        }

        g_list_free(windows);
    }

    gtk_widget_set_visible(w, b);
}

void gtr_dialog_set_content(GtkDialog* dialog, GtkWidget* content)
{
    GtkWidget* vbox = gtk_dialog_get_content_area(dialog);
    gtk_box_pack_start(GTK_BOX(vbox), content, TRUE, TRUE, 0);
    gtk_widget_show_all(content);
}

/***
****
***/

void gtr_unrecognized_url_dialog(GtkWidget* parent, char const* url)
{
    char const* xt = "xt=urn:btih";

    GtkWindow* window = getWindow(parent);

    GString* gstr = g_string_new(NULL);

    GtkWidget* w = gtk_message_dialog_new(window, 0, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s", _("Unrecognized URL"));

    g_string_append_printf(gstr, _("Transmission doesn't know how to use \"%s\""), url);

    if (gtr_is_magnet_link(url) && strstr(url, xt) == NULL)
    {
        g_string_append_printf(gstr, "\n \n");
        g_string_append_printf(gstr, _("This magnet link appears to be intended for something other than BitTorrent. "
            "BitTorrent magnet links have a section containing \"%s\"."), xt);
    }

    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(w), "%s", gstr->str);
    g_signal_connect_swapped(w, "response", G_CALLBACK(gtk_widget_destroy), w);
    gtk_widget_show(w);
    g_string_free(gstr, TRUE);
}

/***
****
***/

void gtr_paste_clipboard_url_into_entry(GtkWidget* e)
{
    char* text[] =
    {
        g_strstrip(gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_PRIMARY))),
        g_strstrip(gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD)))
    };

    for (size_t i = 0; i < G_N_ELEMENTS(text); ++i)
    {
        char* s = text[i];

        if (s != NULL && (gtr_is_supported_url(s) || gtr_is_magnet_link(s) || gtr_is_hex_hashcode(s)))
        {
            gtk_entry_set_text(GTK_ENTRY(e), s);
            break;
        }
    }

    for (size_t i = 0; i < G_N_ELEMENTS(text); ++i)
    {
        g_free(text[i]);
    }
}

/***
****
***/

void gtr_label_set_text(GtkLabel* lb, char const* newstr)
{
    char const* oldstr = gtk_label_get_text(lb);

    if (g_strcmp0(oldstr, newstr) != 0)
    {
        gtk_label_set_text(lb, newstr);
    }
}

/***
****
***/

struct freespace_label_data
{
    guint timer_id;
    TrCore* core;
    GtkLabel* label;
    char* dir;
};

static void on_freespace_label_core_destroyed(gpointer gdata, GObject* dead_core);
static void on_freespace_label_destroyed(gpointer gdata, GObject* dead_label);

static void freespace_label_data_free(gpointer gdata)
{
    struct freespace_label_data* data = gdata;

    if (data->core != NULL)
    {
        g_object_weak_unref(G_OBJECT(data->core), on_freespace_label_core_destroyed, data);
    }

    if (data->label != NULL)
    {
        g_object_weak_ref(G_OBJECT(data->label), on_freespace_label_destroyed, data);
    }

    g_source_remove(data->timer_id);
    g_free(data->dir);
    g_free(data);
}

static TR_DEFINE_QUARK(freespace_label_data, freespace_label_data)

static void on_freespace_label_core_destroyed(gpointer gdata, GObject* dead_core G_GNUC_UNUSED)
{
    struct freespace_label_data* data = gdata;
    data->core = NULL;
    freespace_label_data_free(data);
}

static void on_freespace_label_destroyed(gpointer gdata, GObject* dead_label G_GNUC_UNUSED)
{
    struct freespace_label_data* data = gdata;
    data->label = NULL;
    freespace_label_data_free(data);
}

static gboolean on_freespace_timer(gpointer gdata)
{
    char text[128];
    char markup[128];
    int64_t bytes;
    tr_session* session;
    struct freespace_label_data* data = gdata;

    session = gtr_core_session(data->core);
    bytes = tr_sessionGetDirFreeSpace(session, data->dir);

    if (bytes < 0)
    {
        g_snprintf(text, sizeof(text), _("Error"));
    }
    else
    {
        char size[128];
        tr_strlsize(size, bytes, sizeof(size));
        g_snprintf(text, sizeof(text), _("%s free"), size);
    }

    g_snprintf(markup, sizeof(markup), "<i>%s</i>", text);
    gtk_label_set_markup(data->label, markup);

    return G_SOURCE_CONTINUE;
}

GtkWidget* gtr_freespace_label_new(struct _TrCore* core, char const* dir)
{
    struct freespace_label_data* data;

    data = g_new0(struct freespace_label_data, 1);
    data->timer_id = g_timeout_add_seconds(3, on_freespace_timer, data);
    data->core = core;
    data->label = GTK_LABEL(gtk_label_new(NULL));
    data->dir = g_strdup(dir);

    /* when either the core or the label is destroyed, stop updating */
    g_object_weak_ref(G_OBJECT(core), on_freespace_label_core_destroyed, data);
    g_object_weak_ref(G_OBJECT(data->label), on_freespace_label_destroyed, data);

    g_object_set_qdata(G_OBJECT(data->label), freespace_label_data_quark(), data);
    on_freespace_timer(data);
    return GTK_WIDGET(data->label);
}

void gtr_freespace_label_set_dir(GtkWidget* label, char const* dir)
{
    struct freespace_label_data* data;

    data = g_object_get_qdata(G_OBJECT(label), freespace_label_data_quark());

    tr_free(data->dir);
    data->dir = g_strdup(dir);
    on_freespace_timer(data);
}
