/*
 * This file Copyright (C) 2007-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include <libtransmission/transmission.h>
#include <libtransmission/makemeta.h>
#include <libtransmission/utils.h> /* tr_formatter_mem_B () */

#include "hig.h"
#include "makemeta-ui.h"
#include "tr-core.h"
#include "tr-prefs.h"
#include "util.h"

#define FILE_CHOSEN_KEY "file-is-chosen"

typedef struct
{
  char * target;
  guint progress_tag;
  GtkWidget * file_radio;
  GtkWidget * file_chooser;
  GtkWidget * folder_radio;
  GtkWidget * folder_chooser;
  GtkWidget * pieces_lb;
  GtkWidget * destination_chooser;
  GtkWidget * comment_check;
  GtkWidget * comment_entry;
  GtkWidget * private_check;
  GtkWidget * progress_label;
  GtkWidget * progress_bar;
  GtkWidget * progress_dialog;
  GtkWidget * dialog;
  GtkTextBuffer * announce_text_buffer;
  TrCore * core;
  tr_metainfo_builder *  builder;
}
MakeMetaUI;

static void
freeMetaUI (gpointer p)
{
  MakeMetaUI * ui = p;

  tr_metaInfoBuilderFree (ui->builder);
  g_free (ui->target);
  memset (ui, ~0, sizeof (MakeMetaUI));
  g_free (ui);
}

static gboolean
onProgressDialogRefresh (gpointer data)
{
  char * str = NULL;
  MakeMetaUI * ui = data;
  const tr_metainfo_builder * b = ui->builder;
  GtkDialog * d = GTK_DIALOG (ui->progress_dialog);
  GtkProgressBar * p = GTK_PROGRESS_BAR (ui->progress_bar);
  const double fraction = b->pieceCount ? ((double)b->pieceIndex / b->pieceCount) : 0;
  char * base = g_path_get_basename (b->top);

  /* progress label */
  if (!b->isDone)
    str = g_strdup_printf (_("Creating \"%s\""), base);
  else if (b->result == TR_MAKEMETA_OK)
    str = g_strdup_printf (_("Created \"%s\"!"), base);
  else if (b->result == TR_MAKEMETA_URL)
    str = g_strdup_printf (_("Error: invalid announce URL \"%s\""), b->errfile);
  else if (b->result == TR_MAKEMETA_CANCELLED)
    str = g_strdup_printf (_("Cancelled"));
  else if (b->result == TR_MAKEMETA_IO_READ)
    str = g_strdup_printf (_("Error reading \"%s\": %s"), b->errfile, g_strerror (b->my_errno));
  else if (b->result == TR_MAKEMETA_IO_WRITE)
    str = g_strdup_printf (_("Error writing \"%s\": %s"), b->errfile, g_strerror (b->my_errno));
  else
    g_assert_not_reached ();

  if (str != NULL)
    {
      gtr_label_set_text (GTK_LABEL (ui->progress_label), str);
      g_free (str);
    }

  /* progress bar */
  if (!b->pieceIndex)
    {
      str = g_strdup ("");
    }
  else
    {
      char sizebuf[128];
      tr_strlsize (sizebuf, (uint64_t)b->pieceIndex *
                            (uint64_t)b->pieceSize, sizeof (sizebuf));
      /* how much data we've scanned through to generate checksums */
      str = g_strdup_printf (_("Scanned %s"), sizebuf);
    }
  gtk_progress_bar_set_fraction (p, fraction);
  gtk_progress_bar_set_text (p, str);
  g_free (str);

  /* buttons */
  gtk_dialog_set_response_sensitive (d, GTK_RESPONSE_CANCEL, !b->isDone);
  gtk_dialog_set_response_sensitive (d, GTK_RESPONSE_CLOSE, b->isDone);
  gtk_dialog_set_response_sensitive (d, GTK_RESPONSE_ACCEPT, b->isDone && !b->result);

  g_free (base);
  return G_SOURCE_CONTINUE;
}

