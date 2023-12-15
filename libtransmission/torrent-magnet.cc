// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <fstream>
#include <ios>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility> // std::move
#include <vector>

#include <fmt/core.h>

#include "libtransmission/transmission.h"

#include "libtransmission/crypto-utils.h" // for tr_sha1()
#include "libtransmission/error.h"
#include "libtransmission/file.h"
#include "libtransmission/quark.h"
#include "libtransmission/torrent-magnet.h"
#include "libtransmission/torrent-metainfo.h"
#include "libtransmission/torrent.h"
#include "libtransmission/tr-assert.h"
#include "libtransmission/utils.h"
#include "libtransmission/variant.h"

#define tr_logAddDebugMagnet(magnet, msg) tr_logAddDebug(msg, (magnet)->log_name())

namespace
{
// don't ask for the same metadata piece more than this often
auto constexpr MinRepeatIntervalSecs = int{ 3 };

[[nodiscard]] auto create_all_needed(int n_pieces)
{
    auto ret = std::deque<tr_incomplete_metadata::metadata_node>{};

    ret.resize(n_pieces);

    for (int i = 0; i < n_pieces; ++i)
    {
        ret[i].piece = i;
    }

    return ret;
}

[[nodiscard]] int div_ceil(int numerator, int denominator)
{
    auto const [quot, rem] = std::div(numerator, denominator);
    return quot + (rem == 0 ? 0 : 1);
}
} // namespace

tr_incomplete_metadata::tr_incomplete_metadata(std::unique_ptr<Mediator> mediator, int64_t const size)
    : mediator_{ std::move(mediator) }
{
    TR_ASSERT(is_valid_metadata_size(size));

    auto const n = div_ceil(static_cast<int>(size), MetadataPieceSize);
    tr_logAddDebugMagnet(this, fmt::format("metadata is {} bytes in {} pieces", size, n));

    piece_count = n;
    metadata.resize(size);
    pieces_needed = create_all_needed(n);
}

void tr_torrent::maybe_start_metadata_transfer(int64_t const size) noexcept
{
    if (has_metainfo() || incomplete_metadata)
    {
        return;
    }

    if (!tr_incomplete_metadata::is_valid_metadata_size(size))
    {
        TR_ASSERT(false);
        return;
    }

    incomplete_metadata = std::make_unique<tr_incomplete_metadata>(std::make_unique<MagnetMediator>(*this), size);
}

[[nodiscard]] std::optional<tr_metadata_piece> tr_torrent::get_metadata_piece(int const piece) const
{
    TR_ASSERT(piece >= 0);

    if (!has_metainfo())
    {
        return {};
    }

    auto const info_dict_size = this->info_dict_size();
    TR_ASSERT(info_dict_size > 0);
    if (auto const n_pieces = std::max(1, div_ceil(info_dict_size, MetadataPieceSize)); piece < 0 || piece >= n_pieces)
    {
        return {};
    }

    auto in = std::ifstream{ torrent_file(), std::ios_base::in | std::ios_base::binary };
    if (!in.is_open())
    {
        return {};
    }
    auto const offset_in_info_dict = static_cast<uint64_t>(piece) * MetadataPieceSize;
    if (auto const offset_in_file = info_dict_offset() + offset_in_info_dict; !in.seekg(offset_in_file))
    {
        return {};
    }

    auto const piece_len = offset_in_info_dict + MetadataPieceSize <= info_dict_size ? MetadataPieceSize :
                                                                                       info_dict_size - offset_in_info_dict;
    if (auto ret = tr_metadata_piece(piece_len); in.read(reinterpret_cast<char*>(std::data(ret)), std::size(ret)))
    {
        return ret;
    }

    return {};
}

bool tr_torrent::use_metainfo_from_file(tr_torrent_metainfo const* metainfo, char const* filename_in, tr_error* error)
{
    // add .torrent file
    if (!tr_sys_path_copy(filename_in, torrent_file().c_str(), error))
    {
        return false;
    }

    // remove .magnet file
    tr_sys_path_remove(magnet_file());

    // tor should keep this metainfo
    set_metainfo(*metainfo);

    incomplete_metadata.reset();

    return true;
}

// ---

