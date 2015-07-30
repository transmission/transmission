/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <cassert>
#include <cstdlib>

#include <QDateTime>
#include <QDir>
#include <QFile>

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h>
#include <libtransmission/variant.h>

#include "CustomVariantType.h"
#include "Prefs.h"
#include "Utils.h"

/***
****
***/

Prefs::PrefItem Prefs::myItems[] =
{
  /* gui settings */
  { OPTIONS_PROMPT, TR_KEY_show_options_window, QVariant::Bool },
  { OPEN_DIALOG_FOLDER, TR_KEY_open_dialog_dir, QVariant::String },
  { INHIBIT_HIBERNATION, TR_KEY_inhibit_desktop_hibernation, QVariant::Bool },
  { DIR_WATCH, TR_KEY_watch_dir, QVariant::String },
  { DIR_WATCH_ENABLED, TR_KEY_watch_dir_enabled, QVariant::Bool },
  { SHOW_TRAY_ICON, TR_KEY_show_notification_area_icon, QVariant::Bool },
  { START_MINIMIZED, TR_KEY_start_minimized, QVariant::Bool },
  { SHOW_NOTIFICATION_ON_ADD, TR_KEY_torrent_added_notification_enabled, QVariant::Bool },
  { SHOW_NOTIFICATION_ON_COMPLETE, TR_KEY_torrent_complete_notification_enabled, QVariant::Bool },
  { ASKQUIT, TR_KEY_prompt_before_exit, QVariant::Bool },
  { SORT_MODE, TR_KEY_sort_mode, CustomVariantType::SortModeType },
  { SORT_REVERSED, TR_KEY_sort_reversed, QVariant::Bool },
  { COMPACT_VIEW, TR_KEY_compact_view, QVariant::Bool },
  { FILTERBAR, TR_KEY_show_filterbar, QVariant::Bool },
  { STATUSBAR, TR_KEY_show_statusbar, QVariant::Bool },
  { STATUSBAR_STATS, TR_KEY_statusbar_stats, QVariant::String },
  { SHOW_TRACKER_SCRAPES, TR_KEY_show_extra_peer_details, QVariant::Bool },
  { SHOW_BACKUP_TRACKERS, TR_KEY_show_backup_trackers, QVariant::Bool },
  { TOOLBAR, TR_KEY_show_toolbar , QVariant::Bool },
  { BLOCKLIST_DATE, TR_KEY_blocklist_date, QVariant::DateTime },
  { BLOCKLIST_UPDATES_ENABLED, TR_KEY_blocklist_updates_enabled , QVariant::Bool },
  { MAIN_WINDOW_LAYOUT_ORDER, TR_KEY_main_window_layout_order, QVariant::String },
  { MAIN_WINDOW_HEIGHT, TR_KEY_main_window_height, QVariant::Int },
  { MAIN_WINDOW_WIDTH, TR_KEY_main_window_width, QVariant::Int },
  { MAIN_WINDOW_X, TR_KEY_main_window_x, QVariant::Int },
  { MAIN_WINDOW_Y, TR_KEY_main_window_y, QVariant::Int },
  { FILTER_MODE, TR_KEY_filter_mode, CustomVariantType::FilterModeType },
  { FILTER_TRACKERS, TR_KEY_filter_trackers, QVariant::String },
  { FILTER_TEXT, TR_KEY_filter_text, QVariant::String },
  { SESSION_IS_REMOTE, TR_KEY_remote_session_enabled, QVariant::Bool },
  { SESSION_REMOTE_HOST, TR_KEY_remote_session_host, QVariant::String },
  { SESSION_REMOTE_PORT, TR_KEY_remote_session_port, QVariant::Int },
  { SESSION_REMOTE_AUTH, TR_KEY_remote_session_requres_authentication, QVariant::Bool },
  { SESSION_REMOTE_USERNAME, TR_KEY_remote_session_username, QVariant::String },
  { SESSION_REMOTE_PASSWORD, TR_KEY_remote_session_password, QVariant::String },
  { COMPLETE_SOUND_COMMAND, TR_KEY_torrent_complete_sound_command, QVariant::String },
  { COMPLETE_SOUND_ENABLED, TR_KEY_torrent_complete_sound_enabled, QVariant::Bool },
  { USER_HAS_GIVEN_INFORMED_CONSENT, TR_KEY_user_has_given_informed_consent, QVariant::Bool },

  /* libtransmission settings */
  { ALT_SPEED_LIMIT_UP, TR_KEY_alt_speed_up, QVariant::Int },
  { ALT_SPEED_LIMIT_DOWN, TR_KEY_alt_speed_down, QVariant::Int },
  { ALT_SPEED_LIMIT_ENABLED, TR_KEY_alt_speed_enabled, QVariant::Bool },
  { ALT_SPEED_LIMIT_TIME_BEGIN, TR_KEY_alt_speed_time_begin, QVariant::Int },
  { ALT_SPEED_LIMIT_TIME_END, TR_KEY_alt_speed_time_end, QVariant::Int },
  { ALT_SPEED_LIMIT_TIME_ENABLED, TR_KEY_alt_speed_time_enabled, QVariant::Bool },
  { ALT_SPEED_LIMIT_TIME_DAY, TR_KEY_alt_speed_time_day, QVariant::Int },
  { BLOCKLIST_ENABLED, TR_KEY_blocklist_enabled, QVariant::Bool },
  { BLOCKLIST_URL, TR_KEY_blocklist_url, QVariant::String },
  { DSPEED, TR_KEY_speed_limit_down, QVariant::Int },
  { DSPEED_ENABLED, TR_KEY_speed_limit_down_enabled, QVariant::Bool },
  { DOWNLOAD_DIR, TR_KEY_download_dir, QVariant::String },
  { DOWNLOAD_QUEUE_ENABLED, TR_KEY_download_queue_enabled, QVariant::Bool },
  { DOWNLOAD_QUEUE_SIZE, TR_KEY_download_queue_size, QVariant::Int },
  { ENCRYPTION, TR_KEY_encryption, QVariant::Int },
  { IDLE_LIMIT, TR_KEY_idle_seeding_limit, QVariant::Int },
  { IDLE_LIMIT_ENABLED, TR_KEY_idle_seeding_limit_enabled, QVariant::Bool },
  { INCOMPLETE_DIR, TR_KEY_incomplete_dir, QVariant::String },
  { INCOMPLETE_DIR_ENABLED, TR_KEY_incomplete_dir_enabled, QVariant::Bool },
  { MSGLEVEL, TR_KEY_message_level, QVariant::Int },
  { PEER_LIMIT_GLOBAL, TR_KEY_peer_limit_global, QVariant::Int },
  { PEER_LIMIT_TORRENT, TR_KEY_peer_limit_per_torrent, QVariant::Int },
  { PEER_PORT, TR_KEY_peer_port, QVariant::Int },
  { PEER_PORT_RANDOM_ON_START, TR_KEY_peer_port_random_on_start, QVariant::Bool },
  { PEER_PORT_RANDOM_LOW, TR_KEY_peer_port_random_low, QVariant::Int },
  { PEER_PORT_RANDOM_HIGH, TR_KEY_peer_port_random_high, QVariant::Int },
  { QUEUE_STALLED_MINUTES, TR_KEY_queue_stalled_minutes, QVariant::Int },
  { SCRIPT_TORRENT_DONE_ENABLED, TR_KEY_script_torrent_done_enabled, QVariant::Bool },
  { SCRIPT_TORRENT_DONE_FILENAME, TR_KEY_script_torrent_done_filename, QVariant::String },
  { SOCKET_TOS, TR_KEY_peer_socket_tos, QVariant::Int },
  { START, TR_KEY_start_added_torrents, QVariant::Bool },
  { TRASH_ORIGINAL, TR_KEY_trash_original_torrent_files, QVariant::Bool },
  { PEX_ENABLED, TR_KEY_pex_enabled, QVariant::Bool },
  { DHT_ENABLED, TR_KEY_dht_enabled, QVariant::Bool },
  { UTP_ENABLED, TR_KEY_utp_enabled, QVariant::Bool },
  { LPD_ENABLED, TR_KEY_lpd_enabled, QVariant::Bool },
  { PORT_FORWARDING, TR_KEY_port_forwarding_enabled, QVariant::Bool },
  { PREALLOCATION, TR_KEY_preallocation, QVariant::Int },
  { RATIO, TR_KEY_ratio_limit, QVariant::Double },
  { RATIO_ENABLED, TR_KEY_ratio_limit_enabled, QVariant::Bool },
  { RENAME_PARTIAL_FILES, TR_KEY_rename_partial_files, QVariant::Bool },
  { RPC_AUTH_REQUIRED, TR_KEY_rpc_authentication_required, QVariant::Bool },
  { RPC_ENABLED, TR_KEY_rpc_enabled, QVariant::Bool },
  { RPC_PASSWORD, TR_KEY_rpc_password, QVariant::String },
  { RPC_PORT, TR_KEY_rpc_port, QVariant::Int },
  { RPC_USERNAME, TR_KEY_rpc_username, QVariant::String },
  { RPC_WHITELIST_ENABLED, TR_KEY_rpc_whitelist_enabled, QVariant::Bool },
  { RPC_WHITELIST, TR_KEY_rpc_whitelist, QVariant::String },
  { USPEED_ENABLED, TR_KEY_speed_limit_up_enabled, QVariant::Bool },
  { USPEED, TR_KEY_speed_limit_up, QVariant::Int },
  { UPLOAD_SLOTS_PER_TORRENT, TR_KEY_upload_slots_per_torrent, QVariant::Int }
};

