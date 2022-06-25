// This file Copyright © 2007-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <cerrno> /* error codes ERANGE, ... */
#include <climits> /* INT_MAX */
#include <cmath>
#include <cstdint>
#include <cstdlib> /* qsort */
#include <ctime> // time_t
#include <deque>
#include <iterator> // std::back_inserter
#include <memory>
#include <numeric> // std::accumulate
#include <optional>
#include <tuple> // std::tie
#include <utility>
#include <vector>

#include <event2/event.h>

#include <fmt/format.h>

#define LIBTRANSMISSION_PEER_MODULE
#include "transmission.h"

#include "announcer.h"
#include "bandwidth.h"
#include "blocklist.h"
#include "cache.h"
#include "clients.h"
#include "completion.h"
#include "crypto-utils.h"
#include "handshake.h"
#include "log.h"
#include "net.h"
#include "peer-io.h"
#include "peer-mgr-active-requests.h"
#include "peer-mgr-request-allocator.h"
#include "peer-mgr-wishlist.h"
#include "peer-mgr.h"
#include "peer-msgs.h"
#include "session.h"
#include "stats.h" /* tr_statsAddUploaded, tr_statsAddDownloaded */
#include "torrent.h"
#include "tr-assert.h"
#include "tr-utp.h"
#include "utils.h"
#include "webseed.h"

// use for bitwise operations w/peer_atom.flags2
static auto constexpr MyflagBanned = int{ 1 };

// use for bitwise operations w/peer_atom.flags2
// unreachable for now... but not banned.
// if they try to connect to us it's okay
static auto constexpr MyflagUnreachable = int{ 2 };

static auto constexpr CancelHistorySec = int{ 60 };

/**
***
**/

/**
 * Peer information that should be kept even before we've connected and
 * after we've disconnected. These are kept in a pool of peer_atoms to decide
 * which ones would make good candidates for connecting to, and to watch out
 * for banned peers.
 *
 * @see tr_peer
 * @see tr_peerMsgs
 */
struct peer_atom
{
    peer_atom(tr_address addr_in, tr_port port_in, uint8_t flags_in, uint8_t from)
        : addr{ addr_in }
        , port{ port_in }
        , fromFirst{ from }
        , fromBest{ from }
        , flags{ flags_in }
    {
    }

#ifdef TR_ENABLE_ASSERTS
    [[nodiscard]] bool isValid() const noexcept
    {
        return fromFirst < TR_PEER_FROM__MAX && fromBest < TR_PEER_FROM__MAX && tr_address_is_valid(&addr);
    }
#endif

    [[nodiscard]] constexpr auto isSeed() const noexcept
    {
        return (flags & ADDED_F_SEED_FLAG) != 0;
    }

    [[nodiscard]] auto readable() const
    {
        return addr.readable(port);
    }

    [[nodiscard]] bool isBlocklisted(tr_session const* session) const
    {
        if (blocklisted_)
        {
            return *blocklisted_;
        }

        auto const value = tr_sessionIsAddressBlocked(session, &addr);
        blocklisted_ = value;
        return value;
    }

    [[nodiscard]] int getReconnectIntervalSecs(time_t const now) const noexcept
    {
        auto sec = int{};
        bool const unreachable = (this->flags2 & MyflagUnreachable) != 0;

        /* if we were recently connected to this peer and transferring piece
         * data, try to reconnect to them sooner rather that later -- we don't
         * want network troubles to get in the way of a good peer. */
        if (!unreachable && now - this->piece_data_time <= MinimumReconnectIntervalSecs * 2)
        {
            sec = MinimumReconnectIntervalSecs;
        }
        /* otherwise, the interval depends on how many times we've tried
         * and failed to connect to the peer */
        else
        {
            auto step = this->num_fails;

            /* penalize peers that were unreachable the last time we tried */
            if (unreachable)
            {
                step += 2;
            }

            switch (step)
            {
            case 0:
                sec = 0;
                break;

            case 1:
                sec = 10;
                break;

            case 2:
                sec = 60 * 2;
                break;

            case 3:
                sec = 60 * 15;
                break;

            case 4:
                sec = 60 * 30;
                break;

            case 5:
                sec = 60 * 60;
                break;

            default:
                sec = 60 * 120;
                break;
            }
        }

        tr_logAddTrace(fmt::format("reconnect interval for {} is {} seconds", this->readable(), sec));
        return sec;
    }

    void setBlocklistedDirty()
    {
        blocklisted_.reset();
    }

    std::optional<bool> isReachable() const
    {
        if ((flags2 & MyflagUnreachable) != 0)
        {
            return false;
        }

        if ((flags & ADDED_F_CONNECTABLE) != 0)
        {
            return true;
        }

        return std::nullopt;
    }

    tr_address const addr;

    tr_port port = {};

    uint16_t num_fails = {};

    time_t time = {}; /* when the peer's connection status last changed */
    time_t piece_data_time = {};

    time_t lastConnectionAttemptAt = {};
    time_t lastConnectionAt = {};

    uint8_t const fromFirst; /* where the peer was first found */
    uint8_t fromBest; /* the "best" value of where the peer has been found */
    uint8_t flags = {}; /* these match the added_f flags */
    uint8_t flags2 = {}; /* flags that aren't defined in added_f */

    bool utp_failed = false; /* We recently failed to connect over uTP */
    bool is_connected = false;

private:
    mutable std::optional<bool> blocklisted_;

    // the minimum we'll wait before attempting to reconnect to a peer
    static auto constexpr MinimumReconnectIntervalSecs = int{ 5 };
};

// a container for keeping track of tr_handshakes
class Handshakes
{
public:
    void add(tr_address const& address, tr_handshake* handshake)
    {
        TR_ASSERT(!contains(address));

        handshakes_.emplace_back(address, handshake);
    }

    [[nodiscard]] bool contains(tr_address const& address) const noexcept
    {
        return std::any_of(
            std::begin(handshakes_),
            std::end(handshakes_),
            [&address](auto const& pair) { return pair.first == address; });
    }

    void erase(tr_address const& address)
    {
        for (auto iter = std::begin(handshakes_), end = std::end(handshakes_); iter != end; ++iter)
        {
            if (iter->first == address)
            {
                handshakes_.erase(iter);
                return;
            }
        }
    }

    [[nodiscard]] auto empty() const noexcept
    {
        return std::empty(handshakes_);
    }

    void abortAll()
    {
        // make a tmp copy so that calls to tr_handshakeAbort() won't
        // be able to invalidate its loop iteration
        auto tmp = handshakes_;
        for (auto& [addr, handshake] : tmp)
        {
            tr_handshakeAbort(handshake);
        }

        handshakes_ = {};
    }

private:
    std::vector<std::pair<tr_address, tr_handshake*>> handshakes_;
};

#define tr_logAddDebugSwarm(swarm, msg) tr_logAddDebugTor((swarm)->tor, msg)
#define tr_logAddTraceSwarm(swarm, msg) tr_logAddTraceTor((swarm)->tor, msg)

static void peerCallbackFunc(tr_peer* /*peer*/, tr_peer_event const* /*e*/, void* /*vs*/);

/** @brief Opaque, per-torrent data structure for peer connection information */
class tr_swarm
{
public:
    tr_swarm(tr_peerMgr* manager_in, tr_torrent* tor_in) noexcept
        : manager{ manager_in }
        , tor{ tor_in }
    {
        rebuildWebseeds();
    }

    void cancelOldRequests()
    {
        auto const now = tr_time();
        auto const oldest = now - RequestTtlSecs;

        for (auto const& [block, peer] : active_requests.sentBefore(oldest))
        {
            maybeSendCancelRequest(peer, block, nullptr);
            active_requests.remove(block, peer);
        }
    }

    void cancelAllRequestsForBlock(tr_block_index_t block, tr_peer const* no_notify)
    {
        for (auto* peer : active_requests.remove(block))
        {
            maybeSendCancelRequest(peer, block, no_notify);
        }
    }

    [[nodiscard]] auto unique_lock() const
    {
        return tor->unique_lock();
    }

    [[nodiscard]] size_t countActiveWebseeds() const noexcept
    {
        if (!tor->isRunning || tor->isDone())
        {
            return {};
        }

        auto const now = tr_time_msec();

        return std::count_if(
            std::begin(webseeds),
            std::end(webseeds),
            [&now](auto const& webseed) { return webseed->isTransferringPieces(now, TR_DOWN, nullptr); });
    }

    [[nodiscard]] auto peerCount() const noexcept
    {
        return std::size(peers);
    }

    void stop()
    {
        auto const lock = unique_lock();

        is_running = false;
        removeAllPeers();
        outgoing_handshakes.abortAll();
    }

    void removePeer(tr_peer* peer)
    {
        auto const lock = unique_lock();

        auto* const atom = peer->atom;
        TR_ASSERT(atom != nullptr);

        atom->time = tr_time();

        if (auto iter = std::find(std::begin(peers), std::end(peers), peer); iter != std::end(peers))
        {
            peers.erase(iter);
        }

        --stats.peer_count;
        --stats.peer_from_count[atom->fromFirst];

        TR_ASSERT(stats.peer_count == peerCount());

        delete peer;
    }

    void removeAllPeers()
    {
        auto tmp = peers;

        for (auto* peer : tmp)
        {
            removePeer(peer);
        }

        TR_ASSERT(stats.peer_count == 0);
    }

    void updateEndgame()
    {
        /* we consider ourselves to be in endgame if the number of bytes
           we've got requested is >= the number of bytes left to download */
        is_endgame_ = uint64_t(std::size(active_requests)) * tr_block_info::BlockSize >= tor->leftUntilDone();
    }

    [[nodiscard]] auto constexpr isEndgame() const noexcept
    {
        return is_endgame_;
    }

    void addStrike(tr_peer* peer)
    {
        tr_logAddTraceSwarm(this, fmt::format("increasing peer {} strike count to {}", peer->readable(), peer->strikes + 1));

        if (++peer->strikes >= MaxBadPiecesPerPeer)
        {
            peer->atom->flags2 |= MyflagBanned;
            peer->do_purge = true;
            tr_logAddTraceSwarm(this, fmt::format("banning peer {}", peer->readable()));
        }
    }