namespace
{
namespace set_metadata_piece_helpers
{
tr_variant build_metainfo_except_info_dict(tr_torrent_metainfo const& tm)
{
    auto top = tr_variant::Map{ 8U };

    if (auto const& val = tm.comment(); !std::empty(val))
    {
        top.try_emplace(TR_KEY_comment, val);
    }

    if (auto const& val = tm.source(); !std::empty(val))
    {
        top.try_emplace(TR_KEY_source, val);
    }

    if (auto const& val = tm.creator(); !std::empty(val))
    {
        top.try_emplace(TR_KEY_created_by, val);
    }

    if (auto const val = tm.date_created(); val != 0)
    {
        top.try_emplace(TR_KEY_creation_date, val);
    }

    if (auto const& announce_list = tm.announce_list(); !std::empty(announce_list))
    {
        announce_list.add_to_map(top);
    }

    if (auto const n_webseeds = tm.webseed_count(); n_webseeds > 0U)
    {
        auto webseed_vec = tr_variant::Vector{};
        webseed_vec.reserve(n_webseeds);
        for (size_t i = 0U; i < n_webseeds; ++i)
        {
            webseed_vec.emplace_back(tm.webseed(i));
        }
    }

    return tr_variant{ std::move(top) };
}

bool use_new_metainfo(tr_torrent* tor, tr_error* error)
{
    auto const& m = tor->incomplete_metadata;
    TR_ASSERT(m);

    // test the info_dict checksum
    if (tr_sha1::digest(m->metadata) != tor->info_hash())
    {
        return false;
    }

    // checksum passed; now try to parse it as benc
    auto serde = tr_variant_serde::benc().inplace();
    auto info_dict_v = serde.parse(m->metadata);
    if (!info_dict_v)
    {
        if (error != nullptr)
        {
            *error = std::move(serde.error_);
            serde.error_ = {};
        }

        return false;
    }

    // yay we have an info dict. Let's make a torrent file
    auto top_var = build_metainfo_except_info_dict(tor->metainfo());
    tr_variantMergeDicts(tr_variantDictAddDict(&top_var, TR_KEY_info, 0), &*info_dict_v);
    auto const benc = serde.to_string(top_var);

    // does this synthetic torrent file parse?
    auto metainfo = tr_torrent_metainfo{};
    if (!metainfo.parse_benc(benc))
    {
        return false;
    }

    // save it
    if (!tr_file_save(tor->torrent_file(), benc, error))
    {
        return false;
    }

    // remove .magnet file
    tr_sys_path_remove(tor->magnet_file());

    // tor should keep this metainfo
    tor->set_metainfo(metainfo);

    return true;
}

void on_have_all_metainfo(tr_torrent* tor)
{
    auto error = tr_error{};
    auto& m = tor->incomplete_metadata;
    TR_ASSERT(m);

    if (!use_new_metainfo(tor, &error)) /* drat. */
    {
        auto msg = std::string_view{ error && !std::empty(error.message()) ? error.message() : "unknown error" };
        tr_logAddWarnTor(
            tor,
            fmt::format("Couldn't parse magnet metainfo: '{error}'. Redownloading metadata", fmt::arg("error", msg)));
    }

    m.reset();
}
} // namespace set_metadata_piece_helpers
} // namespace

void tr_torrentMagnetDoIdleWork(tr_torrent* const tor)
{
    using namespace set_metadata_piece_helpers;

    TR_ASSERT(tr_isTorrent(tor));

    if (auto const& m = tor->incomplete_metadata; m && std::empty(m->pieces_needed))
    {
        tr_logAddDebugTor(tor, fmt::format("we now have all the metainfo!"));
        on_have_all_metainfo(tor);
    }
}

void tr_torrent::set_metadata_piece(int const piece, void const* data, size_t const len)
{
    TR_ASSERT(data != nullptr);

    tr_logAddDebugTor(this, fmt::format("got metadata piece {} of {} bytes", piece, len));

    // are we set up to download metadata?
    auto& m = incomplete_metadata;
    if (!m)
    {
        return;
    }

    // sanity test: is `piece` in range?
    if (piece < 0 || piece >= m->piece_count)
    {
        return;
    }

    // sanity test: is `len` the right size?
    if (m->get_piece_length(piece) != len)
    {
        return;
    }

    // do we need this piece?
    auto& needed = m->pieces_needed;
    auto const iter = std::find_if(
        std::begin(needed),
        std::end(needed),
        [piece](auto const& item) { return item.piece == piece; });
    if (iter == std::end(needed))
    {
        return;
    }

    auto const offset = piece * MetadataPieceSize;
    std::copy_n(reinterpret_cast<char const*>(data), len, std::begin(m->metadata) + offset);

    needed.erase(iter);
    tr_logAddDebugTor(this, fmt::format("saving metainfo piece {}... {} remain", piece, std::size(needed)));
}

// ---

std::optional<int> tr_torrent::get_next_metadata_request(time_t now) noexcept
{
    auto& m = incomplete_metadata;
    if (!m)
    {
        return {};
    }

    auto& needed = m->pieces_needed;
    if (std::empty(needed) || needed.front().requested_at + MinRepeatIntervalSecs >= now)
    {
        return {};
    }

    auto req = needed.front();
    needed.pop_front();
    req.requested_at = now;
    needed.push_back(req);
    tr_logAddDebugTor(this, fmt::format("next piece to request: {}", req.piece));
    return req.piece;
}

double tr_torrent::get_metadata_percent() const noexcept
{
    if (has_metainfo())
    {
        return 1.0;
    }

    if (auto const& m = incomplete_metadata; m)
    {
        if (auto const n = m->piece_count; n != 0)
        {
            return (n - std::size(m->pieces_needed)) / static_cast<double>(n);
        }
    }

    return 0.0;
}

std::string tr_torrentGetMagnetLink(tr_torrent const* tor)
{
    return tor->magnet();
}

size_t tr_torrentGetMagnetLinkToBuf(tr_torrent const* tor, char* buf, size_t buflen)
{
    return tr_strv_to_buf(tr_torrentGetMagnetLink(tor), buf, buflen);
}
