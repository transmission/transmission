// This file Copyright © 2012-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <climits> /* INT_MAX */
#include <ctime>
#include <string>
#include <string_view>

#include <event2/buffer.h>

#include <fmt/core.h>

#include "transmission.h"

#include "crypto-utils.h" /* tr_sha1() */
#include "error.h"
#include "file.h"
#include "log.h"
#include "magnet-metainfo.h"
#include "resume.h"
#include "torrent-magnet.h"
#include "torrent-metainfo.h"
#include "torrent.h"
#include "tr-assert.h"
#include "utils.h"
#include "variant.h"
#include "web-utils.h"

using namespace std::literals;

/***
****
***/

/* don't ask for the same metadata piece more than this often */
static auto constexpr MinRepeatIntervalSecs = int{ 3 };

struct metadata_node
{
    time_t requestedAt;
    int piece;
};

struct tr_incomplete_metadata
{
    char* metadata;
    size_t metadata_size;
    int pieceCount;

    /** sorted from least to most recently requested */
    struct metadata_node* piecesNeeded;
    int piecesNeededCount;
};

static void incompleteMetadataFree(struct tr_incomplete_metadata* m)
{
    tr_free(m->metadata);
    tr_free(m->piecesNeeded);
    tr_free(m);
}

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

    int const n = (size <= 0 || size > INT_MAX) ? -1 : size / METADATA_PIECE_SIZE + (size % METADATA_PIECE_SIZE != 0 ? 1 : 0);

    tr_logAddDebugTor(tor, fmt::format("metadata is {} bytes in {} pieces", size, n));

    if (n <= 0)
    {
        return false;
    }

    auto* const m = tr_new(struct tr_incomplete_metadata, 1);

    if (m == nullptr)
    {
        return false;
    }

    m->pieceCount = n;
    m->metadata = tr_new(char, size);
    m->metadata_size = size;
    m->piecesNeededCount = n;
    m->piecesNeeded = tr_new(struct metadata_node, n);

    if (m->metadata == nullptr || m->piecesNeeded == nullptr)
    {
        incompleteMetadataFree(m);
        return false;
    }

    for (int i = 0; i < n; ++i)
    {
        m->piecesNeeded[i].piece = i;
        m->piecesNeeded[i].requestedAt = 0;
    }

    tor->incompleteMetadata = m;
    return true;
}

void* tr_torrentGetMetadataPiece(tr_torrent const* tor, int piece, size_t* len)
{
    TR_ASSERT(tr_isTorrent(tor));
    TR_ASSERT(piece >= 0);
    TR_ASSERT(len != nullptr);

    if (!tor->hasMetainfo())
    {
        return nullptr;
    }

    auto const fd = tr_sys_file_open(tor->torrentFile(), TR_SYS_FILE_READ, 0);
    if (fd == TR_BAD_SYS_FILE)
    {
        return nullptr;
    }

    auto const info_dict_size = tor->infoDictSize();
    TR_ASSERT(info_dict_size > 0);

    char* ret = nullptr;
    if (size_t o = piece * METADATA_PIECE_SIZE; tr_sys_file_seek(fd, tor->infoDictOffset() + o, TR_SEEK_SET, nullptr))
    {
        size_t const l = o + METADATA_PIECE_SIZE <= info_dict_size ? METADATA_PIECE_SIZE : info_dict_size - o;

        if (0 < l && l <= METADATA_PIECE_SIZE)
        {
            auto* buf = tr_new(char, l);

            if (auto n = uint64_t{}; tr_sys_file_read(fd, buf, l, &n) && n == l)
            {
                *len = l;
                ret = buf;
                buf = nullptr;
            }

            tr_free(buf);
        }
    }

    tr_sys_file_close(fd);

    TR_ASSERT(ret == nullptr || *len > 0);
    return ret;
}

static int getPieceNeededIndex(struct tr_incomplete_metadata const* m, int piece)
{
    for (int i = 0; i < m->piecesNeededCount; ++i)
    {
        if (m->piecesNeeded[i].piece == piece)
        {
            return i;
        }
    }

    return -1;
}

static int getPieceLength(struct tr_incomplete_metadata const* m, int piece)
{
    return piece + 1 == m->pieceCount ? // last piece
        m->metadata_size - (piece * METADATA_PIECE_SIZE) :
        METADATA_PIECE_SIZE;
}

static void tr_buildMetainfoExceptInfoDict(tr_torrent_metainfo const& tm, tr_variant* top)
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
        incompleteMetadataFree(tor->incompleteMetadata);
        tor->incompleteMetadata = nullptr;
    }
    tor->isStopping = true;
    tor->magnetVerify = true;
    if (tr_sessionGetPaused(tor->session))
    {
        tor->startAfterVerify = false;
    }
    tor->markEdited();

    return true;
}

