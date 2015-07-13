/*
 * This file Copyright (C) 2007-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <limits.h> /* INT_MAX */
#include <stddef.h>
#include <stdio.h> /* sscanf () */
#include <stdlib.h> /* abort () */
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h> /* tr_free */

#include "actions.h"
#include "conf.h"
#include "details.h"
#include "favicon.h" /* gtr_get_favicon () */
#include "file-list.h"
#include "hig.h"
#include "tr-prefs.h"
#include "util.h"

static GQuark ARG_KEY = 0;
static GQuark DETAILS_KEY = 0;
static GQuark TORRENT_ID_KEY = 0;
static GQuark TEXT_BUFFER_KEY = 0;
static GQuark URL_ENTRY_KEY = 0;

struct DetailsImpl
{
  GtkWidget * dialog;

  GtkWidget * honor_limits_check;
  GtkWidget * up_limited_check;
  GtkWidget * up_limit_sping;
  GtkWidget * down_limited_check;
  GtkWidget * down_limit_spin;
  GtkWidget * bandwidth_combo;

  GtkWidget * ratio_combo;
  GtkWidget * ratio_spin;
  GtkWidget * idle_combo;
  GtkWidget * idle_spin;
  GtkWidget * max_peers_spin;

  gulong honor_limits_check_tag;
  gulong up_limited_check_tag;
  gulong down_limited_check_tag;
  gulong down_limit_spin_tag;
  gulong up_limit_spin_tag;
  gulong bandwidth_combo_tag;
  gulong ratio_combo_tag;
  gulong ratio_spin_tag;
  gulong idle_combo_tag;
  gulong idle_spin_tag;
  gulong max_peers_spin_tag;

  GtkWidget * size_lb;
  GtkWidget * state_lb;
  GtkWidget * have_lb;
  GtkWidget * dl_lb;
  GtkWidget * ul_lb;
  GtkWidget * error_lb;
  GtkWidget * date_started_lb;
  GtkWidget * eta_lb;
  GtkWidget * last_activity_lb;

  GtkWidget * hash_lb;
  GtkWidget * privacy_lb;
  GtkWidget * origin_lb;
  GtkWidget * destination_lb;
  GtkTextBuffer * comment_buffer;

  GHashTable * peer_hash;
  GHashTable * webseed_hash;
  GtkListStore * peer_store;
  GtkListStore * webseed_store;
  GtkWidget * webseed_view;
  GtkWidget * peer_view;
  GtkWidget * more_peer_details_check;

  GtkListStore  * tracker_store;
  GHashTable    * tracker_hash;
  GtkTreeModel  * trackers_filtered;
  GtkWidget     * add_tracker_button;
  GtkWidget     * edit_trackers_button;
  GtkWidget     * remove_tracker_button;
  GtkWidget     * tracker_view;
  GtkWidget     * scrape_check;
  GtkWidget     * all_check;

  GtkWidget * file_list;
  GtkWidget * file_label;

  GSList * ids;
  TrCore * core;
  guint periodic_refresh_tag;

  GString * gstr;
};

static tr_torrent**
getTorrents (struct DetailsImpl * d, int * setmeCount)
{
  GSList * l;
  int torrentCount = 0;
  const int n = g_slist_length (d->ids);
  tr_torrent ** torrents = g_new (tr_torrent*, n);

  for (l=d->ids; l!=NULL; l=l->next)
    if ((torrents[torrentCount] = gtr_core_find_torrent (d->core, GPOINTER_TO_INT (l->data))))
      ++torrentCount;

  *setmeCount = torrentCount;
  return torrents;
}

/****
*****
*****  OPTIONS TAB
*****
****/

static void
set_togglebutton_if_different (GtkWidget * w, gulong tag, gboolean value)
{
  GtkToggleButton * toggle = GTK_TOGGLE_BUTTON (w);
  const gboolean currentValue = gtk_toggle_button_get_active (toggle);

  if (currentValue != value)
    {
      g_signal_handler_block (toggle, tag);
      gtk_toggle_button_set_active (toggle, value);
      g_signal_handler_unblock (toggle, tag);
    }
}

static void
set_int_spin_if_different (GtkWidget * w, gulong tag, int value)
{
  GtkSpinButton * spin = GTK_SPIN_BUTTON (w);
  const int currentValue = gtk_spin_button_get_value_as_int (spin);

  if (currentValue != value)
    {
      g_signal_handler_block (spin, tag);
      gtk_spin_button_set_value (spin, value);
      g_signal_handler_unblock (spin, tag);
    }
}

static void
set_double_spin_if_different (GtkWidget * w, gulong tag, double value)
{
  GtkSpinButton * spin = GTK_SPIN_BUTTON (w);
  const double currentValue = gtk_spin_button_get_value (spin);

  if (((int)(currentValue*100) != (int)(value*100)))
    {
      g_signal_handler_block (spin, tag);
      gtk_spin_button_set_value (spin, value);
      g_signal_handler_unblock (spin, tag);
    }
}

static void
unset_combo (GtkWidget * w, gulong tag)
{
  GtkComboBox * combobox = GTK_COMBO_BOX (w);

  g_signal_handler_block (combobox, tag);
  gtk_combo_box_set_active (combobox, -1);
  g_signal_handler_unblock (combobox, tag);
}

static void
refreshOptions (struct DetailsImpl * di, tr_torrent ** torrents, int n)
{
  /***
  ****  Options Page
  ***/

  /* honor_limits_check */
  if (n)
    {
      int i;
      const bool baseline = tr_torrentUsesSessionLimits (torrents[0]);

      for (i=1; i<n; ++i)
        if (baseline != tr_torrentUsesSessionLimits (torrents[i]))
          break;

      if (i == n)
        set_togglebutton_if_different (di->honor_limits_check, di->honor_limits_check_tag, baseline);
    }

  /* down_limited_check */
  if (n)
    {
      int i;
      const bool baseline = tr_torrentUsesSpeedLimit (torrents[0], TR_DOWN);

      for (i=1; i<n; ++i)
        if (baseline != tr_torrentUsesSpeedLimit (torrents[i], TR_DOWN))
          break;

      if (i == n)
        set_togglebutton_if_different (di->down_limited_check, di->down_limited_check_tag, baseline);
    }

  /* down_limit_spin */
  if (n)
    {
      int i;
      const unsigned int baseline = tr_torrentGetSpeedLimit_KBps (torrents[0], TR_DOWN);

      for (i=1; i<n; ++i)
          if (baseline != (tr_torrentGetSpeedLimit_KBps (torrents[i], TR_DOWN)))
            break;

      if (i == n)
        set_int_spin_if_different (di->down_limit_spin, di->down_limit_spin_tag, baseline);
    }

  /* up_limited_check */
  if (n)
    {
      int i;
      const bool baseline = tr_torrentUsesSpeedLimit (torrents[0], TR_UP);

      for (i=1; i<n; ++i)
        if (baseline != tr_torrentUsesSpeedLimit (torrents[i], TR_UP))
          break;

      if (i == n)
        set_togglebutton_if_different (di->up_limited_check, di->up_limited_check_tag, baseline);
    }

  /* up_limit_sping */
  if (n)
    {
      int i;
      const unsigned int  baseline = tr_torrentGetSpeedLimit_KBps (torrents[0], TR_UP);

      for (i=1; i<n; ++i)
        if (baseline != (tr_torrentGetSpeedLimit_KBps (torrents[i], TR_UP)))
          break;

      if (i == n)
        set_int_spin_if_different (di->up_limit_sping, di->up_limit_spin_tag, baseline);
    }

  /* bandwidth_combo */
  if (n)
    {
      int i;
      const int baseline = tr_torrentGetPriority (torrents[0]);

      for (i=1; i<n; ++i)
        if (baseline != tr_torrentGetPriority (torrents[i]))
          break;

      if (i == n)
        {
          GtkWidget * w = di->bandwidth_combo;
          g_signal_handler_block (w, di->bandwidth_combo_tag);
          gtr_priority_combo_set_value (GTK_COMBO_BOX (w), baseline);
          g_signal_handler_unblock (w, di->bandwidth_combo_tag);
        }
      else
        {
          unset_combo (di->bandwidth_combo, di->bandwidth_combo_tag);
        }
    }

  /* ratio_combo */
  if (n)
    {
      int i;
      const int baseline = tr_torrentGetRatioMode (torrents[0]);

      for (i=1; i<n; ++i)
        if (baseline != (int)tr_torrentGetRatioMode (torrents[i]))
          break;

      if (i == n)
        {
          GtkWidget * w = di->ratio_combo;
          g_signal_handler_block (w, di->ratio_combo_tag);
          gtr_combo_box_set_active_enum (GTK_COMBO_BOX (w), baseline);
          gtr_widget_set_visible (di->ratio_spin, baseline == TR_RATIOLIMIT_SINGLE);
          g_signal_handler_unblock (w, di->ratio_combo_tag);
        }
    }

  /* ratio_spin */
  if (n)
    {
      const double baseline = tr_torrentGetRatioLimit (torrents[0]);
      set_double_spin_if_different (di->ratio_spin, di->ratio_spin_tag, baseline);
    }

  /* idle_combo */
  if (n)
    {
      int i;
      const int baseline = tr_torrentGetIdleMode (torrents[0]);

      for (i=1; i<n; ++i)
        if (baseline != (int)tr_torrentGetIdleMode (torrents[i]))
          break;

      if (i == n)
        {
          GtkWidget * w = di->idle_combo;
          g_signal_handler_block (w, di->idle_combo_tag);
          gtr_combo_box_set_active_enum (GTK_COMBO_BOX (w), baseline);
          gtr_widget_set_visible (di->idle_spin, baseline == TR_IDLELIMIT_SINGLE);
          g_signal_handler_unblock (w, di->idle_combo_tag);
        }
    }

  /* idle_spin */
  if (n)
    {
      const int baseline = tr_torrentGetIdleLimit (torrents[0]);
      set_int_spin_if_different (di->idle_spin, di->idle_spin_tag, baseline);
    }

  /* max_peers_spin */
  if (n)
    {
      const int baseline = tr_torrentGetPeerLimit (torrents[0]);
      set_int_spin_if_different (di->max_peers_spin, di->max_peers_spin_tag, baseline);
    }
}

static void
torrent_set_bool (struct DetailsImpl * di, const tr_quark key, gboolean value)
{
  GSList *l;
  tr_variant top, *args, *ids;

  tr_variantInitDict (&top, 2);
  tr_variantDictAddStr (&top, TR_KEY_method, "torrent-set");
  args = tr_variantDictAddDict (&top, TR_KEY_arguments, 2);
  tr_variantDictAddBool (args, key, value);
  ids = tr_variantDictAddList (args, TR_KEY_ids, g_slist_length (di->ids));
  for (l=di->ids; l; l=l->next)
    tr_variantListAddInt (ids, GPOINTER_TO_INT (l->data));

  gtr_core_exec (di->core, &top);
  tr_variantFree (&top);
}

static void
torrent_set_int (struct DetailsImpl * di, const tr_quark key, int value)
{
  GSList *l;
  tr_variant top, *args, *ids;

  tr_variantInitDict (&top, 2);
  tr_variantDictAddStr (&top, TR_KEY_method, "torrent-set");
  args = tr_variantDictAddDict (&top, TR_KEY_arguments, 2);
  tr_variantDictAddInt (args, key, value);
  ids = tr_variantDictAddList (args, TR_KEY_ids, g_slist_length (di->ids));
  for (l=di->ids; l; l=l->next)
    tr_variantListAddInt (ids, GPOINTER_TO_INT (l->data));

  gtr_core_exec (di->core, &top);
  tr_variantFree (&top);
}

static void
torrent_set_real (struct DetailsImpl * di, const tr_quark key, double value)
{
  GSList *l;
  tr_variant top, *args, *ids;

  tr_variantInitDict (&top, 2);
  tr_variantDictAddStr (&top, TR_KEY_method, "torrent-set");
  args = tr_variantDictAddDict (&top, TR_KEY_arguments, 2);
  tr_variantDictAddReal (args, key, value);
  ids = tr_variantDictAddList (args, TR_KEY_ids, g_slist_length (di->ids));
  for (l=di->ids; l; l=l->next)
    tr_variantListAddInt (ids, GPOINTER_TO_INT (l->data));

  gtr_core_exec (di->core, &top);
  tr_variantFree (&top);
}

static void
up_speed_toggled_cb (GtkToggleButton * tb, gpointer d)
{
  torrent_set_bool (d, TR_KEY_uploadLimited, gtk_toggle_button_get_active (tb));
}

static void
down_speed_toggled_cb (GtkToggleButton *tb, gpointer d)
{
  torrent_set_bool (d, TR_KEY_downloadLimited, gtk_toggle_button_get_active (tb));
}

static void
global_speed_toggled_cb (GtkToggleButton * tb, gpointer d)
{
  torrent_set_bool (d, TR_KEY_honorsSessionLimits, gtk_toggle_button_get_active (tb));
}