    void rebuildWebseeds()
    {
        auto const n = tor->webseedCount();

        webseeds.clear();
        webseeds.reserve(n);
        for (size_t i = 0; i < n; ++i)
        {
            webseeds.emplace_back(tr_webseedNew(tor, tor->webseed(i), peerCallbackFunc, this));
        }
        webseeds.shrink_to_fit();

        stats.active_webseed_count = 0;
    }

    [[nodiscard]] auto isAllSeeds() const noexcept
    {
        if (!pool_is_all_seeds_)
        {
            pool_is_all_seeds_ = std::all_of(std::begin(pool), std::end(pool), [](auto const& atom) { return atom.isSeed(); });
        }

        return *pool_is_all_seeds_;
    }

    void markAllSeedsFlagDirty() noexcept
    {
        pool_is_all_seeds_.reset();
    }

    Handshakes outgoing_handshakes;

    uint16_t interested_count = 0;
    uint16_t max_peers = 0;

    tr_swarm_stats stats = {};

    uint8_t optimistic_unchoke_time_scaler = 0;

    bool is_running = false;
    bool needs_completeness_check = true;

    tr_peerMgr* const manager;

    tr_torrent* const tor;

    std::vector<std::unique_ptr<tr_peer>> webseeds;
    std::vector<tr_peerMsgs*> peers;

    // tr_peers hold pointers to the items in this container,
    // so use a deque instead of vector to prevent insertion from
    // invalidating those pointers
    std::deque<peer_atom> pool;

    tr_peerMsgs* optimistic = nullptr; /* the optimistic peer, or nullptr if none */

    time_t lastCancel = 0;

    ActiveRequests active_requests;

private:
    static void maybeSendCancelRequest(tr_peer* peer, tr_block_index_t block, tr_peer const* muted)
    {
        auto* msgs = dynamic_cast<tr_peerMsgs*>(peer);
        if (msgs != nullptr && msgs != muted)
        {
            peer->cancels_sent_to_peer.add(tr_time(), 1);
            msgs->cancel_block_request(block);
        }
    }

    // number of bad pieces a peer is allowed to send before we ban them
    static auto constexpr MaxBadPiecesPerPeer = int{ 5 };

    // how long we'll let requests we've made linger before we cancel them
    static auto constexpr RequestTtlSecs = int{ 90 };

    mutable std::optional<bool> pool_is_all_seeds_;

    bool is_endgame_ = false;
};

struct EventDeleter
{
    void operator()(struct event* ev) const
    {
        event_free(ev);
    }
};

using UniqueTimer = std::unique_ptr<struct event, EventDeleter>;

struct tr_peerMgr final : public BlockRequestAllocator<tr_peer*, Bandwidth*>::Mediator
{
private:
    using PeerKey = tr_peer*;
    using PoolKey = Bandwidth*;

public:
    explicit tr_peerMgr(tr_session* session_in)
        : session{ session_in }
        , bandwidth_timer_{ evtimer_new(session->event_base, bandwidthPulseMarshall, this) }
        , rechoke_timer_{ evtimer_new(session->event_base, rechokePulseMarshall, this) }
        , refill_upkeep_timer_{ evtimer_new(session->event_base, refillUpkeepMarshall, this) }
    {
        tr_timerAddMsec(*bandwidth_timer_, BandwidthPeriodMsec);
        tr_timerAddMsec(*rechoke_timer_, RechokePeriodMsec);
        tr_timerAddMsec(*refill_upkeep_timer_, RefillUpkeepPeriodMsec);
    }

    [[nodiscard]] auto unique_lock() const
    {
        return session->unique_lock();
    }

    virtual ~tr_peerMgr()
    {
        auto const lock = unique_lock();
        incoming_handshakes.abortAll();
    }

    void rechokeSoon() noexcept
    {
        tr_timerAddMsec(*rechoke_timer_, 100);
    }

    void bandwidthPulse();
    void rechokePulse() const;
    void reconnectPulse();
    void refillUpkeep() const;
    void makeNewPeerConnections(size_t max);

    /// BlockRequestAllocator::Mediator

    [[nodiscard]] std::vector<PeerKey> peers() const override
    {
        auto peers = std::vector<PeerKey>{};
        peers.reserve(session->peerLimit);

        for (auto& torrent : session->torrents())
        {
            for (auto* peer : torrent->swarm->peers)
            {
                if (peer->is_active(TR_PEER_TO_CLIENT))
                {
                    peers.push_back(peer);
                }
            }

            for (auto& peer : torrent->swarm->webseeds)
            {
                peers.push_back(peer.get());
            }
        }

        return peers;
    }

    [[nodiscard]] size_t activeReqCount(PeerKey peer) const override
    {
        return peer->swarm->active_requests.count(peer);
    }

    [[nodiscard]] std::vector<PoolKey> pools(PeerKey peer) const override
    {
        static auto constexpr Dir = TR_PEER_TO_CLIENT;
        auto pools = std::vector<PoolKey>{};

        for (auto* bandwidth = peer->bandwidth(); bandwidth != nullptr && bandwidth->areParentLimitsHonored(Dir);
             bandwidth = bandwidth->parent())
        {
            if (bandwidth->isLimited(Dir))
            {
                pools.push_back(bandwidth);
            }
        }

        return pools;
    }

    [[nodiscard]] size_t poolBlockLimit(PoolKey pool) const override
    {
        static auto constexpr Dir = TR_PEER_TO_CLIENT;
        TR_ASSERT(pool->isLimited(Dir));
        return static_cast<size_t>((pool->getDesiredSpeedBytesPerSecond(Dir) * downloadReqPeriod()) / tr_block_info::BlockSize);
    }

    [[nodiscard]] uint32_t maxObservedDlSpeedBps() const override
    {
        return session->maxObservedDlSpeedBps();
    }

    [[nodiscard]] size_t downloadReqPeriod() const override
    {
        return 10U; // 10 seconds. TODO: make this configurable
    }

    ///

    tr_session* const session;
    Handshakes incoming_handshakes;

private:
    static void bandwidthPulseMarshall(evutil_socket_t, short /*reason*/, void* vmgr)
    {
        auto* const self = static_cast<tr_peerMgr*>(vmgr);
        self->bandwidthPulse();
        tr_timerAddMsec(*self->bandwidth_timer_, BandwidthPeriodMsec);
    }

    static void rechokePulseMarshall(evutil_socket_t, short /*reason*/, void* vmgr)
    {
        auto* const self = static_cast<tr_peerMgr*>(vmgr);
        self->rechokePulse();
        tr_timerAddMsec(*self->rechoke_timer_, RechokePeriodMsec);
    }

    static void refillUpkeepMarshall(evutil_socket_t, short /*reason*/, void* vmgr)
    {
        auto* const self = static_cast<tr_peerMgr*>(vmgr);
        self->refillUpkeep();
        tr_timerAddMsec(*self->refill_upkeep_timer_, RefillUpkeepPeriodMsec);
    }

    UniqueTimer const bandwidth_timer_;
    UniqueTimer const rechoke_timer_;
    UniqueTimer const refill_upkeep_timer_;

    static auto constexpr BandwidthPeriodMsec = int{ 500 };
    static auto constexpr RechokePeriodMsec = int{ 10 * 1000 };
    static auto constexpr RefillUpkeepPeriodMsec = int{ 10 * 1000 };

    // how frequently to decide which peers live and die
    static auto constexpr ReconnectPeriodMsec = int{ 500 };

    // max number of peers to ask for per second overall.
    // this throttle is to avoid overloading the router
    static auto constexpr MaxConnectionsPerSecond = size_t{ 12 };
};

/**
*** tr_peer virtual functions
**/

unsigned int tr_peerGetPieceSpeed_Bps(tr_peer const* peer, uint64_t now, tr_direction direction)
{
    unsigned int Bps = 0;
    peer->isTransferringPieces(now, direction, &Bps);
    return Bps;
}

tr_peer::tr_peer(tr_torrent const* tor, peer_atom* atom_in)
    : session{ tor->session }
    , swarm{ tor->swarm }
    , atom{ atom_in }
    , blame{ tor->blockCount() }
{
}

tr_peer::~tr_peer()
{
    if (swarm != nullptr)
    {
        swarm->active_requests.remove(this);
    }

    if (atom != nullptr)
    {
        atom->is_connected = false;
    }
}

/**
***
**/

static tr_swarm* getExistingSwarm(tr_peerMgr* manager, tr_sha1_digest_t const& hash)
{
    auto* const tor = manager->session->torrents().get(hash);

    return tor == nullptr ? nullptr : tor->swarm;
}

static struct peer_atom* getExistingAtom(tr_swarm const* cswarm, tr_address const& addr)
{
    auto* swarm = const_cast<tr_swarm*>(cswarm);
    auto const test = [&addr](auto const& atom)
    {
        return atom.addr == addr;
    };
    auto const it = std::find_if(std::begin(swarm->pool), std::end(swarm->pool), test);
    return it != std::end(swarm->pool) ? &*it : nullptr;
}

static bool peerIsInUse(tr_swarm const* cs, struct peer_atom const* atom)
{
    auto const* const s = const_cast<tr_swarm*>(cs);
    auto const lock = s->unique_lock();

    return atom->is_connected || s->outgoing_handshakes.contains(atom->addr) ||
        s->manager->incoming_handshakes.contains(atom->addr);
}

static void swarmFree(tr_swarm* s)
{
    TR_ASSERT(s != nullptr);
    auto const lock = s->unique_lock();

    TR_ASSERT(!s->is_running);
    TR_ASSERT(std::empty(s->outgoing_handshakes));
    TR_ASSERT(s->peerCount() == 0);

    s->stats = {};

    delete s;
}

tr_peerMgr* tr_peerMgrNew(tr_session* session)
{
    return new tr_peerMgr{ session };
}

void tr_peerMgrFree(tr_peerMgr* manager)
{
    delete manager;
}

/***
****
***/

void tr_peerMgrOnBlocklistChanged(tr_peerMgr* mgr)
{
    /* we cache whether or not a peer is blocklisted...
       since the blocklist has changed, erase that cached value */
    for (auto* const tor : mgr->session->torrents())
    {
        for (auto& atom : tor->swarm->pool)
        {
            atom.setBlocklistedDirty();
        }
    }
}

/***
****
***/

