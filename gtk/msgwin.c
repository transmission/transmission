/*
 * This file Copyright (C) 2008-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include <libtransmission/transmission.h>
#include <libtransmission/log.h>

#include "conf.h"
#include "hig.h"
#include "msgwin.h"
#include "tr-core.h"
#include "tr-prefs.h"
#include "util.h"

enum
{
  COL_SEQUENCE,
  COL_NAME,
  COL_MESSAGE,
  COL_TR_MSG,
  N_COLUMNS
};

struct MsgData
{
  TrCore        * core;
  GtkTreeView   * view;
  GtkListStore  * store;
  GtkTreeModel  * filter;
  GtkTreeModel  * sort;
  tr_log_level    maxLevel;
  gboolean        isPaused;
  guint           refresh_tag;
};

static struct tr_log_message * myTail = NULL;
static struct tr_log_message * myHead = NULL;

/****
*****
****/

/* is the user looking at the latest messages? */
static gboolean
is_pinned_to_new (struct MsgData * data)
{
  gboolean pinned_to_new = FALSE;

  if (data->view == NULL)
    {
      pinned_to_new = TRUE;
    }
  else
    {
      GtkTreePath * last_visible;
      if (gtk_tree_view_get_visible_range (data->view, NULL, &last_visible))
        {
          GtkTreeIter iter;
          const int row_count = gtk_tree_model_iter_n_children (data->sort, NULL);
          if (gtk_tree_model_iter_nth_child (data->sort, &iter, NULL, row_count-1))
            {
              GtkTreePath * last_row = gtk_tree_model_get_path (data->sort, &iter);
              pinned_to_new = !gtk_tree_path_compare (last_visible, last_row);
              gtk_tree_path_free (last_row);
            }
          gtk_tree_path_free (last_visible);
        }
    }

  return pinned_to_new;
}

static void
scroll_to_bottom (struct MsgData * data)
{
  if (data->sort != NULL)
    {
      GtkTreeIter iter;
      const int row_count = gtk_tree_model_iter_n_children (data->sort, NULL);
      if (gtk_tree_model_iter_nth_child (data->sort, &iter, NULL, row_count-1))
        {
          GtkTreePath * last_row = gtk_tree_model_get_path (data->sort, &iter);
          gtk_tree_view_scroll_to_cell (data->view, last_row, NULL, TRUE, 1, 0);
          gtk_tree_path_free (last_row);
        }
    }
}

/****
*****
****/

static void
level_combo_changed_cb (GtkComboBox * combo_box, gpointer gdata)
{
  struct MsgData * data = gdata;
  const int level = gtr_combo_box_get_active_enum (combo_box);
  const gboolean pinned_to_new = is_pinned_to_new (data);

  tr_logSetLevel (level);
  gtr_core_set_pref_int (data->core, TR_KEY_message_level, level);
  data->maxLevel = level;
  gtk_tree_model_filter_refilter (GTK_TREE_MODEL_FILTER (data->filter));

  if (pinned_to_new)
    scroll_to_bottom (data);
}

/* similar to asctime, but is utf8-clean */
static char*
gtr_localtime (time_t time)
{
  char buf[256], *eoln;
  const struct tm tm = *localtime (&time);

  g_strlcpy (buf, asctime (&tm), sizeof (buf));
  if ((eoln = strchr (buf, '\n')))
    *eoln = '\0';

  return g_locale_to_utf8 (buf, -1, NULL, NULL, NULL);
}

static void
doSave (GtkWindow * parent, struct MsgData * data, const char * filename)
{
  FILE * fp = fopen (filename, "w+");

  if (!fp)
    {
      GtkWidget * w = gtk_message_dialog_new (parent, 0, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, _("Couldn't save \"%s\""), filename);
      gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (w), "%s", g_strerror (errno));
      g_signal_connect_swapped (w, "response", G_CALLBACK (gtk_widget_destroy), w);
      gtk_widget_show (w);
    }
  else
    {
      GtkTreeIter iter;
      GtkTreeModel * model = GTK_TREE_MODEL (data->sort);
      if (gtk_tree_model_iter_children (model, &iter, NULL)) do
        {
          char * date;
          const char * levelStr;
          const struct tr_log_message * node;

          gtk_tree_model_get (model, &iter, COL_TR_MSG, &node, -1);
          date = gtr_localtime (node->when);
          switch (node->level)
           {
             case TR_LOG_DEBUG:
               levelStr = "debug";
               break;

             case TR_LOG_ERROR:
               levelStr = "error";
               break;

             default:
               levelStr = "     ";
               break;
            }

          fprintf (fp, "%s\t%s\t%s\t%s\n", date, levelStr,
                   (node->name ? node->name : ""),
                   (node->message ? node->message : ""));
          g_free (date);
        }
      while (gtk_tree_model_iter_next (model, &iter));

      fclose (fp);
    }
}

