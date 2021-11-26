/*
 * This file Copyright (C) 2009-2021 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "transmission.h"

#include "bandwidth.h"
#include "bitfield.h"
#include "block-info.h"
#include "completion.h"
#include "file.h"
#include "quark.h"
#include "session.h"
#include "tr-assert.h"
#include "tr-macros.h"

class tr_swarm;
struct tr_magnet_info;
struct tr_metainfo_parsed;
struct tr_session;
struct tr_torrent;
struct tr_torrent_tiers;

/**
***  Package-visible ctor API
**/

void tr_torrentFree(tr_torrent* tor);

void tr_ctorSetSave(tr_ctor* ctor, bool saveMetadataInOurTorrentsDir);

bool tr_ctorGetSave(tr_ctor const* ctor);

void tr_ctorInitTorrentPriorities(tr_ctor const* ctor, tr_torrent* tor);

void tr_ctorInitTorrentWanted(tr_ctor const* ctor, tr_torrent* tor);

/**
***
**/

/* just like tr_torrentSetFileDLs but doesn't trigger a fastresume save */
void tr_torrentInitFileDLs(tr_torrent* tor, tr_file_index_t const* files, tr_file_index_t fileCount, bool do_download);

using tr_labels_t = std::unordered_set<std::string>;

void tr_torrentSetLabels(tr_torrent* tor, tr_labels_t&& labels);

void tr_torrentRecheckCompleteness(tr_torrent*);

void tr_torrentChangeMyPort(tr_torrent* session);

tr_sha1_digest_t tr_torrentInfoHash(tr_torrent const* torrent);

tr_torrent* tr_torrentFindFromHash(tr_session* session, tr_sha1_digest_t const& info_dict_hah);

tr_torrent* tr_torrentFindFromHashString(tr_session* session, std::string_view hash_string);

tr_torrent* tr_torrentFindFromObfuscatedHash(tr_session* session, uint8_t const* hash);

bool tr_torrentIsPieceTransferAllowed(tr_torrent const* torrent, tr_direction direction);

bool tr_torrentReqIsValid(tr_torrent const* tor, tr_piece_index_t index, uint32_t offset, uint32_t length);

uint64_t tr_pieceOffset(tr_torrent const* tor, tr_piece_index_t index, uint32_t offset, uint32_t length);

void tr_torrentGetBlockLocation(
    tr_torrent const* tor,
    tr_block_index_t block,
    tr_piece_index_t* piece,
    uint32_t* offset,
    uint32_t* length);

tr_block_span_t tr_torGetFileBlockSpan(tr_torrent const* tor, tr_file_index_t const file);

void tr_torrentInitFilePriority(tr_torrent* tor, tr_file_index_t fileIndex, tr_priority_t priority);

void tr_torrentCheckSeedLimit(tr_torrent* tor);

/** save a torrent's .resume file if it's changed since the last time it was saved */
void tr_torrentSave(tr_torrent* tor);

void tr_torrentSetLocalError(tr_torrent* tor, char const* fmt, ...) TR_GNUC_PRINTF(2, 3);

void tr_torrentSetDateAdded(tr_torrent* torrent, time_t addedDate);

void tr_torrentSetDateActive(tr_torrent* torrent, time_t activityDate);

void tr_torrentSetDateDone(tr_torrent* torrent, time_t doneDate);

/** Return the mime-type (e.g. "audio/x-flac") that matches more of the
    torrent's content than any other mime-type. */
std::string_view tr_torrentPrimaryMimeType(tr_torrent const* tor);

enum tr_verify_state
{
    TR_VERIFY_NONE,
    TR_VERIFY_WAIT,
    TR_VERIFY_NOW
};

void tr_torrentSetVerifyState(tr_torrent* tor, tr_verify_state state);

tr_torrent_activity tr_torrentGetActivity(tr_torrent const* tor);

struct tr_incomplete_metadata;

