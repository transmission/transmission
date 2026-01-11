// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <array>

#include <QObject>
#include <QString>
#include <QVariant>

#include <libtransmission/quark.h>
#include <libtransmission/variant.h>

#include <libtransmission-app/display-modes.h>

class Prefs : public QObject
{
    Q_OBJECT

public:
    enum
    {
        /* client prefs */
        OPTIONS_PROMPT,
        OPEN_DIALOG_FOLDER,
        INHIBIT_HIBERNATION,
        DIR_WATCH,
        DIR_WATCH_ENABLED,
        SHOW_TRAY_ICON,
        START_MINIMIZED,
        SHOW_NOTIFICATION_ON_ADD,
        SHOW_NOTIFICATION_ON_COMPLETE,
        ASKQUIT,
        SORT_MODE,
        SORT_REVERSED,
        COMPACT_VIEW,
        FILTERBAR,
        STATUSBAR,
        STATUSBAR_STATS,
        SHOW_TRACKER_SCRAPES,
        SHOW_BACKUP_TRACKERS,
        TOOLBAR,
        BLOCKLIST_DATE,
        BLOCKLIST_UPDATES_ENABLED,
        MAIN_WINDOW_LAYOUT_ORDER,
        MAIN_WINDOW_HEIGHT,
        MAIN_WINDOW_WIDTH,
        MAIN_WINDOW_X,
        MAIN_WINDOW_Y,
        FILTER_MODE,
        FILTER_TRACKERS,
        FILTER_TEXT,
        SESSION_IS_REMOTE,
        SESSION_REMOTE_HOST,
        SESSION_REMOTE_HTTPS,
        SESSION_REMOTE_PASSWORD,
        SESSION_REMOTE_PORT,
        SESSION_REMOTE_AUTH,
        SESSION_REMOTE_USERNAME,
        SESSION_REMOTE_URL_BASE_PATH,
        COMPLETE_SOUND_COMMAND,
        COMPLETE_SOUND_ENABLED,
        READ_CLIPBOARD,
        /* core prefs */
        FIRST_CORE_PREF,
        ALT_SPEED_LIMIT_UP = FIRST_CORE_PREF,
        ALT_SPEED_LIMIT_DOWN,
        ALT_SPEED_LIMIT_ENABLED,
        ALT_SPEED_LIMIT_TIME_BEGIN,
        ALT_SPEED_LIMIT_TIME_END,
        ALT_SPEED_LIMIT_TIME_ENABLED,
        ALT_SPEED_LIMIT_TIME_DAY,
        BLOCKLIST_ENABLED,
        BLOCKLIST_URL,
        DEFAULT_TRACKERS,
        DSPEED,
        DSPEED_ENABLED,
        DOWNLOAD_DIR,
        DOWNLOAD_QUEUE_ENABLED,
        DOWNLOAD_QUEUE_SIZE,
        ENCRYPTION,
        IDLE_LIMIT,
        IDLE_LIMIT_ENABLED,
        INCOMPLETE_DIR,
        INCOMPLETE_DIR_ENABLED,
        MSGLEVEL,
        PEER_LIMIT_GLOBAL,
        PEER_LIMIT_TORRENT,
        PEER_PORT,
        PEER_PORT_RANDOM_ON_START,
        PEER_PORT_RANDOM_LOW,
        PEER_PORT_RANDOM_HIGH,
        QUEUE_STALLED_MINUTES,
        SCRIPT_TORRENT_DONE_ENABLED,
        SCRIPT_TORRENT_DONE_FILENAME,
        SCRIPT_TORRENT_DONE_SEEDING_ENABLED,
        SCRIPT_TORRENT_DONE_SEEDING_FILENAME,
        SOCKET_DIFFSERV,
        START,
        TRASH_ORIGINAL,
        PEX_ENABLED,
        DHT_ENABLED,
        UTP_ENABLED,
        LPD_ENABLED,
        PORT_FORWARDING,
        PREALLOCATION,
        RATIO,
        RATIO_ENABLED,
        RENAME_PARTIAL_FILES,
        RPC_AUTH_REQUIRED,
        RPC_ENABLED,
        RPC_PASSWORD,
        RPC_PORT,
        RPC_USERNAME,
        RPC_WHITELIST_ENABLED,
        RPC_WHITELIST,
        USPEED_ENABLED,
        USPEED,
        UPLOAD_SLOTS_PER_TORRENT,
        LAST_CORE_PREF = UPLOAD_SLOTS_PER_TORRENT,
        //
        PREFS_COUNT
    };

    Prefs();
    Prefs(Prefs&&) = delete;
    Prefs(Prefs const&) = delete;
    Prefs& operator=(Prefs&&) = delete;
    Prefs& operator=(Prefs const&) = delete;
    ~Prefs() override = default;

    [[nodiscard]] static auto constexpr isCore(int const idx)
    {
        return FIRST_CORE_PREF <= idx && idx <= LAST_CORE_PREF;
    }

    [[nodiscard]] static auto constexpr getKey(int const idx)
    {
        return Items[idx].key;
    }

    [[nodiscard]] static auto constexpr type(int const idx)
    {
        return Items[idx].type;
    }

    void loadFromConfigDir(QString dir);

    void load(tr_variant::Map const& settings);

    // DEPRECATED
    [[nodiscard]] constexpr auto const& variant(int const idx) const noexcept
    {
        return values_[idx];
    }

    template<typename T>
    void set(int key, T const& value)
    {
        QVariant& v(values_[key]);
        QVariant const tmp = QVariant::fromValue(value);

        if (v.isNull() || v != tmp)
        {
            v = tmp;
            emit changed(key);
        }
    }

    template<typename T>
    [[nodiscard]] T get(int const idx) const
    {
        return values_[idx].value<T>();
    }

    void save(QString const& filename) const;

signals:
    void changed(int idx);

private:
    struct PrefItem
    {
        int id;
        tr_quark key;
        int type;
    };

    static std::array<PrefItem, PREFS_COUNT> const Items;

    [[nodiscard]] static tr_variant::Map get_defaults();

    void set(int key, char const* value) = delete;

    std::array<QVariant, PREFS_COUNT> mutable values_;
};
