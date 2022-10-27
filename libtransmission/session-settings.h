// This file Copyright © 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <bitset>
#include <cstddef> // for size_t
#include <cstdint> // for int64_t
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "transmission.h"

#include "log.h" // for tr_log_level
#include "net.h" // for tr_port
#include "quark.h"

struct tr_variant;

namespace libtransmission
{

class Setting
{
public:
    using Value = std::variant<
        bool,
        double,
        tr_encryption_mode,
        int,
        tr_log_level,
        mode_t,
        tr_port,
        tr_preallocation_mode,
        size_t,
        std::string>;

    constexpr Setting() noexcept = default;

    Setting(tr_quark key, Value const& value)
        : key_{ key }
        , value_{ value }
    {
    }

    [[nodiscard]] constexpr auto key() const noexcept
    {
        return key_;
    }

    template<typename T>
    [[nodiscard]] constexpr auto const& get() const
    {
        return std::get<T>(value_);
    }

    template<typename T>
    constexpr bool set(T const& new_value)
    {
        if (get<T>() == new_value)
        {
            return false;
        }

        value_.emplace<T>(new_value);
        return true;
    }

    bool load(tr_variant*);
    void save(tr_variant*) const;

private:
    tr_quark key_ = TR_KEY_NONE;
    Value value_;
};

class SessionSettings
{
public:
    enum Field
    {
        SpeedDown = 0,
        SpeedEnabled,
        AltSpeedTimeBegin,
        AltSpeedTimeDay,
        AltSpeedTimeEnabled,
        AltSpeedTimeEnd,
        AltSpeedUp,
        AnnounceIp,
        AnnounceIpEnabled,
        AntiBruteForceEnabled,
        AntiBruteForceThreshold,
        BindAddressIpv4,
        BindAddressIpv6,
        BlocklistEnabled,
        BlocklistUrl,
        CacheSizeMb,
        DefaultTrackers,
        DhtEnabled,
        DownloadDir,
        DownloadQueueEnabled,
        DownloadQueueSize,
        Encryption,
        IdleSeedingLimit,
        IdleSeedingLimitEnabled,
        IncompleteDir,
        IncompleteDirEnabled,
        LpdEnabled,
        MessageLevel,
        PeerCongestionAlgorithm,
        PeerIdTtlHours,
        PeerLimitGlobal,
        PeerLimitPerTorrent,
        PeerPort,
        PeerPortRandomHigh,
        PeerPortRandomLow,
        PeerPortRandomOnStart,
        PeerSocketTos,
        PexEnabled,
        PortForwardingEnabled,
        Preallocation,
        PrefetchEnabled,
        QueueStalledEnabled,
        QueueStalledMinutes,
        RatioLimit,
        RatioLimitEnabled,
        RenamePartialFiles,
        RpcAuthenticationRequired,
        RpcBindAddress,
        RpcEnabled,
        RpcHostWhitelist,
        RpcHostWhitelistEnabled,
        RpcPassword,
        RpcPort,
        RpcSocketMode,
        RpcUrl,
        RpcUsername,
        RpcWhitelist,
        RpcWhitelistEnabled,
        ScrapePausedTorrentsEnabled,
        ScriptTorrentAddedEnabled,
        ScriptTorrentAddedFilename,
        ScriptTorrentDoneEnabled,
        ScriptTorrentDoneFilename,
        ScriptTorrentDoneSeedingEnabled,
        ScriptTorrentDoneSeedingFilename,
        SeedQueueEnabled,
        SeedQueueSize,
        SpeedLimitDown,
        SpeedLimitDownEnabled,
        SpeedLimitUp,
        SpeedLimitUpEnabled,
        StartAddedTorrents,
        TcpEnabled,
        TrashOriginalTorrentFiles,
        Umask,
        UploadSlotsPerTorrent,
        UtpEnabled,

        FieldCount
    };

    using Changed = std::bitset<FieldCount>;

    SessionSettings();

    Changed load(tr_variant* dict);

    void save(tr_variant* dict) const;

    template<typename T>
    [[nodiscard]] constexpr auto const& get(Field field) const
    {
        return settings_[field].get<T>();
    }

    template<typename T>
    constexpr bool set(Field field, T const& new_value)
    {
        return settings_[field].set<T>(new_value);
    }

    [[nodiscard]] constexpr auto key(Field field) const noexcept
    {
        return settings_[field].key();
    }

private:
    std::array<Setting, FieldCount> settings_;
};

} // namespace libtransmission