/***
****
***/

Prefs::Prefs (const QString& configDir):
  myConfigDir (configDir)
{
  assert (sizeof(myItems) / sizeof(myItems[0]) == PREFS_COUNT);

#ifndef NDEBUG
  for (int i=0; i<PREFS_COUNT; ++i)
    assert (myItems[i].id == i);
#endif

  // these are the prefs that don't get saved to settings.json
  // when the application exits.
  myTemporaryPrefs << FILTER_TEXT;

  tr_variant top;
  tr_variantInitDict (&top, 0);
  initDefaults (&top);
  tr_sessionLoadSettings (&top, myConfigDir.toUtf8 ().constData (), NULL);

  for (int i=0; i<PREFS_COUNT; ++i)
    {
      double d;
      bool boolVal;
      int64_t intVal;
      const char * str;
      size_t strLen;
      tr_variant * b (tr_variantDictFind (&top, myItems[i].key));

      switch (myItems[i].type)
        {
          case QVariant::Int:
            if (tr_variantGetInt (b, &intVal))
              myValues[i].setValue (static_cast<qlonglong> (intVal));
            break;

          case CustomVariantType::SortModeType:
            if (tr_variantGetStr (b, &str, NULL))
              myValues[i] = QVariant::fromValue (SortMode (QString::fromUtf8 (str)));
            break;

          case CustomVariantType::FilterModeType:
            if (tr_variantGetStr (b, &str, NULL))
              myValues[i] = QVariant::fromValue (FilterMode (QString::fromUtf8 (str)));
            break;

          case QVariant::String:
            if (tr_variantGetStr (b, &str, &strLen))
              myValues[i].setValue (QString::fromUtf8 (str, strLen));
            break;

          case QVariant::Bool:
            if (tr_variantGetBool (b, &boolVal))
              myValues[i].setValue (static_cast<bool> (boolVal));
            break;

          case QVariant::Double:
            if (tr_variantGetReal (b, &d))
              myValues[i].setValue (d);
            break;

          case QVariant::DateTime:
            if (tr_variantGetInt (b, &intVal))
                myValues[i].setValue (QDateTime::fromTime_t (intVal));
            break;

          default:
            assert ("unhandled type" && 0);
            break;
        }
    }

    tr_variantFree (&top);
}

