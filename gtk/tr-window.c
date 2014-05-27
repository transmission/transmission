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

#include <string.h> /* strlen() */

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h> /* tr_formatter_speed_KBps() */

#include "actions.h"
#include "conf.h"
#include "filter.h"
#include "hig.h"
#include "torrent-cell-renderer.h"
#include "tr-limit-popover.h"
#include "tr-prefs.h"
#include "tr-status-menu-button.h"
#include "tr-window.h"
#include "util.h"

typedef struct
{
    GAction* speedlimit_on_item[2];
    GAction* speedlimit_off_item[2];
    GtkWidget* ratio_on_item;
    GtkWidget* ratio_off_item;
    GtkWidget* scroll;
    GtkWidget* view;
    GtkWidget* toolbar;
    GtkWidget* filter;
    GtkWidget* status;
    GtkWidget* status_menu;
    GtkLabel* ul_lb;
    GtkLabel* dl_lb;
    GtkLabel* stats_lb;
    GtkWidget* alt_speed_image;
    GtkWidget* alt_speed_button;
    GtkWidget* options_menu;
    GtkTreeSelection* selection;
    GtkCellRenderer* renderer;
    GtkTreeViewColumn* column;
    GtkTreeModel* filter_model;
    TrCore* core;
    gulong pref_handler_id;
}
PrivateData;

static TR_DEFINE_QUARK(private_data, private_data)

static PrivateData* get_private_data(GtkWindow * w)
{
    return g_object_get_qdata(G_OBJECT(w), private_data_quark());
}

/***
****
***/

static void on_popup_menu(GtkWidget* self UNUSED, GdkEventButton* event)
{
    static GtkWidget* menu = NULL;

    if (menu == NULL)
    {
        GMenuModel* model = gtr_action_get_menu_model("main-window-popup");
        menu = gtk_menu_new_from_model(model);
        gtk_menu_attach_to_widget(GTK_MENU(menu), self, NULL);

        g_object_unref(model);
    }

    gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL, event != NULL ? event->button : 0, event != NULL ? event->time : 0);
}

static void view_row_activated(GtkTreeView* tree_view UNUSED, GtkTreePath* path UNUSED, GtkTreeViewColumn* column UNUSED,
    gpointer user_data UNUSED)
{
    gtr_action_activate("show-torrent-properties");
}

static gboolean tree_view_search_equal_func(GtkTreeModel* model, gint column UNUSED, gchar const* key, GtkTreeIter* iter,
    gpointer search_data UNUSED)
{
    gboolean match;
    char* lower;
    char const* name = NULL;

    lower = g_strstrip(g_utf8_strdown(key, -1));
    gtk_tree_model_get(model, iter, MC_NAME_COLLATED, &name, -1);
    match = strstr(name, lower) != NULL;
    g_free(lower);

    return !match;
}