static void
up_speed_spun_cb (GtkSpinButton * s, struct DetailsImpl * di)
{
  torrent_set_int (di, TR_KEY_uploadLimit, gtk_spin_button_get_value_as_int (s));
}

static void
down_speed_spun_cb (GtkSpinButton * s, struct DetailsImpl * di)
{
  torrent_set_int (di, TR_KEY_downloadLimit, gtk_spin_button_get_value_as_int (s));
}

static void
idle_spun_cb (GtkSpinButton * s, struct DetailsImpl * di)
{
  torrent_set_int (di, TR_KEY_seedIdleLimit, gtk_spin_button_get_value_as_int (s));
}

static void
ratio_spun_cb (GtkSpinButton * s, struct DetailsImpl * di)
{
  torrent_set_real (di, TR_KEY_seedRatioLimit, gtk_spin_button_get_value (s));
}

static void
max_peers_spun_cb (GtkSpinButton * s, struct DetailsImpl * di)
{
  torrent_set_int (di, TR_KEY_peer_limit, gtk_spin_button_get_value (s));
}

static void
onPriorityChanged (GtkComboBox * combo_box, struct DetailsImpl * di)
{
  const tr_priority_t priority = gtr_priority_combo_get_value (combo_box);
  torrent_set_int (di, TR_KEY_bandwidthPriority, priority);
}

static GtkWidget*
new_priority_combo (struct DetailsImpl * di)
{
  GtkWidget * w = gtr_priority_combo_new ();
  di->bandwidth_combo_tag = g_signal_connect (w, "changed", G_CALLBACK (onPriorityChanged), di);
  return w;
}

static void refresh (struct DetailsImpl * di);

static void
onComboEnumChanged (GtkComboBox * combo_box, struct DetailsImpl * di)
{
  const tr_quark key = GPOINTER_TO_INT (g_object_get_qdata (G_OBJECT (combo_box), ARG_KEY));
  torrent_set_int (di, key, gtr_combo_box_get_active_enum (combo_box));
  refresh (di);
}

static GtkWidget*
ratio_combo_new (void)
{
  GtkWidget * w = gtr_combo_box_new_enum (
    _("Use global settings"),       TR_RATIOLIMIT_GLOBAL,
    _("Seed regardless of ratio"),  TR_RATIOLIMIT_UNLIMITED,
    _("Stop seeding at ratio:"),    TR_RATIOLIMIT_SINGLE,
    NULL);
  g_object_set_qdata (G_OBJECT (w), ARG_KEY, GINT_TO_POINTER(TR_KEY_seedRatioMode));
  return w;
}

static GtkWidget*
idle_combo_new (void)
{
  GtkWidget * w = gtr_combo_box_new_enum (
    _("Use global settings"),                 TR_IDLELIMIT_GLOBAL,
    _("Seed regardless of activity"),         TR_IDLELIMIT_UNLIMITED,
    _("Stop seeding if idle for N minutes:"), TR_IDLELIMIT_SINGLE,
    NULL);
  g_object_set_qdata (G_OBJECT (w), ARG_KEY, GINT_TO_POINTER(TR_KEY_seedIdleMode));
  return w;
}

static GtkWidget*
options_page_new (struct DetailsImpl * d)
{
  guint row;
  gulong tag;
  char buf[128];
  GtkWidget *t, *w, *tb, *h;

  row = 0;
  t = hig_workarea_create ();
  hig_workarea_add_section_title (t, &row, _("Speed"));

  tb = hig_workarea_add_wide_checkbutton (t, &row, _("Honor global _limits"), 0);
  d->honor_limits_check = tb;
  tag = g_signal_connect (tb, "toggled", G_CALLBACK (global_speed_toggled_cb), d);
  d->honor_limits_check_tag = tag;

  g_snprintf (buf, sizeof (buf), _("Limit _download speed (%s):"), _ (speed_K_str));
  tb = gtk_check_button_new_with_mnemonic (buf);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (tb), FALSE);
  d->down_limited_check = tb;
  tag = g_signal_connect (tb, "toggled", G_CALLBACK (down_speed_toggled_cb), d);
  d->down_limited_check_tag = tag;

  w = gtk_spin_button_new_with_range (0, INT_MAX, 5);
  tag = g_signal_connect (w, "value-changed", G_CALLBACK (down_speed_spun_cb), d);
  d->down_limit_spin_tag = tag;
  hig_workarea_add_row_w (t, &row, tb, w, NULL);
  d->down_limit_spin = w;

  g_snprintf (buf, sizeof (buf), _("Limit _upload speed (%s):"), _ (speed_K_str));
  tb = gtk_check_button_new_with_mnemonic (buf);
  d->up_limited_check = tb;
  tag = g_signal_connect (tb, "toggled", G_CALLBACK (up_speed_toggled_cb), d);
  d->up_limited_check_tag = tag;

  w = gtk_spin_button_new_with_range (0, INT_MAX, 5);
  tag = g_signal_connect (w, "value-changed", G_CALLBACK (up_speed_spun_cb), d);
  d->up_limit_spin_tag = tag;
  hig_workarea_add_row_w (t, &row, tb, w, NULL);
  d->up_limit_sping = w;

  w = new_priority_combo (d);
  hig_workarea_add_row (t, &row, _("Torrent _priority:"), w, NULL);
  d->bandwidth_combo = w;

  hig_workarea_add_section_divider (t, &row);
  hig_workarea_add_section_title (t, &row, _("Seeding Limits"));

  h = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, GUI_PAD);
  w = d->ratio_combo = ratio_combo_new ();
  d->ratio_combo_tag = g_signal_connect (w, "changed", G_CALLBACK (onComboEnumChanged), d);
  gtk_box_pack_start (GTK_BOX (h), w, TRUE, TRUE, 0);
  w = d->ratio_spin = gtk_spin_button_new_with_range (0, 1000, .05);
  gtk_entry_set_width_chars (GTK_ENTRY (w), 7);
  d->ratio_spin_tag = g_signal_connect (w, "value-changed", G_CALLBACK (ratio_spun_cb), d);
  gtk_box_pack_start (GTK_BOX (h), w, FALSE, FALSE, 0);
  hig_workarea_add_row (t, &row, _("_Ratio:"), h, NULL);

  h = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, GUI_PAD);
  w = d->idle_combo = idle_combo_new ();
  d->idle_combo_tag = g_signal_connect (w, "changed", G_CALLBACK (onComboEnumChanged), d);
  gtk_box_pack_start (GTK_BOX (h), w, TRUE, TRUE, 0);
  w = d->idle_spin = gtk_spin_button_new_with_range (1, INT_MAX, 5);
  d->idle_spin_tag = g_signal_connect (w, "value-changed", G_CALLBACK (idle_spun_cb), d);
  gtk_box_pack_start (GTK_BOX (h), w, FALSE, FALSE, 0);
  hig_workarea_add_row (t, &row, _("_Idle:"), h, NULL);

  hig_workarea_add_section_divider (t, &row);
  hig_workarea_add_section_title (t, &row, _("Peer Connections"));

  w = gtk_spin_button_new_with_range (1, 3000, 5);
  hig_workarea_add_row (t, &row, _("_Maximum peers:"), w, w);
  tag = g_signal_connect (w, "value-changed", G_CALLBACK (max_peers_spun_cb), d);
  d->max_peers_spin = w;
  d->max_peers_spin_tag = tag;

  return t;
}

/****
*****
*****  INFO TAB
*****
****/

static const char *
activityString (int activity, bool finished)
{
  switch (activity)
    {
      case TR_STATUS_CHECK_WAIT:    return  _("Queued for verification");
      case TR_STATUS_CHECK:         return  _("Verifying local data");
      case TR_STATUS_DOWNLOAD_WAIT: return  _("Queued for download");
      case TR_STATUS_DOWNLOAD:      return C_("Verb", "Downloading");
      case TR_STATUS_SEED_WAIT:     return  _("Queued for seeding");
      case TR_STATUS_SEED:          return C_("Verb", "Seeding");
      case TR_STATUS_STOPPED:       return  finished ? _("Finished") : _("Paused");
    }

  return "";
}

/* Only call gtk_text_buffer_set_text () if the new text differs from the old.
 * This way if the user has text selected, refreshing won't deselect it */
static void
gtr_text_buffer_set_text (GtkTextBuffer * b, const char * str)
{
  char * old_str;
  GtkTextIter start, end;

  if (str == NULL)
    str = "";

  gtk_text_buffer_get_bounds (b, &start, &end);
  old_str = gtk_text_buffer_get_text (b, &start, &end, FALSE);

  if ((old_str == NULL) || g_strcmp0 (old_str, str))
    gtk_text_buffer_set_text (b, str, -1);

  g_free (old_str);
}

static char*
get_short_date_string (time_t t)
{
  char buf[64];
  struct tm tm;

  if (!t)
    return g_strdup (_("N/A"));

  tr_localtime_r (&t, &tm);
  strftime (buf, sizeof (buf), "%d %b %Y", &tm);
  return g_locale_to_utf8 (buf, -1, NULL, NULL, NULL);
};