static void atomSetSeed(tr_swarm* swarm, peer_atom& atom)
{
    tr_logAddTraceSwarm(swarm, fmt::format("marking peer {} as a seed", atom.readable()));
    atom.flags |= ADDED_F_SEED_FLAG;
    swarm->markAllSeedsFlagDirty();
}

bool tr_peerMgrPeerIsSeed(tr_torrent const* tor, tr_address const& addr)
{
    if (auto const* atom = getExistingAtom(tor->swarm, addr); atom != nullptr)
    {
        return atom->isSeed();
    }

    return false;
}

void tr_peerMgrSetUtpSupported(tr_torrent* tor, tr_address const& addr)
{
    if (auto* const atom = getExistingAtom(tor->swarm, addr); atom != nullptr)
    {
        atom->flags |= ADDED_F_UTP_FLAGS;
    }
}

void tr_peerMgrSetUtpFailed(tr_torrent* tor, tr_address const& addr, bool failed)
{
    if (auto* const atom = getExistingAtom(tor->swarm, addr); atom != nullptr)
    {
        atom->utp_failed = failed;
    }
}

/**
***  REQUESTS
***
*** There are two data structures associated with managing block requests:
***
*** 1. tr_swarm::active_requests, an opaque class that tracks what requests
***    we currently have, i.e. which blocks and from which peers.
***    This is used for cancelling requests that have been waiting
***    for too long and avoiding duplicate requests.
***
*** 2. tr_swarm::pieces, an array of "struct weighted_piece" which lists the
***    pieces that we want to request. It's used to decide which blocks to
***    return next when tr_peerMgrGetBlockRequests() is called.
**/

/**
*** struct block_request
**/

// TODO: if we keep this, add equivalent API to ActiveRequest
void tr_peerMgrClientSentRequests(tr_torrent* torrent, tr_peer* peer, tr_block_span_t span)
{
    auto const now = tr_time();

    for (tr_block_index_t block = span.begin; block < span.end; ++block)
    {
        torrent->swarm->active_requests.add(block, peer, now);
    }
}

std::vector<tr_block_span_t> tr_peerMgrGetNextRequests(tr_torrent* torrent, tr_peer const* peer, size_t numwant)
{
    class MediatorImpl final : public Wishlist::Mediator
    {
    public:
        MediatorImpl(tr_torrent const* torrent_in, tr_peer const* peer_in)
            : torrent_{ torrent_in }
            , swarm_{ torrent_in->swarm }
            , peer_{ peer_in }
        {
        }

        ~MediatorImpl() override = default;

        [[nodiscard]] bool clientCanRequestBlock(tr_block_index_t block) const override
        {
            return !torrent_->hasBlock(block) && !swarm_->active_requests.has(block, peer_);
        }

        [[nodiscard]] bool clientCanRequestPiece(tr_piece_index_t piece) const override
        {
            return torrent_->pieceIsWanted(piece) && peer_->hasPiece(piece);
        }

        [[nodiscard]] bool isEndgame() const override
        {
            return swarm_->isEndgame();
        }

        [[nodiscard]] size_t countActiveRequests(tr_block_index_t block) const override
        {
            return swarm_->active_requests.count(block);
        }

        [[nodiscard]] size_t countMissingBlocks(tr_piece_index_t piece) const override
        {
            return torrent_->countMissingBlocksInPiece(piece);
        }

        [[nodiscard]] tr_block_span_t blockSpan(tr_piece_index_t piece) const override
        {
            return torrent_->blockSpanForPiece(piece);
        }

        [[nodiscard]] tr_piece_index_t countAllPieces() const override
        {
            return torrent_->pieceCount();
        }

        [[nodiscard]] tr_priority_t priority(tr_piece_index_t piece) const override
        {
            return torrent_->piecePriority(piece);
        }

    private:
        tr_torrent const* const torrent_;
        tr_swarm const* const swarm_;
        tr_peer const* const peer_;
    };

    torrent->swarm->updateEndgame();
    return Wishlist::next(MediatorImpl(torrent, peer), numwant);
}

/****
*****
*****  Piece List Manipulation / Accessors
*****
****/

bool tr_peerMgrDidPeerRequest(tr_torrent const* tor, tr_peer const* peer, tr_block_index_t block)
{
    return tor->swarm->active_requests.has(block, peer);
}

size_t tr_peerMgrCountActiveRequestsToPeer(tr_torrent const* tor, tr_peer const* peer)
{
    return tor->swarm->active_requests.count(peer);
}

void tr_peerMgr::refillUpkeep() const
{
    auto const lock = unique_lock();

    for (auto* const tor : session->torrents())
    {
        tor->swarm->cancelOldRequests();
    }
}

static void peerSuggestedPiece(tr_swarm* /*s*/, tr_peer* /*peer*/, tr_piece_index_t /*pieceIndex*/, bool /*isFastAllowed*/)
{
#if 0

    TR_ASSERT(t != nullptr);
    TR_ASSERT(peer != nullptr);
    TR_ASSERT(peer->msgs != nullptr);

    /* is this a valid piece? */
    if (pieceIndex >= t->tor->pieceCount())
    {
        return;
    }

    /* don't ask for it if we've already got it */
    if (t->tor->hasPiece(pieceIndex))
    {
        return;
    }

    /* don't ask for it if they don't have it */
    if (!peer->have.readBit(pieceIndex))
    {
        return;
    }

    /* don't ask for it if we're choked and it's not fast */
    if (!isFastAllowed && peer->clientIsChoked)
    {
        return;
    }

    /* request the blocks that we don't have in this piece */
    {
        tr_torrent const* tor = t->tor;
        auto const [begin, end] = tor->blockSpanForPiece(pieceIndex);

        for (tr_block_index_t b = begin; b < end; ++b)
        {
            if (tor->hasBlock(b))
            {
                uint32_t const offset = getBlockOffsetInPiece(tor, b);
                uint32_t const length = tor->blockSize(b);
                tr_peerMsgsAddRequest(peer->msgs, pieceIndex, offset, length);
                incrementPieceRequests(t, pieceIndex);
            }
        }
    }
#endif
}

void tr_peerMgrPieceCompleted(tr_torrent* tor, tr_piece_index_t p)
{
    bool pieceCameFromPeers = false;
    tr_swarm* const s = tor->swarm;

    /* walk through our peers */
    for (auto* const peer : s->peers)
    {
        // notify the peer that we now have this piece
        peer->on_piece_completed(p);

        if (!pieceCameFromPeers)
        {
            pieceCameFromPeers = peer->blame.test(p);
        }
    }

    if (pieceCameFromPeers) /* webseed downloads don't belong in announce totals */
    {
        tr_announcerAddBytes(tor, TR_ANN_DOWN, tor->pieceSize(p));
    }

    /* bookkeeping */
    s->needs_completeness_check = true;
}

static void peerCallbackFunc(tr_peer* peer, tr_peer_event const* e, void* vs)
{
    TR_ASSERT(peer != nullptr);
    auto* s = static_cast<tr_swarm*>(vs);
    auto const lock = s->unique_lock();

    switch (e->eventType)
    {
    case TR_PEER_PEER_GOT_PIECE_DATA:
        {
            auto const now = tr_time();
            auto* const tor = s->tor;

            tor->uploadedCur += e->length;
            tr_announcerAddBytes(tor, TR_ANN_UP, e->length);
            tor->setDateActive(now);
            tor->setDirty();
            tr_statsAddUploaded(tor->session, e->length);

            if (peer->atom != nullptr)
            {
                peer->atom->piece_data_time = now;
            }

            break;
        }

    case TR_PEER_CLIENT_GOT_PIECE_DATA:
        {
            auto const now = tr_time();
            auto* const tor = s->tor;

            tor->downloadedCur += e->length;
            tor->setDateActive(now);
            tor->setDirty();

            tr_statsAddDownloaded(tor->session, e->length);

            if (peer->atom != nullptr)
            {
                peer->atom->piece_data_time = now;
            }

            break;
        }

    case TR_PEER_CLIENT_GOT_HAVE:
    case TR_PEER_CLIENT_GOT_HAVE_ALL:
    case TR_PEER_CLIENT_GOT_HAVE_NONE:
    case TR_PEER_CLIENT_GOT_BITFIELD:
        /* TODO: if we don't need these, should these events be removed? */
        /* noop */
        break;

    case TR_PEER_CLIENT_GOT_REJ:
        s->active_requests.remove(s->tor->pieceLoc(e->pieceIndex, e->offset).block, peer);
        break;

    case TR_PEER_CLIENT_GOT_CHOKE:
        s->active_requests.remove(peer);
        break;

    case TR_PEER_CLIENT_GOT_PORT:
        if (peer->atom != nullptr)
        {
            peer->atom->port = e->port;
        }

        break;

    case TR_PEER_CLIENT_GOT_SUGGEST:
        peerSuggestedPiece(s, peer, e->pieceIndex, false);
        break;

    case TR_PEER_CLIENT_GOT_ALLOWED_FAST:
        peerSuggestedPiece(s, peer, e->pieceIndex, true);
        break;

    case TR_PEER_CLIENT_GOT_BLOCK:
        {
            auto* const tor = s->tor;
            auto const loc = tor->pieceLoc(e->pieceIndex, e->offset);
            s->cancelAllRequestsForBlock(loc.block, peer);
            peer->blocks_sent_to_client.add(tr_time(), 1);
            tr_torrentGotBlock(tor, loc.block);
            break;
        }

    case TR_PEER_ERROR:
        if (e->err == ERANGE || e->err == EMSGSIZE || e->err == ENOTCONN)
        {
            /* some protocol error from the peer */
            peer->do_purge = true;
            tr_logAddDebugSwarm(
                s,
                fmt::format(
                    "setting {} do_purge flag because we got an ERANGE, EMSGSIZE, or ENOTCONN error",
                    peer->readable()));
        }
        else
        {
            tr_logAddDebugSwarm(s, fmt::format("unhandled error: {}", tr_strerror(e->err)));
        }

        break;

    default:
        TR_ASSERT_MSG(false, fmt::format(FMT_STRING("unhandled peer event type {:d}"), e->eventType));
    }
}

