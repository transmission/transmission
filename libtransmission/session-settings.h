// This file Copyright © 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

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
        int64_t,
        tr_log_level,
        mode_t,
        tr_port,
        tr_preallocation_mode,
        size_t,
        std::string>;

    enum Type
    {
        Bool,
        Double,
        Encryption,
        Int,
        Int64,
        Log,
        ModeT,
        Port,
        Preallocation,
        SizeT,
        String,

        TypeCount
    };

    Setting() = default;
    Setting(tr_quark key, Type type, Value const& default_value);

    void fromDict(tr_variant* dict);

    [[nodiscard]] static std::optional<Value> import(tr_variant* var, Type type);

    template<typename T>
    [[nodiscard]] auto const& get() const
    {
        static_assert(std::variant_size_v<Value> == TypeCount);
        return std::get<T>(value_);
    }

private:
    tr_quark key_;
    Type type_;
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

    SessionSettings();

    template<typename T>
    [[nodiscard]] auto const& get(Field field) const
    {
        return settings_[field].get<T>();
    }

private:
    std::array<Setting, FieldCount> settings_;
};

} // namespace libtransmission