static void
refreshInfo (struct DetailsImpl * di, tr_torrent ** torrents, int n)
{
  int i;
  const char * str;
  const char * mixed = _("Mixed");
  const char * no_torrent = _("No Torrents Selected");
  const char * stateString;
  char buf[512];
  uint64_t sizeWhenDone = 0;
  const tr_stat ** stats = g_new (const tr_stat*, n);
  const tr_info ** infos = g_new (const tr_info*, n);

  for (i=0; i<n; ++i)
    {
      stats[i] = tr_torrentStatCached (torrents[i]);
      infos[i] = tr_torrentInfo (torrents[i]);
    }

  /* privacy_lb */
  if (n<=0)
    {
      str = no_torrent;
    }
  else
    {
      const bool baseline = infos[0]->isPrivate;

      for (i=1; i<n; ++i)
        if (baseline != infos[i]->isPrivate)
          break;

      if (i!=n)
        str = mixed;
      else if (baseline)
        str = _("Private to this tracker -- DHT and PEX disabled");
      else
        str = _("Public torrent");
    }
  gtr_label_set_text (GTK_LABEL (di->privacy_lb), str);


  /* origin_lb */
  if (n<=0)
    {
      str = no_torrent;
    }
  else
    {
      const char * creator = infos[0]->creator ? infos[0]->creator : "";
      const time_t date = infos[0]->dateCreated;
      char * datestr = get_short_date_string (date);
      gboolean mixed_creator = FALSE;
      gboolean mixed_date = FALSE;

      for (i=1; i<n; ++i)
        {
          mixed_creator |= g_strcmp0 (creator, infos[i]->creator ? infos[i]->creator : "");
          mixed_date |= (date != infos[i]->dateCreated);
        }

      const gboolean empty_creator = !*creator;
      const gboolean empty_date = date == 0;

      if (mixed_date || mixed_creator)
        {
          str = mixed;
        }
      else if (empty_date && empty_creator)
        {
          str = _("N/A");
        }
      else
        {
          if (empty_date && !empty_creator)
            g_snprintf (buf, sizeof (buf), _("Created by %1$s"), creator);
          else if (empty_creator && !empty_date)
            g_snprintf (buf, sizeof (buf), _("Created on %1$s"), datestr);
          else
            g_snprintf (buf, sizeof (buf), _("Created by %1$s on %2$s"), creator, datestr);
          str = buf;
        }

      g_free (datestr);
    }
  gtr_label_set_text (GTK_LABEL (di->origin_lb), str);


  /* comment_buffer */
  if (n<=0)
    {
      str = "";
    }
  else
    {
      const char * baseline = infos[0]->comment ? infos[0]->comment : "";

      for (i=1; i<n; ++i)
        if (g_strcmp0 (baseline, infos[i]->comment ? infos[i]->comment : ""))
          break;

      if (i==n)
        str = baseline;
      else
        str = mixed;
    }
  gtr_text_buffer_set_text (di->comment_buffer, str);

  /* destination_lb */
  if (n<=0)
    {
      str = no_torrent;
    }
  else
    {
      const char * baseline = tr_torrentGetDownloadDir (torrents[0]);

      for (i=1; i<n; ++i)
        if (g_strcmp0 (baseline, tr_torrentGetDownloadDir (torrents[i])))
          break;

      if (i==n)
        str = baseline;
      else
        str = mixed;
    }
  gtr_label_set_text (GTK_LABEL (di->destination_lb), str);

  /* state_lb */
  if (n<=0)
    {
      str = no_torrent;
    }
  else
    {
      const tr_torrent_activity activity = stats[0]->activity;
      bool allFinished = stats[0]->finished;

      for (i=1; i<n; ++i)
        {
          if (activity != stats[i]->activity)
            break;
          if (!stats[i]->finished)
            allFinished = FALSE;
        }

      str = i<n ? mixed : activityString (activity, allFinished);
    }
  stateString = str;
  gtr_label_set_text (GTK_LABEL (di->state_lb), str);


  /* date started */
  if (n<=0)
    {
      str = no_torrent;
    }
  else
    {
      const time_t baseline = stats[0]->startDate;

      for (i=1; i<n; ++i)
        if (baseline != stats[i]->startDate)
          break;

      if (i != n)
        str = mixed;
      else if ((baseline<=0) || (stats[0]->activity == TR_STATUS_STOPPED))
        str = stateString;
      else
        str = tr_strltime (buf, time (NULL)-baseline, sizeof (buf));
    }
  gtr_label_set_text (GTK_LABEL (di->date_started_lb), str);


  /* eta */
  if (n<=0)
    {
      str = no_torrent;
    }
  else
    {
      const int baseline = stats[0]->eta;

      for (i=1; i<n; ++i)
        if (baseline != stats[i]->eta)
          break;

      if (i!=n)
        str = mixed;
      else if (baseline < 0)
        str = _("Unknown");
      else
        str = tr_strltime (buf, baseline, sizeof (buf));
    }
  gtr_label_set_text (GTK_LABEL (di->eta_lb), str);


  /* size_lb */
  {
    char sizebuf[128];
    uint64_t size = 0;
    int pieces = 0;
    int32_t pieceSize = 0;

    for (i=0; i<n; ++i)
      {
        size += infos[i]->totalSize;
        pieces += infos[i]->pieceCount;

        if (!pieceSize)
          pieceSize = infos[i]->pieceSize;
        else if (pieceSize != (int)infos[i]->pieceSize)
          pieceSize = -1;
      }

    tr_strlsize (sizebuf, size, sizeof (sizebuf));
    if (!size)
      str = "";
    else if (pieceSize >= 0)
      {
        char piecebuf[128];
        tr_formatter_mem_B (piecebuf, pieceSize, sizeof (piecebuf));
        g_snprintf (buf, sizeof (buf),
                    ngettext ("%1$s (%2$'d piece @ %3$s)",
                              "%1$s (%2$'d pieces @ %3$s)", pieces),
                    sizebuf, pieces, piecebuf);
        str = buf;
      }
    else
      {
        g_snprintf (buf, sizeof (buf),
                    ngettext ("%1$s (%2$'d piece)",
                              "%1$s (%2$'d pieces)", pieces),
                    sizebuf, pieces);
        str = buf;
      }
    gtr_label_set_text (GTK_LABEL (di->size_lb), str);
  }


  /* have_lb */
  if (n <= 0)
    {
      str = no_torrent;
    }
  else
    {
      uint64_t leftUntilDone = 0;
      uint64_t haveUnchecked = 0;
      uint64_t haveValid = 0;
      uint64_t available = 0;

      for (i=0; i<n; ++i)
        {
          const tr_stat * st = stats[i];
          haveUnchecked += st->haveUnchecked;
          haveValid += st->haveValid;
          sizeWhenDone += st->sizeWhenDone;
          leftUntilDone += st->leftUntilDone;
          available += st->sizeWhenDone - st->leftUntilDone + st->desiredAvailable;
        }

      {
        char buf2[32], unver[64], total[64], avail[32];
        const double d = sizeWhenDone ? (100.0 * available) / sizeWhenDone : 0;
        const double ratio = 100.0 * (sizeWhenDone ? (haveValid + haveUnchecked) / (double)sizeWhenDone : 1);

        tr_strlpercent (avail, d, sizeof (avail));
        tr_strlpercent (buf2, ratio, sizeof (buf2));
        tr_strlsize (total, haveUnchecked + haveValid, sizeof (total));
        tr_strlsize (unver, haveUnchecked,             sizeof (unver));

        if (!haveUnchecked && !leftUntilDone)
          g_snprintf (buf, sizeof (buf), _("%1$s (%2$s%%)"), total, buf2);
        else if (!haveUnchecked)
          g_snprintf (buf, sizeof (buf), _("%1$s (%2$s%% of %3$s%% Available)"), total, buf2, avail);
        else
          g_snprintf (buf, sizeof (buf), _("%1$s (%2$s%% of %3$s%% Available); %4$s Unverified"), total, buf2, avail, unver);

        str = buf;
      }
    }
  gtr_label_set_text (GTK_LABEL (di->have_lb), str);

  /* dl_lb */
  if (n <= 0)
    {
      str = no_torrent;
    }
  else
    {
      char dbuf[64], fbuf[64];
      uint64_t d=0, f=0;

      for (i=0; i<n; ++i)
        {
          d += stats[i]->downloadedEver;
          f += stats[i]->corruptEver;
        }

      tr_strlsize (dbuf, d, sizeof (dbuf));
      tr_strlsize (fbuf, f, sizeof (fbuf));

      if (f)
        g_snprintf (buf, sizeof (buf), _("%1$s (+%2$s corrupt)"), dbuf, fbuf);
      else
        tr_strlcpy (buf, dbuf, sizeof (buf));
      str = buf;
    }
  gtr_label_set_text (GTK_LABEL (di->dl_lb), str);


  /* ul_lb */
  if (n <= 0)
    {
      str = no_torrent;
    }
  else
    {
      char upstr[64];
      char ratiostr[64];
      uint64_t up = 0;
      uint64_t down = 0;

      for (i=0; i<n; ++i)
        {
          up += stats[i]->uploadedEver;
          down += stats[i]->downloadedEver;
        }

      tr_strlsize (upstr, up, sizeof (upstr));
      tr_strlratio (ratiostr, tr_getRatio (up, down), sizeof (ratiostr));
      g_snprintf (buf, sizeof (buf), _("%s (Ratio: %s)"), upstr, ratiostr);
      str = buf;
    }
  gtr_label_set_text (GTK_LABEL (di->ul_lb), str);

  /* hash_lb */
  if (n <= 0)
    str = no_torrent;
  else if (n==1)
    str = infos[0]->hashString;
  else
    str = mixed;
  gtr_label_set_text (GTK_LABEL (di->hash_lb), str);

  /* error */
  if (n <= 0)
    {
      str = no_torrent;
    }
  else
    {
      const char * baseline = stats[0]->errorString;

      for (i=1; i<n; ++i)
        if (g_strcmp0 (baseline, stats[i]->errorString))
          break;

      if (i==n)
        str = baseline;
      else
        str = mixed;
    }
  if (!str || !*str)
    str = _("No errors");
  gtr_label_set_text (GTK_LABEL (di->error_lb), str);


  /* activity date */
  if (n <= 0)
    {
      str = no_torrent;
    }
  else
    {
      time_t latest = 0;

      for (i=0; i<n; ++i)
        if (latest < stats[i]->activityDate)
          latest = stats[i]->activityDate;

      if (latest <= 0)
        {
          str = _("Never");
        }
      else
        {
          const int period = time (NULL) - latest;
          if (period < 5)
            {
              tr_strlcpy (buf, _("Active now"), sizeof (buf));
            }
          else
            {
              char tbuf[128];
              tr_strltime (tbuf, period, sizeof (tbuf));
              g_snprintf (buf, sizeof (buf), _("%1$s ago"), tbuf);
            }
          str = buf;
        }
    }
  gtr_label_set_text (GTK_LABEL (di->last_activity_lb), str);

  g_free (stats);
  g_free (infos);
}

static GtkWidget*
info_page_new (struct DetailsImpl * di)
{
  guint row = 0;
  GtkTextBuffer * b;
  GtkWidget *l, *w, *fr, *sw;
  GtkWidget *t = hig_workarea_create ();

  hig_workarea_add_section_title (t, &row, _("Activity"));

    /* size */
    l = di->size_lb = gtk_label_new (NULL);
    gtk_label_set_single_line_mode (GTK_LABEL (l), TRUE);
    hig_workarea_add_row (t, &row, _("Torrent size:"), l, NULL);

    /* have */
    l = di->have_lb = gtk_label_new (NULL);
    gtk_label_set_single_line_mode (GTK_LABEL (l), TRUE);
    hig_workarea_add_row (t, &row, _("Have:"), l, NULL);

    /* uploaded */
    l = di->ul_lb = gtk_label_new (NULL);
    gtk_label_set_single_line_mode (GTK_LABEL (l), TRUE);
    hig_workarea_add_row (t, &row, _("Uploaded:"), l, NULL);

    /* downloaded */
    l = di->dl_lb = gtk_label_new (NULL);
    gtk_label_set_single_line_mode (GTK_LABEL (l), TRUE);
    hig_workarea_add_row (t, &row, _("Downloaded:"), l, NULL);

    /* state */
    l = di->state_lb = gtk_label_new (NULL);
    gtk_label_set_single_line_mode (GTK_LABEL (l), TRUE);
    hig_workarea_add_row (t, &row, _("State:"), l, NULL);

    /* running for */
    l = di->date_started_lb = gtk_label_new (NULL);
    gtk_label_set_single_line_mode (GTK_LABEL (l), TRUE);
    hig_workarea_add_row (t, &row, _("Running time:"), l, NULL);

    /* eta */
    l = di->eta_lb = gtk_label_new (NULL);
    gtk_label_set_single_line_mode (GTK_LABEL (l), TRUE);
    hig_workarea_add_row (t, &row, _("Remaining time:"), l, NULL);

    /* last activity */
    l = di->last_activity_lb = gtk_label_new (NULL);
    gtk_label_set_single_line_mode (GTK_LABEL (l), TRUE);
    hig_workarea_add_row (t, &row, _("Last activity:"), l, NULL);

    /* error */
    l = g_object_new (GTK_TYPE_LABEL, "selectable", TRUE,
                                      "ellipsize", PANGO_ELLIPSIZE_END,
                                      NULL);
    hig_workarea_add_row (t, &row, _("Error:"), l, NULL);
    di->error_lb = l;

  hig_workarea_add_section_divider (t, &row);
  hig_workarea_add_section_title (t, &row, _("Details"));

    /* destination */
    l = g_object_new (GTK_TYPE_LABEL, "selectable", TRUE,
                                      "ellipsize", PANGO_ELLIPSIZE_END,
                                      NULL);
    hig_workarea_add_row (t, &row, _("Location:"), l, NULL);
    di->destination_lb = l;

    /* hash */
    l = g_object_new (GTK_TYPE_LABEL, "selectable", TRUE,
                                      "ellipsize", PANGO_ELLIPSIZE_END,
                                      NULL);
    hig_workarea_add_row (t, &row, _("Hash:"), l, NULL);
    di->hash_lb = l;

    /* privacy */
    l = gtk_label_new (NULL);
    gtk_label_set_single_line_mode (GTK_LABEL (l), TRUE);
    hig_workarea_add_row (t, &row, _("Privacy:"), l, NULL);
    di->privacy_lb = l;

    /* origins */
    l = g_object_new (GTK_TYPE_LABEL, "selectable", TRUE,
                                      "ellipsize", PANGO_ELLIPSIZE_END,
                                      NULL);
    hig_workarea_add_row (t, &row, _("Origin:"), l, NULL);
    di->origin_lb = l;

    /* comment */
    b = di->comment_buffer = gtk_text_buffer_new (NULL);
    w = gtk_text_view_new_with_buffer (b);
    gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (w), GTK_WRAP_WORD);
    gtk_text_view_set_editable (GTK_TEXT_VIEW (w), FALSE);
    sw = gtk_scrolled_window_new (NULL, NULL);
    gtk_widget_set_size_request (sw, 350, 36);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw),
                                    GTK_POLICY_AUTOMATIC,
                                    GTK_POLICY_AUTOMATIC);
    gtk_container_add (GTK_CONTAINER (sw), w);
    fr = gtk_frame_new (NULL);
    gtk_frame_set_shadow_type (GTK_FRAME (fr), GTK_SHADOW_IN);
    gtk_container_add (GTK_CONTAINER (fr), sw);
    w = hig_workarea_add_tall_row (t, &row, _("Comment:"), fr, NULL);
    gtk_misc_set_alignment (GTK_MISC (w), 0.0f, 0.0f);

  hig_workarea_add_section_divider (t, &row);
  return t;
}

/****
*****
*****  PEERS TAB
*****
****/

enum
{
  WEBSEED_COL_KEY,
  WEBSEED_COL_WAS_UPDATED,
  WEBSEED_COL_URL,
  WEBSEED_COL_DOWNLOAD_RATE_DOUBLE,
  WEBSEED_COL_DOWNLOAD_RATE_STRING,
  N_WEBSEED_COLS
};

static const char*
getWebseedColumnNames (int column)
{
  switch (column)
    {
      case WEBSEED_COL_URL: return _("Web Seeds");
      case WEBSEED_COL_DOWNLOAD_RATE_DOUBLE:
      case WEBSEED_COL_DOWNLOAD_RATE_STRING: return _("Down");
      default: return "";
    }
}

static GtkListStore*
webseed_model_new (void)
{
  return gtk_list_store_new (N_WEBSEED_COLS,
                             G_TYPE_STRING,   /* key */
                             G_TYPE_BOOLEAN,  /* was-updated */
                             G_TYPE_STRING,   /* url */
                             G_TYPE_DOUBLE,   /* download rate double */
                             G_TYPE_STRING); /* download rate string */
}

