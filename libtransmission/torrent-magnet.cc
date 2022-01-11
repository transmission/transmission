/*
 * This file Copyright (C) 2012-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <algorithm>
#include <climits> /* INT_MAX */
#include <cstring> /* memcpy(), memset(), memcmp() */
#include <ctime>
#include <string_view>

#include <event2/buffer.h>

#include "transmission.h"

#include "crypto-utils.h" /* tr_sha1() */
#include "error.h"
#include "file.h"
#include "log.h"
#include "magnet-metainfo.h"
#include "metainfo.h"
#include "resume.h"
#include "torrent-magnet.h"
#include "torrent-metainfo.h"
#include "torrent.h"
#include "tr-assert.h"
#include "utils.h"
#include "variant.h"
#include "web-utils.h"

using namespace std::literals;

#define dbgmsg(tor, ...) tr_logAddDeepNamed(tr_torrentName(tor), __VA_ARGS__)

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
    if (tor->hasMetadata())
    {
        return false;
    }

    if (tor->incompleteMetadata != nullptr)
    {
        return false;
    }

    int const n = (size <= 0 || size > INT_MAX) ? -1 : size / METADATA_PIECE_SIZE + (size % METADATA_PIECE_SIZE != 0 ? 1 : 0);

    dbgmsg(tor, "metadata is %" PRId64 " bytes in %d pieces", size, n);

    if (n <= 0)
    {
        return false;
    }

    struct tr_incomplete_metadata* m = tr_new(struct tr_incomplete_metadata, 1);

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

static size_t findInfoDictOffset(tr_torrent const* tor)
{
    // load the torrent's .torrent file
    auto benc = std::vector<char>{};
    if (!tr_loadFile(benc, tor->torrentFile()) || std::empty(benc))
    {
        return {};
    }

    // parse the benc
    auto top = tr_variant{};
    auto const benc_sv = std::string_view{ std::data(benc), std::size(benc) };
    if (!tr_variantFromBuf(&top, TR_VARIANT_PARSE_BENC | TR_VARIANT_PARSE_INPLACE, benc_sv))
    {
        return {};
    }

    auto offset = size_t{};
    tr_variant* info_dict = nullptr;
    if (tr_variantDictFindDict(&top, TR_KEY_info, &info_dict))
    {
        auto const info_dict_benc = tr_variantToStr(info_dict, TR_VARIANT_FMT_BENC);
        auto const it = std::search(std::begin(benc), std::end(benc), std::begin(info_dict_benc), std::end(info_dict_benc));
        if (it != std::end(benc))
        {
            offset = std::distance(std::begin(benc), it);
        }
    }

    tr_variantFree(&top);
    return offset;
}

static void ensureInfoDictOffsetIsCached(tr_torrent* tor)
{
    TR_ASSERT(tor->hasMetadata());

    if (!tor->info_dict_offset_is_cached)
    {
        tor->info_dict_offset = findInfoDictOffset(tor);
        tor->info_dict_offset_is_cached = true;
    }
}

void* tr_torrentGetMetadataPiece(tr_torrent* tor, int piece, size_t* len)
{
    TR_ASSERT(tr_isTorrent(tor));
    TR_ASSERT(piece >= 0);
    TR_ASSERT(len != nullptr);

    if (!tor->hasMetadata())
    {
        return nullptr;
    }

    auto const fd = tr_sys_file_open(tor->torrentFile().c_str(), TR_SYS_FILE_READ, 0, nullptr);
    if (fd == TR_BAD_SYS_FILE)
    {
        return nullptr;
    }

    ensureInfoDictOffsetIsCached(tor);

    auto const info_dict_size = tor->infoDictSize();
    TR_ASSERT(info_dict_size > 0);

    char* ret = nullptr;
    if (size_t o = piece * METADATA_PIECE_SIZE; tr_sys_file_seek(fd, tor->infoDictOffset() + o, TR_SEEK_SET, nullptr, nullptr))
    {
        size_t const l = o + METADATA_PIECE_SIZE <= info_dict_size ? METADATA_PIECE_SIZE : info_dict_size - o;

        if (0 < l && l <= METADATA_PIECE_SIZE)
        {
            char* buf = tr_new(char, l);
            auto n = uint64_t{};

            if (tr_sys_file_read(fd, buf, l, &n, nullptr) && n == l)
            {
                *len = l;
                ret = buf;
                buf = nullptr;
            }

            tr_free(buf);
        }
    }

    tr_sys_file_close(fd, nullptr);

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

static void tr_buildMetainfoExceptInfoDict(tr_info const& tm, tr_variant* top)
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
            tr_variantDictAddStr(top, TR_KEY_announce, announce_list.at(0).announce_str.sv());
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

                tr_variantListAddStr(tier_variant, tracker.announce_str.sv());
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

    if (tm.fileCount() == 0)
    {
        // local transmission extensions.
        // these temporary placeholders are used for magnets until we have the info dict.
        auto* const magnet_info = tr_variantDictAddDict(top, TR_KEY_magnet_info, 2);
        tr_variantDictAddStr(
            magnet_info,
            TR_KEY_info_hash,
            std::string_view{ reinterpret_cast<char const*>(std::data(tm.infoHash())), std::size(tm.infoHash()) });
        if (auto const& val = tm.name(); !std::empty(val))
        {
            tr_variantDictAddStr(magnet_info, TR_KEY_display_name, val);
        }
    }
}