static void
onProgressDialogDestroyed (gpointer data, GObject * dead UNUSED)
{
  MakeMetaUI * ui = data;
  g_source_remove (ui->progress_tag);
}

static void
addTorrent (MakeMetaUI * ui)
{
  char * path;
  const tr_metainfo_builder * b = ui->builder;
  tr_ctor * ctor = tr_ctorNew (gtr_core_session (ui->core));

  tr_ctorSetMetainfoFromFile (ctor, ui->target);

  path = g_path_get_dirname (b->top);
  tr_ctorSetDownloadDir (ctor, TR_FORCE, path);
  g_free (path);

  gtr_core_add_ctor (ui->core, ctor);
}

static void
onProgressDialogResponse (GtkDialog * d, int response, gpointer data)
{
  MakeMetaUI * ui = data;

  switch (response)
    {
      case GTK_RESPONSE_CANCEL:
        ui->builder->abortFlag = TRUE;
        gtk_widget_destroy (GTK_WIDGET (d));
        break;

      case GTK_RESPONSE_ACCEPT:
        addTorrent (ui);
        /* fall-through */

      case GTK_RESPONSE_CLOSE:
        gtk_widget_destroy (ui->builder->result ? GTK_WIDGET (d) : ui->dialog);
        break;

      default:
        g_assert (0 && "unhandled response");
    }
}

static void
makeProgressDialog (GtkWidget * parent, MakeMetaUI * ui)
{
  GtkWidget *d, *l, *w, *v, *fr;

  d = gtk_dialog_new_with_buttons (_("New Torrent"),
        GTK_WINDOW (parent),
        GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
        GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
        GTK_STOCK_ADD, GTK_RESPONSE_ACCEPT,
        NULL);
  ui->progress_dialog = d;
  g_signal_connect (d, "response", G_CALLBACK (onProgressDialogResponse), ui);

  fr = gtk_frame_new (NULL);
  gtk_container_set_border_width (GTK_CONTAINER (fr), GUI_PAD_BIG);
  gtk_frame_set_shadow_type (GTK_FRAME (fr), GTK_SHADOW_NONE);
  v = gtk_box_new (GTK_ORIENTATION_VERTICAL, GUI_PAD);
  gtk_container_add (GTK_CONTAINER (fr), v);

  l = gtk_label_new (_("Creating torrent…"));
  gtk_misc_set_alignment (GTK_MISC (l), 0.0, 0.5);
  gtk_label_set_justify (GTK_LABEL (l), GTK_JUSTIFY_LEFT);
  ui->progress_label = l;
  gtk_box_pack_start (GTK_BOX (v), l, FALSE, FALSE, 0);

  w = gtk_progress_bar_new ();
  ui->progress_bar = w;
  gtk_box_pack_start (GTK_BOX (v), w, FALSE, FALSE, 0);

  ui->progress_tag = gdk_threads_add_timeout_seconds (SECONDARY_WINDOW_REFRESH_INTERVAL_SECONDS, onProgressDialogRefresh, ui);
  g_object_weak_ref (G_OBJECT (d), onProgressDialogDestroyed, ui);
  onProgressDialogRefresh (ui);

  gtr_dialog_set_content (GTK_DIALOG (d), fr);
  gtk_widget_show (d);
}