static bool useNewMetainfo(tr_torrent* tor, tr_incomplete_metadata const* m, tr_error** error)
{
    // test the info_dict checksum
    auto const sha1 = tr_sha1(std::string_view{ m->metadata, m->metadata_size });
    if (bool const checksum_passed = sha1 && *sha1 == tor->infoHash(); !checksum_passed)
    {
        return false;
    }

    // checksum passed; now try to parse it as benc
    auto info_dict_v = tr_variant{};
    if (!tr_variantFromBuf(
            &info_dict_v,
            TR_VARIANT_PARSE_BENC | TR_VARIANT_PARSE_INPLACE,
            { m->metadata, m->metadata_size },
            nullptr,
            error))
    {
        return false;
    }

    // yay we have an info dict. Let's make a torrent file
    auto top_v = tr_variant{};
    tr_buildMetainfoExceptInfoDict(tor->metainfo_, &top_v);
    tr_variantMergeDicts(tr_variantDictAddDict(&top_v, TR_KEY_info, 0), &info_dict_v);
    auto const benc = tr_variantToStr(&top_v, TR_VARIANT_FMT_BENC);
    tr_variantFree(&top_v);
    tr_variantFree(&info_dict_v);

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

static void onHaveAllMetainfo(tr_torrent* tor, tr_incomplete_metadata* m)
{
    tr_error* error = nullptr;
    if (useNewMetainfo(tor, m, &error))
    {
        incompleteMetadataFree(tor->incompleteMetadata);
        tor->incompleteMetadata = nullptr;
        tor->isStopping = true;
        tor->magnetVerify = true;
        if (tr_sessionGetPaused(tor->session))
        {
            tor->startAfterVerify = false;
        }
        tor->markEdited();
    }
    else /* drat. */
    {
        int const n = m->pieceCount;

        for (int i = 0; i < n; ++i)
        {
            m->piecesNeeded[i].piece = i;
            m->piecesNeeded[i].requestedAt = 0;
        }

        m->piecesNeededCount = n;
        char const* const msg = error != nullptr && error->message != nullptr ? error->message : "unknown error";
        tr_logAddWarnTor(
            tor,
            fmt::format(
                ngettext(
                    "Couldn't parse magnet metainfo: '{error}'. Redownloading {piece_count} piece",
                    "Couldn't parse magnet metainfo: '{error}'. Redownloading {piece_count} pieces",
                    n),
                fmt::arg("error", msg),
                fmt::arg("piece_count", n)));
        tr_error_clear(&error);
    }
}

void tr_torrentSetMetadataPiece(tr_torrent* tor, int piece, void const* data, int len)
{
    TR_ASSERT(tr_isTorrent(tor));
    TR_ASSERT(data != nullptr);
    TR_ASSERT(len >= 0);

    tr_logAddDebugTor(tor, fmt::format("got metadata piece {} of {} bytes", piece, len));

    // are we set up to download metadata?
    tr_incomplete_metadata* const m = tor->incompleteMetadata;
    if (m == nullptr)
    {
        return;
    }

    // sanity test: is `piece` in range?
    if ((piece < 0) || (piece >= m->pieceCount))
    {
        return;
    }

    // sanity test: is `len` the right size?
    if (getPieceLength(m, piece) != len)
    {
        return;
    }

    // do we need this piece?
    int const idx = getPieceNeededIndex(m, piece);
    if (idx == -1)
    {
        return;
    }

    size_t const offset = piece * METADATA_PIECE_SIZE;
    std::copy_n(reinterpret_cast<char const*>(data), len, m->metadata + offset);

    tr_removeElementFromArray(m->piecesNeeded, idx, sizeof(struct metadata_node), m->piecesNeededCount);
    --m->piecesNeededCount;

    tr_logAddDebugTor(tor, fmt::format("saving metainfo piece {}... {} remain", piece, m->piecesNeededCount));

    /* are we done? */
    if (m->piecesNeededCount == 0)
    {
        tr_logAddDebugTor(tor, fmt::format("metainfo piece {} was the last one", piece));
        onHaveAllMetainfo(tor, m);
    }
}

bool tr_torrentGetNextMetadataRequest(tr_torrent* tor, time_t now, int* setme_piece)
{
    TR_ASSERT(tr_isTorrent(tor));

    bool have_request = false;
    struct tr_incomplete_metadata* m = tor->incompleteMetadata;

    if (m != nullptr && m->piecesNeededCount > 0 && m->piecesNeeded[0].requestedAt + MinRepeatIntervalSecs < now)
    {
        int const piece = m->piecesNeeded[0].piece;
        tr_removeElementFromArray(m->piecesNeeded, 0, sizeof(struct metadata_node), m->piecesNeededCount);

        int i = m->piecesNeededCount - 1;
        m->piecesNeeded[i].piece = piece;
        m->piecesNeeded[i].requestedAt = now;

        tr_logAddDebugTor(tor, fmt::format("next piece to request: {}", piece));
        *setme_piece = piece;
        have_request = true;
    }

    return have_request;
}

double tr_torrentGetMetadataPercent(tr_torrent const* tor)
{
    if (tor->hasMetainfo())
    {
        return 1.0;
    }

    auto const* const m = tor->incompleteMetadata;
    return m == nullptr || m->pieceCount == 0 ? 0.0 : (m->pieceCount - m->piecesNeededCount) / (double)m->pieceCount;
}

char* tr_torrentGetMagnetLink(tr_torrent const* tor)
{
    return tr_strvDup(tor->metainfo_.magnet());
}