static GtkWidget* makeview(PrivateData* p)
{
    GtkWidget* view;
    GtkTreeViewColumn* col;
    GtkTreeSelection* sel;
    GtkCellRenderer* r;
    GtkTreeView* tree_view;

    view = gtk_tree_view_new();
    tree_view = GTK_TREE_VIEW(view);
    gtk_tree_view_set_search_column(tree_view, MC_NAME_COLLATED);
    gtk_tree_view_set_search_equal_func(tree_view, tree_view_search_equal_func, NULL, NULL);
    gtk_tree_view_set_headers_visible(tree_view, FALSE);
    gtk_tree_view_set_fixed_height_mode(tree_view, TRUE);

    p->selection = gtk_tree_view_get_selection(tree_view);

    p->column = col = GTK_TREE_VIEW_COLUMN(g_object_new(GTK_TYPE_TREE_VIEW_COLUMN, "title", _("Torrent"), "resizable", TRUE,
        "sizing", GTK_TREE_VIEW_COLUMN_FIXED, NULL));

    p->renderer = r = torrent_cell_renderer_new();
    gtk_tree_view_column_pack_start(col, r, FALSE);
    gtk_tree_view_column_add_attribute(col, r, "torrent", MC_TORRENT);
    gtk_tree_view_column_add_attribute(col, r, "piece-upload-speed", MC_SPEED_UP);
    gtk_tree_view_column_add_attribute(col, r, "piece-download-speed", MC_SPEED_DOWN);

    gtk_tree_view_append_column(tree_view, col);
    g_object_set(r, "xpad", GUI_PAD_SMALL, "ypad", GUI_PAD_SMALL, NULL);

    gtk_tree_view_set_rules_hint(tree_view, TRUE);
    sel = gtk_tree_view_get_selection(tree_view);
    gtk_tree_selection_set_mode(GTK_TREE_SELECTION(sel), GTK_SELECTION_MULTIPLE);

    g_signal_connect(view, "popup-menu", G_CALLBACK(on_popup_menu), NULL);
    g_signal_connect(view, "button-press-event", G_CALLBACK(on_tree_view_button_pressed), (void*)on_popup_menu);
    g_signal_connect(view, "button-release-event", G_CALLBACK(on_tree_view_button_released), NULL);
    g_signal_connect(view, "row-activated", G_CALLBACK(view_row_activated), NULL);

    gtk_tree_view_set_model(tree_view, p->filter_model);
    g_object_unref(p->filter_model);

    return view;
}

static void syncAltSpeedButton(PrivateData* p);

static void prefsChanged(TrCore* core UNUSED, tr_quark const key, gpointer wind)
{
    gboolean isEnabled;
    PrivateData* p = get_private_data(GTK_WINDOW(wind));

    printf("prefsChanges(%d)\n", key);

    switch (key)
    {
    case TR_KEY_compact_view:
        printf("compact-view\n");
        g_object_set(p->renderer, "compact", gtr_pref_flag_get(key), NULL);
        /* since the cell size has changed, we need gtktreeview to revalidate
         * its fixed-height mode values. Unfortunately there's not an API call
         * for that, but it *does* revalidate when it thinks the style's been tweaked */
        g_signal_emit_by_name(p->view, "style-updated", NULL, NULL);
        break;

    case TR_KEY_show_statusbar:
        printf("show-status-bar\n");
        isEnabled = gtr_pref_flag_get(key);
        g_object_set(p->status, "visible", isEnabled, NULL);
        break;

    case TR_KEY_show_filterbar:
        printf("show-filter-bar\n");
        isEnabled = gtr_pref_flag_get(key);
        g_object_set(p->filter, "visible", isEnabled, NULL);
        break;

    case TR_KEY_show_toolbar:
        isEnabled = gtr_pref_flag_get(key);
        printf("TR_KEY_show_toolbar now is %d\n", isEnabled);
        g_object_set(p->toolbar, "visible", isEnabled, NULL);
        break;

    case TR_KEY_statusbar_stats:
        gtr_window_refresh(wind);
        break;

    case TR_KEY_alt_speed_enabled:
    case TR_KEY_alt_speed_up:
    case TR_KEY_alt_speed_down:
        syncAltSpeedButton(p);
        break;

    default:
        break;
    }
}

static void privateFree(gpointer vprivate)
{
    PrivateData* p = vprivate;
    g_signal_handler_disconnect(p->core, p->pref_handler_id);
    g_free(p);
}

static void onYinYangReleased(GtkWidget* w UNUSED, gpointer vprivate)
{
    PrivateData* p = vprivate;

    gtk_menu_popup(GTK_MENU(p->status_menu), NULL, NULL, NULL, NULL, 0, gtk_get_current_event_time());
}

#define STATS_MODE "stats-mode"

static struct
{
    char const* val;
    char const* i18n;
}
stats_modes[] =
{
    { "total-ratio", N_("Total Ratio") },
    { "session-ratio", N_("Session Ratio") },
    { "total-transfer", N_("Total Transfer") },
    { "session-transfer", N_("Session Transfer") }
};