/** @brief Torrent object */
struct tr_torrent
    : public tr_block_info
    , public tr_completion::torrent_view
{
public:
    tr_torrent(tr_info const& inf)
        : tr_block_info{ inf.totalSize, inf.pieceSize }
        , completion{ this, this }
    {
    }

    virtual ~tr_torrent() = default;

    void setLocation(
        std::string_view location,
        bool move_from_current_location,
        double volatile* setme_progress,
        int volatile* setme_state);

    void renamePath(
        std::string_view oldpath,
        std::string_view newname,
        tr_torrent_rename_done_func callback,
        void* callback_user_data);

    tr_sha1_digest_t pieceHash(tr_piece_index_t i) const
    {
        TR_ASSERT(i < std::size(this->piece_checksums_));
        return this->piece_checksums_[i];
    }

    // these functions should become private when possible,
    // but more refactoring is needed before that can happen
    // because much of tr_torrent's impl is in the non-member C bindings
    //
    // private:
    void swapMetainfo(tr_metainfo_parsed& parsed);

public:
    auto unique_lock() const
    {
        return session->unique_lock();
    }

    /// COMPLETION

    [[nodiscard]] uint64_t leftUntilDone() const
    {
        return completion.leftUntilDone();
    }

    [[nodiscard]] bool hasAll() const
    {
        return completion.hasAll();
    }

    [[nodiscard]] bool hasNone() const
    {
        return completion.hasNone();
    }

    [[nodiscard]] bool hasPiece(tr_piece_index_t piece) const
    {
        return completion.hasPiece(piece);
    }

    [[nodiscard]] bool hasBlock(tr_block_index_t block) const
    {
        return completion.hasBlock(block);
    }

    [[nodiscard]] size_t countMissingBlocksInPiece(tr_piece_index_t piece) const
    {
        return completion.countMissingBlocksInPiece(piece);
    }

    [[nodiscard]] size_t countMissingBytesInPiece(tr_piece_index_t piece) const
    {
        return completion.countMissingBytesInPiece(piece);
    }

    [[nodiscard]] uint64_t hasTotal() const
    {
        return completion.hasTotal();
    }

    [[nodiscard]] std::vector<uint8_t> createPieceBitfield() const
    {
        return completion.createPieceBitfield();
    }

    [[nodiscard]] bool isDone() const
    {
        return completion.isDone();
    }

    [[nodiscard]] tr_bitfield const& blocks() const
    {
        return completion.blocks();
    }

    void amountDoneBins(float* tab, int n_tabs) const
    {
        return completion.amountDone(tab, n_tabs);
    }

    void setBlocks(tr_bitfield blocks)
    {
        completion.setBlocks(std::move(blocks));
    }

    void setHasPiece(tr_piece_index_t piece, bool has)
    {
        completion.setHasPiece(piece, has);
    }

    bool pieceIsDnd(tr_piece_index_t piece) const final
    {
        return dnd_pieces_.test(piece);
    }

    /// PRIORITIES

    void setPiecePriority(tr_piece_index_t piece, tr_priority_t priority)
    {
        // since 'TR_PRI_NORMAL' is by far the most common, save some
        // space by treating anything not in the map as normal
        if (priority == TR_PRI_NORMAL)
        {
            piece_priorities_.erase(piece);

            if (std::empty(piece_priorities_))
            {
                // ensure we release piece_priorities_' internal memory
                piece_priorities_ = decltype(piece_priorities_){};
            }
        }
        else
        {
            piece_priorities_[piece] = priority;
        }
    }

    tr_priority_t piecePriority(tr_piece_index_t piece) const
    {
        auto const it = piece_priorities_.find(piece);
        if (it == std::end(piece_priorities_))
        {
            return TR_PRI_NORMAL;
        }
        return it->second;
    }

    /// CHECKSUMS

    bool ensurePieceIsChecked(tr_piece_index_t piece)
    {
        TR_ASSERT(piece < info.pieceCount);

        if (checked_pieces_.test(piece))
        {
            return true;
        }

        bool const checked = checkPiece(piece);
        this->anyDate = tr_time();
        this->setDirty();

        checked_pieces_.set(piece, checked);
        return checked;
    }

    void initCheckedPieces(tr_bitfield const& checked, time_t const* mtimes /*fileCount*/)
    {
        TR_ASSERT(std::size(checked) == info.pieceCount);
        checked_pieces_ = checked;

        auto filename = std::string{};
        for (size_t i = 0; i < info.fileCount; ++i)
        {
            auto const found = this->findFile(filename, i);
            auto const mtime = found ? found->last_modified_at : 0;

            info.files[i].mtime = mtime;

            // if a file has changed, mark its pieces as unchecked
            if (mtime == 0 || mtime != mtimes[i])
            {
                auto const begin = info.files[i].firstPiece;
                auto const end = info.files[i].lastPiece + 1;
                checked_pieces_.unsetSpan(begin, end);
            }
        }
    }

    /// FINDING FILES

    struct tr_found_file_t : public tr_sys_path_info
    {
        std::string& filename; // /home/foo/Downloads/torrent/01-file-one.txt
        std::string_view base; // /home/foo/Downloads
        std::string_view subpath; // /torrent/01-file-one.txt

        tr_found_file_t(tr_sys_path_info info, std::string& f, std::string_view b)
            : tr_sys_path_info{ info }
            , filename{ f }
            , base{ b }
            , subpath{ f.c_str() + std::size(b) + 1 }
        {
        }
    };

    std::optional<tr_found_file_t> findFile(std::string& filename, tr_file_index_t i) const;

public:
    tr_info info = {};

    tr_bitfield dnd_pieces_ = tr_bitfield{ 0 };

    tr_bitfield checked_pieces_ = tr_bitfield{ 0 };

    // TODO(ckerr): make private once some of torrent.cc's `tr_torrentFoo()` methods are member functions
    tr_completion completion;

    tr_session* session = nullptr;

    struct tr_torrent_tiers* tiers = nullptr;

    // Changed to non-owning pointer temporarily till tr_torrent becomes C++-constructible and destructible
    // TODO: change tr_bandwidth* to owning pointer to the bandwidth, or remove * and own the value
    Bandwidth* bandwidth = nullptr;

    tr_swarm* swarm = nullptr;

    // TODO: is this actually still needed?
    int const magicNumber = MagicNumber;

    std::optional<double> verify_progress;

    std::unordered_map<tr_piece_index_t, tr_priority_t> piece_priorities_;

    tr_stat_errtype error = TR_STAT_OK;
    char errorString[128] = {};
    tr_quark error_announce_url = TR_KEY_NONE;

    bool checkPiece(tr_piece_index_t piece);

    uint8_t obfuscatedHash[SHA_DIGEST_LENGTH] = {};

    /* Used when the torrent has been created with a magnet link
     * and we're in the process of downloading the metainfo from
     * other peers */
    struct tr_incomplete_metadata* incompleteMetadata = nullptr;

    /* If the initiator of the connection receives a handshake in which the
     * peer_id does not match the expected peerid, then the initiator is
     * expected to drop the connection. Note that the initiator presumably
     * received the peer information from the tracker, which includes the
     * peer_id that was registered by the peer. The peer_id from the tracker
     * and in the handshake are expected to match.
     */
    std::optional<tr_peer_id_t> peer_id;

    time_t peer_id_creation_time = 0;

    /* Where the files will be when it's complete */
    char* downloadDir = nullptr;

    /* Where the files are when the torrent is incomplete */
    char* incompleteDir = nullptr;

    /* Where the files are now.
     * This pointer will be equal to downloadDir or incompleteDir */
    char const* currentDir = nullptr;

    /* Length, in bytes, of the "info" dict in the .torrent file. */
    uint64_t infoDictLength = 0;

    /* Offset, in bytes, of the beginning of the "info" dict in the .torrent file.
     *
     * Used by the torrent-magnet code for serving metainfo to peers.
     * This field is lazy-generated and might not be initialized yet. */
    size_t infoDictOffset = 0;

    tr_completeness completeness = TR_LEECH;

    time_t dhtAnnounceAt = 0;
    time_t dhtAnnounce6At = 0;
    bool dhtAnnounceInProgress = false;
    bool dhtAnnounce6InProgress = false;

    time_t lpdAnnounceAt = 0;

    uint64_t downloadedCur = 0;
    uint64_t downloadedPrev = 0;
    uint64_t uploadedCur = 0;
    uint64_t uploadedPrev = 0;
    uint64_t corruptCur = 0;
    uint64_t corruptPrev = 0;

    uint64_t etaDLSpeedCalculatedAt = 0;
    uint64_t etaULSpeedCalculatedAt = 0;
    unsigned int etaDLSpeed_Bps = 0;
    unsigned int etaULSpeed_Bps = 0;

    time_t activityDate = 0;
    time_t addedDate = 0;
    time_t anyDate = 0;
    time_t doneDate = 0;
    time_t editDate = 0;
    time_t startDate = 0;

    int secondsDownloading = 0;
    int secondsSeeding = 0;

    int queuePosition = 0;

    tr_torrent_metadata_func metadata_func = nullptr;
    void* metadata_func_user_data = nullptr;

    tr_torrent_completeness_func completeness_func = nullptr;
    void* completeness_func_user_data = nullptr;

    tr_torrent_ratio_limit_hit_func ratio_limit_hit_func = nullptr;
    void* ratio_limit_hit_func_user_data = nullptr;

    tr_torrent_idle_limit_hit_func idle_limit_hit_func = nullptr;
    void* idle_limit_hit_func_user_data = nullptr;

    void* queue_started_user_data = nullptr;
    void (*queue_started_callback)(tr_torrent*, void* queue_started_user_data) = nullptr;

    bool isDeleting = false;
    bool isDirty = false;
    bool isQueued = false;
    bool isRunning = false;
    bool isStopping = false;
    bool startAfterVerify = false;

    bool prefetchMagnetMetadata = false;
    bool magnetVerify = false;

    // TODO(ckerr) use std::optional
    bool infoDictOffsetIsCached = false;

    void setDirty()
    {
        this->isDirty = true;
    }

    uint16_t maxConnectedPeers = TR_DEFAULT_PEER_LIMIT_TORRENT;

    tr_verify_state verifyState = TR_VERIFY_NONE;

    time_t lastStatTime = 0;
    tr_stat stats = {};

    int uniqueId = 0;

    float desiredRatio = 0.0F;
    tr_ratiolimit ratioLimitMode = TR_RATIOLIMIT_GLOBAL;

    uint16_t idleLimitMinutes = 0;
    tr_idlelimit idleLimitMode = TR_IDLELIMIT_GLOBAL;
    bool finishedSeedingByIdle = false;

    tr_labels_t labels;

    static auto constexpr MagicNumber = int{ 95549 };

private:
    mutable std::vector<tr_sha1_digest_t> piece_checksums_;
};