static void
onSaveDialogResponse (GtkWidget * d, int response, gpointer data)
{
  if (response == GTK_RESPONSE_ACCEPT)
    {
      char * file = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (d));
      doSave (GTK_WINDOW (d), data, file);
      g_free (file);
    }

  gtk_widget_destroy (d);
}

static void
onSaveRequest (GtkWidget * w,
               gpointer    data)
{
  GtkWindow * window = GTK_WINDOW (gtk_widget_get_toplevel (w));
  GtkWidget * d = gtk_file_chooser_dialog_new (_("Save Log"), window,
                                               GTK_FILE_CHOOSER_ACTION_SAVE,
                                               GTK_STOCK_CANCEL,
                                               GTK_RESPONSE_CANCEL,
                                               GTK_STOCK_SAVE,
                                               GTK_RESPONSE_ACCEPT,
                                               NULL);

  gtk_dialog_set_alternative_button_order (GTK_DIALOG (d),
                                           GTK_RESPONSE_ACCEPT,
                                           GTK_RESPONSE_CANCEL,
                                           -1);
  g_signal_connect (d, "response",
                    G_CALLBACK (onSaveDialogResponse), data);
  gtk_widget_show (d);
}

static void
onClearRequest (GtkWidget * w UNUSED, gpointer gdata)
{
  struct MsgData * data = gdata;

  gtk_list_store_clear (data->store);
  tr_logFreeQueue (myHead);
  myHead = myTail = NULL;
}

static void
onPauseToggled (GtkToggleToolButton * w, gpointer gdata)
{
  struct MsgData * data = gdata;

  data->isPaused = gtk_toggle_tool_button_get_active (w);
}

static const char*
getForegroundColor (int msgLevel)
{
  switch (msgLevel)
    {
      case TR_LOG_DEBUG: return "forestgreen";
      case TR_LOG_INFO:  return "black";
      case TR_LOG_ERROR: return "red";
      default: g_assert_not_reached (); return "black";
    }
}

static void
renderText (GtkTreeViewColumn  * column UNUSED,
            GtkCellRenderer *           renderer,
            GtkTreeModel *              tree_model,
            GtkTreeIter *               iter,
            gpointer                    gcol)
{
  const int col = GPOINTER_TO_INT (gcol);
  char * str = NULL;
  const struct tr_log_message * node;

  gtk_tree_model_get (tree_model, iter, col, &str, COL_TR_MSG, &node, -1);
  g_object_set (renderer, "text", str,
                          "foreground", getForegroundColor (node->level),
                          "ellipsize", PANGO_ELLIPSIZE_END,
                          NULL);
}

static void
renderTime (GtkTreeViewColumn  * column UNUSED,
            GtkCellRenderer *           renderer,
            GtkTreeModel *              tree_model,
            GtkTreeIter *               iter,
            gpointer             data   UNUSED)
{
  struct tm tm;
  char buf[16];
  const struct tr_log_message * node;

  gtk_tree_model_get (tree_model, iter, COL_TR_MSG, &node, -1);
  tm = *localtime (&node->when);
  g_snprintf (buf, sizeof (buf), "%02d:%02d:%02d", tm.tm_hour, tm.tm_min,
              tm.tm_sec);
  g_object_set (renderer, "text", buf,
                          "foreground", getForegroundColor (node->level),
                          NULL);
}

