// This file Copyright © 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <bitset>
#include <cstddef> // for size_t
#include <cstdint> // for int64_t
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

    Setting() = default;
    Setting(tr_quark key, Value const& default_value);

    [[nodiscard]] static std::optional<Value> parse(tr_variant* var, size_t desired_type);

    [[nodiscard]] constexpr auto key() const noexcept
    {
        return key_;
    }

    bool import(tr_variant* var)
    {
        auto const idx = default_value_.index();

        if (auto value = parse(var, idx); value && value->index() == idx)
        {
            value_ = *value;
            return true;
        }

        return false;
    }

    template<typename T>
    [[nodiscard]] constexpr auto const& get() const
    {
        //static_assert(std::variant_size_v<Value> == TypeCount);
        return std::get<T>(value_);
    }

    template<typename T>
    constexpr bool set(T const& new_value)
    {
        if (get<T>() != new_value)
        {
            value_.emplace<T>(new_value);
            return true;
        }

        return false;
    }

private:
    tr_quark key_ = {};
    Value default_value_;
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

    Changed import(tr_variant* dict);

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

private:
    std::array<Setting, FieldCount> settings_;
};

} // namespace libtransmission