static bool useNewMetainfo(tr_torrent* tor, tr_incomplete_metadata* m, tr_error** error)
{
    auto const sha1 = tr_sha1(std::string_view{ m->metadata, m->metadata_size });
    bool const checksum_passed = sha1 && *sha1 == tor->infoHash();
    if (!checksum_passed)
    {
        return false;
    }

    // checksum passed; now try to parse it as benc
    auto info_dict_v = tr_variant{};
    auto const info_dict_sv = std::string_view{ m->metadata, m->metadata_size };
    if (!tr_variantFromBuf(&info_dict_v, TR_VARIANT_PARSE_BENC | TR_VARIANT_PARSE_INPLACE, info_dict_sv, nullptr, error))
    {
        return false;
    }

    // yay we have an info dict. Let's make a .torrent file
    auto top_v = tr_variant{};
    tr_buildMetainfoExceptInfoDict(tor->info, &top_v);
    tr_variantMergeDicts(tr_variantDictAddDict(&top_v, TR_KEY_info, 0), &info_dict_v);
    auto const benc = tr_variantToStr(&top_v, TR_VARIANT_FMT_BENC);

    // does this synthetic .torrent file parse?
    auto parsed = tr_metainfoParse(tor->session, &top_v, error);
    tr_variantFree(&top_v);
    tr_variantFree(&info_dict_v);
    if (!parsed)
    {
        return false;
    }

    // save it
    auto const& torrent_dir = tor->session->torrent_dir;
    auto const filename = tr_magnet_metainfo::makeFilename(
        torrent_dir,
        tor->name(),
        tor->infoHashString(),
        tr_magnet_metainfo::BasenameFormat::Hash,
        ".torrent"sv);
    if (!tr_saveFile(filename, benc, error))
    {
        return false;
    }

    // tor should keep this metainfo
    tor->swapMetainfo(*parsed);
    tr_torrentGotNewInfoDict(tor);
    tor->setDirty();

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
        tor->startAfterVerify = !tor->prefetchMagnetMetadata;
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
        dbgmsg(tor, "metadata error: %s. (trying again; %d pieces left)", msg, n);
        tr_error_clear(&error);
    }
}

void tr_torrentSetMetadataPiece(tr_torrent* tor, int piece, void const* data, int len)
{
    TR_ASSERT(tr_isTorrent(tor));
    TR_ASSERT(data != nullptr);
    TR_ASSERT(len >= 0);

    dbgmsg(tor, "got metadata piece %d of %d bytes", piece, len);

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

    dbgmsg(tor, "saving metainfo piece %d... %d remain", piece, m->piecesNeededCount);

    /* are we done? */
    if (m->piecesNeededCount == 0)
    {
        dbgmsg(tor, "metainfo piece %d was the last one", piece);
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

        dbgmsg(tor, "next piece to request: %d", piece);
        *setme_piece = piece;
        have_request = true;
    }

    return have_request;
}

double tr_torrentGetMetadataPercent(tr_torrent const* tor)
{
    if (tor->hasMetadata())
    {
        return 1.0;
    }

    auto const* const m = tor->incompleteMetadata;
    return m == nullptr || m->pieceCount == 0 ? 0.0 : (m->pieceCount - m->piecesNeededCount) / (double)m->pieceCount;
}

/* TODO: this should be renamed tr_metainfoGetMagnetLink() and moved to metainfo.c for consistency */
char* tr_torrentInfoGetMagnetLink(tr_info const* inf)
{
    auto buf = std::string{};

    buf += "magnet:?xt=urn:btih:"sv;
    buf += inf->infoHashString();

    auto const& name = inf->name();
    if (!std::empty(name))
    {
        buf += "&dn="sv;
        tr_http_escape(buf, name, true);
    }

    for (size_t i = 0, n = std::size(*inf->announce_list); i < n; ++i)
    {
        buf += "&tr="sv;
        tr_http_escape(buf, inf->announce_list->at(i).announce.full, true);
    }

    for (size_t i = 0, n = inf->webseedCount(); i < n; ++i)
    {
        buf += "&ws="sv;
        tr_http_escape(buf, inf->webseed(i), true);
    }

    return tr_strvDup(buf);
}

char* tr_torrentGetMagnetLink(tr_torrent const* tor)
{
    return tr_torrentInfoGetMagnetLink(&tor->info);
}