static void
appendColumn (GtkTreeView * view, int col)
{
  GtkCellRenderer *   r;
  GtkTreeViewColumn * c;
  const char * title = NULL;

  switch (col)
    {
      case COL_SEQUENCE:
        title = _("Time");
        break;

      /* noun. column title for a list */
      case COL_NAME:
        title = _("Name");
        break;

      /* noun. column title for a list */
      case COL_MESSAGE:
        title = _("Message");
        break;

      default:
        g_assert_not_reached ();
    }

  switch (col)
    {
      case COL_NAME:
        r = gtk_cell_renderer_text_new ();
        c = gtk_tree_view_column_new_with_attributes (title, r, NULL);
        gtk_tree_view_column_set_cell_data_func (c, r, renderText,
                                                 GINT_TO_POINTER (col), NULL);
        gtk_tree_view_column_set_sizing (c, GTK_TREE_VIEW_COLUMN_FIXED);
        gtk_tree_view_column_set_fixed_width (c, 200);
        gtk_tree_view_column_set_resizable (c, TRUE);
        break;

      case COL_MESSAGE:
        r = gtk_cell_renderer_text_new ();
        c = gtk_tree_view_column_new_with_attributes (title, r, NULL);
        gtk_tree_view_column_set_cell_data_func (c, r, renderText,
                                                 GINT_TO_POINTER (col), NULL);
        gtk_tree_view_column_set_sizing (c, GTK_TREE_VIEW_COLUMN_FIXED);
        gtk_tree_view_column_set_fixed_width (c, 500);
        gtk_tree_view_column_set_resizable (c, TRUE);
        break;

      case COL_SEQUENCE:
        r = gtk_cell_renderer_text_new ();
        c = gtk_tree_view_column_new_with_attributes (title, r, NULL);
        gtk_tree_view_column_set_cell_data_func (c, r, renderTime, NULL, NULL);
        gtk_tree_view_column_set_resizable (c, TRUE);
        break;

      default:
        g_assert_not_reached ();
        break;
    }

  gtk_tree_view_append_column (view, c);
}

static gboolean
isRowVisible (GtkTreeModel * model, GtkTreeIter * iter, gpointer gdata)
{
  const struct tr_log_message * node;
  const struct MsgData * data = gdata;

  gtk_tree_model_get (model, iter, COL_TR_MSG, &node, -1);

  return node->level <= data->maxLevel;
}

static void
onWindowDestroyed (gpointer gdata, GObject * deadWindow UNUSED)
{
  struct MsgData * data = gdata;

  g_source_remove (data->refresh_tag);

  g_free (data);
}

static tr_log_message *
addMessages (GtkListStore * store, struct tr_log_message * head)
{
  tr_log_message * i;
  static unsigned int sequence = 0;
  const char * default_name = g_get_application_name ();

  for (i=head; i && i->next; i=i->next)
    {
      const char * name = i->name ? i->name : default_name;

      gtk_list_store_insert_with_values (store, NULL, 0,
                                         COL_TR_MSG, i,
                                         COL_NAME, name,
                                         COL_MESSAGE, i->message,
                                         COL_SEQUENCE, ++sequence,
                                         -1);

      /* if it's an error message, dump it to the terminal too */
      if (i->level == TR_LOG_ERROR)
        {
          GString * gstr = g_string_sized_new (512);
          g_string_append_printf (gstr, "%s:%d %s", i->file, i->line, i->message);
          if (i->name != NULL)
            g_string_append_printf (gstr, " (%s)", i->name);
          g_warning ("%s", gstr->str);
          g_string_free (gstr, TRUE);
        }
    }

  return i; /* tail */
}

static gboolean
onRefresh (gpointer gdata)
{
  struct MsgData * data = gdata;
  const gboolean pinned_to_new = is_pinned_to_new (data);

  if (!data->isPaused)
    {
      tr_log_message * msgs = tr_logGetQueue ();
      if (msgs)
        {
          /* add the new messages and append them to the end of
           * our persistent list */
          tr_log_message * tail = addMessages (data->store, msgs);
          if (myTail)
              myTail->next = msgs;
          else
              myHead = msgs;
          myTail = tail;
        }

      if (pinned_to_new)
        scroll_to_bottom (data);
    }

  return G_SOURCE_CONTINUE;
}

static GtkWidget*
debug_level_combo_new (void)
{
  GtkWidget * w = gtr_combo_box_new_enum (_("Error"),       TR_LOG_ERROR,
                                          _("Information"), TR_LOG_INFO,
                                          _("Debug"),       TR_LOG_DEBUG,
                                          NULL);
  gtr_combo_box_set_active_enum (GTK_COMBO_BOX (w), gtr_pref_int_get (TR_KEY_message_level));
  return w;
}

/**
***  Public Functions
**/