static void status_menu_toggled_cb(GtkCheckMenuItem* menu_item, gpointer vprivate)
{
    if (gtk_check_menu_item_get_active(menu_item))
    {
        PrivateData* p = vprivate;
        char const* val = g_object_get_data(G_OBJECT(menu_item), STATS_MODE);
        gtr_core_set_pref(p->core, TR_KEY_statusbar_stats, val);
    }
}

static void syncAltSpeedButton(PrivateData* p)
{
    char u[32];
    char d[32];
    char* str;
    char const* fmt;
    gboolean const b = gtr_pref_flag_get(TR_KEY_alt_speed_enabled);
    char const* stock = b ? "alt-speed-on" : "alt-speed-off";
    GtkWidget* w = p->alt_speed_button;

    tr_formatter_speed_KBps(u, gtr_pref_int_get(TR_KEY_alt_speed_up), sizeof(u));
    tr_formatter_speed_KBps(d, gtr_pref_int_get(TR_KEY_alt_speed_down), sizeof(d));
    fmt = b ? _("Click to disable Alternative Speed Limits\n (%1$s down, %2$s up)") :
        _("Click to enable Alternative Speed Limits\n (%1$s down, %2$s up)");
    str = g_strdup_printf(fmt, d, u);

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), b);
    gtk_image_set_from_icon_name(GTK_IMAGE(p->alt_speed_image), stock, GTK_ICON_SIZE_SMALL_TOOLBAR);
    gtk_button_set_alignment(GTK_BUTTON(w), 0.5, 0.5);
    gtk_widget_set_tooltip_text(w, str);

    g_free(str);
}

static void alt_speed_toggled_cb(GtkToggleButton* button, gpointer vprivate)
{
    PrivateData* p = vprivate;
    gboolean const b = gtk_toggle_button_get_active(button);
    gtr_core_set_pref_bool(p->core, TR_KEY_alt_speed_enabled, b);
}

/***
****  FILTER
***/

static void onFilterChanged(GtkToggleButton* button, gpointer vp)
{
    PrivateData* p = vp;
    GtkWidget* w = p->filter;
    gboolean const b = gtk_toggle_button_get_active(button);

    gtk_revealer_set_reveal_child(GTK_REVEALER(w), b);
}

static gboolean onAltSpeedToggledIdle(gpointer vp)
{
    PrivateData* p = vp;
    gboolean b = tr_sessionUsesAltSpeed(gtr_core_session(p->core));
    gtr_core_set_pref_bool(p->core, TR_KEY_alt_speed_enabled, b);

    return G_SOURCE_REMOVE;
}

static void onAltSpeedToggled(tr_session* s UNUSED, bool isEnabled UNUSED, bool byUser UNUSED, void* p)
{
    gdk_threads_add_idle(onAltSpeedToggledIdle, p);
}

/***
****  Speed limit menu
***/

#define DIRECTION_KEY "direction-key"
#define ENABLED_KEY "enabled-key"
#define SPEED_KEY "speed-key"

static void onSpeedToggled(GtkCheckMenuItem* check, gpointer vp)
{
    PrivateData* p = vp;
    GObject* o = G_OBJECT(check);
    gboolean isEnabled = g_object_get_data(o, ENABLED_KEY) != 0;
    tr_direction dir = GPOINTER_TO_INT(g_object_get_data(o, DIRECTION_KEY));
    tr_quark const key = dir == TR_UP ? TR_KEY_speed_limit_up_enabled : TR_KEY_speed_limit_down_enabled;

    if (gtk_check_menu_item_get_active(check))
    {
        gtr_core_set_pref_bool(p->core, key, isEnabled);
    }
}