enum
{
  PEER_COL_KEY,
  PEER_COL_WAS_UPDATED,
  PEER_COL_ADDRESS,
  PEER_COL_ADDRESS_COLLATED,
  PEER_COL_DOWNLOAD_RATE_DOUBLE,
  PEER_COL_DOWNLOAD_RATE_STRING,
  PEER_COL_UPLOAD_RATE_DOUBLE,
  PEER_COL_UPLOAD_RATE_STRING,
  PEER_COL_CLIENT,
  PEER_COL_PROGRESS,
  PEER_COL_UPLOAD_REQUEST_COUNT_INT,
  PEER_COL_UPLOAD_REQUEST_COUNT_STRING,
  PEER_COL_DOWNLOAD_REQUEST_COUNT_INT,
  PEER_COL_DOWNLOAD_REQUEST_COUNT_STRING,
  PEER_COL_BLOCKS_DOWNLOADED_COUNT_INT,
  PEER_COL_BLOCKS_DOWNLOADED_COUNT_STRING,
  PEER_COL_BLOCKS_UPLOADED_COUNT_INT,
  PEER_COL_BLOCKS_UPLOADED_COUNT_STRING,
  PEER_COL_REQS_CANCELLED_BY_CLIENT_COUNT_INT,
  PEER_COL_REQS_CANCELLED_BY_CLIENT_COUNT_STRING,
  PEER_COL_REQS_CANCELLED_BY_PEER_COUNT_INT,
  PEER_COL_REQS_CANCELLED_BY_PEER_COUNT_STRING,
  PEER_COL_ENCRYPTION_STOCK_ID,
  PEER_COL_FLAGS,
  PEER_COL_TORRENT_NAME,
  N_PEER_COLS
};

static const char*
getPeerColumnName (int column)
{
  switch (column)
    {
      case PEER_COL_ADDRESS: return _("Address");
      case PEER_COL_DOWNLOAD_RATE_STRING:
      case PEER_COL_DOWNLOAD_RATE_DOUBLE: return _("Down");
      case PEER_COL_UPLOAD_RATE_STRING:
      case PEER_COL_UPLOAD_RATE_DOUBLE: return _("Up");
      case PEER_COL_CLIENT: return _("Client");
      case PEER_COL_PROGRESS: return _("%");
      case PEER_COL_UPLOAD_REQUEST_COUNT_INT:
      case PEER_COL_UPLOAD_REQUEST_COUNT_STRING: return _("Up Reqs");
      case PEER_COL_DOWNLOAD_REQUEST_COUNT_INT:
      case PEER_COL_DOWNLOAD_REQUEST_COUNT_STRING: return _("Dn Reqs");
      case PEER_COL_BLOCKS_DOWNLOADED_COUNT_INT:
      case PEER_COL_BLOCKS_DOWNLOADED_COUNT_STRING: return _("Dn Blocks");
      case PEER_COL_BLOCKS_UPLOADED_COUNT_INT:
      case PEER_COL_BLOCKS_UPLOADED_COUNT_STRING: return _("Up Blocks");
      case PEER_COL_REQS_CANCELLED_BY_CLIENT_COUNT_INT:
      case PEER_COL_REQS_CANCELLED_BY_CLIENT_COUNT_STRING: return _("We Cancelled");
      case PEER_COL_REQS_CANCELLED_BY_PEER_COUNT_INT:
      case PEER_COL_REQS_CANCELLED_BY_PEER_COUNT_STRING: return _("They Cancelled");
      case PEER_COL_FLAGS: return _("Flags");
      default: return "";
    }
}

static GtkListStore*
peer_store_new (void)
{
  return gtk_list_store_new (N_PEER_COLS,
                             G_TYPE_STRING,   /* key */
                             G_TYPE_BOOLEAN,  /* was-updated */
                             G_TYPE_STRING,   /* address */
                             G_TYPE_STRING,   /* collated address */
                             G_TYPE_DOUBLE,   /* download speed int */
                             G_TYPE_STRING,   /* download speed string */
                             G_TYPE_DOUBLE,   /* upload speed int */
                             G_TYPE_STRING,   /* upload speed string  */
                             G_TYPE_STRING,   /* client */
                             G_TYPE_INT,      /* progress [0..100] */
                             G_TYPE_INT,      /* upload request count int */
                             G_TYPE_STRING,   /* upload request count string */
                             G_TYPE_INT,      /* download request count int */
                             G_TYPE_STRING,   /* download request count string */
                             G_TYPE_INT,      /* # blocks downloaded int */
                             G_TYPE_STRING,   /* # blocks downloaded string */
                             G_TYPE_INT,      /* # blocks uploaded int */
                             G_TYPE_STRING,   /* # blocks uploaded string */
                             G_TYPE_INT,      /* # blocks cancelled by client int */
                             G_TYPE_STRING,   /* # blocks cancelled by client string */
                             G_TYPE_INT,      /* # blocks cancelled by peer int */
                             G_TYPE_STRING,   /* # blocks cancelled by peer string */
                             G_TYPE_STRING,   /* encryption stock id */
                             G_TYPE_STRING,   /* flagString */
                             G_TYPE_STRING);  /* torrent name */
}

static void
initPeerRow (GtkListStore        * store,
             GtkTreeIter         * iter,
             const char          * key,
             const char          * torrentName,
             const tr_peer_stat  * peer)
{
  int q[4];
  char collated_name[128];
  const char * client = peer->client;

  if (!client || !g_strcmp0 (client, "Unknown Client"))
    client = "";

  if (sscanf (peer->addr, "%d.%d.%d.%d", q, q+1, q+2, q+3) != 4)
    g_strlcpy (collated_name, peer->addr, sizeof (collated_name));
  else
    g_snprintf (collated_name, sizeof (collated_name),
                "%03d.%03d.%03d.%03d", q[0], q[1], q[2], q[3]);

  gtk_list_store_set (store, iter,
                      PEER_COL_ADDRESS, peer->addr,
                      PEER_COL_ADDRESS_COLLATED, collated_name,
                      PEER_COL_CLIENT, client,
                      PEER_COL_ENCRYPTION_STOCK_ID, peer->isEncrypted ? "transmission-lock" : NULL,
                      PEER_COL_KEY, key,
                      PEER_COL_TORRENT_NAME, torrentName,
                      -1);
}

static void
refreshPeerRow (GtkListStore        * store,
                GtkTreeIter         * iter,
                const tr_peer_stat  * peer)
{
  char up_speed[64] = { '\0' };
  char down_speed[64] = { '\0' };
  char up_count[64] = { '\0' };
  char down_count[64] = { '\0' };
  char blocks_to_peer[64] = { '\0' };
  char blocks_to_client[64] = { '\0' };
  char cancelled_by_peer[64] = { '\0' };
  char cancelled_by_client[64] = { '\0' };

  if (peer->rateToPeer_KBps > 0.01)
    tr_formatter_speed_KBps (up_speed, peer->rateToPeer_KBps, sizeof (up_speed));

  if (peer->rateToClient_KBps > 0)
    tr_formatter_speed_KBps (down_speed, peer->rateToClient_KBps, sizeof (down_speed));

  if (peer->pendingReqsToPeer > 0)
    g_snprintf (down_count, sizeof (down_count), "%d", peer->pendingReqsToPeer);

  if (peer->pendingReqsToClient > 0)
    g_snprintf (up_count, sizeof (down_count), "%d", peer->pendingReqsToClient);

  if (peer->blocksToPeer > 0)
    g_snprintf (blocks_to_peer, sizeof (blocks_to_peer), "%"PRIu32, peer->blocksToPeer);

  if (peer->blocksToClient > 0)
    g_snprintf (blocks_to_client, sizeof (blocks_to_client), "%"PRIu32, peer->blocksToClient);

  if (peer->cancelsToPeer > 0)
    g_snprintf (cancelled_by_client, sizeof (cancelled_by_client), "%"PRIu32, peer->cancelsToPeer);

  if (peer->cancelsToClient > 0)
    g_snprintf (cancelled_by_peer, sizeof (cancelled_by_peer), "%"PRIu32, peer->cancelsToClient);

  gtk_list_store_set (store, iter,
    PEER_COL_PROGRESS, (int)(100.0 * peer->progress),
    PEER_COL_UPLOAD_REQUEST_COUNT_INT, peer->pendingReqsToClient,
    PEER_COL_UPLOAD_REQUEST_COUNT_STRING, up_count,
    PEER_COL_DOWNLOAD_REQUEST_COUNT_INT, peer->pendingReqsToPeer,
    PEER_COL_DOWNLOAD_REQUEST_COUNT_STRING, down_count,
    PEER_COL_DOWNLOAD_RATE_DOUBLE, peer->rateToClient_KBps,
    PEER_COL_DOWNLOAD_RATE_STRING, down_speed,
    PEER_COL_UPLOAD_RATE_DOUBLE, peer->rateToPeer_KBps,
    PEER_COL_UPLOAD_RATE_STRING, up_speed,
    PEER_COL_FLAGS, peer->flagStr,
    PEER_COL_WAS_UPDATED, TRUE,
    PEER_COL_BLOCKS_DOWNLOADED_COUNT_INT, (int)peer->blocksToClient,
    PEER_COL_BLOCKS_DOWNLOADED_COUNT_STRING, blocks_to_client,
    PEER_COL_BLOCKS_UPLOADED_COUNT_INT, (int)peer->blocksToPeer,
    PEER_COL_BLOCKS_UPLOADED_COUNT_STRING, blocks_to_peer,
    PEER_COL_REQS_CANCELLED_BY_CLIENT_COUNT_INT, (int)peer->cancelsToPeer,
    PEER_COL_REQS_CANCELLED_BY_CLIENT_COUNT_STRING, cancelled_by_client,
    PEER_COL_REQS_CANCELLED_BY_PEER_COUNT_INT, (int)peer->cancelsToClient,
    PEER_COL_REQS_CANCELLED_BY_PEER_COUNT_STRING, cancelled_by_peer,
    -1);
}

static void
refreshPeerList (struct DetailsImpl * di, tr_torrent ** torrents, int n)
{
  int i;
  int * peerCount;
  GtkTreeIter iter;
  GtkTreeModel * model;
  GHashTable * hash = di->peer_hash;
  GtkListStore * store = di->peer_store;
  struct tr_peer_stat ** peers;

  /* step 1: get all the peers */
  peers = g_new (struct tr_peer_stat*, n);
  peerCount = g_new (int, n);
  for (i=0; i<n; ++i)
    peers[i] = tr_torrentPeers (torrents[i], &peerCount[i]);

  /* step 2: mark all the peers in the list as not-updated */
  model = GTK_TREE_MODEL (store);
  if (gtk_tree_model_iter_nth_child (model, &iter, NULL, 0)) do
    gtk_list_store_set (store, &iter, PEER_COL_WAS_UPDATED, FALSE, -1);
  while (gtk_tree_model_iter_next (model, &iter));

  /* step 3: add any new peers */
  for (i=0; i<n; ++i)
    {
      int j;
      const tr_torrent * tor = torrents[i];

      for (j=0; j<peerCount[i]; ++j)
        {
          char key[128];
          const tr_peer_stat * s = &peers[i][j];

          g_snprintf (key, sizeof (key), "%d.%s", tr_torrentId (tor), s->addr);
          if (g_hash_table_lookup (hash, key) == NULL)
            {
              GtkTreePath * p;
              gtk_list_store_append (store, &iter);
              initPeerRow (store, &iter, key, tr_torrentName (tor), s);
              p = gtk_tree_model_get_path (model, &iter);
              g_hash_table_insert (hash, g_strdup (key), gtk_tree_row_reference_new (model, p));
              gtk_tree_path_free (p);
            }
        }
    }

  /* step 4: update the peers */
  for (i=0; i<n; ++i)
    {
      int j;
      const tr_torrent * tor = torrents[i];

      for (j=0; j<peerCount[i]; ++j)
        {
          char key[128];
          GtkTreePath * p;
          GtkTreeRowReference * ref;
          const tr_peer_stat * s = &peers[i][j];

          g_snprintf (key, sizeof (key), "%d.%s", tr_torrentId (tor), s->addr);
          ref = g_hash_table_lookup (hash, key);
          p = gtk_tree_row_reference_get_path (ref);
          gtk_tree_model_get_iter (model, &iter, p);
          refreshPeerRow (store, &iter, s);
          gtk_tree_path_free (p);
        }
    }

  /* step 5: remove peers that have disappeared */
  model = GTK_TREE_MODEL (store);
  if (gtk_tree_model_iter_nth_child (model, &iter, NULL, 0))
    {
      gboolean more = TRUE;

      while (more)
        {
          gboolean b;
          gtk_tree_model_get (model, &iter, PEER_COL_WAS_UPDATED, &b, -1);
          if (b)
            {
              more = gtk_tree_model_iter_next (model, &iter);
            }
          else
            {
              char * key;
              gtk_tree_model_get (model, &iter, PEER_COL_KEY, &key, -1);
              g_hash_table_remove (hash, key);
              more = gtk_list_store_remove (store, &iter);
              g_free (key);
            }
        }
    }

  /* step 6: cleanup */
  for (i=0; i<n; ++i)
    tr_torrentPeersFree (peers[i], peerCount[i]);
  tr_free (peers);
  tr_free (peerCount);
}