static void
onResponse (GtkDialog* d, int response, gpointer user_data)
{
  MakeMetaUI * ui = user_data;

  if (response == GTK_RESPONSE_ACCEPT)
    {
      if (ui->builder != NULL)
        {
          int i;
          int n;
          int tier;
          GtkTextIter start, end;
          char * dir;
          char * base;
          char * tracker_text;
          char ** tracker_strings;
          GtkEntry * c_entry = GTK_ENTRY (ui->comment_entry);
          GtkToggleButton * p_check = GTK_TOGGLE_BUTTON (ui->private_check);
          GtkToggleButton * c_check = GTK_TOGGLE_BUTTON (ui->comment_check);
          const char * comment = gtk_entry_get_text (c_entry);
          const gboolean isPrivate = gtk_toggle_button_get_active (p_check);
          const gboolean useComment = gtk_toggle_button_get_active (c_check);
          tr_tracker_info * trackers;

          /* destination file */
          dir = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (ui->destination_chooser));
          base = g_path_get_basename (ui->builder->top);
          g_free (ui->target);
          ui->target = g_strdup_printf ("%s/%s.torrent", dir, base);

          /* build the array of trackers */
          gtk_text_buffer_get_bounds (ui->announce_text_buffer, &start, &end);
          tracker_text = gtk_text_buffer_get_text (ui->announce_text_buffer,
                                                     &start, &end, FALSE);
          tracker_strings = g_strsplit (tracker_text, "\n", 0);
          for (i=0; tracker_strings[i];)
            ++i;
          trackers = g_new0 (tr_tracker_info, i);
          for (i=n=tier=0; tracker_strings[i]; ++i)
            {
              const char * str = tracker_strings[i];
              if (!*str)
                {
                  ++tier;
                }
              else
                {
                  trackers[n].tier = tier;
                  trackers[n].announce = tracker_strings[i];
                  ++n;
                }
            }

          /* build the .torrent */
          makeProgressDialog (GTK_WIDGET (d), ui);
          tr_makeMetaInfo (ui->builder, ui->target, trackers, n,
                           useComment ? comment : NULL, isPrivate);

          /* cleanup */
          g_free (trackers);
          g_strfreev (tracker_strings);
          g_free (tracker_text);
          g_free (base);
          g_free (dir);
        }
    }
  else if (response == GTK_RESPONSE_CLOSE)
    {
      gtk_widget_destroy (GTK_WIDGET (d));
    }
}

/***
****
***/

static void
onSourceToggled (GtkToggleButton * tb, gpointer user_data)
{
  gtk_widget_set_sensitive (GTK_WIDGET (user_data),
                            gtk_toggle_button_get_active (tb));
}

static void
updatePiecesLabel (MakeMetaUI * ui)
{
  const tr_metainfo_builder * builder = ui->builder;
  const char * filename = builder ? builder->top : NULL;
  GString * gstr = g_string_new (NULL);

  g_string_append (gstr, "<i>");
  if (!filename)
    {
      g_string_append (gstr, _("No source selected"));
    }
  else
    {
      char buf[128];
      tr_strlsize (buf, builder->totalSize, sizeof (buf));
      g_string_append_printf (gstr, ngettext ("%1$s; %2$'d File",
                                              "%1$s; %2$'d Files",
                                              builder->fileCount),
                              buf, builder->fileCount);
      g_string_append (gstr, "; ");

      tr_formatter_mem_B (buf, builder->pieceSize, sizeof (buf));
      g_string_append_printf (gstr, ngettext ("%1$'d Piece @ %2$s",
                                              "%1$'d Pieces @ %2$s",
                                              builder->pieceCount),
                                    builder->pieceCount, buf);
    }

  g_string_append (gstr, "</i>");
  gtk_label_set_markup (GTK_LABEL (ui->pieces_lb), gstr->str);
  g_string_free (gstr, TRUE);
}

static void
setFilename (MakeMetaUI * ui, const char * filename)
{
  if (ui->builder != NULL)
    {
      tr_metaInfoBuilderFree (ui->builder);
      ui->builder = NULL;
    }

  if (filename)
    ui->builder = tr_metaInfoBuilderCreate (filename);

  updatePiecesLabel (ui);
}

static void
onChooserChosen (GtkFileChooser * chooser, gpointer user_data)
{
  char * filename;
  MakeMetaUI * ui = user_data;

  g_object_set_data (G_OBJECT (chooser), FILE_CHOSEN_KEY,
                     GINT_TO_POINTER (TRUE));

  filename = gtk_file_chooser_get_filename (chooser);
  setFilename (ui, filename);
  g_free (filename);
}