static void onSpeedSet(GtkCheckMenuItem* check, gpointer vp)
{
    tr_quark key;
    PrivateData* p = vp;
    GObject* o = G_OBJECT(check);
    int const KBps = GPOINTER_TO_INT(g_object_get_data(o, SPEED_KEY));
    tr_direction dir = GPOINTER_TO_INT(g_object_get_data(o, DIRECTION_KEY));

    key = dir == TR_UP ? TR_KEY_speed_limit_up : TR_KEY_speed_limit_down;
    gtr_core_set_pref_int(p->core, key, KBps);

    key = dir == TR_UP ? TR_KEY_speed_limit_up_enabled : TR_KEY_speed_limit_down_enabled;
    gtr_core_set_pref_bool(p->core, key, TRUE);
}

/***
****  Speed limit menu
***/

#define RATIO_KEY "stock-ratio-index"

static double const stockRatios[] = { 0.25, 0.5, 0.75, 1, 1.5, 2, 3 };

static void onRatioToggled(GtkCheckMenuItem* check, gpointer vp)
{
    PrivateData* p = vp;

    if (gtk_check_menu_item_get_active(check))
    {
        gboolean f = g_object_get_data(G_OBJECT(check), ENABLED_KEY) != 0;
        gtr_core_set_pref_bool(p->core, TR_KEY_ratio_limit_enabled, f);
    }
}

static void onRatioSet(GtkCheckMenuItem* check, gpointer vp)
{
    PrivateData* p = vp;
    int i = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(check), RATIO_KEY));
    double const ratio = stockRatios[i];
    gtr_core_set_pref_double(p->core, TR_KEY_ratio_limit, ratio);
    gtr_core_set_pref_bool(p->core, TR_KEY_ratio_limit_enabled, TRUE);
}

/***
****  Option menu
***/

static void onOptionsClicked(GtkButton* button UNUSED, gpointer vp)
{
    char buf1[512];
    char buf2[512];
    gboolean b;
    GtkWidget* w;
    PrivateData* p = vp;

    w = p->speedlimit_on_item[TR_DOWN];
    tr_formatter_speed_KBps(buf1, gtr_pref_int_get(TR_KEY_speed_limit_down), sizeof(buf1));
    gtr_label_set_text(GTK_LABEL(gtk_bin_get_child(GTK_BIN(w))), buf1);

    b = gtr_pref_flag_get(TR_KEY_speed_limit_down_enabled);
    w = b ? p->speedlimit_on_item[TR_DOWN] : p->speedlimit_off_item[TR_DOWN];
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(w), TRUE);

    w = p->speedlimit_on_item[TR_UP];
    tr_formatter_speed_KBps(buf1, gtr_pref_int_get(TR_KEY_speed_limit_up), sizeof(buf1));
    gtr_label_set_text(GTK_LABEL(gtk_bin_get_child(GTK_BIN(w))), buf1);

    b = gtr_pref_flag_get(TR_KEY_speed_limit_up_enabled);
    w = b ? p->speedlimit_on_item[TR_UP] : p->speedlimit_off_item[TR_UP];
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(w), TRUE);

    tr_strlratio(buf1, gtr_pref_double_get(TR_KEY_ratio_limit), sizeof(buf1));
    g_snprintf(buf2, sizeof(buf2), _("Stop at Ratio (%s)"), buf1);
    gtr_label_set_text(GTK_LABEL(gtk_bin_get_child(GTK_BIN(p->ratio_on_item))), buf2);

    b = gtr_pref_flag_get(TR_KEY_ratio_limit_enabled);
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(b ? p->ratio_on_item : p->ratio_off_item), TRUE);

    gtk_menu_popup(GTK_MENU(p->options_menu), NULL, NULL, NULL, NULL, 0, gtk_get_current_event_time());
}

/***
****  PUBLIC
***/

