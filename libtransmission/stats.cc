// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <initializer_list>
#include <optional>
#include <utility>

#include "libtransmission/api-compat.h"
#include "libtransmission/file.h"
#include "libtransmission/quark.h"
#include "libtransmission/serializer.h"
#include "libtransmission/stats.h"
#include "libtransmission/tr-strbuf.h"
#include "libtransmission/utils.h" // for tr_getRatio(), tr_time()
#include "libtransmission/variant.h"

using namespace std::literals;
using namespace tr;

namespace
{
template<auto MemberPtr>
using Field = serializer::Field<MemberPtr>;

constexpr auto Fields = std::tuple{
    Field<&tr_session_stats::downloadedBytes>{ TR_KEY_downloaded_bytes },
    Field<&tr_session_stats::filesAdded>{ TR_KEY_files_added },
    Field<&tr_session_stats::secondsActive>{ TR_KEY_seconds_active },
    Field<&tr_session_stats::sessionCount>{ TR_KEY_session_count },
    Field<&tr_session_stats::uploadedBytes>{ TR_KEY_uploaded_bytes },
};
} // namespace

tr_session_stats tr_stats::load_old_stats(std::string_view const config_dir)
{
    auto var = std::optional<tr_variant>{};

    if (auto file = tr_pathbuf{ config_dir, "/stats.json"sv }; tr_sys_path_exists(file))
    {
        var = tr_variant_serde::json().parse_file(file);
    }
    else if (auto oldfile = tr_pathbuf{ config_dir, "/stats.benc"sv }; tr_sys_path_exists(oldfile))
    {
        var = tr_variant_serde::benc().parse_file(oldfile);
    }

    if (!var)
    {
        return {};
    }

    api_compat::convert_incoming_data(*var);
    auto ret = tr_session_stats{};
    serializer::load(ret, Fields, *var);
    return ret;
}

void tr_stats::save() const
{
    auto var = tr_variant{ serializer::save(cumulative(), Fields) };
    api_compat::convert_outgoing_data(var);
    tr_variant_serde::json().to_file(var, tr_pathbuf{ config_dir_, "/stats.json"sv });
}

void tr_stats::clear()
{
    single_ = old_ = Zero;
    start_time_ = tr_time();
}

[[nodiscard]] tr_session_stats tr_stats::current() const
{
    auto ret = single_;
    ret.secondsActive = time(nullptr) - start_time_;
    ret.ratio = tr_getRatio(ret.uploadedBytes, ret.downloadedBytes);
    return ret;
}

tr_session_stats tr_stats::add(tr_session_stats const& a, tr_session_stats const& b)
{
    auto ret = tr_session_stats{};
    ret.uploadedBytes = a.uploadedBytes + b.uploadedBytes;
    ret.downloadedBytes = a.downloadedBytes + b.downloadedBytes;
    ret.filesAdded = a.filesAdded + b.filesAdded;
    ret.sessionCount = a.sessionCount + b.sessionCount;
    ret.secondsActive = a.secondsActive + b.secondsActive;
    ret.ratio = tr_getRatio(ret.uploadedBytes, ret.downloadedBytes);
    return ret;
}