static void
onSourceToggled2 (GtkToggleButton * tb, GtkWidget * chooser, MakeMetaUI * ui)
{
  if (gtk_toggle_button_get_active (tb))
    {
      if (g_object_get_data (G_OBJECT (chooser), FILE_CHOSEN_KEY) != NULL)
        onChooserChosen (GTK_FILE_CHOOSER (chooser), ui);
      else
        setFilename (ui, NULL);
    }
}
static void
onFolderToggled (GtkToggleButton * tb, gpointer data)
{
  MakeMetaUI * ui = data;
  onSourceToggled2 (tb, ui->folder_chooser, ui);
}
static void
onFileToggled (GtkToggleButton * tb, gpointer data)
{
  MakeMetaUI * ui = data;
  onSourceToggled2 (tb, ui->file_chooser, ui);
}

static const char *
getDefaultSavePath (void)
{
  return g_get_user_special_dir (G_USER_DIRECTORY_DESKTOP);
}

static void
on_drag_data_received (GtkWidget         * widget           UNUSED,
                       GdkDragContext    * drag_context,
                       gint                x                UNUSED,
                       gint                y                UNUSED,
                       GtkSelectionData  * selection_data,
                       guint               info             UNUSED,
                       guint               time_,
                       gpointer            user_data)
{
  gboolean success = FALSE;
  MakeMetaUI * ui = user_data;
  char ** uris = gtk_selection_data_get_uris (selection_data);

  if (uris && uris[0])
    {
      const char * uri = uris[ 0 ];
      gchar * filename = g_filename_from_uri (uri, NULL, NULL);

      if (g_file_test (filename, G_FILE_TEST_IS_DIR))
        {
          /* a directory was dragged onto the dialog... */
          gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ui->folder_radio), TRUE);
          gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (ui->folder_chooser), filename);
          success = TRUE;
        }
      else if (g_file_test (filename, G_FILE_TEST_IS_REGULAR))
        {
          /* a file was dragged on to the dialog... */
          gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ui->file_radio), TRUE);
          gtk_file_chooser_set_filename (GTK_FILE_CHOOSER (ui->file_chooser), filename);
          success = TRUE;
        }

      g_free (filename);
    }

  g_strfreev (uris);
  gtk_drag_finish (drag_context, success, FALSE, time_);
}