GtkWidget* gtr_status_bar_new(PrivateData *p)
{
    GtkWidget* sibling = NULL;
    GtkWidget* ul_lb;
    GtkWidget* dl_lb;
    GtkWidget* w;
    GtkWidget* box;
    GtkWidget* pop;
    GtkWidget* box_wrapper;
    GtkCssProvider* css_provider;
    char* style =
        "GtkBox.status-bar {\n"
            "padding: 0 3px 0 3px;\n"
        "}\n"
        "GtkBox.status-bar GtkButton {\n"
            "border-image: 0;\n"
            "border-style: outset;\n"
            "border-width: 0px 1px;\n"
            "border-color: rgba(80,80,80,1.0);\n"
            "border-radius: 0px;\n"
            "padding: 0px;\n"
        "}\n"
        "GtkBox.status-bar GtkButton:first-child {\n"
            "border-width: 0px 1px 0px 0px;\n"
        "}\n"
        "GtkBox.status-bar GtkButton:last-child {\n"
            "border-width: 0px 0px 0px 1px;\n"
        "}\n"
        "GtkBox.status-bar GtkLabel {"
            "font-size: 9px;\n"
        "}";

    box_wrapper = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));

    gtk_style_context_add_class(gtk_widget_get_style_context(box_wrapper), "status-bar");

    css_provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css_provider, style, strlen(style), NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(css_provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    w = gtk_menu_button_new();
    gtk_button_set_image(GTK_BUTTON(w), gtk_image_new_from_icon_name("emblem-system-symbolic", GTK_ICON_SIZE_SMALL_TOOLBAR));
    gtk_button_set_relief(GTK_BUTTON(w), GTK_RELIEF_NONE);

    pop = gtk_popover_new(w);

    box = tr_limit_popover_new();

    gtk_container_add(GTK_CONTAINER(pop), GTK_WIDGET(box));
    gtk_widget_show_all(box);

    gtk_menu_button_set_popover(GTK_MENU_BUTTON(w), GTK_WIDGET(pop));

    gtk_box_pack_start(GTK_GRID(box_wrapper), w, FALSE, FALSE, 0);

    /* turtle */
    p->alt_speed_image = gtk_image_new();
    w = p->alt_speed_button = gtk_toggle_button_new();
    gtk_button_set_image(GTK_BUTTON(w), p->alt_speed_image);
    gtk_button_set_relief(GTK_BUTTON(w), GTK_RELIEF_NONE);
    g_signal_connect(w, "toggled", G_CALLBACK(alt_speed_toggled_cb), p);
    gtk_box_pack_start(box_wrapper, w, FALSE, FALSE, 0);

    /* download */
    w = dl_lb = gtk_label_new(NULL);
    p->dl_lb = GTK_LABEL(w);
    gtk_label_set_single_line_mode(p->dl_lb, TRUE);
    gtk_box_pack_start(box_wrapper, w, TRUE, FALSE, 0);

    /* upload */
    w = ul_lb = gtk_label_new(NULL);
    p->ul_lb = GTK_LABEL(w);
    gtk_label_set_single_line_mode(p->ul_lb, TRUE);
    gtk_box_pack_start(box_wrapper, w, TRUE, FALSE, 0);

    /* ratio */
    w = gtk_label_new(NULL);
    g_object_set(G_OBJECT(w), "margin-left", GUI_PAD_BIG, NULL);
    p->stats_lb = GTK_LABEL(w);
    gtk_label_set_single_line_mode(p->stats_lb, TRUE);
    gtk_box_pack_start(box_wrapper, w, TRUE, FALSE, 0);

    w = gtk_button_new_from_icon_name("ratio", GTK_ICON_SIZE_SMALL_TOOLBAR);
    gtk_widget_set_tooltip_text(w, _("Statistics"));
    gtk_button_set_relief(GTK_BUTTON(w), GTK_RELIEF_NONE);
    g_signal_connect(w, "clicked", G_CALLBACK(onYinYangReleased), p);
    gtk_box_pack_end(box_wrapper, w, FALSE, FALSE, 0);

    {
        /* this is to determine the maximum width/height for the label */
        int w = 0;
        int h = 0;
        PangoLayout* pango_layout;
        pango_layout = gtk_widget_create_pango_layout(ul_lb, "999.99 kB/s");
        pango_layout_get_pixel_size(pango_layout, &w, &h);
        gtk_widget_set_size_request(ul_lb, w, h);
        gtk_widget_set_size_request(dl_lb, w, h);
        gtk_misc_set_alignment(GTK_MISC(ul_lb), 1.0, 0.5);
        gtk_misc_set_alignment(GTK_MISC(dl_lb), 1.0, 0.5);
        g_object_unref(G_OBJECT(pango_layout));
    }

    return box_wrapper;
}