GtkWidget *
gtr_message_log_window_new (GtkWindow * parent, TrCore * core)
{
  GtkWidget * win;
  GtkWidget * vbox;
  GtkWidget * toolbar;
  GtkWidget * w;
  GtkWidget * view;
  GtkToolItem * item;
  struct MsgData * data;

  data = g_new0 (struct MsgData, 1);
  data->core = core;

  win = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_window_set_transient_for (GTK_WINDOW (win), parent);
  gtk_window_set_title (GTK_WINDOW (win), _("Message Log"));
  gtk_window_set_default_size (GTK_WINDOW (win), 560, 350);
  gtk_window_set_role (GTK_WINDOW (win), "message-log");
  vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

  /**
  ***  toolbar
  **/

  toolbar = gtk_toolbar_new ();
  gtk_toolbar_set_style (GTK_TOOLBAR (toolbar), GTK_TOOLBAR_BOTH_HORIZ);
  gtk_style_context_add_class (gtk_widget_get_style_context (toolbar),
                               GTK_STYLE_CLASS_PRIMARY_TOOLBAR);

  item = gtk_tool_button_new_from_stock (GTK_STOCK_SAVE_AS);
  g_object_set (G_OBJECT (item), "is-important", TRUE, NULL);
  g_signal_connect (item, "clicked", G_CALLBACK (onSaveRequest), data);
  gtk_toolbar_insert (GTK_TOOLBAR (toolbar), item, -1);

  item = gtk_tool_button_new_from_stock (GTK_STOCK_CLEAR);
  g_object_set (G_OBJECT (item), "is-important", TRUE, NULL);
  g_signal_connect (item, "clicked", G_CALLBACK (onClearRequest), data);
  gtk_toolbar_insert (GTK_TOOLBAR (toolbar), item, -1);

  item = gtk_separator_tool_item_new ();
  gtk_toolbar_insert (GTK_TOOLBAR (toolbar), item, -1);

  item = gtk_toggle_tool_button_new_from_stock (GTK_STOCK_MEDIA_PAUSE);
  g_object_set (G_OBJECT (item), "is-important", TRUE, NULL);
  g_signal_connect (item, "toggled", G_CALLBACK (onPauseToggled), data);
  gtk_toolbar_insert (GTK_TOOLBAR (toolbar), item, -1);

  item = gtk_separator_tool_item_new ();
  gtk_toolbar_insert (GTK_TOOLBAR (toolbar), item, -1);

  w = gtk_label_new (_("Level"));
  gtk_misc_set_padding (GTK_MISC (w), GUI_PAD, 0);
  item = gtk_tool_item_new ();
  gtk_container_add (GTK_CONTAINER (item), w);
  gtk_toolbar_insert (GTK_TOOLBAR (toolbar), item, -1);

  w = debug_level_combo_new ();
  g_signal_connect (w, "changed", G_CALLBACK (level_combo_changed_cb), data);
  item = gtk_tool_item_new ();
  gtk_container_add (GTK_CONTAINER (item), w);
  gtk_toolbar_insert (GTK_TOOLBAR (toolbar), item, -1);

  gtk_box_pack_start (GTK_BOX (vbox), toolbar, FALSE, FALSE, 0);

  /**
  ***  messages
  **/

  data->store = gtk_list_store_new (N_COLUMNS,
                                    G_TYPE_UINT,       /* sequence */
                                    G_TYPE_POINTER,    /* category */
                                    G_TYPE_POINTER,    /* message */
                                    G_TYPE_POINTER);   /* struct tr_log_message */

  addMessages (data->store, myHead);
  onRefresh (data); /* much faster to populate *before* it has listeners */

  data->filter = gtk_tree_model_filter_new (GTK_TREE_MODEL (data->store), NULL);
  data->sort = gtk_tree_model_sort_new_with_model (data->filter);
  g_object_unref (data->filter);
  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (data->sort),
                                        COL_SEQUENCE,
                                        GTK_SORT_ASCENDING);
  data->maxLevel = gtr_pref_int_get (TR_KEY_message_level);
  gtk_tree_model_filter_set_visible_func (GTK_TREE_MODEL_FILTER (data->filter),
                                          isRowVisible, data, NULL);


  view = gtk_tree_view_new_with_model (data->sort);
  g_object_unref (data->sort);
  g_signal_connect (view, "button-release-event",
                    G_CALLBACK (on_tree_view_button_released), NULL);
  data->view = GTK_TREE_VIEW (view);
  gtk_tree_view_set_rules_hint (data->view, TRUE);
  appendColumn (data->view, COL_SEQUENCE);
  appendColumn (data->view, COL_NAME);
  appendColumn (data->view, COL_MESSAGE);
  w = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (w),
                                  GTK_POLICY_AUTOMATIC,
                                  GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (w),
                                       GTK_SHADOW_IN);
  gtk_container_add (GTK_CONTAINER (w), view);
  gtk_box_pack_start (GTK_BOX (vbox), w, TRUE, TRUE, 0);
  gtk_container_add (GTK_CONTAINER (win), vbox);

  data->refresh_tag = gdk_threads_add_timeout_seconds (SECONDARY_WINDOW_REFRESH_INTERVAL_SECONDS, onRefresh, data);
  g_object_weak_ref (G_OBJECT (win), onWindowDestroyed, data);

  scroll_to_bottom (data);
  gtk_widget_show_all (win);
  return win;
}