static void
refreshWebseedList (struct DetailsImpl * di, tr_torrent ** torrents, int n)
{
  int i;
  int total = 0;
  GtkTreeIter iter;
  GHashTable * hash = di->webseed_hash;
  GtkListStore * store = di->webseed_store;
  GtkTreeModel * model = GTK_TREE_MODEL (store);

  /* step 1: mark all webseeds as not-updated */
  if (gtk_tree_model_iter_nth_child (model, &iter, NULL, 0)) do
    gtk_list_store_set (store, &iter, WEBSEED_COL_WAS_UPDATED, FALSE, -1);
  while (gtk_tree_model_iter_next (model, &iter));

  /* step 2: add any new webseeds */
  for (i=0; i<n; ++i)
    {
      unsigned int j;
      const tr_torrent * tor = torrents[i];
      const tr_info * inf = tr_torrentInfo (tor);

      total += inf->webseedCount;

      for (j=0; j<inf->webseedCount; ++j)
        {
          char key[256];
          const char * url = inf->webseeds[j];
          g_snprintf (key, sizeof (key), "%d.%s", tr_torrentId (tor), url);
          if (g_hash_table_lookup (hash, key) == NULL)
            {
              GtkTreePath * p;
              gtk_list_store_append (store, &iter);
              gtk_list_store_set (store, &iter, WEBSEED_COL_URL, url,
                                                WEBSEED_COL_KEY, key,
                                                -1);
              p = gtk_tree_model_get_path (model, &iter);
              g_hash_table_insert (hash, g_strdup (key), gtk_tree_row_reference_new (model, p));
              gtk_tree_path_free (p);
            }
        }
    }

  /* step 3: update the webseeds */
  for (i=0; i<n; ++i)
    {
      unsigned int j;
      tr_torrent * tor = torrents[i];
      const tr_info * inf = tr_torrentInfo (tor);
      double * speeds_KBps = tr_torrentWebSpeeds_KBps (tor);

      for (j=0; j<inf->webseedCount; ++j)
        {
          char buf[128];
          char key[256];
          GtkTreePath * p;
          GtkTreeRowReference * ref;
          const char * url = inf->webseeds[j];

          g_snprintf (key, sizeof (key), "%d.%s", tr_torrentId (tor), url);
          ref = g_hash_table_lookup (hash, key);
          p = gtk_tree_row_reference_get_path (ref);
          gtk_tree_model_get_iter (model, &iter, p);
          if (speeds_KBps[j] > 0)
            tr_formatter_speed_KBps (buf, speeds_KBps[j], sizeof (buf));
          else
            *buf = '\0';
          gtk_list_store_set (store, &iter,
            WEBSEED_COL_DOWNLOAD_RATE_DOUBLE, speeds_KBps[j],
            WEBSEED_COL_DOWNLOAD_RATE_STRING, buf,
            WEBSEED_COL_WAS_UPDATED, TRUE,
            -1);

          gtk_tree_path_free (p);
        }

      tr_free (speeds_KBps);
    }

  /* step 4: remove webseeds that have disappeared */
  if (gtk_tree_model_iter_nth_child (model, &iter, NULL, 0))
    {
      gboolean more = TRUE;
      while (more)
        {
          gboolean b;
          gtk_tree_model_get (model, &iter, WEBSEED_COL_WAS_UPDATED, &b, -1);
          if (b)
            {
              more = gtk_tree_model_iter_next (model, &iter);
            }
          else
            {
              char * key;
              gtk_tree_model_get (model, &iter, WEBSEED_COL_KEY, &key, -1);
              if (key != NULL)
                g_hash_table_remove (hash, key);
              more = gtk_list_store_remove (store, &iter);
              g_free (key);
            }
        }
    }

  /* most of the time there are no webseeds...
     don't waste space showing an empty list */
  gtk_widget_set_visible (di->webseed_view, total > 0);
}

static void
refreshPeers (struct DetailsImpl * di, tr_torrent ** torrents, int n)
{
  refreshPeerList (di, torrents, n);
  refreshWebseedList (di, torrents, n);
}

static gboolean
onPeerViewQueryTooltip (GtkWidget   * widget,
                        gint          x,
                        gint          y,
                        gboolean      keyboard_tip,
                        GtkTooltip  * tooltip,
                        gpointer      gdi)
{
  GtkTreeIter iter;
  GtkTreeModel * model;
  gboolean show_tip = FALSE;

  if (gtk_tree_view_get_tooltip_context (GTK_TREE_VIEW (widget),
                                         &x, &y, keyboard_tip,
                                         &model, NULL, &iter))
    {
      const char * pch;
      char * name = NULL;
      char * addr = NULL;
      char * markup = NULL;
      char * flagstr = NULL;
      struct DetailsImpl * di = gdi;
      GString * gstr = di->gstr;

      gtk_tree_model_get (model, &iter, PEER_COL_TORRENT_NAME, &name,
                                        PEER_COL_ADDRESS, &addr,
                                        PEER_COL_FLAGS, &flagstr,
                                        -1);

      g_string_truncate (gstr, 0);
      markup = g_markup_escape_text (name, -1);
      g_string_append_printf (gstr, "<b>%s</b>\n%s\n \n", markup, addr);
      g_free (markup);

      for (pch=flagstr; pch && *pch; ++pch)
        {
          const char * s = NULL;

          switch (*pch)
            {
              case 'O': s = _("Optimistic unchoke"); break;
              case 'D': s = _("Downloading from this peer"); break;
              case 'd': s = _("We would download from this peer if they would let us"); break;
              case 'U': s = _("Uploading to peer"); break;
              case 'u': s = _("We would upload to this peer if they asked"); break;
              case 'K': s = _("Peer has unchoked us, but we're not interested"); break;
              case '?': s = _("We unchoked this peer, but they're not interested"); break;
              case 'E': s = _("Encrypted connection"); break;
              case 'X': s = _("Peer was found through Peer Exchange (PEX)"); break;
              case 'H': s = _("Peer was found through DHT"); break;
              case 'I': s = _("Peer is an incoming connection"); break;
              case 'T': s = _("Peer is connected over µTP"); break;
            }

          if (s)
            g_string_append_printf (gstr, "%c: %s\n", *pch, s);
        }

      if (gstr->len) /* remove the last linefeed */
        g_string_set_size (gstr, gstr->len - 1);

      gtk_tooltip_set_markup (tooltip, gstr->str);

      g_free (flagstr);
      g_free (addr);
      g_free (name);
      show_tip = TRUE;
    }

  return show_tip;
}

static void
setPeerViewColumns (GtkTreeView * peer_view)
{
  int i;
  int n;
  int view_columns[32];
  GtkCellRenderer * r;
  GtkTreeViewColumn * c;
  const bool more = gtr_pref_flag_get (TR_KEY_show_extra_peer_details);

  n = 0;
  view_columns[n++] = PEER_COL_ENCRYPTION_STOCK_ID;
  view_columns[n++] = PEER_COL_UPLOAD_RATE_STRING;
  if (more) view_columns[n++] = PEER_COL_UPLOAD_REQUEST_COUNT_STRING;
  view_columns[n++] = PEER_COL_DOWNLOAD_RATE_STRING;
  if (more) view_columns[n++] = PEER_COL_DOWNLOAD_REQUEST_COUNT_STRING;
  if (more) view_columns[n++] = PEER_COL_BLOCKS_DOWNLOADED_COUNT_STRING;
  if (more) view_columns[n++] = PEER_COL_BLOCKS_UPLOADED_COUNT_STRING;
  if (more) view_columns[n++] = PEER_COL_REQS_CANCELLED_BY_CLIENT_COUNT_STRING;
  if (more) view_columns[n++] = PEER_COL_REQS_CANCELLED_BY_PEER_COUNT_STRING;
  view_columns[n++] = PEER_COL_PROGRESS;
  view_columns[n++] = PEER_COL_FLAGS;
  view_columns[n++] = PEER_COL_ADDRESS;
  view_columns[n++] = PEER_COL_CLIENT;

  /* remove any existing columns */
  while ((c = gtk_tree_view_get_column (peer_view, 0)))
    gtk_tree_view_remove_column (peer_view, c);

  for (i=0; i<n; ++i)
    {
      const int col = view_columns[i];
      const char * t = getPeerColumnName (col);
      int sort_col = col;

      switch (col)
        {
          case PEER_COL_ADDRESS:
            r = gtk_cell_renderer_text_new ();
            c = gtk_tree_view_column_new_with_attributes (t, r, "text", col, NULL);
            sort_col = PEER_COL_ADDRESS_COLLATED;
            break;

          case PEER_COL_CLIENT:
            r = gtk_cell_renderer_text_new ();
            c = gtk_tree_view_column_new_with_attributes (t, r, "text", col, NULL);
            break;

          case PEER_COL_PROGRESS:
            r = gtk_cell_renderer_progress_new ();
            c = gtk_tree_view_column_new_with_attributes (t, r, "value", PEER_COL_PROGRESS, NULL);
            break;

          case PEER_COL_ENCRYPTION_STOCK_ID:
            r = gtk_cell_renderer_pixbuf_new ();
            g_object_set (r, "xalign", (gfloat)0.0,
                             "yalign", (gfloat)0.5,
                             NULL);
            c = gtk_tree_view_column_new_with_attributes (t, r, "stock-id", PEER_COL_ENCRYPTION_STOCK_ID, NULL);
            gtk_tree_view_column_set_sizing (c, GTK_TREE_VIEW_COLUMN_FIXED);
            gtk_tree_view_column_set_fixed_width (c, 20);
            break;

          case PEER_COL_DOWNLOAD_REQUEST_COUNT_STRING:
            r = gtk_cell_renderer_text_new ();
            c = gtk_tree_view_column_new_with_attributes (t, r, "text", col, NULL);
            sort_col = PEER_COL_DOWNLOAD_REQUEST_COUNT_INT;
            break;

          case PEER_COL_UPLOAD_REQUEST_COUNT_STRING:
            r = gtk_cell_renderer_text_new ();
            c = gtk_tree_view_column_new_with_attributes (t, r, "text", col, NULL);
            sort_col = PEER_COL_UPLOAD_REQUEST_COUNT_INT;
            break;

          case PEER_COL_BLOCKS_DOWNLOADED_COUNT_STRING:
            r = gtk_cell_renderer_text_new ();
            c = gtk_tree_view_column_new_with_attributes (t, r, "text", col, NULL);
            sort_col = PEER_COL_BLOCKS_DOWNLOADED_COUNT_INT;
            break;

          case PEER_COL_BLOCKS_UPLOADED_COUNT_STRING:
            r = gtk_cell_renderer_text_new ();
            c = gtk_tree_view_column_new_with_attributes (t, r, "text", col, NULL);
            sort_col = PEER_COL_BLOCKS_UPLOADED_COUNT_INT;
            break;

          case PEER_COL_REQS_CANCELLED_BY_CLIENT_COUNT_STRING:
            r = gtk_cell_renderer_text_new ();
            c = gtk_tree_view_column_new_with_attributes (t, r, "text", col, NULL);
            sort_col = PEER_COL_REQS_CANCELLED_BY_CLIENT_COUNT_INT;
            break;

          case PEER_COL_REQS_CANCELLED_BY_PEER_COUNT_STRING:
            r = gtk_cell_renderer_text_new ();
            c = gtk_tree_view_column_new_with_attributes (t, r, "text", col, NULL);
            sort_col = PEER_COL_REQS_CANCELLED_BY_PEER_COUNT_INT;
            break;

          case PEER_COL_DOWNLOAD_RATE_STRING:
            r = gtk_cell_renderer_text_new ();
            g_object_set (G_OBJECT (r), "xalign", 1.0f, NULL);
            c = gtk_tree_view_column_new_with_attributes (t, r, "text", col, NULL);
            sort_col = PEER_COL_DOWNLOAD_RATE_DOUBLE;
            break;

          case PEER_COL_UPLOAD_RATE_STRING:
            r = gtk_cell_renderer_text_new ();
            g_object_set (G_OBJECT (r), "xalign", 1.0f, NULL);
            c = gtk_tree_view_column_new_with_attributes (t, r, "text", col, NULL);
            sort_col = PEER_COL_UPLOAD_RATE_DOUBLE;
            break;

          case PEER_COL_FLAGS:
            r = gtk_cell_renderer_text_new ();
            c = gtk_tree_view_column_new_with_attributes (t, r, "text", col, NULL);
            break;

          default:
            abort ();
        }

      gtk_tree_view_column_set_resizable (c, FALSE);
      gtk_tree_view_column_set_sort_column_id (c, sort_col);
      gtk_tree_view_append_column (GTK_TREE_VIEW (peer_view), c);
    }

  /* the 'expander' column has a 10-pixel margin on the left
     that doesn't look quite correct in any of these columns...
     so create a non-visible column and assign it as the
     'expander column. */
  {
    GtkTreeViewColumn *c = gtk_tree_view_column_new ();
    gtk_tree_view_column_set_visible (c, FALSE);
    gtk_tree_view_append_column (GTK_TREE_VIEW (peer_view), c);
    gtk_tree_view_set_expander_column (GTK_TREE_VIEW (peer_view), c);
  }
}