static struct peer_atom* ensureAtomExists(
    tr_swarm* s,
    tr_address const& addr,
    tr_port const port,
    uint8_t const flags,
    uint8_t const from)
{
    TR_ASSERT(tr_address_is_valid(&addr));
    TR_ASSERT(from < TR_PEER_FROM__MAX);

    struct peer_atom* a = getExistingAtom(s, addr);

    if (a == nullptr)
    {
        a = &s->pool.emplace_back(addr, port, flags, from);
    }
    else
    {
        a->fromBest = std::min(a->fromBest, from);
        a->flags |= flags;
    }

    s->markAllSeedsFlagDirty();

    return a;
}

[[nodiscard]] static constexpr auto getMaxPeerCount(tr_torrent const* tor) noexcept
{
    return tor->max_connected_peers;
}

static void createBitTorrentPeer(tr_torrent* tor, tr_peerIo* io, struct peer_atom* atom, tr_quark client)
{
    TR_ASSERT(atom != nullptr);
    TR_ASSERT(tr_isTorrent(tor));
    TR_ASSERT(tor->swarm != nullptr);

    tr_swarm* swarm = tor->swarm;

    auto* peer = tr_peerMsgsNew(tor, atom, io, peerCallbackFunc, swarm);
    peer->client = client;
    atom->is_connected = true;

    swarm->peers.push_back(peer);

    ++swarm->stats.peer_count;
    ++swarm->stats.peer_from_count[atom->fromFirst];

    TR_ASSERT(swarm->stats.peer_count == swarm->peerCount());
    TR_ASSERT(swarm->stats.peer_from_count[atom->fromFirst] <= swarm->stats.peer_count);

    // TODO is this needed?
    // isn't it already initialized in tr_peerMsgsImpl's ctor?
    peer->update_active(TR_UP);
    peer->update_active(TR_DOWN);
}

/* FIXME: this is kind of a mess. */
static bool on_handshake_done(tr_handshake_result const& result)
{
    TR_ASSERT(result.io != nullptr);

    bool ok = result.isConnected;
    bool success = false;
    auto* manager = static_cast<tr_peerMgr*>(result.userData);

    auto const hash = tr_peerIoGetTorrentHash(result.io);
    tr_swarm* const s = hash ? getExistingSwarm(manager, *hash) : nullptr;

    auto const [addr, port] = result.io->socketAddress();

    if (result.io->isIncoming())
    {
        manager->incoming_handshakes.erase(addr);
    }
    else if (s != nullptr)
    {
        s->outgoing_handshakes.erase(addr);
    }

    auto const lock = manager->unique_lock();

    if (!ok || s == nullptr || !s->is_running)
    {
        if (s != nullptr)
        {
            struct peer_atom* atom = getExistingAtom(s, addr);

            if (atom != nullptr)
            {
                ++atom->num_fails;

                if (!result.readAnythingFromPeer)
                {
                    tr_logAddTraceSwarm(
                        s,
                        fmt::format("marking peer {} as unreachable... num_fails is {}", atom->readable(), atom->num_fails));
                    atom->flags2 |= MyflagUnreachable;
                }
            }
        }
    }
    else /* looking good */
    {
        struct peer_atom* atom = ensureAtomExists(s, addr, port, 0, TR_PEER_FROM_INCOMING);

        atom->time = tr_time();
        atom->piece_data_time = 0;
        atom->lastConnectionAt = tr_time();

        if (!result.io->isIncoming())
        {
            atom->flags |= ADDED_F_CONNECTABLE;
            atom->flags2 &= ~MyflagUnreachable;
        }

        /* In principle, this flag specifies whether the peer groks uTP,
           not whether it's currently connected over uTP. */
        if (result.io->socket.type == TR_PEER_SOCKET_TYPE_UTP)
        {
            atom->flags |= ADDED_F_UTP_FLAGS;
        }

        if ((atom->flags2 & MyflagBanned) != 0)
        {
            tr_logAddTraceSwarm(s, fmt::format("banned peer {} tried to reconnect", atom->readable()));
        }
        else if (result.io->isIncoming() && s->peerCount() >= getMaxPeerCount(s->tor))
        {
            /* too many peers already */
        }
        else if (atom->is_connected)
        {
            // we're already connected to this peer; do nothing
        }
        else
        {
            auto client = tr_quark{ TR_KEY_NONE };
            if (result.peer_id)
            {
                char buf[128] = {};
                tr_clientForId(buf, sizeof(buf), *result.peer_id);
                client = tr_quark_new(buf);
            }

            /* this steals its refcount too, which is balanced by our unref in peerDelete() */
            tr_peerIo* stolen = tr_handshakeStealIO(result.handshake);
            stolen->setParent(&s->tor->bandwidth_);
            createBitTorrentPeer(s->tor, stolen, atom, client);

            success = true;
        }
    }

    return success;
}

void tr_peerMgrAddIncoming(tr_peerMgr* manager, tr_address const* addr, tr_port port, struct tr_peer_socket const socket)
{
    TR_ASSERT(tr_isSession(manager->session));
    auto const lock = manager->unique_lock();

    tr_session* session = manager->session;

    if (tr_sessionIsAddressBlocked(session, addr))
    {
        tr_logAddTrace(fmt::format("Banned IP address '{}' tried to connect to us", tr_address_to_string(addr)));
        tr_netClosePeerSocket(session, socket);
    }
    else if (manager->incoming_handshakes.contains(*addr))
    {
        tr_netClosePeerSocket(session, socket);
    }
    else /* we don't have a connection to them yet... */
    {
        tr_peerIo* const io = tr_peerIoNewIncoming(session, &session->top_bandwidth_, addr, port, tr_time(), socket);
        tr_handshake* const handshake = tr_handshakeNew(io, session->encryptionMode, on_handshake_done, manager);

        tr_peerIoUnref(io); /* balanced by the implicit ref in tr_peerIoNewIncoming() */

        manager->incoming_handshakes.add(*addr, handshake);
    }
}

void tr_peerMgrSetSwarmIsAllSeeds(tr_torrent* tor)
{
    auto const lock = tor->unique_lock();

    auto* const swarm = tor->swarm;

    for (auto& atom : swarm->pool)
    {
        atomSetSeed(swarm, atom);
    }

    swarm->markAllSeedsFlagDirty();
}

size_t tr_peerMgrAddPex(tr_torrent* tor, uint8_t from, tr_pex const* pex, size_t n_pex)
{
    size_t n_used = 0;
    tr_swarm* s = tor->swarm;
    auto const lock = s->manager->unique_lock();

    for (tr_pex const* const end = pex + n_pex; pex != end; ++pex)
    {
        if (tr_isPex(pex) && /* safeguard against corrupt data */
            !tr_sessionIsAddressBlocked(s->manager->session, &pex->addr) &&
            tr_address_is_valid_for_peers(&pex->addr, pex->port))
        {
            ensureAtomExists(s, pex->addr, pex->port, pex->flags, from);
            ++n_used;
        }
    }

    return n_used;
}

std::vector<tr_pex> tr_peerMgrCompactToPex(void const* compact, size_t compactLen, uint8_t const* added_f, size_t added_f_len)
{
    size_t n = compactLen / 6;
    auto const* walk = static_cast<uint8_t const*>(compact);
    auto pex = std::vector<tr_pex>(n);

    for (size_t i = 0; i < n; ++i)
    {
        std::tie(pex[i].addr, walk) = tr_address::fromCompact4(walk);
        std::tie(pex[i].port, walk) = tr_port::fromCompact(walk);

        if (added_f != nullptr && n == added_f_len)
        {
            pex[i].flags = added_f[i];
        }
    }

    return pex;
}

std::vector<tr_pex> tr_peerMgrCompact6ToPex(void const* compact, size_t compactLen, uint8_t const* added_f, size_t added_f_len)
{
    size_t n = compactLen / 18;
    auto const* walk = static_cast<uint8_t const*>(compact);
    auto pex = std::vector<tr_pex>(n);

    for (size_t i = 0; i < n; ++i)
    {
        std::tie(pex[i].addr, walk) = tr_address::fromCompact6(walk);
        std::tie(pex[i].port, walk) = tr_port::fromCompact(walk);

        if (added_f != nullptr && n == added_f_len)
        {
            pex[i].flags = added_f[i];
        }
    }

    return pex;
}

/**
***
**/

void tr_peerMgrGotBadPiece(tr_torrent* tor, tr_piece_index_t pieceIndex)
{
    auto* const swarm = tor->swarm;
    auto const byteCount = tor->pieceSize(pieceIndex);

    for (auto* const peer : swarm->peers)
    {
        if (peer->blame.test(pieceIndex))
        {
            tr_logAddTraceSwarm(
                swarm,
                fmt::format(
                    "peer {} contributed to corrupt piece ({}); now has {} strikes",
                    peer->readable(),
                    pieceIndex,
                    peer->strikes + 1));
            swarm->addStrike(peer);
        }
    }

    tr_announcerAddBytes(tor, TR_ANN_CORRUPT, byteCount);
}

namespace get_peers_helpers
{

/* better goes first */
struct CompareAtomsByUsefulness
{
    [[nodiscard]] constexpr static int compare(peer_atom const& a, peer_atom const& b) noexcept // <=>
    {
        if (a.piece_data_time != b.piece_data_time)
        {
            return a.piece_data_time > b.piece_data_time ? -1 : 1;
        }

        if (a.fromBest != b.fromBest)
        {
            return a.fromBest < b.fromBest ? -1 : 1;
        }

        if (a.num_fails != b.num_fails)
        {
            return a.num_fails < b.num_fails ? -1 : 1;
        }

        return 0;
    }

    [[nodiscard]] constexpr bool operator()(peer_atom const& a, peer_atom const& b) const noexcept
    {
        return compare(a, b) < 0;
    }

    [[nodiscard]] constexpr bool operator()(peer_atom const* a, peer_atom const* b) const noexcept
    {
        return compare(*a, *b) < 0;
    }
};

[[nodiscard]] bool isAtomInteresting(tr_torrent const* tor, peer_atom const& atom)
{
    if (tor->isDone() && atom.isSeed())
    {
        return false;
    }

    if (peerIsInUse(tor->swarm, &atom))
    {
        return true;
    }

    if (atom.isBlocklisted(tor->session))
    {
        return false;
    }

    if ((atom.flags2 & MyflagBanned) != 0)
    {
        return false;
    }

    return true;
}

} // namespace get_peers_helpers