Prefs::~Prefs ()
{
  // make a dict from settings.json
  tr_variant current_settings;
  tr_variantInitDict (&current_settings, PREFS_COUNT);
  for (int i=0; i<PREFS_COUNT; ++i)
    {
      if (myTemporaryPrefs.contains(i))
        continue;

      const tr_quark key = myItems[i].key;
      const QVariant& val = myValues[i];

      switch (myItems[i].type)
        {
          case QVariant::Int:
            tr_variantDictAddInt (&current_settings, key, val.toInt());
            break;

          case CustomVariantType::SortModeType:
            tr_variantDictAddStr (&current_settings, key, val.value<SortMode>().name().toUtf8().constData());
            break;

          case CustomVariantType::FilterModeType:
            tr_variantDictAddStr (&current_settings, key, val.value<FilterMode>().name().toUtf8().constData());
            break;

          case QVariant::String:
            {
              const QByteArray ba (val.toByteArray());
              const char * s = ba.constData();
              if (Utils::isValidUtf8 (s))
                tr_variantDictAddStr (&current_settings, key, s);
              else
                tr_variantDictAddStr (&current_settings, key, val.toString().toUtf8().constData());
            }
            break;

          case QVariant::Bool:
            tr_variantDictAddBool (&current_settings, key, val.toBool());
            break;

          case QVariant::Double:
            tr_variantDictAddReal (&current_settings, key, val.toDouble());
            break;

          case QVariant::DateTime:
            tr_variantDictAddInt (&current_settings, key, val.toDateTime().toTime_t());
            break;

          default:
            assert ("unhandled type" && 0);
            break;
        }
    }

  // update settings.json with our settings
  tr_variant file_settings;
  const QFile file (QDir(myConfigDir).absoluteFilePath(QLatin1String ("settings.json")));
  if (!tr_variantFromFile (&file_settings, TR_VARIANT_FMT_JSON, file.fileName().toUtf8().constData(), NULL))
    tr_variantInitDict (&file_settings, PREFS_COUNT);
  tr_variantMergeDicts (&file_settings, &current_settings);
  tr_variantToFile (&file_settings, TR_VARIANT_FMT_JSON, file.fileName().toUtf8().constData());
  tr_variantFree (&file_settings);

  // cleanup
  tr_variantFree (&current_settings);
}