static void
onMorePeerInfoToggled (GtkToggleButton * button, struct DetailsImpl * di)
{
  const tr_quark key = TR_KEY_show_extra_peer_details;
  const gboolean value = gtk_toggle_button_get_active (button);
  gtr_core_set_pref_bool (di->core, key, value);
  setPeerViewColumns (GTK_TREE_VIEW (di->peer_view));
}

static GtkWidget*
peer_page_new (struct DetailsImpl * di)
{
  gboolean b;
  const char * str;
  GtkListStore *store;
  GtkWidget *v, *w, *ret, *sw, *vbox;
  GtkWidget *webtree = NULL;
  GtkTreeModel * m;
  GtkTreeViewColumn * c;
  GtkCellRenderer *   r;

  /* webseeds */

  store = di->webseed_store = webseed_model_new ();
  v = gtk_tree_view_new_with_model (GTK_TREE_MODEL (store));
  g_signal_connect (v, "button-release-event", G_CALLBACK (on_tree_view_button_released), NULL);
  gtk_tree_view_set_rules_hint (GTK_TREE_VIEW (v), TRUE);
  g_object_unref (store);

  str = getWebseedColumnNames (WEBSEED_COL_URL);
  r = gtk_cell_renderer_text_new ();
  g_object_set (G_OBJECT (r), "ellipsize", PANGO_ELLIPSIZE_END, NULL);
  c = gtk_tree_view_column_new_with_attributes (str, r, "text", WEBSEED_COL_URL, NULL);
  g_object_set (G_OBJECT (c), "expand", TRUE, NULL);
  gtk_tree_view_column_set_sort_column_id (c, WEBSEED_COL_URL);
  gtk_tree_view_append_column (GTK_TREE_VIEW (v), c);

  str = getWebseedColumnNames (WEBSEED_COL_DOWNLOAD_RATE_STRING);
  r = gtk_cell_renderer_text_new ();
  c = gtk_tree_view_column_new_with_attributes (str, r, "text", WEBSEED_COL_DOWNLOAD_RATE_STRING, NULL);
  gtk_tree_view_column_set_sort_column_id (c, WEBSEED_COL_DOWNLOAD_RATE_DOUBLE);
  gtk_tree_view_append_column (GTK_TREE_VIEW (v), c);

  w = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (w),
                                  GTK_POLICY_AUTOMATIC,
                                  GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (w),
                                       GTK_SHADOW_IN);
  gtk_container_add (GTK_CONTAINER (w), v);

  webtree = w;
  di->webseed_view = w;

  /* peers */

  store  = di->peer_store = peer_store_new ();
  m = gtk_tree_model_sort_new_with_model (GTK_TREE_MODEL (store));
  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (m),
                                        PEER_COL_PROGRESS,
                                        GTK_SORT_DESCENDING);
  v = GTK_WIDGET (g_object_new (GTK_TYPE_TREE_VIEW,
                                "model",  m,
                                "rules-hint", TRUE,
                                "has-tooltip", TRUE,
                                NULL));
  di->peer_view = v;

  g_signal_connect (v, "query-tooltip",
                    G_CALLBACK (onPeerViewQueryTooltip), di);
  g_object_unref (store);
  g_signal_connect (v, "button-release-event",
                    G_CALLBACK (on_tree_view_button_released), NULL);

  setPeerViewColumns (GTK_TREE_VIEW (v));

  w = sw = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (w),
                                  GTK_POLICY_AUTOMATIC,
                                  GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (w),
                                       GTK_SHADOW_IN);
  gtk_container_add (GTK_CONTAINER (w), v);

  vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, GUI_PAD);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), GUI_PAD_BIG);

  v = gtk_paned_new (GTK_ORIENTATION_VERTICAL);
  gtk_paned_pack1 (GTK_PANED (v), webtree, FALSE, TRUE);
  gtk_paned_pack2 (GTK_PANED (v), sw, TRUE, TRUE);
  gtk_box_pack_start (GTK_BOX (vbox), v, TRUE, TRUE, 0);

  w = gtk_check_button_new_with_mnemonic (_("Show _more details"));
  di->more_peer_details_check = w;
  b = gtr_pref_flag_get (TR_KEY_show_extra_peer_details);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), b);
  g_signal_connect (w, "toggled", G_CALLBACK (onMorePeerInfoToggled), di);
  gtk_box_pack_start (GTK_BOX (vbox), w, FALSE, FALSE, 0);


  /* ip-to-GtkTreeRowReference */
  di->peer_hash = g_hash_table_new_full (g_str_hash,
                                         g_str_equal,
                                         (GDestroyNotify)g_free,
                                         (GDestroyNotify)gtk_tree_row_reference_free);

  /* url-to-GtkTreeRowReference */
  di->webseed_hash = g_hash_table_new_full (g_str_hash,
                                            g_str_equal,
                                            (GDestroyNotify)g_free,
                                            (GDestroyNotify)gtk_tree_row_reference_free);
  ret = vbox;
  return ret;
}



/****
*****
*****  TRACKER
*****
****/

/* if it's been longer than a minute, don't bother showing the seconds */
static void
tr_strltime_rounded (char * buf, time_t t, size_t buflen)
{
  if (t > 60) t -= (t % 60);
  tr_strltime (buf, t, buflen);
}

static void
buildTrackerSummary (GString * gstr, const char * key, const tr_tracker_stat * st, gboolean showScrape)
{
  char * str;
  char timebuf[256];
  const time_t now = time (NULL);
  const char * err_markup_begin = "<span color=\"red\">";
  const char * err_markup_end = "</span>";
  const char * timeout_markup_begin = "<span color=\"#224466\">";
  const char * timeout_markup_end = "</span>";
  const char * success_markup_begin = "<span color=\"#008B00\">";
  const char * success_markup_end = "</span>";

  /* hostname */
  {
    g_string_append (gstr, st->isBackup ? "<i>" : "<b>");
    if (key)
      str = g_markup_printf_escaped ("%s - %s", st->host, key);
    else
      str = g_markup_printf_escaped ("%s", st->host);
    g_string_append (gstr, str);
    g_free (str);
    g_string_append (gstr, st->isBackup ? "</i>" : "</b>");
  }

  if (!st->isBackup)
    {
      if (st->hasAnnounced && st->announceState != TR_TRACKER_INACTIVE)
        {
          g_string_append_c (gstr, '\n');
          tr_strltime_rounded (timebuf, now - st->lastAnnounceTime, sizeof (timebuf));
          if (st->lastAnnounceSucceeded)
            g_string_append_printf (gstr, _("Got a list of %1$s%2$'d peers%3$s %4$s ago"),
                                    success_markup_begin, st->lastAnnouncePeerCount, success_markup_end,
                                    timebuf);
          else if (st->lastAnnounceTimedOut)
            g_string_append_printf (gstr, _("Peer list request %1$stimed out%2$s %3$s ago; will retry"),
                                    timeout_markup_begin, timeout_markup_end, timebuf);
          else
            g_string_append_printf (gstr, _("Got an error %1$s\"%2$s\"%3$s %4$s ago"),
                                    err_markup_begin, st->lastAnnounceResult, err_markup_end, timebuf);
        }

      switch (st->announceState)
        {
          case TR_TRACKER_INACTIVE:
            g_string_append_c (gstr, '\n');
            g_string_append (gstr, _("No updates scheduled"));
            break;

          case TR_TRACKER_WAITING:
            tr_strltime_rounded (timebuf, st->nextAnnounceTime - now, sizeof (timebuf));
            g_string_append_c (gstr, '\n');
            g_string_append_printf (gstr, _("Asking for more peers in %s"), timebuf);
            break;

          case TR_TRACKER_QUEUED:
            g_string_append_c (gstr, '\n');
            g_string_append (gstr, _("Queued to ask for more peers"));
            break;

          case TR_TRACKER_ACTIVE:
            tr_strltime_rounded (timebuf, now - st->lastAnnounceStartTime, sizeof (timebuf));
            g_string_append_c (gstr, '\n');
            g_string_append_printf (gstr, _("Asking for more peers now… <small>%s</small>"), timebuf);
            break;
        }

      if (showScrape)
        {
          if (st->hasScraped)
            {
              g_string_append_c (gstr, '\n');
              tr_strltime_rounded (timebuf, now - st->lastScrapeTime, sizeof (timebuf));
              if (st->lastScrapeSucceeded)
                g_string_append_printf (gstr, _("Tracker had %s%'d seeders and %'d leechers%s %s ago"),
                                        success_markup_begin, st->seederCount, st->leecherCount, success_markup_end,
                                        timebuf);
              else
                g_string_append_printf (gstr, _("Got a scrape error \"%s%s%s\" %s ago"), err_markup_begin, st->lastScrapeResult, err_markup_end, timebuf);
            }

          switch (st->scrapeState)
            {
            case TR_TRACKER_INACTIVE:
              break;

            case TR_TRACKER_WAITING:
              g_string_append_c (gstr, '\n');
              tr_strltime_rounded (timebuf, st->nextScrapeTime - now, sizeof (timebuf));
              g_string_append_printf (gstr, _("Asking for peer counts in %s"), timebuf);
              break;

            case TR_TRACKER_QUEUED:
              g_string_append_c (gstr, '\n');
              g_string_append (gstr, _("Queued to ask for peer counts"));
              break;

            case TR_TRACKER_ACTIVE:
              g_string_append_c (gstr, '\n');
              tr_strltime_rounded (timebuf, now - st->lastScrapeStartTime, sizeof (timebuf));
              g_string_append_printf (gstr, _("Asking for peer counts now… <small>%s</small>"), timebuf);
              break;
            }
        }
    }
}

enum
{
  TRACKER_COL_TORRENT_ID,
  TRACKER_COL_TEXT,
  TRACKER_COL_IS_BACKUP,
  TRACKER_COL_TRACKER_ID,
  TRACKER_COL_FAVICON,
  TRACKER_COL_WAS_UPDATED,
  TRACKER_COL_KEY,
  TRACKER_N_COLS
};

static gboolean
trackerVisibleFunc (GtkTreeModel * model, GtkTreeIter * iter, gpointer data)
{
  gboolean isBackup;
  struct DetailsImpl * di = data;

  /* show all */
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (di->all_check)))
    return TRUE;

  /* don't show the backups... */
  gtk_tree_model_get (model, iter, TRACKER_COL_IS_BACKUP, &isBackup, -1);
  return !isBackup;
}

static int
tracker_list_get_current_torrent_id (struct DetailsImpl * di)
{
  int torrent_id = -1;

  /* if there's only one torrent in the dialog, always use it */
  if (torrent_id < 0)
    if (g_slist_length (di->ids) == 1)
      torrent_id = GPOINTER_TO_INT (di->ids->data);

  /* otherwise, use the selected tracker's torrent */
  if (torrent_id < 0)
    {
      GtkTreeIter iter;
      GtkTreeModel * model;
      GtkTreeSelection * sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (di->tracker_view));
      if (gtk_tree_selection_get_selected (sel, &model, &iter))
        gtk_tree_model_get (model, &iter, TRACKER_COL_TORRENT_ID, &torrent_id, -1);
    }

  return torrent_id;
}

static tr_torrent*
tracker_list_get_current_torrent (struct DetailsImpl * di)
{
  const int torrent_id = tracker_list_get_current_torrent_id (di);
  return gtr_core_find_torrent (di->core, torrent_id);
}

static void
favicon_ready_cb (gpointer pixbuf, gpointer vreference)
{
  GtkTreeIter iter;
  GtkTreeRowReference * reference = vreference;

  if (pixbuf != NULL)
    {
      GtkTreePath * path = gtk_tree_row_reference_get_path (reference);
      GtkTreeModel * model = gtk_tree_row_reference_get_model (reference);

      if (gtk_tree_model_get_iter (model, &iter, path))
        gtk_list_store_set (GTK_LIST_STORE (model), &iter, TRACKER_COL_FAVICON, pixbuf, -1);

      gtk_tree_path_free (path);
      g_object_unref (pixbuf);
    }

  gtk_tree_row_reference_free (reference);
}