GtkWidget* gtr_window_new(GtkApplication* app, TrCore* core)
{
    char const* pch;
    char const* style;
    PrivateData* p;
    GtkWidget* sibling = NULL;
    GtkWidget* toolbar;
    GtkWidget* filter;
    GtkWidget* list;
    GtkWidget* status;
    GtkWidget* vbox;
    GtkWidget* tbox;
    GtkWidget* w;
    GtkWidget* pop;
    GtkWidget* self;
    GtkWidget* menu;
    GtkWidget* button;
    GtkWidget* toggle_button;
    GtkWidget* image;
    GtkWidget* grid_w;
    GtkWindow* win;
    GtkCssProvider* css_provider;
    GSList* l;
    GtkGrid* grid;
    GMenuModel* model;

    p = g_new0(PrivateData, 1);

    /* make the window */
    self = gtk_application_window_new(app);
    g_object_set_qdata_full(G_OBJECT(self), private_data_quark(), p, privateFree);
    win = GTK_WINDOW(self);
    gtk_window_set_role(win, "tr-main");
    gtk_window_set_title(win, g_get_application_name());
    gtk_widget_set_size_request(win, 620, 220);
    gtk_window_set_default_size(win, gtr_pref_int_get(TR_KEY_main_window_width), gtr_pref_int_get(TR_KEY_main_window_height));
    gtk_window_move(win, gtr_pref_int_get(TR_KEY_main_window_x), gtr_pref_int_get(TR_KEY_main_window_y));

    if (gtr_pref_flag_get(TR_KEY_main_window_is_maximized))
    {
        gtk_window_maximize(win);
    }

    /* Add style provider to the window. */
    /* Please move it to separate .css file if you’re adding more styles here. */
    style = ".tr-workarea {border-width: 0px 0px 1px 0px; border-style: solid; border-radius: 0;}";
    css_provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css_provider, style, strlen(style), NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(css_provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    /* window's main container */
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(self), vbox);

    /* toolbar */
    toolbar = gtk_header_bar_new();
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(toolbar), TRUE);
    gtk_header_bar_set_title(GTK_HEADER_BAR(toolbar), g_get_application_name());
    gtk_header_bar_set_subtitle(GTK_HEADER_BAR(toolbar), "All Torrents");
    gtk_window_set_titlebar(GTK_WINDOW(win), toolbar);

    /* new document actions */

    button = gtk_button_new_from_icon_name("document-send-symbolic", GTK_ICON_SIZE_MENU);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(toolbar), button);

    button = gtk_button_new_from_icon_name("document-open-symbolic", GTK_ICON_SIZE_MENU);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(toolbar), button);

    button = gtk_button_new_from_icon_name("user-trash-symbolic", GTK_ICON_SIZE_MENU);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(toolbar), button);

    /*
    gtk_style_context_add_class(gtk_widget_get_style_context(tbox), GTK_STYLE_CLASS_RAISED);
    gtk_style_context_add_class(gtk_widget_get_style_context(tbox), GTK_STYLE_CLASS_LINKED);

    gtk_header_bar_pack_start(GTK_HEADER_BAR(toolbar), tbox);
    */

    /* gear */

    button = gtk_menu_button_new();
    image = gtk_image_new_from_icon_name("emblem-system-symbolic", GTK_ICON_SIZE_MENU);
    gtk_container_add(GTK_CONTAINER(button), image);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(toolbar), button);
    model = gtr_action_get_menu_model("main-window-popup");
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(button), model);

    toggle_button = gtk_toggle_button_new();
    image = gtk_image_new_from_icon_name("edit-find-symbolic", GTK_ICON_SIZE_MENU);
    gtk_container_add(GTK_CONTAINER(toggle_button), image);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(toolbar), toggle_button);
    g_signal_connect(toggle_button, "toggled", G_CALLBACK(onFilterChanged), p);

    /* selection actions */

    button = gtk_button_new_from_icon_name("media-playback-pause-symbolic", GTK_ICON_SIZE_MENU);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(toolbar), button);

    /* filter */
    w = filter = p->filter = gtr_filter_bar_new(gtr_core_session(core), gtr_core_model(core), &p->filter_model,
        GTK_HEADER_BAR(toolbar));
    // gtk_revealer_set_reveal_child(GTK_REVEALER(filter), TRUE);
    gtk_container_set_border_width(GTK_CONTAINER(w), GUI_PAD_SMALL);

    /* status menu */
    menu = p->status_menu = gtk_menu_new();
    l = NULL;
    pch = gtr_pref_string_get(TR_KEY_statusbar_stats);

    for (size_t i = 0; i < G_N_ELEMENTS(stats_modes); ++i)
    {
        char const* val = stats_modes[i].val;
        w = gtk_radio_menu_item_new_with_label(l, _(stats_modes[i].i18n));
        l = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(w));
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(w), g_strcmp0(val, pch) == 0);
        g_object_set_data(G_OBJECT(w), STATS_MODE, (gpointer)stats_modes[i].val);
        g_signal_connect(w, "toggled", G_CALLBACK(status_menu_toggled_cb), p);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), w);
        gtk_widget_show(w);
    }

    /**
    *** Statusbar
    **/

    status = p->status = gtr_status_bar_new(p); // gtk_grid_new();

    /* workarea */
    p->view = makeview(p);
    w = list = p->scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(w), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(w), GTK_SHADOW_OUT);
    gtk_style_context_add_class(gtk_widget_get_style_context(w), "tr-workarea");
    gtk_container_add(GTK_CONTAINER(w), p->view);

    /* lay out the widgets */
    gtk_box_pack_start(GTK_BOX(vbox), filter, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), list, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), status, FALSE, FALSE, 0);

    /* show the window */
    gtk_widget_show_all(GTK_WIDGET(win));

    /* listen for prefs changes that affect the window */
    p->core = core;
    printf("should call prefsChanged\n");
    prefsChanged(core, TR_KEY_compact_view, self);
    prefsChanged(core, TR_KEY_show_filterbar, self);
    prefsChanged(core, TR_KEY_show_statusbar, self);
    prefsChanged(core, TR_KEY_statusbar_stats, self);
    prefsChanged(core, TR_KEY_show_toolbar, self);
    prefsChanged(core, TR_KEY_alt_speed_enabled, self);
    p->pref_handler_id = g_signal_connect(core, "prefs-changed", G_CALLBACK(prefsChanged), self);

    tr_sessionSetAltSpeedFunc(gtr_core_session(core), onAltSpeedToggled, p);

    gtr_window_refresh(GTK_WINDOW(self));
    return self;
}