std::vector<tr_pex> tr_peerMgrGetPeers(tr_torrent const* tor, uint8_t af, uint8_t list_mode, size_t max_count)
{
    using namespace get_peers_helpers;

    TR_ASSERT(tr_isTorrent(tor));
    auto const lock = tor->unique_lock();

    TR_ASSERT(af == TR_AF_INET || af == TR_AF_INET6);
    TR_ASSERT(list_mode == TR_PEERS_CONNECTED || list_mode == TR_PEERS_INTERESTING);

    tr_swarm const* s = tor->swarm;

    /**
    ***  build a list of atoms
    **/

    auto atoms = std::vector<peer_atom const*>{};
    if (list_mode == TR_PEERS_CONNECTED) /* connected peers only */
    {
        atoms.reserve(s->peerCount());
        std::transform(
            std::begin(s->peers),
            std::end(s->peers),
            std::back_inserter(atoms),
            [](auto const* peer) { return peer->atom; });
    }
    else /* TR_PEERS_INTERESTING */
    {
        for (auto const& atom : s->pool)
        {
            if (isAtomInteresting(tor, atom))
            {
                atoms.push_back(&atom);
            }
        }
    }

    std::sort(std::begin(atoms), std::end(atoms), CompareAtomsByUsefulness{});

    /**
    ***  add the first N of them into our return list
    **/

    auto const n = std::min(std::size(atoms), max_count);
    auto pex = std::vector<tr_pex>{};
    pex.reserve(n);

    for (size_t i = 0; i < std::size(atoms) && std::size(pex) < n; ++i)
    {
        auto const* const atom = atoms[i];

        if (atom->addr.type == af)
        {
            TR_ASSERT(tr_address_is_valid(&atom->addr));
            pex.emplace_back(atom->addr, atom->port, atom->flags);
        }
    }

    std::sort(std::begin(pex), std::end(pex));
    return pex;
}

void tr_peerMgrStartTorrent(tr_torrent* tor)
{
    TR_ASSERT(tr_isTorrent(tor));
    auto const lock = tor->unique_lock();

    tr_swarm* const swarm = tor->swarm;

    swarm->is_running = true;
    swarm->max_peers = getMaxPeerCount(tor);

    swarm->manager->rechokeSoon();
}

void tr_peerMgrStopTorrent(tr_torrent* tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    tor->swarm->stop();
}

void tr_peerMgrAddTorrent(tr_peerMgr* manager, tr_torrent* tor)
{
    TR_ASSERT(tr_isTorrent(tor));
    auto const lock = tor->unique_lock();
    TR_ASSERT(tor->swarm == nullptr);

    tor->swarm = new tr_swarm{ manager, tor };
}

void tr_peerMgrRemoveTorrent(tr_torrent* tor)
{
    TR_ASSERT(tr_isTorrent(tor));
    auto const lock = tor->unique_lock();

    tor->swarm->stop();
    swarmFree(tor->swarm);
}

void tr_peerMgrOnTorrentGotMetainfo(tr_torrent* tor)
{
    /* the webseed list may have changed... */
    tor->swarm->rebuildWebseeds();

    /* some peer_msgs' progress fields may not be accurate if we
       didn't have the metadata before now... so refresh them all... */
    for (auto* peer : tor->swarm->peers)
    {
        peer->onTorrentGotMetainfo();

        if (peer->isSeed())
        {
            atomSetSeed(tor->swarm, *peer->atom);
        }
    }

    /* update the bittorrent peers' willingness... */
    for (auto* peer : tor->swarm->peers)
    {
        peer->update_active(TR_UP);
        peer->update_active(TR_DOWN);
    }
}

int8_t tr_peerMgrPieceAvailability(tr_torrent const* tor, tr_piece_index_t piece)
{
    if (!tor->hasMetainfo())
    {
        return 0;
    }

    if (tor->isSeed() || tor->hasPiece(piece))
    {
        return -1;
    }

    auto const& peers = tor->swarm->peers;
    return std::count_if(std::begin(peers), std::end(peers), [piece](auto const* peer) { return peer->hasPiece(piece); });
}

void tr_peerMgrTorrentAvailability(tr_torrent const* tor, int8_t* tab, unsigned int n_tabs)
{
    TR_ASSERT(tr_isTorrent(tor));
    TR_ASSERT(tab != nullptr);
    TR_ASSERT(n_tabs > 0);

    std::fill_n(tab, n_tabs, int8_t{});

    auto const interval = tor->pieceCount() / static_cast<float>(n_tabs);
    for (tr_piece_index_t i = 0; i < n_tabs; ++i)
    {
        auto const piece = static_cast<tr_piece_index_t>(i * interval);
        tab[i] = tr_peerMgrPieceAvailability(tor, piece);
    }
}

tr_swarm_stats tr_swarmGetStats(tr_swarm const* swarm)
{
    TR_ASSERT(swarm != nullptr);

    return swarm->stats;
}

void tr_swarmIncrementActivePeers(tr_swarm* swarm, tr_direction direction, bool is_active)
{
    int n = swarm->stats.active_peer_count[direction];

    if (is_active)
    {
        ++n;
    }
    else
    {
        --n;
    }

    TR_ASSERT(n >= 0);
    TR_ASSERT(n <= swarm->stats.peer_count);

    swarm->stats.active_peer_count[direction] = n;
}

/* count how many bytes we want that connected peers have */
uint64_t tr_peerMgrGetDesiredAvailable(tr_torrent const* tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    // common shortcuts...

    if (!tor->isRunning || tor->isStopping || tor->isDone() || !tor->hasMetainfo())
    {
        return 0;
    }

    tr_swarm const* const s = tor->swarm;
    if (s == nullptr || !s->is_running)
    {
        return 0;
    }

    auto const n_peers = s->peerCount();
    if (n_peers == 0)
    {
        return 0;
    }

    for (auto const* const peer : s->peers)
    {
        if (peer->atom != nullptr && peer->atom->isSeed())
        {
            return tor->leftUntilDone();
        }
    }

    // do it the hard way

    auto desired_available = uint64_t{};
    auto const n_pieces = tor->pieceCount();
    auto have = std::vector<bool>(n_pieces);

    for (auto const* const peer : s->peers)
    {
        for (size_t j = 0; j < n_pieces; ++j)
        {
            if (peer->hasPiece(j))
            {
                have[j] = true;
            }
        }
    }

    for (size_t i = 0; i < n_pieces; ++i)
    {
        if (tor->pieceIsWanted(i) && have.at(i))
        {
            desired_available += tor->countMissingBytesInPiece(i);
        }
    }

    TR_ASSERT(desired_available <= tor->totalSize());
    return desired_available;
}

tr_webseed_view tr_peerMgrWebseed(tr_torrent const* tor, size_t i)
{
    TR_ASSERT(tr_isTorrent(tor));
    TR_ASSERT(tor->swarm != nullptr);
    size_t const n = std::size(tor->swarm->webseeds);
    TR_ASSERT(i < n);

    return i >= n ? tr_webseed_view{} : tr_webseedView(tor->swarm->webseeds[i].get());
}

namespace peer_stat_helpers
{

[[nodiscard]] auto getPeerStats(tr_peerMsgs const* peer, time_t now, uint64_t now_msec)
{
    auto stats = tr_peer_stat{};
    auto const* const atom = peer->atom;

    auto const [addr, port] = peer->socketAddress();

    tr_address_to_string_with_buf(&addr, stats.addr, sizeof(stats.addr));
    stats.client = peer->client.c_str();
    stats.port = port.host();
    stats.from = atom->fromFirst;
    stats.progress = peer->percentDone();
    stats.isUTP = peer->is_utp_connection();
    stats.isEncrypted = peer->is_encrypted();
    stats.rateToPeer_KBps = tr_toSpeedKBps(tr_peerGetPieceSpeed_Bps(peer, now_msec, TR_CLIENT_TO_PEER));
    stats.rateToClient_KBps = tr_toSpeedKBps(tr_peerGetPieceSpeed_Bps(peer, now_msec, TR_PEER_TO_CLIENT));
    stats.peerIsChoked = peer->is_peer_choked();
    stats.peerIsInterested = peer->is_peer_interested();
    stats.clientIsChoked = peer->is_client_choked();
    stats.clientIsInterested = peer->is_client_interested();
    stats.isIncoming = peer->is_incoming_connection();
    stats.isDownloadingFrom = peer->is_active(TR_PEER_TO_CLIENT);
    stats.isUploadingTo = peer->is_active(TR_CLIENT_TO_PEER);
    stats.isSeed = peer->isSeed();

    stats.blocksToPeer = peer->blocks_sent_to_peer.count(now, CancelHistorySec);
    stats.blocksToClient = peer->blocks_sent_to_client.count(now, CancelHistorySec);
    stats.cancelsToPeer = peer->cancels_sent_to_peer.count(now, CancelHistorySec);
    stats.cancelsToClient = peer->cancels_sent_to_client.count(now, CancelHistorySec);

    stats.activeReqsToPeer = peer->activeReqCount(TR_CLIENT_TO_PEER);
    stats.activeReqsToClient = peer->activeReqCount(TR_PEER_TO_CLIENT);

    char* pch = stats.flagStr;

    if (stats.isUTP)
    {
        *pch++ = 'T';
    }

    if (peer->swarm->optimistic == peer)
    {
        *pch++ = 'O';
    }

    if (stats.isDownloadingFrom)
    {
        *pch++ = 'D';
    }
    else if (stats.clientIsInterested)
    {
        *pch++ = 'd';
    }

    if (stats.isUploadingTo)
    {
        *pch++ = 'U';
    }
    else if (stats.peerIsInterested)
    {
        *pch++ = 'u';
    }

    if (!stats.clientIsChoked && !stats.clientIsInterested)
    {
        *pch++ = 'K';
    }

    if (!stats.peerIsChoked && !stats.peerIsInterested)
    {
        *pch++ = '?';
    }

    if (stats.isEncrypted)
    {
        *pch++ = 'E';
    }

    if (stats.from == TR_PEER_FROM_DHT)
    {
        *pch++ = 'H';
    }
    else if (stats.from == TR_PEER_FROM_PEX)
    {
        *pch++ = 'X';
    }

    if (stats.isIncoming)
    {
        *pch++ = 'I';
    }

    *pch = '\0';

    return stats;
}

} // namespace peer_stat_helpers