static inline bool tr_torrentExists(tr_session const* session, uint8_t const* torrentHash)
{
    return tr_torrentFindFromHash((tr_session*)session, torrentHash) != nullptr;
}

constexpr tr_completeness tr_torrentGetCompleteness(tr_torrent const* tor)
{
    return tor->completeness;
}

// TODO: rename this to tr_torrentIsDone()? both seed and partial seed return true
constexpr bool tr_torrentIsSeed(tr_torrent const* tor)
{
    return tr_torrentGetCompleteness(tor) != TR_LEECH;
}

constexpr bool tr_torrentIsPrivate(tr_torrent const* tor)
{
    return tor != nullptr && tor->info.isPrivate;
}

constexpr bool tr_torrentAllowsPex(tr_torrent const* tor)
{
    return tor != nullptr && tor->session->isPexEnabled && !tr_torrentIsPrivate(tor);
}

constexpr bool tr_torrentAllowsDHT(tr_torrent const* tor)
{
    return tor != nullptr && tr_sessionAllowsDHT(tor->session) && !tr_torrentIsPrivate(tor);
}

constexpr bool tr_torrentAllowsLPD(tr_torrent const* tor)
{
    return tor != nullptr && tr_sessionAllowsLPD(tor->session) && !tr_torrentIsPrivate(tor);
}