static void updateStats(PrivateData* p)
{
    char const* pch;
    char up[32];
    char down[32];
    char ratio[32];
    char buf[512];
    struct tr_session_stats stats;
    tr_session* session = gtr_core_session(p->core);

    /* update the stats */
    pch = gtr_pref_string_get(TR_KEY_statusbar_stats);

    if (g_strcmp0(pch, "session-ratio") == 0)
    {
        tr_sessionGetStats(session, &stats);
        tr_strlratio(ratio, stats.ratio, sizeof(ratio));
        g_snprintf(buf, sizeof(buf), _("Ratio: %s"), ratio);
    }
    else if (g_strcmp0(pch, "session-transfer") == 0)
    {
        tr_sessionGetStats(session, &stats);
        tr_strlsize(up, stats.uploadedBytes, sizeof(up));
        tr_strlsize(down, stats.downloadedBytes, sizeof(down));
        /* Translators: "size|" is here for disambiguation. Please remove it from your translation.
           %1$s is the size of the data we've downloaded
           %2$s is the size of the data we've uploaded */
        g_snprintf(buf, sizeof(buf), Q_("Down: %1$s, Up: %2$s"), down, up);
    }
    else if (g_strcmp0(pch, "total-transfer") == 0)
    {
        tr_sessionGetCumulativeStats(session, &stats);
        tr_strlsize(up, stats.uploadedBytes, sizeof(up));
        tr_strlsize(down, stats.downloadedBytes, sizeof(down));
        /* Translators: "size|" is here for disambiguation. Please remove it from your translation.
           %1$s is the size of the data we've downloaded
           %2$s is the size of the data we've uploaded */
        g_snprintf(buf, sizeof(buf), Q_("size|Down: %1$s, Up: %2$s"), down, up);
    }
    else /* default is total-ratio */
    {
        tr_sessionGetCumulativeStats(session, &stats);
        tr_strlratio(ratio, stats.ratio, sizeof(ratio));
        g_snprintf(buf, sizeof(buf), _("Ratio: %s"), ratio);
    }

    gtr_label_set_text(p->stats_lb, buf);
}