tr_peer_stat* tr_peerMgrPeerStats(tr_torrent const* tor, int* setme_count)
{
    using namespace peer_stat_helpers;

    TR_ASSERT(tr_isTorrent(tor));
    TR_ASSERT(tor->swarm->manager != nullptr);

    auto const n = tor->swarm->peerCount();
    auto* const ret = tr_new0(tr_peer_stat, n);

    auto const now = tr_time();
    auto const now_msec = tr_time_msec();
    std::transform(
        std::begin(tor->swarm->peers),
        std::end(tor->swarm->peers),
        ret,
        [&now, &now_msec](auto const* peer) { return getPeerStats(peer, now, now_msec); });

    *setme_count = n;
    return ret;
}

void tr_peerMgrClearInterest(tr_torrent* tor)
{
    TR_ASSERT(tr_isTorrent(tor));
    auto const lock = tor->unique_lock();

    auto& peers = tor->swarm->peers;
    std::for_each(std::begin(peers), std::end(peers), [](auto* const peer) { peer->set_interested(false); });
}

namespace rechoke_downloads_helpers
{
namespace
{

/* does this peer have any pieces that we want? */
[[nodiscard]] bool isPeerInteresting(
    tr_torrent* const tor,
    bool const* const piece_is_interesting,
    tr_peerMsgs const* const peer)
{
    /* these cases should have already been handled by the calling code... */
    TR_ASSERT(!tor->isDone());
    TR_ASSERT(tor->clientCanDownload());

    if (peer->isSeed())
    {
        return true;
    }

    for (tr_piece_index_t i = 0; i < tor->pieceCount(); ++i)
    {
        if (piece_is_interesting[i] && peer->hasPiece(i))
        {
            return true;
        }
    }

    return false;
}

enum tr_rechoke_state
{
    RECHOKE_STATE_GOOD,
    RECHOKE_STATE_UNTESTED,
    RECHOKE_STATE_BAD
};

struct tr_rechoke_info
{
    tr_peerMsgs* peer;
    int salt;
    int rechoke_state;
};

[[nodiscard]] constexpr int compare_rechoke_info(void const* va, void const* vb)
{
    auto const* const a = static_cast<struct tr_rechoke_info const*>(va);
    auto const* const b = static_cast<struct tr_rechoke_info const*>(vb);

    if (a->rechoke_state != b->rechoke_state)
    {
        return a->rechoke_state - b->rechoke_state;
    }

    return a->salt - b->salt;
}

} // namespace

/* determines who we send "interested" messages to */
void rechokeDownloads(tr_swarm* s)
{
    static auto constexpr MinInterestingPeers = uint16_t{ 5 };

    auto const peerCount = s->peerCount();
    auto const& peers = s->peers;
    auto const now = tr_time();

    uint16_t max_peers = 0;
    uint16_t rechoke_count = 0;
    struct tr_rechoke_info* rechoke = nullptr;

    /* some cases where this function isn't necessary */
    if (s->tor->isDone() || !s->tor->clientCanDownload())
    {
        return;
    }

    /* decide HOW MANY peers to be interested in */
    {
        int blocks = 0;
        int cancels = 0;

        /* Count up how many blocks & cancels each peer has.
         *
         * There are two situations where we send out cancels --
         *
         * 1. We've got unresponsive peers, which is handled by deciding
         *    -which- peers to be interested in.
         *
         * 2. We've hit our bandwidth cap, which is handled by deciding
         *    -how many- peers to be interested in.
         *
         * We're working on 2. here, so we need to ignore unresponsive
         * peers in our calculations lest they confuse Transmission into
         * thinking it's hit its bandwidth cap.
         */
        for (auto const* const peer : peers)
        {
            auto const b = peer->blocks_sent_to_client.count(now, CancelHistorySec);

            if (b == 0) /* ignore unresponsive peers, as described above */
            {
                continue;
            }

            blocks += b;
            cancels += peer->cancels_sent_to_peer.count(now, CancelHistorySec);
        }

        if (cancels > 0)
        {
            /* cancelRate: of the block requests we've recently made, the percentage we cancelled.
             * higher values indicate more congestion. */
            double const cancelRate = cancels / (double)(cancels + blocks);
            double const mult = 1 - std::min(cancelRate, 0.5);
            max_peers = s->interested_count * mult;
            tr_logAddTraceSwarm(
                s,
                fmt::format(
                    "cancel rate is {} -- reducing the number of peers we're interested in by {} percent",
                    cancelRate,
                    mult * 100));
            s->lastCancel = now;
        }

        time_t const timeSinceCancel = now - s->lastCancel;

        if (timeSinceCancel != 0)
        {
            int const maxIncrease = 15;
            time_t const maxHistory = 2 * CancelHistorySec;
            double const mult = std::min(timeSinceCancel, maxHistory) / (double)maxHistory;
            int const inc = maxIncrease * mult;
            max_peers = s->max_peers + inc;
            tr_logAddTraceSwarm(
                s,
                fmt::format(
                    "time since last cancel is {} -- increasing the number of peers we're interested in by {}",
                    timeSinceCancel,
                    inc));
        }
    }

    /* don't let the previous section's number tweaking go too far... */
    max_peers = std::clamp(max_peers, MinInterestingPeers, s->tor->max_connected_peers);

    s->max_peers = max_peers;

    if (peerCount > 0)
    {
        auto const* const tor = s->tor;
        int const n = tor->pieceCount();

        /* build a bitfield of interesting pieces... */
        auto* const piece_is_interesting = tr_new(bool, n);

        for (int i = 0; i < n; ++i)
        {
            piece_is_interesting[i] = tor->pieceIsWanted(i) && !tor->hasPiece(i);
        }

        /* decide WHICH peers to be interested in (based on their cancel-to-block ratio) */
        for (auto* const peer : peers)
        {
            if (!isPeerInteresting(s->tor, piece_is_interesting, peer))
            {
                peer->set_interested(false);
            }
            else
            {
                auto rechoke_state = tr_rechoke_state{};
                auto const blocks = peer->blocks_sent_to_client.count(now, CancelHistorySec);
                auto const cancels = peer->cancels_sent_to_peer.count(now, CancelHistorySec);

                if (blocks == 0 && cancels == 0)
                {
                    rechoke_state = RECHOKE_STATE_UNTESTED;
                }
                else if (cancels == 0)
                {
                    rechoke_state = RECHOKE_STATE_GOOD;
                }
                else if (blocks == 0)
                {
                    rechoke_state = RECHOKE_STATE_BAD;
                }
                else if (cancels * 10 < blocks)
                {
                    rechoke_state = RECHOKE_STATE_GOOD;
                }
                else
                {
                    rechoke_state = RECHOKE_STATE_BAD;
                }

                if (rechoke == nullptr)
                {
                    rechoke = tr_new(struct tr_rechoke_info, peerCount);
                }

                rechoke[rechoke_count].peer = peer;
                rechoke[rechoke_count].rechoke_state = rechoke_state;
                rechoke[rechoke_count].salt = tr_rand_int_weak(INT_MAX);
                rechoke_count++;
            }
        }

        tr_free(piece_is_interesting);
    }

    if ((rechoke != nullptr) && (rechoke_count > 0))
    {
        qsort(rechoke, rechoke_count, sizeof(struct tr_rechoke_info), compare_rechoke_info);
    }

    /* now that we know which & how many peers to be interested in... update the peer interest */

    s->interested_count = std::min(max_peers, rechoke_count);

    for (int i = 0; i < rechoke_count; ++i)
    {
        rechoke[i].peer->set_interested(i < s->interested_count);
    }

    /* cleanup */
    tr_free(rechoke);
}

} // namespace rechoke_downloads_helpers

/**
***
**/

[[nodiscard]] static inline bool isBandwidthMaxedOut(tr_bandwidth const& b, uint64_t const now_msec, tr_direction dir)
{
    if (!b.isLimited(dir))
    {
        return false;
    }

    unsigned int const got = b.getPieceSpeedBytesPerSecond(now_msec, dir);
    unsigned int const want = b.getDesiredSpeedBytesPerSecond(dir);
    return got >= want;
}