static void
refreshTracker (struct DetailsImpl * di, tr_torrent ** torrents, int n)
{
  int i;
  int * statCount;
  tr_tracker_stat ** stats;
  GtkTreeIter iter;
  GtkTreeModel * model;
  GString * gstr = di->gstr; /* buffer for temporary strings */
  GHashTable * hash = di->tracker_hash;
  GtkListStore * store = di->tracker_store;
  tr_session * session = gtr_core_session (di->core);
  const gboolean showScrape = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (di->scrape_check));

  /* step 1: get all the trackers */
  statCount = g_new0 (int, n);
  stats = g_new0 (tr_tracker_stat *, n);
  for (i=0; i<n; ++i)
    stats[i] = tr_torrentTrackers (torrents[i], &statCount[i]);

  /* step 2: mark all the trackers in the list as not-updated */
  model = GTK_TREE_MODEL (store);
  if (gtk_tree_model_iter_nth_child (model, &iter, NULL, 0)) do
    gtk_list_store_set (store, &iter, TRACKER_COL_WAS_UPDATED, FALSE, -1);
  while (gtk_tree_model_iter_next (model, &iter));

  /* step 3: add any new trackers */
  for (i=0; i<n; ++i)
    {
      int j;
      const int jn = statCount[i];
      for (j=0; j<jn; ++j)
        {
          const tr_torrent * tor = torrents[i];
          const tr_tracker_stat * st = &stats[i][j];
          const int torrent_id = tr_torrentId (tor);

          /* build the key to find the row */
          g_string_truncate (gstr, 0);
          g_string_append_printf (gstr, "%d\t%d\t%s", torrent_id, st->tier, st->announce);

          if (g_hash_table_lookup (hash, gstr->str) == NULL)
            {
              GtkTreePath * p;
              GtkTreeIter iter;
              GtkTreeRowReference * ref;

              gtk_list_store_insert_with_values (store, &iter, -1,
                TRACKER_COL_TORRENT_ID, torrent_id,
                TRACKER_COL_TRACKER_ID, st->id,
                TRACKER_COL_KEY, gstr->str,
                -1);

              p = gtk_tree_model_get_path (model, &iter);
              ref = gtk_tree_row_reference_new (model, p);
              g_hash_table_insert (hash, g_strdup (gstr->str), ref);
              ref = gtk_tree_row_reference_new (model, p);
              gtr_get_favicon_from_url (session, st->announce, favicon_ready_cb, ref);
              gtk_tree_path_free (p);
            }
        }
    }

  /* step 4: update the peers */
  for (i=0; i<n; ++i)
    {
      int j;
      const tr_torrent * tor = torrents[i];
      const char * summary_name = n>1 ? tr_torrentName (tor) : NULL;

      for (j=0; j<statCount[i]; ++j)
        {
          GtkTreePath * p;
          GtkTreeRowReference * ref;
          const tr_tracker_stat * st = &stats[i][j];

          /* build the key to find the row */
          g_string_truncate (gstr, 0);
          g_string_append_printf (gstr, "%d\t%d\t%s", tr_torrentId (tor), st->tier, st->announce);
          ref = g_hash_table_lookup (hash, gstr->str);
          p = gtk_tree_row_reference_get_path (ref);
          gtk_tree_model_get_iter (model, &iter, p);

          /* update the row */
          g_string_truncate (gstr, 0);
          buildTrackerSummary (gstr, summary_name, st, showScrape);
          gtk_list_store_set (store, &iter, TRACKER_COL_TEXT, gstr->str,
                                            TRACKER_COL_IS_BACKUP, st->isBackup,
                                            TRACKER_COL_TRACKER_ID, st->id,
                                            TRACKER_COL_WAS_UPDATED, TRUE,
                                            -1);

          /* cleanup */
          gtk_tree_path_free (p);
        }
    }

  /* step 5: remove trackers that have disappeared */
  if (gtk_tree_model_iter_nth_child (model, &iter, NULL, 0))
    {
      gboolean more = TRUE;

      while (more)
        {
          gboolean b;
          gtk_tree_model_get (model, &iter, TRACKER_COL_WAS_UPDATED, &b, -1);
          if (b)
            {
              more = gtk_tree_model_iter_next (model, &iter);
            }
          else
            {
              char * key;
              gtk_tree_model_get (model, &iter, TRACKER_COL_KEY, &key, -1);
              g_hash_table_remove (hash, key);
              more = gtk_list_store_remove (store, &iter);
              g_free (key);
            }
        }
    }

  gtk_widget_set_sensitive (di->edit_trackers_button,
                            tracker_list_get_current_torrent_id (di) >= 0);

  /* cleanup */
  for (i=0; i<n; ++i)
    tr_torrentTrackersFree (stats[i], statCount[i]);
  g_free (stats);
  g_free (statCount);
}

static void
onScrapeToggled (GtkToggleButton * button, struct DetailsImpl * di)
{
  const tr_quark key = TR_KEY_show_tracker_scrapes;
  const gboolean value = gtk_toggle_button_get_active (button);
  gtr_core_set_pref_bool (di->core, key, value);
  refresh (di);
}

static void
onBackupToggled (GtkToggleButton * button, struct DetailsImpl * di)
{
  const tr_quark key = TR_KEY_show_backup_trackers;
  const gboolean value = gtk_toggle_button_get_active (button);
  gtr_core_set_pref_bool (di->core, key, value);
  refresh (di);
}

static void
on_edit_trackers_response (GtkDialog * dialog, int response, gpointer data)
{
  gboolean do_destroy = TRUE;
  struct DetailsImpl * di = data;

  if (response == GTK_RESPONSE_ACCEPT)
    {
      int i, n;
      int tier;
      GtkTextIter start, end;
      const int torrent_id = GPOINTER_TO_INT (g_object_get_qdata (G_OBJECT (dialog), TORRENT_ID_KEY));
      GtkTextBuffer * text_buffer = g_object_get_qdata (G_OBJECT (dialog), TEXT_BUFFER_KEY);
      tr_torrent * tor = gtr_core_find_torrent (di->core, torrent_id);

      if (tor != NULL)
        {
          tr_tracker_info * trackers;
          char ** tracker_strings;
          char * tracker_text;

          /* build the array of trackers */
          gtk_text_buffer_get_bounds (text_buffer, &start, &end);
          tracker_text = gtk_text_buffer_get_text (text_buffer, &start, &end, FALSE);
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

          /* update the torrent */
          if (tr_torrentSetAnnounceList (tor, trackers, n))
            {
              refresh (di);
            }
          else
            {
              GtkWidget * w;
              const char * text = _("List contains invalid URLs");
              w = gtk_message_dialog_new (GTK_WINDOW (dialog),
                                          GTK_DIALOG_MODAL,
                                          GTK_MESSAGE_ERROR,
                                          GTK_BUTTONS_CLOSE, "%s", text);
              gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (w), "%s", _("Please correct the errors and try again."));
              gtk_dialog_run (GTK_DIALOG (w));
              gtk_widget_destroy (w);
              do_destroy = FALSE;
            }

          /* cleanup */
          g_free (trackers);
          g_strfreev (tracker_strings);
          g_free (tracker_text);
        }
    }

  if (do_destroy)
    gtk_widget_destroy (GTK_WIDGET (dialog));
}

static void
get_editable_tracker_list (GString * gstr, const tr_torrent * tor)
{
  unsigned int i;
  int tier = 0;
  const tr_info * inf = tr_torrentInfo (tor);

  for (i=0; i<inf->trackerCount; ++i)
    {
      const tr_tracker_info * t = &inf->trackers[i];

      if (tier != t->tier)
        {
          tier = t->tier;
          g_string_append_c (gstr, '\n');
        }

      g_string_append_printf (gstr, "%s\n", t->announce);
    }

  if (gstr->len > 0)
    g_string_truncate (gstr, gstr->len-1);
}

static void
on_edit_trackers (GtkButton * button, gpointer data)
{
  struct DetailsImpl * di = data;
  tr_torrent * tor = tracker_list_get_current_torrent (di);

  if (tor != NULL)
    {
      guint row;
      GtkWidget *w, *d, *fr, *t, *l, *sw;
      GtkWindow * win = GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (button)));
      GString * gstr = di->gstr; /* buffer for temporary strings */
      const int torrent_id = tr_torrentId (tor);

      g_string_truncate (gstr, 0);
      g_string_append_printf (gstr, _("%s - Edit Trackers"), tr_torrentName (tor));
      d = gtk_dialog_new_with_buttons (gstr->str, win,
            GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
            GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT,
            NULL);
      g_signal_connect (d, "response", G_CALLBACK (on_edit_trackers_response), data);

      row = 0;
      t = hig_workarea_create ();
      hig_workarea_add_section_title (t, &row, _("Tracker Announce URLs"));

      l = gtk_label_new (NULL);
      gtk_label_set_markup (GTK_LABEL (l), _("To add a backup URL, add it on the line after the primary URL.\n"
                                             "To add another primary URL, add it after a blank line."));
      gtk_label_set_justify (GTK_LABEL (l), GTK_JUSTIFY_LEFT);
      gtk_misc_set_alignment (GTK_MISC (l), 0.0, 0.5);
      hig_workarea_add_wide_control (t, &row, l);

      w = gtk_text_view_new ();
      g_string_truncate (gstr, 0);
      get_editable_tracker_list (gstr, tor);
      gtk_text_buffer_set_text (gtk_text_view_get_buffer (GTK_TEXT_VIEW (w)), gstr->str, -1);
      fr = gtk_frame_new (NULL);
      gtk_frame_set_shadow_type (GTK_FRAME (fr), GTK_SHADOW_IN);
      sw = gtk_scrolled_window_new (NULL, NULL);
      gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw),
                                      GTK_POLICY_AUTOMATIC,
                                      GTK_POLICY_AUTOMATIC);
      gtk_container_add (GTK_CONTAINER (sw), w);
      gtk_container_add (GTK_CONTAINER (fr), sw);
      gtk_widget_set_size_request (fr, 500u, 166u);
      hig_workarea_add_wide_tall_control (t, &row, fr);

      gtr_dialog_set_content (GTK_DIALOG (d), t);

      g_object_set_qdata (G_OBJECT (d), TORRENT_ID_KEY, GINT_TO_POINTER (torrent_id));
      g_object_set_qdata (G_OBJECT (d), TEXT_BUFFER_KEY, gtk_text_view_get_buffer (GTK_TEXT_VIEW (w)));
      gtk_widget_show (d);
    }
}

static void
on_tracker_list_selection_changed (GtkTreeSelection * sel, gpointer gdi)
{
  struct DetailsImpl * di = gdi;
  const int n = gtk_tree_selection_count_selected_rows (sel);
  tr_torrent * tor = tracker_list_get_current_torrent (di);

  gtk_widget_set_sensitive (di->remove_tracker_button, n>0);
  gtk_widget_set_sensitive (di->add_tracker_button, tor!=NULL);
  gtk_widget_set_sensitive (di->edit_trackers_button, tor!=NULL);
}

static void
on_add_tracker_response (GtkDialog * dialog, int response, gpointer gdi)
{
  gboolean destroy = TRUE;

  if (response == GTK_RESPONSE_ACCEPT)
    {
      struct DetailsImpl * di = gdi;
      GtkWidget * e = GTK_WIDGET (g_object_get_qdata (G_OBJECT (dialog), URL_ENTRY_KEY));
      const int torrent_id = GPOINTER_TO_INT (g_object_get_qdata (G_OBJECT (dialog), TORRENT_ID_KEY));
      char * url = g_strdup (gtk_entry_get_text (GTK_ENTRY (e)));
      g_strstrip (url);

      if (url && *url)
        {
          if (tr_urlIsValidTracker (url))
            {
              tr_variant top, * args, * trackers;

              tr_variantInitDict (&top, 2);
              tr_variantDictAddStr (&top, TR_KEY_method, "torrent-set");
              args = tr_variantDictAddDict (&top, TR_KEY_arguments, 2);
              tr_variantDictAddInt (args, TR_KEY_id, torrent_id);
              trackers = tr_variantDictAddList (args, TR_KEY_trackerAdd, 1);
              tr_variantListAddStr (trackers, url);

              gtr_core_exec (di->core, &top);
              refresh (di);

              tr_variantFree (&top);
            }
          else
            {
              gtr_unrecognized_url_dialog (GTK_WIDGET (dialog), url);
              destroy = FALSE;
            }
        }

      g_free (url);
    }

  if (destroy)
    gtk_widget_destroy (GTK_WIDGET (dialog));
}