static void updateSpeeds(PrivateData* p)
{
    tr_session* session = gtr_core_session(p->core);

    if (session != NULL)
    {
        char text_str[256];
        char speed_str[128];
        double upSpeed = 0;
        double downSpeed = 0;
        int upCount = 0;
        int downCount = 0;
        GtkTreeIter iter;
        GtkTreeModel* model = gtr_core_model(p->core);

        if (gtk_tree_model_iter_nth_child(model, &iter, NULL, 0))
        {
            do
            {
                int uc;
                int dc;
                double us;
                double ds;
                gtk_tree_model_get(model, &iter,
                    MC_SPEED_UP, &us,
                    MC_SPEED_DOWN, &ds,
                    MC_ACTIVE_PEERS_UP, &uc,
                    MC_ACTIVE_PEERS_DOWN, &dc,
                    -1);
                upSpeed += us;
                upCount += uc;
                downSpeed += ds;
                downCount += dc;
            }
            while (gtk_tree_model_iter_next(model, &iter));
        }

        tr_formatter_speed_KBps(speed_str, downSpeed, sizeof(speed_str));
        g_snprintf(text_str, sizeof(text_str), "%s %s", speed_str, gtr_get_unicode_string(GTR_UNICODE_DOWN));
        gtr_label_set_text(p->dl_lb, text_str);
        gtk_widget_set_visible(GTK_WIDGET(p->dl_lb), (downCount > 0));

        tr_formatter_speed_KBps(speed_str, upSpeed, sizeof(speed_str));
        g_snprintf(text_str, sizeof(text_str), "%s %s", speed_str, gtr_get_unicode_string(GTR_UNICODE_UP));
        gtr_label_set_text(p->ul_lb, text_str);
        gtk_widget_set_visible(GTK_WIDGET(p->ul_lb), ((downCount > 0) || (upCount > 0)));
    }
}

void gtr_window_refresh(GtkWindow* self)
{
    PrivateData* p = get_private_data(self);

    if (p != NULL && p->core != NULL && gtr_core_session(p->core) != NULL)
    {
        updateSpeeds(p);
        updateStats(p);
    }
}

GtkTreeSelection* gtr_window_get_selection(GtkWindow* w)
{
    return get_private_data(w)->selection;
}

void gtr_window_set_busy(GtkWindow* win, gboolean isBusy)
{
    GtkWidget* w = GTK_WIDGET(win);

    if (w != NULL && gtk_widget_get_realized(w))
    {
        GdkDisplay* display = gtk_widget_get_display(w);
        GdkCursor* cursor = isBusy ? gdk_cursor_new_for_display(display, GDK_WATCH) : NULL;

        gdk_window_set_cursor(gtk_widget_get_window(w), cursor);
        gdk_display_flush(display);

        g_clear_object(&cursor);
    }
}