namespace rechoke_uploads_helpers
{
namespace
{

struct ChokeData
{
    bool isInterested;
    bool wasChoked;
    bool isChoked;
    int rate;
    int salt;
    tr_peerMsgs* msgs;
};

[[nodiscard]] constexpr int compareChoke(void const* va, void const* vb) noexcept
{
    auto const* const a = static_cast<struct ChokeData const*>(va);
    auto const* const b = static_cast<struct ChokeData const*>(vb);

    if (a->rate != b->rate) // prefer higher overall speeds
    {
        return a->rate > b->rate ? -1 : 1;
    }

    if (a->wasChoked != b->wasChoked) // prefer unchoked
    {
        return a->wasChoked ? 1 : -1;
    }

    if (a->salt != b->salt) // random order
    {
        return a->salt - b->salt;
    }

    return 0;
}

/* is this a new connection? */
[[nodiscard]] bool isNew(tr_peerMsgs const* msgs)
{
    auto constexpr CutoffSecs = time_t{ 45 };
    return msgs != nullptr && !msgs->is_connection_older_than(tr_time() - CutoffSecs);
}

/* get a rate for deciding which peers to choke and unchoke. */
[[nodiscard]] auto getRateBps(tr_torrent const* tor, tr_peer const* peer, uint64_t now)
{
    if (tor->isDone())
    {
        return tr_peerGetPieceSpeed_Bps(peer, now, TR_CLIENT_TO_PEER);
    }

    /* downloading a private torrent... take upload speed into account
     * because there may only be a small window of opportunity to share */
    if (tor->isPrivate())
    {
        return tr_peerGetPieceSpeed_Bps(peer, now, TR_PEER_TO_CLIENT) + tr_peerGetPieceSpeed_Bps(peer, now, TR_CLIENT_TO_PEER);
    }

    /* downloading a public torrent */
    return tr_peerGetPieceSpeed_Bps(peer, now, TR_PEER_TO_CLIENT);
}

// an optimistically unchoked peer is immune from rechoking
// for this many calls to rechokeUploads().
auto constexpr OptimisticUnchokeMultiplier = uint8_t{ 4 };

} // namespace

void rechokeUploads(tr_swarm* s, uint64_t const now)
{
    auto const lock = s->unique_lock();

    auto const peerCount = s->peerCount();
    auto& peers = s->peers;
    auto* const choke = tr_new0(struct ChokeData, peerCount);
    auto const* const session = s->manager->session;
    bool const chokeAll = !s->tor->clientCanUpload();
    bool const isMaxedOut = isBandwidthMaxedOut(s->tor->bandwidth_, now, TR_UP);

    /* an optimistic unchoke peer's "optimistic"
     * state lasts for N calls to rechokeUploads(). */
    if (s->optimistic_unchoke_time_scaler > 0)
    {
        --s->optimistic_unchoke_time_scaler;
    }
    else
    {
        s->optimistic = nullptr;
    }

    int size = 0;

    /* sort the peers by preference and rate */
    for (auto* const peer : peers)
    {
        if (peer->isSeed())
        {
            /* choke seeds and partial seeds */
            peer->set_choke(true);
        }
        else if (chokeAll)
        {
            /* choke everyone if we're not uploading */
            peer->set_choke(true);
        }
        else if (peer != s->optimistic)
        {
            struct ChokeData* n = &choke[size++];
            n->msgs = peer;
            n->isInterested = peer->is_peer_interested();
            n->wasChoked = peer->is_peer_choked();
            n->rate = getRateBps(s->tor, peer, now);
            n->salt = tr_rand_int_weak(INT_MAX);
            n->isChoked = true;
        }
    }

    qsort(choke, size, sizeof(struct ChokeData), compareChoke);

    /**
     * Reciprocation and number of uploads capping is managed by unchoking
     * the N peers which have the best upload rate and are interested.
     * This maximizes the client's download rate. These N peers are
     * referred to as downloaders, because they are interested in downloading
     * from the client.
     *
     * Peers which have a better upload rate (as compared to the downloaders)
     * but aren't interested get unchoked. If they become interested, the
     * downloader with the worst upload rate gets choked. If a client has
     * a complete file, it uses its upload rate rather than its download
     * rate to decide which peers to unchoke.
     *
     * If our bandwidth is maxed out, don't unchoke any more peers.
     */
    int checkedChokeCount = 0;
    int unchokedInterested = 0;

    for (int i = 0; i < size && unchokedInterested < session->uploadSlotsPerTorrent; ++i)
    {
        choke[i].isChoked = isMaxedOut ? choke[i].wasChoked : false;

        ++checkedChokeCount;

        if (choke[i].isInterested)
        {
            ++unchokedInterested;
        }
    }

    /* optimistic unchoke */
    if (s->optimistic == nullptr && !isMaxedOut && checkedChokeCount < size)
    {
        auto randPool = std::vector<ChokeData*>{};

        for (int i = checkedChokeCount; i < size; ++i)
        {
            if (choke[i].isInterested)
            {
                tr_peerMsgs const* msgs = choke[i].msgs;
                int const x = isNew(msgs) ? 3 : 1;

                for (int y = 0; y < x; ++y)
                {
                    randPool.push_back(&choke[i]);
                }
            }
        }

        auto const n = std::size(randPool);
        if (n != 0)
        {
            auto* c = randPool[tr_rand_int_weak(n)];
            c->isChoked = false;
            s->optimistic = c->msgs;
            s->optimistic_unchoke_time_scaler = OptimisticUnchokeMultiplier;
        }
    }

    for (int i = 0; i < size; ++i)
    {
        choke[i].msgs->set_choke(choke[i].isChoked);
    }

    /* cleanup */
    tr_free(choke);
}

} // namespace rechoke_uploads_helpers

void tr_peerMgr::rechokePulse() const
{
    using namespace rechoke_downloads_helpers;
    using namespace rechoke_uploads_helpers;

    auto const lock = unique_lock();
    auto const now = tr_time_msec();

    for (auto* const tor : session->torrents())
    {
        if (!tor->isRunning)
        {
            continue;
        }

        if (auto* const swarm = tor->swarm; swarm->stats.peer_count > 0)
        {
            rechokeUploads(swarm, now);
            rechokeDownloads(swarm);
        }
    }
}

/***
****
****  Life and Death
****
***/

namespace disconnect_helpers
{
namespace
{
// when many peers are available, keep idle ones this long
auto constexpr MinUploadIdleSecs = int{ 60 };

// when few peers are available, keep idle ones this long
auto constexpr MaxUploadIdleSecs = int{ 60 * 5 };

[[nodiscard]] bool shouldPeerBeClosed(tr_swarm const* s, tr_peerMsgs const* peer, int peerCount, time_t const now)
{
    /* if it's marked for purging, close it */
    if (peer->do_purge)
    {
        tr_logAddTraceSwarm(s, fmt::format("purging peer {} because its do_purge flag is set", peer->readable()));
        return true;
    }

    auto const* tor = s->tor;
    auto const* const atom = peer->atom;

    /* disconnect if we're both seeds and enough time has passed for PEX */
    if (tor->isDone() && peer->isSeed())
    {
        return !tor->allowsPex() || now - atom->time >= 30;
    }

    /* disconnect if it's been too long since piece data has been transferred.
     * this is on a sliding scale based on number of available peers... */
    {
        auto const relaxStrictnessIfFewerThanN = std::lround(getMaxPeerCount(tor) * 0.9);
        /* if we have >= relaxIfFewerThan, strictness is 100%.
         * if we have zero connections, strictness is 0% */
        float const strictness = peerCount >= relaxStrictnessIfFewerThanN ? 1.0 :
                                                                            peerCount / (float)relaxStrictnessIfFewerThanN;
        int const lo = MinUploadIdleSecs;
        int const hi = MaxUploadIdleSecs;
        int const limit = hi - (hi - lo) * strictness;
        int const idleTime = now - std::max(atom->time, atom->piece_data_time);

        if (idleTime > limit)
        {
            tr_logAddTraceSwarm(
                s,
                fmt::format("purging peer {} because it's been {} secs since we shared anything", peer->readable(), idleTime));
            return true;
        }
    }

    return false;
}

void closePeer(tr_peer* peer)
{
    TR_ASSERT(peer != nullptr);
    auto const* const s = peer->swarm;

    /* if we transferred piece data, then they might be good peers,
       so reset their `num_fails' weight to zero. otherwise we connected
       to them fruitlessly, so mark it as another fail */
    if (auto* const atom = peer->atom; atom->piece_data_time != 0)
    {
        tr_logAddTraceSwarm(s, fmt::format("resetting atom {} num_fails to 0", peer->readable()));
        atom->num_fails = 0;
    }
    else
    {
        ++atom->num_fails;
        tr_logAddTraceSwarm(s, fmt::format("incremented atom {} num_fails to {}", peer->readable(), atom->num_fails));
    }

    tr_logAddTraceSwarm(s, fmt::format("removing bad peer {}", peer->readable()));
    peer->swarm->removePeer(peer);
}

struct ComparePeerByActivity
{
    [[nodiscard]] constexpr static int compare(tr_peer const* a, tr_peer const* b) // <=>
    {
        if (a->do_purge != b->do_purge)
        {
            return a->do_purge ? 1 : -1;
        }

        /* the one to give us data more recently goes first */
        if (a->atom->piece_data_time != b->atom->piece_data_time)
        {
            return a->atom->piece_data_time > b->atom->piece_data_time ? -1 : 1;
        }

        /* the one we connected to most recently goes first */
        if (a->atom->time != b->atom->time)
        {
            return a->atom->time > b->atom->time ? -1 : 1;
        }

        return 0;
    }

    [[nodiscard]] constexpr bool operator()(tr_peer const* a, tr_peer const* b) const // less then
    {
        return compare(a, b) < 0;
    }
};

[[nodiscard]] auto getPeersToClose(tr_swarm* swarm, time_t const now_sec)
{
    auto peers_to_close = std::vector<tr_peer*>{};

    auto const peer_count = swarm->peerCount();
    for (auto* peer : swarm->peers)
    {
        if (shouldPeerBeClosed(swarm, peer, peer_count, now_sec))
        {
            peers_to_close.push_back(peer);
        }
    }

    return peers_to_close;
}

} // namespace

void closeBadPeers(tr_swarm* s, time_t const now_sec)
{
    auto const lock = s->unique_lock();

    for (auto* peer : getPeersToClose(s, now_sec))
    {
        closePeer(peer);
    }
}

void enforceTorrentPeerLimit(tr_swarm* swarm)
{
    // do we have too many peers?
    auto const n = swarm->peerCount();
    auto const max = tr_torrentGetPeerLimit(swarm->tor);
    if (n <= max)
    {
        return;
    }

    // close all but the `max` most active
    auto peers = swarm->peers;
    std::partial_sort(std::begin(peers), std::begin(peers) + max, std::end(peers), ComparePeerByActivity{});
    std::for_each(std::begin(peers) + max, std::end(peers), closePeer);
}

void enforceSessionPeerLimit(tr_session* session)
{
    // do we have too many peers?
    auto const& torrents = session->torrents();
    size_t const n_peers = std::accumulate(
        std::begin(torrents),
        std::end(torrents),
        size_t{},
        [](size_t sum, tr_torrent const* tor) { return sum + tor->swarm->peerCount(); });
    size_t const max = tr_sessionGetPeerLimit(session);
    if (n_peers <= max)
    {
        return;
    }

    // make a list of all the peers
    auto peers = std::vector<tr_peer*>{};
    peers.reserve(n_peers);
    for (auto const* const tor : session->torrents())
    {
        peers.insert(std::end(peers), std::begin(tor->swarm->peers), std::end(tor->swarm->peers));
    }

    // close all but the `max` most active
    std::partial_sort(std::begin(peers), std::begin(peers) + max, std::end(peers), ComparePeerByActivity{});
    std::for_each(std::begin(peers) + max, std::end(peers), closePeer);
}

} // namespace disconnect_helpers