/**
 * This is where we initialize the preferences file with the default values.
 * If you add a new preferences key, you /must/ add a default value here.
 */
void
Prefs::initDefaults (tr_variant * d)
{
  tr_variantDictReserve (d, 38);
  tr_variantDictAddBool (d, TR_KEY_blocklist_updates_enabled, true);
  tr_variantDictAddBool (d, TR_KEY_compact_view, false);
  tr_variantDictAddBool (d, TR_KEY_inhibit_desktop_hibernation, false);
  tr_variantDictAddBool (d, TR_KEY_prompt_before_exit, true);
  tr_variantDictAddBool (d, TR_KEY_remote_session_enabled, false);
  tr_variantDictAddBool (d, TR_KEY_remote_session_requres_authentication, false);
  tr_variantDictAddBool (d, TR_KEY_show_backup_trackers, false);
  tr_variantDictAddBool (d, TR_KEY_show_extra_peer_details, false),
  tr_variantDictAddBool (d, TR_KEY_show_filterbar, true);
  tr_variantDictAddBool (d, TR_KEY_show_notification_area_icon, false);
  tr_variantDictAddBool (d, TR_KEY_start_minimized, false);
  tr_variantDictAddBool (d, TR_KEY_show_options_window, true);
  tr_variantDictAddBool (d, TR_KEY_show_statusbar, true);
  tr_variantDictAddBool (d, TR_KEY_show_toolbar, true);
  tr_variantDictAddBool (d, TR_KEY_show_tracker_scrapes, false);
  tr_variantDictAddBool (d, TR_KEY_sort_reversed, false);
  tr_variantDictAddBool (d, TR_KEY_torrent_added_notification_enabled, true);
  tr_variantDictAddBool (d, TR_KEY_torrent_complete_notification_enabled, true);
  tr_variantDictAddStr  (d, TR_KEY_torrent_complete_sound_command, "canberra-gtk-play -i complete-download -d 'transmission torrent downloaded'");
  tr_variantDictAddBool (d, TR_KEY_torrent_complete_sound_enabled, true);
  tr_variantDictAddBool (d, TR_KEY_user_has_given_informed_consent, false);
  tr_variantDictAddBool (d, TR_KEY_watch_dir_enabled, false);
  tr_variantDictAddInt  (d, TR_KEY_blocklist_date, 0);
  tr_variantDictAddInt  (d, TR_KEY_main_window_height, 500);
  tr_variantDictAddInt  (d, TR_KEY_main_window_width, 300);
  tr_variantDictAddInt  (d, TR_KEY_main_window_x, 50);
  tr_variantDictAddInt  (d, TR_KEY_main_window_y, 50);
  tr_variantDictAddInt  (d, TR_KEY_remote_session_port, atoi(TR_DEFAULT_RPC_PORT_STR));
  tr_variantDictAddStr  (d, TR_KEY_download_dir, tr_getDefaultDownloadDir());
  tr_variantDictAddStr  (d, TR_KEY_filter_mode, "all");
  tr_variantDictAddStr  (d, TR_KEY_main_window_layout_order, "menu,toolbar,filter,list,statusbar");
  tr_variantDictAddStr  (d, TR_KEY_open_dialog_dir, QDir::home().absolutePath().toUtf8());
  tr_variantDictAddStr  (d, TR_KEY_remote_session_host, "localhost");
  tr_variantDictAddStr  (d, TR_KEY_remote_session_password, "");
  tr_variantDictAddStr  (d, TR_KEY_remote_session_username, "");
  tr_variantDictAddStr  (d, TR_KEY_sort_mode, "sort-by-name");
  tr_variantDictAddStr  (d, TR_KEY_statusbar_stats, "total-ratio");
  tr_variantDictAddStr  (d, TR_KEY_watch_dir, tr_getDefaultDownloadDir());
}

/***
****
***/

bool
Prefs::getBool (int key) const
{
  assert (myItems[key].type == QVariant::Bool);
  return myValues[key].toBool();
}

QString
Prefs::getString (int key) const
{
  assert (myItems[key].type == QVariant::String);
  const QByteArray b = myValues[key].toByteArray();
  if (Utils::isValidUtf8 (b.constData()))
    myValues[key].setValue (QString::fromUtf8 (b.constData()));
  return myValues[key].toString();
}

int
Prefs::getInt (int key) const
{
  assert (myItems[key].type == QVariant::Int);
  return myValues[key].toInt();
}

double
Prefs::getDouble (int key) const
{
  assert (myItems[key].type == QVariant::Double);
  return myValues[key].toDouble();
}

QDateTime
Prefs::getDateTime (int key) const
{
  assert (myItems[key].type == QVariant::DateTime);
  return myValues[key].toDateTime();
}

/***
****
***/

void
Prefs::toggleBool (int key)
{
  set (key, !getBool(key));
}