/***
****
***/

constexpr bool tr_isTorrent(tr_torrent const* tor)
{
    return tor != nullptr && tor->magicNumber == tr_torrent::MagicNumber && tr_isSession(tor->session);
}

/* set a flag indicating that the torrent's .resume file
 * needs to be saved when the torrent is closed */
constexpr void tr_torrentSetDirty(tr_torrent* tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    tor->isDirty = true;
}

/* note that the torrent's tr_info just changed */
static inline void tr_torrentMarkEdited(tr_torrent* tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    tor->editDate = tr_time();
}

/**
 * Tell the tr_torrent that it's gotten a block
 */
void tr_torrentGotBlock(tr_torrent* tor, tr_block_index_t blockIndex);

/**
 * @brief Like tr_torrentFindFile(), but splits the filename into base and subpath.
 *
 * If the file is found, "tr_buildPath(base, subpath, nullptr)"
 * will generate the complete filename.
 *
 * @return true if the file is found, false otherwise.
 *
 * @param base if the torrent is found, this will be either
 *             tor->downloadDir or tor->incompleteDir
 * @param subpath on success, this pointer is assigned a newly-allocated
 *                string holding the second half of the filename.
 */
bool tr_torrentFindFile2(tr_torrent const*, tr_file_index_t fileNo, char const** base, char** subpath, time_t* mtime);

/* Returns a newly-allocated version of the tr_file.name string
 * that's been modified to denote that it's not a complete file yet.
 * In the current implementation this is done by appending ".part"
 * a la Firefox. */
char* tr_torrentBuildPartial(tr_torrent const*, tr_file_index_t fileNo);

/* for when the info dict has been fundamentally changed wrt files,
 * piece size, etc. such as in BEP 9 where peers exchange metadata */
void tr_torrentGotNewInfoDict(tr_torrent* tor);

void tr_torrentSetSpeedLimit_Bps(tr_torrent*, tr_direction, unsigned int Bps);
unsigned int tr_torrentGetSpeedLimit_Bps(tr_torrent const*, tr_direction);

/**
 * @brief Test a piece against its info dict checksum
 * @return true if the piece's passes the checksum test
 */
bool tr_torrentCheckPiece(tr_torrent* tor, tr_piece_index_t pieceIndex);

uint64_t tr_torrentGetCurrentSizeOnDisk(tr_torrent const* tor);

tr_peer_id_t const& tr_torrentGetPeerId(tr_torrent* tor);

constexpr bool tr_torrentIsQueued(tr_torrent const* tor)
{
    return tor->isQueued;
}

constexpr tr_direction tr_torrentGetQueueDirection(tr_torrent const* tor)
{
    return tr_torrentIsSeed(tor) ? TR_UP : TR_DOWN;
}