void tr_peerMgr::reconnectPulse()
{
    using namespace disconnect_helpers;

    auto const now_sec = tr_time();

    // remove crappy peers
    for (auto* const tor : session->torrents())
    {
        if (!tor->swarm->is_running)
        {
            tor->swarm->removeAllPeers();
        }
        else
        {
            closeBadPeers(tor->swarm, now_sec);
        }
    }

    // if we're over the per-torrent peer limits, cull some peers
    for (auto* const tor : session->torrents())
    {
        if (tor->isRunning)
        {
            enforceTorrentPeerLimit(tor->swarm);
        }
    }

    // if we're over the per-session peer limits, cull some peers
    enforceSessionPeerLimit(session);

    // try to make new peer connections
    auto const max_connections_per_pulse = int(MaxConnectionsPerSecond * (ReconnectPeriodMsec / 1000.0));
    makeNewPeerConnections(max_connections_per_pulse);
}

/****
*****
*****  BANDWIDTH ALLOCATION
*****
****/

namespace bandwidth_helpers
{

void pumpAllPeers(tr_peerMgr* mgr)
{
    for (auto* const tor : mgr->session->torrents())
    {
        for (auto* const peer : tor->swarm->peers)
        {
            peer->pulse();
        }
    }
}

void queuePulse(tr_session* session, tr_direction dir)
{
    TR_ASSERT(tr_isSession(session));
    TR_ASSERT(tr_isDirection(dir));

    if (tr_sessionGetQueueEnabled(session, dir))
    {
        auto const n = tr_sessionCountQueueFreeSlots(session, dir);

        for (auto* tor : tr_sessionGetNextQueuedTorrents(session, dir, n))
        {
            tr_torrentStartNow(tor);

            if (tor->queue_started_callback != nullptr)
            {
                (*tor->queue_started_callback)(tor, tor->queue_started_user_data);
            }
        }
    }
}

} // namespace bandwidth_helpers

void tr_peerMgr::bandwidthPulse()
{
    using namespace bandwidth_helpers;

    auto const lock = unique_lock();

    pumpAllPeers(this);

    /* allocate bandwidth to the peers */
    session->top_bandwidth_.allocate(TR_UP, BandwidthPeriodMsec);
    session->top_bandwidth_.allocate(TR_DOWN, BandwidthPeriodMsec);

    /* torrent upkeep */
    for (auto* const tor : session->torrents())
    {
        /* possibly stop torrents that have seeded enough */
        tr_torrentCheckSeedLimit(tor);

        /* run the completeness check for any torrents that need it */
        if (tor->swarm->needs_completeness_check)
        {
            tor->swarm->needs_completeness_check = false;
            tor->recheckCompleteness();
        }

        /* stop torrents that are ready to stop, but couldn't be stopped
           earlier during the peer-io callback call chain */
        if (tor->isStopping)
        {
            tr_torrentStop(tor);
        }

        /* update the torrent's stats */
        tor->swarm->stats.active_webseed_count = tor->swarm->countActiveWebseeds();
    }

    /* pump the queues */
    queuePulse(session, TR_UP);
    queuePulse(session, TR_DOWN);

    reconnectPulse();
}

/***
****
****
****
***/

namespace connect_helpers
{
namespace
{

/* is this atom someone that we'd want to initiate a connection to? */
[[nodiscard]] bool isPeerCandidate(tr_torrent const* tor, peer_atom const& atom, time_t const now)
{
    // have we already tried and failed to connect?
    if (auto const reachable = atom.isReachable(); reachable && !*reachable)
    {
        return false;
    }

    // not if we're both seeds
    if (tor->isDone() && atom.isSeed())
    {
        return false;
    }

    // not if we've already got a connection to them...
    if (peerIsInUse(tor->swarm, &atom))
    {
        return false;
    }

    // not if we just tried them already
    if (now - atom.time < atom.getReconnectIntervalSecs(now))
    {
        return false;
    }

    // not if they're blocklisted
    if (atom.isBlocklisted(tor->session))
    {
        return false;
    }

    // not if they're banned...
    if ((atom.flags2 & MyflagBanned) != 0)
    {
        return false;
    }

    return true;
}

struct peer_candidate
{
    uint64_t score;
    tr_torrent* tor;
    peer_atom* atom;
};

[[nodiscard]] bool torrentWasRecentlyStarted(tr_torrent const* tor)
{
    return difftime(tr_time(), tor->startDate) < 120;
}

[[nodiscard]] constexpr uint64_t addValToKey(uint64_t value, int width, uint64_t addme)
{
    value = value << (uint64_t)width;
    value |= addme;
    return value;
}

/* smaller value is better */
[[nodiscard]] uint64_t getPeerCandidateScore(tr_torrent const* tor, peer_atom const& atom, uint8_t salt)
{
    auto i = uint64_t{};
    auto score = uint64_t{};
    bool const failed = atom.lastConnectionAt < atom.lastConnectionAttemptAt;

    /* prefer peers we've connected to, or never tried, over peers we failed to connect to. */
    i = failed ? 1 : 0;
    score = addValToKey(score, 1, i);

    /* prefer the one we attempted least recently (to cycle through all peers) */
    i = atom.lastConnectionAttemptAt;
    score = addValToKey(score, 32, i);

    /* prefer peers belonging to a torrent of a higher priority */
    switch (tr_torrentGetPriority(tor))
    {
    case TR_PRI_HIGH:
        i = 0;
        break;

    case TR_PRI_NORMAL:
        i = 1;
        break;

    case TR_PRI_LOW:
        i = 2;
        break;
    }

    score = addValToKey(score, 4, i);

    /* prefer recently-started torrents */
    i = torrentWasRecentlyStarted(tor) ? 0 : 1;
    score = addValToKey(score, 1, i);

    /* prefer torrents we're downloading with */
    i = tor->isDone() ? 1 : 0;
    score = addValToKey(score, 1, i);

    /* prefer peers that are known to be connectible */
    i = (atom.flags & ADDED_F_CONNECTABLE) != 0 ? 0 : 1;
    score = addValToKey(score, 1, i);

    /* prefer peers that we might be able to upload to */
    i = (atom.flags & ADDED_F_SEED_FLAG) == 0 ? 0 : 1;
    score = addValToKey(score, 1, i);

    /* Prefer peers that we got from more trusted sources.
     * lower `fromBest' values indicate more trusted sources */
    score = addValToKey(score, 4, atom.fromBest);

    /* salt */
    score = addValToKey(score, 8, salt);

    return score;
}

} // namespace

/** @return an array of all the atoms we might want to connect to */
[[nodiscard]] std::vector<peer_candidate> getPeerCandidates(tr_session* session, size_t max)
{
    auto const now = tr_time();
    auto const now_msec = tr_time_msec();

    // leave 5% of connection slots for incoming connections -- ticket #2609
    auto const max_candidates = static_cast<size_t>(tr_sessionGetPeerLimit(session) * 0.95);

    /* count how many peers and atoms we've got */
    auto atom_count = size_t{};
    auto peer_count = size_t{};
    for (auto const* tor : session->torrents())
    {
        atom_count += std::size(tor->swarm->pool);
        peer_count += tor->swarm->peerCount();
    }

    /* don't start any new handshakes if we're full up */
    if (max_candidates <= peer_count)
    {
        return {};
    }

    auto candidates = std::vector<peer_candidate>{};
    candidates.reserve(atom_count);

    /* populate the candidate array */
    for (auto* tor : session->torrents())
    {
        if (!tor->swarm->is_running)
        {
            continue;
        }

        /* if everyone in the swarm is seeds and pex is disabled because
         * the torrent is private, then don't initiate connections */
        bool const seeding = tor->isDone();
        if (seeding && tor->swarm->isAllSeeds() && tor->isPrivate())
        {
            continue;
        }

        /* if we've already got enough peers in this torrent... */
        if (tr_torrentGetPeerLimit(tor) <= tor->swarm->peerCount())
        {
            continue;
        }

        /* if we've already got enough speed in this torrent... */
        if (seeding && isBandwidthMaxedOut(tor->bandwidth_, now_msec, TR_UP))
        {
            continue;
        }

        for (auto& atom : tor->swarm->pool)
        {
            if (isPeerCandidate(tor, atom, now))
            {
                uint8_t const salt = tr_rand_int_weak(1024);
                candidates.push_back({ getPeerCandidateScore(tor, atom, salt), tor, &atom });
            }
        }
    }

    // only keep the best `max` candidates
    if (std::size(candidates) > max)
    {
        std::partial_sort(
            std::begin(candidates),
            std::begin(candidates) + max,
            std::end(candidates),
            [](auto const& a, auto const& b) { return a.score < b.score; });
        candidates.resize(max);
    }

    return candidates;
}

void initiateConnection(tr_peerMgr* mgr, tr_swarm* s, peer_atom& atom)
{
    time_t const now = tr_time();
    bool utp = tr_sessionIsUTPEnabled(mgr->session) && !atom.utp_failed;

    if (atom.fromFirst == TR_PEER_FROM_PEX)
    {
        /* PEX has explicit signalling for uTP support.  If an atom
           originally came from PEX and doesn't have the uTP flag, skip the
           uTP connection attempt.  Are we being optimistic here? */
        utp = utp && (atom.flags & ADDED_F_UTP_FLAGS) != 0;
    }

    tr_logAddTraceSwarm(s, fmt::format("Starting an OUTGOING {} connection with {}", utp ? " µTP" : "TCP", atom.readable()));

    tr_peerIo* const io = tr_peerIoNewOutgoing(
        mgr->session,
        &mgr->session->top_bandwidth_,
        &atom.addr,
        atom.port,
        tr_time(),
        s->tor->infoHash(),
        s->tor->completeness == TR_SEED,
        utp);

    if (io == nullptr)
    {
        tr_logAddTraceSwarm(s, fmt::format("peerIo not created; marking peer {} as unreachable", atom.readable()));
        atom.flags2 |= MyflagUnreachable;
        ++atom.num_fails;
    }
    else
    {
        tr_handshake* handshake = tr_handshakeNew(io, mgr->session->encryptionMode, on_handshake_done, mgr);

        TR_ASSERT(tr_peerIoGetTorrentHash(io));

        tr_peerIoUnref(io); /* balanced by the initial ref in tr_peerIoNewOutgoing() */

        s->outgoing_handshakes.add(atom.addr, handshake);
    }

    atom.lastConnectionAttemptAt = now;
    atom.time = now;
}

} // namespace connect_helpers

void tr_peerMgr::makeNewPeerConnections(size_t max)
{
    using namespace connect_helpers;

    for (auto& candidate : getPeerCandidates(session, max))
    {
        initiateConnection(this, candidate.tor->swarm, *candidate.atom);
    }
}
