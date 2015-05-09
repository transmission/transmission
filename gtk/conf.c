/******************************************************************************
 * $Id$
 *
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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h> /* strtol () */
#include <string.h>

#include <unistd.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>

#include <libtransmission/transmission.h>
#include <libtransmission/variant.h>

#include "conf.h"
#include "tr-prefs.h"
#include "util.h"

#define MY_CONFIG_NAME "transmission"
#define MY_READABLE_NAME "transmission-gtk"

static char * gl_confdir = NULL;

void
gtr_pref_init (const char * config_dir)
{
  gl_confdir = g_strdup (config_dir);
}

/***
****
****  Preferences
****
***/

/**
 * This is where we initialize the preferences file with the default values.
 * If you add a new preferences key, you /must/ add a default value here.
 */
static void
tr_prefs_init_defaults (tr_variant * d)
{
  const char * dir;

  dir = g_get_user_special_dir (G_USER_DIRECTORY_DOWNLOAD);
  if (dir == NULL)
    dir = g_get_user_special_dir (G_USER_DIRECTORY_DESKTOP);
  if (dir == NULL)
    dir = tr_getDefaultDownloadDir ();

  tr_variantDictReserve (d, 29);
  tr_variantDictAddStr  (d, TR_KEY_watch_dir, dir);
  tr_variantDictAddBool (d, TR_KEY_watch_dir_enabled, FALSE);
  tr_variantDictAddBool (d, TR_KEY_user_has_given_informed_consent, FALSE);
  tr_variantDictAddBool (d, TR_KEY_inhibit_desktop_hibernation, FALSE);
  tr_variantDictAddBool (d, TR_KEY_blocklist_updates_enabled, TRUE);
  tr_variantDictAddStr  (d, TR_KEY_open_dialog_dir, g_get_home_dir ());
  tr_variantDictAddBool (d, TR_KEY_show_toolbar, TRUE);
  tr_variantDictAddBool (d, TR_KEY_show_filterbar, TRUE);
  tr_variantDictAddBool (d, TR_KEY_show_statusbar, TRUE);
  tr_variantDictAddBool (d, TR_KEY_trash_can_enabled, TRUE);
  tr_variantDictAddBool (d, TR_KEY_show_notification_area_icon, FALSE);
  tr_variantDictAddBool (d, TR_KEY_show_tracker_scrapes, FALSE);
  tr_variantDictAddBool (d, TR_KEY_show_extra_peer_details, FALSE);
  tr_variantDictAddBool (d, TR_KEY_show_backup_trackers, FALSE);
  tr_variantDictAddStr  (d, TR_KEY_statusbar_stats, "total-ratio");
  tr_variantDictAddBool (d, TR_KEY_torrent_added_notification_enabled, true);
  tr_variantDictAddBool (d, TR_KEY_torrent_complete_notification_enabled, true);
  tr_variantDictAddStr  (d, TR_KEY_torrent_complete_sound_command, "canberra-gtk-play -i complete-download -d 'transmission torrent downloaded'");
  tr_variantDictAddBool (d, TR_KEY_torrent_complete_sound_enabled, true);
  tr_variantDictAddBool (d, TR_KEY_show_options_window, TRUE);
  tr_variantDictAddBool (d, TR_KEY_main_window_is_maximized, FALSE);
  tr_variantDictAddInt  (d, TR_KEY_main_window_height, 500);
  tr_variantDictAddInt  (d, TR_KEY_main_window_width, 300);
  tr_variantDictAddInt  (d, TR_KEY_main_window_x, 50);
  tr_variantDictAddInt  (d, TR_KEY_main_window_y, 50);
  tr_variantDictAddStr  (d, TR_KEY_download_dir, dir);
  tr_variantDictAddStr  (d, TR_KEY_sort_mode, "sort-by-name");
  tr_variantDictAddBool (d, TR_KEY_sort_reversed, FALSE);
  tr_variantDictAddBool (d, TR_KEY_compact_view, FALSE);
}

static tr_variant*
getPrefs (void)
{
  static tr_variant settings;
  static gboolean loaded = FALSE;

  if (!loaded)
    {
      tr_variantInitDict (&settings, 0);
      tr_prefs_init_defaults (&settings);
      tr_sessionLoadSettings (&settings, gl_confdir, MY_CONFIG_NAME);
      loaded = TRUE;
    }

  return &settings;
}

/***
****
***/

tr_variant*
gtr_pref_get_all (void)
{
  return getPrefs ();
}

int64_t
gtr_pref_int_get (const tr_quark key)
{
  int64_t i;

  return tr_variantDictFindInt (getPrefs (), key, &i) ? i : 0;
}

void
gtr_pref_int_set (const tr_quark key, int64_t value)
{
  tr_variantDictAddInt (getPrefs (), key, value);
}

double
gtr_pref_double_get (const tr_quark key)
{
  double d;

  return tr_variantDictFindReal (getPrefs (), key, &d) ? d : 0.0;
}

void
gtr_pref_double_set (const tr_quark key, double value)
{
  tr_variantDictAddReal (getPrefs (), key, value);
}

/***
****
***/

gboolean
gtr_pref_flag_get (const tr_quark key)
{
  bool boolVal;

  return tr_variantDictFindBool (getPrefs (), key, &boolVal) ? boolVal : false;
}

void
gtr_pref_flag_set (const tr_quark key, gboolean value)
{
  tr_variantDictAddBool (getPrefs (), key, value);
}

/***
****
***/

const char*
gtr_pref_string_get (const tr_quark key)
{
  const char * str;

  return tr_variantDictFindStr (getPrefs (), key, &str, NULL) ? str : NULL;
}

void
gtr_pref_string_set (const tr_quark key, const char * value)
{
  tr_variantDictAddStr (getPrefs (), key, value);
}

/***
****
***/

void
gtr_pref_save (tr_session * session)
{
  tr_sessionSaveSettings (session, gl_confdir, getPrefs ());
}

