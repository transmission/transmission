// This file Copyright © 2012-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <climits> /* INT_MAX */
#include <ctime>
#include <deque>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/core.h>

#include "transmission.h"

#include "crypto-utils.h" /* tr_sha1() */
#include "error.h"
#include "log.h"
#include "magnet-metainfo.h"
#include "resume.h"
#include "torrent-magnet.h"
#include "torrent-metainfo.h"
#include "torrent.h"
#include "tr-assert.h"
#include "utils.h"
#include "variant.h"

namespace
{
struct metadata_node
{
    time_t requested_at = 0U;
    int piece = 0;
};
} // namespace

struct tr_incomplete_metadata
{
    std::vector<char> metadata;

    /** sorted from least to most recently requested */
    std::deque<metadata_node> pieces_needed;

    int piece_count = 0;
};

namespace
{
// don't ask for the same metadata piece more than this often
auto constexpr MinRepeatIntervalSecs = int{ 3 };

auto create_all_needed(int n_pieces)
{
    auto ret = std::deque<metadata_node>{};

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

bool tr_torrentSetMetadataSizeHint(tr_torrent* tor, int64_t size)
{
    if (tor->hasMetainfo())
    {
        return false;
    }

    if (tor->incompleteMetadata != nullptr)
    {
        return false;
    }

    int const n = (size <= 0 || size > INT_MAX) ? -1 : div_ceil(size, METADATA_PIECE_SIZE);
    tr_logAddDebugTor(tor, fmt::format("metadata is {} bytes in {} pieces", size, n));
    if (n <= 0)
    {
        return false;
    }

    auto* const m = new tr_incomplete_metadata{};

    if (m == nullptr)
    {
        return false;
    }

    m->piece_count = n;
    m->metadata.resize(size);
    m->pieces_needed = create_all_needed(n);

    if (std::empty(m->metadata) || std::empty(m->pieces_needed))
    {
        delete m;
        return false;
    }

    tor->incompleteMetadata = m;
    return true;
}

std::optional<std::vector<std::byte>> tr_torrentGetMetadataPiece(tr_torrent const* tor, int piece)
{
    TR_ASSERT(tr_isTorrent(tor));
    TR_ASSERT(piece >= 0);

    if (!tor->hasMetainfo())
    {
        return {};
    }

    auto const n_pieces = std::max(1, div_ceil(tor->infoDictSize(), METADATA_PIECE_SIZE));
    if (piece < 0 || piece >= n_pieces)
    {
        return {};
    }

    auto in = std::ifstream{ tor->torrentFile(), std::ios_base::in };
    if (!in.is_open())
    {
        return {};
    }

    auto const info_dict_size = tor->infoDictSize();
    TR_ASSERT(info_dict_size > 0);
    auto const offset_in_info_dict = static_cast<uint64_t>(piece) * METADATA_PIECE_SIZE;
    if (auto const offset_in_file = tor->infoDictOffset() + offset_in_info_dict; !in.seekg(offset_in_file))
    {
        return {};
    }

    auto buf = std::vector<std::byte>{};
    auto const piece_len = offset_in_info_dict + METADATA_PIECE_SIZE <= info_dict_size ? METADATA_PIECE_SIZE :
                                                                                         info_dict_size - offset_in_info_dict;
    buf.resize(piece_len);
    if (!in.read(reinterpret_cast<char*>(std::data(buf)), std::size(buf)))
    {
        return {};
    }

    return buf;
}

bool tr_torrentUseMetainfoFromFile(
    tr_torrent* tor,
    tr_torrent_metainfo const* metainfo,
    char const* filename_in,
    tr_error** error)
{
    // add .torrent file
    if (!tr_sys_path_copy(filename_in, tor->torrentFile(), error))
    {
        return false;
    }

    // remove .magnet file
    tr_sys_path_remove(tor->magnetFile());

    // tor should keep this metainfo
    tor->setMetainfo(*metainfo);

    if (tor->incompleteMetadata != nullptr)
    {
        delete tor->incompleteMetadata;
        tor->incompleteMetadata = nullptr;
    }

    return true;
}

// ---

namespace
{
namespace set_metadata_piece_helpers
{
[[nodiscard]] constexpr size_t get_piece_length(struct tr_incomplete_metadata const* m, int piece)
{
    return piece + 1 == m->piece_count ? // last piece
        std::size(m->metadata) - (piece * METADATA_PIECE_SIZE) :
        METADATA_PIECE_SIZE;
}

void build_metainfo_except_info_dict(tr_torrent_metainfo const& tm, tr_variant* top)
{
    tr_variantInitDict(top, 6);

    if (auto const& val = tm.comment(); !std::empty(val))
    {
        tr_variantDictAddStr(top, TR_KEY_comment, val);
    }

    if (auto const& val = tm.source(); !std::empty(val))
    {
        tr_variantDictAddStr(top, TR_KEY_source, val);
    }

    if (auto const& val = tm.creator(); !std::empty(val))
    {
        tr_variantDictAddStr(top, TR_KEY_created_by, val);
    }

    if (auto const val = tm.dateCreated(); val != 0)
    {
        tr_variantDictAddInt(top, TR_KEY_creation_date, val);
    }

    if (auto const& announce_list = tm.announceList(); !std::empty(announce_list))
    {
        auto const n = std::size(announce_list);
        if (n == 1)
        {
            tr_variantDictAddStr(top, TR_KEY_announce, announce_list.at(0).announce.sv());
        }
        else
        {
            auto* const announce_list_variant = tr_variantDictAddList(top, TR_KEY_announce_list, n);
            tr_variant* tier_variant = nullptr;
            auto current_tier = std::optional<tr_tracker_tier_t>{};
            for (auto const& tracker : announce_list)
            {
                if (!current_tier || *current_tier != tracker.tier)
                {
                    tier_variant = tr_variantListAddList(announce_list_variant, n);
                }

                tr_variantListAddStr(tier_variant, tracker.announce.sv());
            }
        }
    }

    if (auto const n_webseeds = tm.webseedCount(); n_webseeds > 0)
    {
        auto* const webseeds_variant = tr_variantDictAddList(top, TR_KEY_url_list, n_webseeds);
        for (size_t i = 0; i < n_webseeds; ++i)
        {
            tr_variantListAddStr(webseeds_variant, tm.webseed(i));
        }
    }
}

bool use_new_metainfo(tr_torrent* tor, tr_incomplete_metadata const* m, tr_error** error)
{
    // test the info_dict checksum
    if (tr_sha1::digest(m->metadata) != tor->infoHash())
    {
        return false;
    }

    // checksum passed; now try to parse it as benc
    auto info_dict_v = tr_variant{};
    if (!tr_variantFromBuf(&info_dict_v, TR_VARIANT_PARSE_BENC | TR_VARIANT_PARSE_INPLACE, m->metadata, nullptr, error))
    {
        return false;
    }

    // yay we have an info dict. Let's make a torrent file
    auto top_v = tr_variant{};
    build_metainfo_except_info_dict(tor->metainfo_, &top_v);
    tr_variantMergeDicts(tr_variantDictAddDict(&top_v, TR_KEY_info, 0), &info_dict_v);
    auto const benc = tr_variantToStr(&top_v, TR_VARIANT_FMT_BENC);
    tr_variantClear(&top_v);
    tr_variantClear(&info_dict_v);

    // does this synthetic torrent file parse?
    auto metainfo = tr_torrent_metainfo{};
    if (!metainfo.parseBenc(benc))
    {
        return false;
    }

    // save it
    if (!tr_saveFile(tor->torrentFile(), benc, error))
    {
        return false;
    }

    // remove .magnet file
    tr_sys_path_remove(tor->magnetFile());

    // tor should keep this metainfo
    tor->setMetainfo(metainfo);

    return true;
}

void on_have_all_metainfo(tr_torrent* tor, tr_incomplete_metadata* m)
{
    tr_error* error = nullptr;
    if (use_new_metainfo(tor, m, &error))
    {
        delete tor->incompleteMetadata;
        tor->incompleteMetadata = nullptr;
    }
    else /* drat. */
    {
        int const n = m->piece_count;

        m->pieces_needed = create_all_needed(n);

        char const* const msg = error != nullptr && error->message != nullptr ? error->message : "unknown error";
        tr_logAddWarnTor(
            tor,
            fmt::format(
                tr_ngettext(
                    "Couldn't parse magnet metainfo: '{error}'. Redownloading {piece_count} piece",
                    "Couldn't parse magnet metainfo: '{error}'. Redownloading {piece_count} pieces",
                    n),
                fmt::arg("error", msg),
                fmt::arg("piece_count", n)));
        tr_error_clear(&error);
    }
}
} // namespace set_metadata_piece_helpers
} // namespace

void tr_torrentMagnetDoIdleWork(tr_torrent* const tor)
{
    using namespace set_metadata_piece_helpers;

    TR_ASSERT(tr_isTorrent(tor));

    if (auto* const m = tor->incompleteMetadata; m != nullptr && std::empty(m->pieces_needed))
    {
        tr_logAddDebugTor(tor, fmt::format("we now have all the metainfo!"));
        on_have_all_metainfo(tor, m);
    }
}

void tr_torrentSetMetadataPiece(tr_torrent* tor, int piece, void const* data, size_t len)
{
    using namespace set_metadata_piece_helpers;

    TR_ASSERT(tr_isTorrent(tor));
    TR_ASSERT(data != nullptr);

    tr_logAddDebugTor(tor, fmt::format("got metadata piece {} of {} bytes", piece, len));

    // are we set up to download metadata?
    tr_incomplete_metadata* const m = tor->incompleteMetadata;
    if (m == nullptr)
    {
        return;
    }

    // sanity test: is `piece` in range?
    if ((piece < 0) || (piece >= m->piece_count))
    {
        return;
    }

    // sanity test: is `len` the right size?
    if (get_piece_length(m, piece) != len)
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

    size_t const offset = piece * METADATA_PIECE_SIZE;
    std::copy_n(reinterpret_cast<char const*>(data), len, std::begin(m->metadata) + offset);

    needed.erase(iter);
    tr_logAddDebugTor(tor, fmt::format("saving metainfo piece {}... {} remain", piece, std::size(needed)));
}

// ---

std::optional<int> tr_torrentGetNextMetadataRequest(tr_torrent* tor, time_t now)
{
    TR_ASSERT(tr_isTorrent(tor));

    struct tr_incomplete_metadata* m = tor->incompleteMetadata;
    if (m == nullptr)
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
    tr_logAddDebugTor(tor, fmt::format("next piece to request: {}", req.piece));
    return req.piece;
}

double tr_torrentGetMetadataPercent(tr_torrent const* tor)
{
    if (tor->hasMetainfo())
    {
        return 1.0;
    }

    auto const* const m = tor->incompleteMetadata;
    return m == nullptr || m->piece_count == 0 ? 0.0 : (m->piece_count - std::size(m->pieces_needed)) / (double)m->piece_count;
}

std::string tr_torrentGetMagnetLink(tr_torrent const* tor)
{
    return std::string{ tor->metainfo_.magnet().sv() };
}

size_t tr_torrentGetMagnetLinkToBuf(tr_torrent const* tor, char* buf, size_t buflen)
{
    return tr_strvToBuf(tr_torrentGetMagnetLink(tor), buf, buflen);
}