static void
on_tracker_list_add_button_clicked (GtkButton * button UNUSED, gpointer gdi)
{
  struct DetailsImpl * di = gdi;
  tr_torrent * tor = tracker_list_get_current_torrent (di);

  if (tor != NULL)
    {
      guint row;
      GtkWidget * e;
      GtkWidget * t;
      GtkWidget * w;
      GString * gstr = di->gstr; /* buffer for temporary strings */

      g_string_truncate (gstr, 0);
      g_string_append_printf (gstr, _("%s - Add Tracker"), tr_torrentName (tor));
      w = gtk_dialog_new_with_buttons (gstr->str, GTK_WINDOW (di->dialog),
                                       GTK_DIALOG_DESTROY_WITH_PARENT,
                                       GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                       GTK_STOCK_ADD, GTK_RESPONSE_ACCEPT,
                                       NULL);
      gtk_dialog_set_alternative_button_order (GTK_DIALOG (w),
                                               GTK_RESPONSE_ACCEPT,
                                               GTK_RESPONSE_CANCEL,
                                               -1);
      g_signal_connect (w, "response", G_CALLBACK (on_add_tracker_response), gdi);

      row = 0;
      t = hig_workarea_create ();
      hig_workarea_add_section_title (t, &row, _("Tracker"));
      e = gtk_entry_new ();
      gtk_widget_set_size_request (e, 400, -1);
      gtr_paste_clipboard_url_into_entry (e);
      g_object_set_qdata (G_OBJECT (w), URL_ENTRY_KEY, e);
      g_object_set_qdata (G_OBJECT (w), TORRENT_ID_KEY, GINT_TO_POINTER (tr_torrentId (tor)));
      hig_workarea_add_row (t, &row, _("_Announce URL:"), e, NULL);
      gtr_dialog_set_content (GTK_DIALOG (w), t);
      gtk_widget_show_all (w);
    }
}

static void
on_tracker_list_remove_button_clicked (GtkButton * button UNUSED, gpointer gdi)
{
  GtkTreeIter iter;
  GtkTreeModel * model;
  struct DetailsImpl * di = gdi;
  GtkTreeView * v = GTK_TREE_VIEW (di->tracker_view);
  GtkTreeSelection * sel = gtk_tree_view_get_selection (v);

  if (gtk_tree_selection_get_selected (sel, &model, &iter))
    {
      int torrent_id;
      int tracker_id;
      tr_variant top, * args, * trackers;

      gtk_tree_model_get (model, &iter, TRACKER_COL_TRACKER_ID, &tracker_id,
                                        TRACKER_COL_TORRENT_ID, &torrent_id,
                                        -1);

      tr_variantInitDict (&top, 2);
      tr_variantDictAddStr (&top, TR_KEY_method, "torrent-set");
      args = tr_variantDictAddDict (&top, TR_KEY_arguments, 2);
      tr_variantDictAddInt (args, TR_KEY_id, torrent_id);
      trackers = tr_variantDictAddList (args, TR_KEY_trackerRemove, 1);
      tr_variantListAddInt (trackers, tracker_id);

      gtr_core_exec (di->core, &top);
      refresh (di);

      tr_variantFree (&top);
    }
}

static GtkWidget*
tracker_page_new (struct DetailsImpl * di)
{
  gboolean b;
  GtkCellRenderer * r;
  GtkTreeViewColumn * c;
  GtkTreeSelection * sel;
  GtkWidget *vbox, *sw, *w, *v, *hbox;
  const int pad = (GUI_PAD + GUI_PAD_BIG) / 2;

  vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, GUI_PAD);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), GUI_PAD_BIG);

  di->tracker_store = gtk_list_store_new (TRACKER_N_COLS,
                                          G_TYPE_INT,
                                          G_TYPE_STRING,
                                          G_TYPE_BOOLEAN,
                                          G_TYPE_INT,
                                          GDK_TYPE_PIXBUF,
                                          G_TYPE_BOOLEAN,
                                          G_TYPE_STRING);

  di->tracker_hash = g_hash_table_new_full (g_str_hash,
                                            g_str_equal,
                                            (GDestroyNotify)g_free,
                                            (GDestroyNotify)gtk_tree_row_reference_free);

  di->trackers_filtered = gtk_tree_model_filter_new (GTK_TREE_MODEL (di->tracker_store), NULL);
  gtk_tree_model_filter_set_visible_func (GTK_TREE_MODEL_FILTER (di->trackers_filtered),
                                          trackerVisibleFunc, di, NULL);

  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, GUI_PAD_BIG);

    v = di->tracker_view = gtk_tree_view_new_with_model (GTK_TREE_MODEL (di->trackers_filtered));
    g_object_unref (di->trackers_filtered);
    gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (v), FALSE);
    g_signal_connect (v, "button-press-event", G_CALLBACK (on_tree_view_button_pressed), NULL);
    g_signal_connect (v, "button-release-event", G_CALLBACK (on_tree_view_button_released), NULL);
    gtk_tree_view_set_rules_hint (GTK_TREE_VIEW (v), TRUE);

    sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (v));
    g_signal_connect (sel, "changed", G_CALLBACK (on_tracker_list_selection_changed), di);

    c = gtk_tree_view_column_new ();
    gtk_tree_view_column_set_title (c, _("Trackers"));
    gtk_tree_view_append_column (GTK_TREE_VIEW (v), c);

    r = gtk_cell_renderer_pixbuf_new ();
    g_object_set (r, "width", 20 + (GUI_PAD_SMALL*2), "xpad", GUI_PAD_SMALL, "ypad", pad, "yalign", 0.0f, NULL);
    gtk_tree_view_column_pack_start (c, r, FALSE);
    gtk_tree_view_column_add_attribute (c, r, "pixbuf", TRACKER_COL_FAVICON);

    r = gtk_cell_renderer_text_new ();
    g_object_set (G_OBJECT (r), "ellipsize", PANGO_ELLIPSIZE_END, "xpad", GUI_PAD_SMALL, "ypad", pad, NULL);
    gtk_tree_view_column_pack_start (c, r, TRUE);
    gtk_tree_view_column_add_attribute (c, r, "markup", TRACKER_COL_TEXT);

    sw = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add (GTK_CONTAINER (sw), v);
    w = gtk_frame_new (NULL);
    gtk_frame_set_shadow_type (GTK_FRAME (w), GTK_SHADOW_IN);
    gtk_container_add (GTK_CONTAINER (w), sw);

  gtk_box_pack_start (GTK_BOX (hbox), w, TRUE, TRUE, 0);

    v = gtk_box_new (GTK_ORIENTATION_VERTICAL, GUI_PAD);

    w = gtk_button_new_with_mnemonic (_("_Add"));
    di->add_tracker_button = w;
    g_signal_connect (w, "clicked", G_CALLBACK (on_tracker_list_add_button_clicked), di);
    gtk_box_pack_start (GTK_BOX (v), w, FALSE, FALSE, 0);

    w = gtk_button_new_with_mnemonic (_("_Edit"));
    gtk_button_set_image (GTK_BUTTON (w), gtk_image_new_from_stock (GTK_STOCK_EDIT, GTK_ICON_SIZE_BUTTON));
    g_signal_connect (w, "clicked", G_CALLBACK (on_edit_trackers), di);
    di->edit_trackers_button = w;
    gtk_box_pack_start (GTK_BOX (v), w, FALSE, FALSE, 0);

    w = gtk_button_new_with_mnemonic (_("_Remove"));
    di->remove_tracker_button = w;
    g_signal_connect (w, "clicked", G_CALLBACK (on_tracker_list_remove_button_clicked), di);
    gtk_box_pack_start (GTK_BOX (v), w, FALSE, FALSE, 0);

    gtk_box_pack_start (GTK_BOX (hbox), v, FALSE, FALSE, 0);

  gtk_box_pack_start (GTK_BOX (vbox), hbox, TRUE, TRUE, 0);

  w = gtk_check_button_new_with_mnemonic (_("Show _more details"));
  di->scrape_check = w;
  b = gtr_pref_flag_get (TR_KEY_show_tracker_scrapes);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), b);
  g_signal_connect (w, "toggled", G_CALLBACK (onScrapeToggled), di);
  gtk_box_pack_start (GTK_BOX (vbox), w, FALSE, FALSE, 0);

  w = gtk_check_button_new_with_mnemonic (_("Show _backup trackers"));
  di->all_check = w;
  b = gtr_pref_flag_get (TR_KEY_show_backup_trackers);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), b);
  g_signal_connect (w, "toggled", G_CALLBACK (onBackupToggled), di);
  gtk_box_pack_start (GTK_BOX (vbox), w, FALSE, FALSE, 0);

  return vbox;
}


/****
*****  DIALOG
****/

static void
refresh (struct DetailsImpl * di)
{
  int n;
  tr_torrent ** torrents = getTorrents (di, &n);

  refreshInfo (di, torrents, n);
  refreshPeers (di, torrents, n);
  refreshTracker (di, torrents, n);
  refreshOptions (di, torrents, n);

  if (n == 0)
    gtk_dialog_response (GTK_DIALOG (di->dialog), GTK_RESPONSE_CLOSE);

  g_free (torrents);
}

static gboolean
periodic_refresh (gpointer data)
{
  refresh (data);

  return G_SOURCE_CONTINUE;
}

static void
details_free (gpointer gdata)
{
  struct DetailsImpl * data = gdata;
  g_source_remove (data->periodic_refresh_tag);
  g_hash_table_destroy (data->tracker_hash);
  g_hash_table_destroy (data->webseed_hash);
  g_hash_table_destroy (data->peer_hash);
  g_string_free (data->gstr, TRUE);
  g_slist_free (data->ids);
  g_free (data);
}

GtkWidget*
gtr_torrent_details_dialog_new (GtkWindow * parent, TrCore * core)
{
  GtkWidget *d, *n, *v, *w, *l;
  struct DetailsImpl * di = g_new0 (struct DetailsImpl, 1);

  /* one-time setup */
  if (ARG_KEY == 0)
    {
      ARG_KEY          = g_quark_from_static_string ("tr-arg-key");
      DETAILS_KEY      = g_quark_from_static_string ("tr-details-data-key");
      TORRENT_ID_KEY   = g_quark_from_static_string ("tr-torrent-id-key");
      TEXT_BUFFER_KEY  = g_quark_from_static_string ("tr-text-buffer-key");
      URL_ENTRY_KEY    = g_quark_from_static_string ("tr-url-entry-key");
    }

  /* create the dialog */
  di->core = core;
  di->gstr = g_string_new (NULL);
  d = gtk_dialog_new_with_buttons (NULL, parent, 0,
                                   GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
                                   NULL);
  di->dialog = d;
  gtk_window_set_role (GTK_WINDOW (d), "tr-info");
  g_signal_connect_swapped (d, "response",
                            G_CALLBACK (gtk_widget_destroy), d);
  gtk_container_set_border_width (GTK_CONTAINER (d), GUI_PAD);
  g_object_set_qdata_full (G_OBJECT (d), DETAILS_KEY, di, details_free);

  n = gtk_notebook_new ();
  gtk_container_set_border_width (GTK_CONTAINER (n), GUI_PAD);

  w = info_page_new (di);
  l = gtk_label_new (_("Information"));
  gtk_notebook_append_page (GTK_NOTEBOOK (n), w, l);

  w = peer_page_new (di);
  l = gtk_label_new (_("Peers"));
  gtk_notebook_append_page (GTK_NOTEBOOK (n),  w, l);

  w = tracker_page_new (di);
  l = gtk_label_new (_("Trackers"));
  gtk_notebook_append_page (GTK_NOTEBOOK (n), w, l);

  v = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  di->file_list = gtr_file_list_new (core, 0);
  di->file_label = gtk_label_new (_("File listing not available for combined torrent properties"));
  gtk_box_pack_start (GTK_BOX (v), di->file_list, TRUE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (v), di->file_label, TRUE, TRUE, 0);
  gtk_container_set_border_width (GTK_CONTAINER (v), GUI_PAD_BIG);
  l = gtk_label_new (_("Files"));
  gtk_notebook_append_page (GTK_NOTEBOOK (n), v, l);

  w = options_page_new (di);
  l = gtk_label_new (_("Options"));
  gtk_notebook_append_page (GTK_NOTEBOOK (n), w, l);

  gtr_dialog_set_content (GTK_DIALOG (d), n);
  di->periodic_refresh_tag = gdk_threads_add_timeout_seconds (SECONDARY_WINDOW_REFRESH_INTERVAL_SECONDS,
                                                              periodic_refresh, di);
  return d;
}

void
gtr_torrent_details_dialog_set_torrents (GtkWidget * w, GSList * ids)
{
  char title[256];
  const int len = g_slist_length (ids);
  struct DetailsImpl * di = g_object_get_qdata (G_OBJECT (w), DETAILS_KEY);

  g_slist_free (di->ids);
  di->ids = g_slist_copy (ids);

  if (len == 1)
    {
      const int id = GPOINTER_TO_INT (ids->data);
      tr_torrent * tor = gtr_core_find_torrent (di->core, id);
      const tr_info * inf = tr_torrentInfo (tor);
      g_snprintf (title, sizeof (title), _("%s Properties"), inf->name);

      gtr_file_list_set_torrent (di->file_list, id);
      gtk_widget_show (di->file_list);
      gtk_widget_hide (di->file_label);
    }
  else
    {
      gtr_file_list_clear (di->file_list);
      gtk_widget_hide (di->file_list);
      gtk_widget_show (di->file_label);
      g_snprintf (title, sizeof (title), _("%'d Torrent Properties"), len);
    }

  gtk_window_set_title (GTK_WINDOW (w), title);

  refresh (di);
}