GtkWidget*
gtr_torrent_creation_dialog_new (GtkWindow  * parent, TrCore * core)
{
  const char * str;
  GtkWidget * d, *t, *w, *l, *fr, *sw, *v;
  GSList * slist;
  guint row = 0;
  MakeMetaUI * ui = g_new0 (MakeMetaUI, 1);

  ui->core = core;

  d = gtk_dialog_new_with_buttons (_("New Torrent"),
                                   parent,
                                   GTK_DIALOG_DESTROY_WITH_PARENT,
                                   GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
                                   GTK_STOCK_NEW, GTK_RESPONSE_ACCEPT,
                                   NULL);
  ui->dialog = d;
  g_signal_connect (d, "response", G_CALLBACK (onResponse), ui);
  g_object_set_data_full (G_OBJECT (d), "ui", ui, freeMetaUI);

  t = hig_workarea_create ();

  hig_workarea_add_section_title (t, &row, _("Files"));

    str = _("Sa_ve to:");
    w = gtk_file_chooser_button_new (NULL, GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);
    gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (w), getDefaultSavePath ());
    ui->destination_chooser = w;
    hig_workarea_add_row (t, &row, str, w, NULL);

    l = gtk_radio_button_new_with_mnemonic (NULL, _("Source F_older:"));
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (l), FALSE);
    w = gtk_file_chooser_button_new (NULL, GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);
    g_signal_connect (l, "toggled", G_CALLBACK (onFolderToggled), ui);
    g_signal_connect (l, "toggled", G_CALLBACK (onSourceToggled), w);
    g_signal_connect (w, "selection-changed", G_CALLBACK (onChooserChosen), ui);
    ui->folder_radio = l;
    ui->folder_chooser = w;
    gtk_widget_set_sensitive (GTK_WIDGET (w), FALSE);
    hig_workarea_add_row_w (t, &row, l, w, NULL);

    slist = gtk_radio_button_get_group (GTK_RADIO_BUTTON (l)),
    l = gtk_radio_button_new_with_mnemonic (slist, _("Source _File:"));
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (l), TRUE);
    w = gtk_file_chooser_button_new (NULL, GTK_FILE_CHOOSER_ACTION_OPEN);
    g_signal_connect (l, "toggled", G_CALLBACK (onFileToggled), ui);
    g_signal_connect (l, "toggled", G_CALLBACK (onSourceToggled), w);
    g_signal_connect (w, "selection-changed", G_CALLBACK (onChooserChosen), ui);
    ui->file_radio = l;
    ui->file_chooser = w;
    hig_workarea_add_row_w (t, &row, l, w, NULL);

    w = gtk_label_new (NULL);
    ui->pieces_lb = w;
    gtk_label_set_markup (GTK_LABEL (w), _("<i>No source selected</i>"));
    hig_workarea_add_row (t, &row, NULL, w, NULL);

  hig_workarea_add_section_divider (t, &row);
  hig_workarea_add_section_title (t, &row, _("Properties"));

    str = _("_Trackers:");
    v = gtk_box_new (GTK_ORIENTATION_VERTICAL, GUI_PAD_SMALL);
    ui->announce_text_buffer = gtk_text_buffer_new (NULL);
    w = gtk_text_view_new_with_buffer (ui->announce_text_buffer);
    gtk_widget_set_size_request (w, -1, 80);
    sw = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw),
                                    GTK_POLICY_AUTOMATIC,
                                    GTK_POLICY_AUTOMATIC);
    gtk_container_add (GTK_CONTAINER (sw), w);
    fr = gtk_frame_new (NULL);
    gtk_frame_set_shadow_type (GTK_FRAME (fr), GTK_SHADOW_IN);
    gtk_container_add (GTK_CONTAINER (fr), sw);
    gtk_box_pack_start (GTK_BOX (v), fr, TRUE, TRUE, 0);
    l = gtk_label_new (NULL);
    gtk_label_set_markup (GTK_LABEL (l), _("To add a backup URL, add it on the line after the primary URL.\n"
                                           "To add another primary URL, add it after a blank line."));
    gtk_label_set_justify (GTK_LABEL (l), GTK_JUSTIFY_LEFT);
    gtk_misc_set_alignment (GTK_MISC (l), 0.0, 0.5);
    gtk_box_pack_start (GTK_BOX (v), l, FALSE, FALSE, 0);
    hig_workarea_add_tall_row (t, &row, str, v, NULL);

    l = gtk_check_button_new_with_mnemonic (_("Co_mment:"));
    ui->comment_check = l;
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (l), FALSE);
    w = gtk_entry_new ();
    ui->comment_entry = w;
    gtk_widget_set_sensitive (GTK_WIDGET (w), FALSE);
    g_signal_connect (l, "toggled", G_CALLBACK (onSourceToggled), w);
    hig_workarea_add_row_w (t, &row, l, w, NULL);

    w = hig_workarea_add_wide_checkbutton (t, &row, _("_Private torrent"), FALSE);
    ui->private_check = w;

  gtr_dialog_set_content (GTK_DIALOG (d), t);

  gtk_drag_dest_set (d, GTK_DEST_DEFAULT_ALL, NULL, 0, GDK_ACTION_COPY);
  gtk_drag_dest_add_uri_targets (d);
  g_signal_connect (d, "drag-data-received", G_CALLBACK (on_drag_data_received), ui);

  return d;
}
