// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <bitset>
#include <cerrno>
#include <cstddef>
#include <cstdint> // uint8_t, uint32_t, int64_t
#include <ctime>
#include <deque>
#include <iterator>
#include <memory> // std::unique_ptr
#include <optional>
#include <queue>
#include <ratio>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include <fmt/core.h>

#include <small/vector.hpp>

#include "libtransmission/transmission.h"

#include "libtransmission/bitfield.h"
#include "libtransmission/block-info.h"
#include "libtransmission/cache.h"
#include "libtransmission/crypto-utils.h"
#include "libtransmission/interned-string.h"
#include "libtransmission/log.h"
#include "libtransmission/peer-common.h"
#include "libtransmission/peer-io.h"
#include "libtransmission/peer-mgr.h"
#include "libtransmission/peer-msgs.h"
#include "libtransmission/quark.h"
#include "libtransmission/session.h"
#include "libtransmission/timer.h"
#include "libtransmission/torrent-magnet.h"
#include "libtransmission/torrent.h"
#include "libtransmission/tr-assert.h"
#include "libtransmission/tr-buffer.h"
#include "libtransmission/tr-macros.h"
#include "libtransmission/utils.h"
#include "libtransmission/variant.h"
#include "libtransmission/version.h"

struct tr_error;

#ifndef EBADMSG
#define EBADMSG EINVAL
#endif

using namespace std::literals;

namespace
{
// initial capacity is big enough to hold a BtPeerMsgs::Piece message
using MessageBuffer = libtransmission::StackBuffer<tr_block_info::BlockSize + 16U, std::byte, std::ratio<5, 1>>;
using MessageReader = libtransmission::BufferReader<std::byte>;
using MessageWriter = libtransmission::BufferWriter<std::byte>;

// these values are hardcoded by various BEPs as noted
namespace BtPeerMsgs
{
// https://www.bittorrent.org/beps/bep_0003.html#peer-messages
auto constexpr Choke = uint8_t{ 0 };
auto constexpr Unchoke = uint8_t{ 1 };
auto constexpr Interested = uint8_t{ 2 };
auto constexpr NotInterested = uint8_t{ 3 };
auto constexpr Have = uint8_t{ 4 };
auto constexpr Bitfield = uint8_t{ 5 };
auto constexpr Request = uint8_t{ 6 };
auto constexpr Piece = uint8_t{ 7 };
auto constexpr Cancel = uint8_t{ 8 };

// https://www.bittorrent.org/beps/bep_0005.html#bittorrent-protocol-extension
auto constexpr Port = uint8_t{ 9 };

// https://www.bittorrent.org/beps/bep_0006.html
auto constexpr FextSuggest = uint8_t{ 13 };
auto constexpr FextHaveAll = uint8_t{ 14 };
auto constexpr FextHaveNone = uint8_t{ 15 };
auto constexpr FextReject = uint8_t{ 16 };
auto constexpr FextAllowedFast = uint8_t{ 17 };

// https://www.bittorrent.org/beps/bep_0010.html
// see also LtepMessageIds below
auto constexpr Ltep = uint8_t{ 20 };

[[nodiscard]] constexpr std::string_view debug_name(uint8_t type) noexcept
{
    switch (type)
    {
    case Bitfield:
        return "bitfield"sv;
    case Cancel:
        return "cancel"sv;
    case Choke:
        return "choke"sv;
    case FextAllowedFast:
        return "fext-allow-fast"sv;
    case FextHaveAll:
        return "fext-have-all"sv;
    case FextHaveNone:
        return "fext-have-none"sv;
    case FextReject:
        return "fext-reject"sv;
    case FextSuggest:
        return "fext-suggest"sv;
    case Have:
        return "have"sv;
    case Interested:
        return "interested"sv;
    case Ltep:
        return "ltep"sv;
    case NotInterested:
        return "not-interested"sv;
    case Piece:
        return "piece"sv;
    case Port:
        return "port"sv;
    case Request:
        return "request"sv;
    case Unchoke:
        return "unchoke"sv;
    default:
        return "unknown"sv;
    }
}

} // namespace BtPeerMsgs

namespace LtepMessages
{

// https://www.bittorrent.org/beps/bep_0010.html
auto constexpr Handshake = uint8_t{ 0 };

} // namespace LtepMessages

// https://www.bittorrent.org/beps/bep_0010.html
// Client-defined extension message IDs that we tell peers about
// in the LTEP handshake and will respond to when sent in an LTEP
// message.
enum LtepMessageIds : uint8_t
{
    // we support peer exchange (bep 11)
    // https://www.bittorrent.org/beps/bep_0011.html
    UT_PEX_ID = 1,

    // we support sending metadata files (bep 9)
    // https://www.bittorrent.org/beps/bep_0009.html
    // see also MetadataMsgType below
    UT_METADATA_ID = 3,
};

// https://www.bittorrent.org/beps/bep_0009.html
namespace MetadataMsgType
{

auto constexpr Request = int{ 0 };
auto constexpr Data = int{ 1 };
auto constexpr Reject = int{ 2 };

} // namespace MetadataMsgType

auto constexpr MinChokePeriodSec = time_t{ 10 };

// idle seconds before we send a keepalive
auto constexpr KeepaliveIntervalSecs = time_t{ 100 };

auto constexpr MetadataReqQ = size_t{ 64U };

auto constexpr ReqQ = int{ 512 };

// when we're making requests from another peer,
// batch them together to send enough requests to
// meet our bandwidth goals for the next N seconds
auto constexpr RequestBufSecs = time_t{ 10 };

// ---

auto constexpr MaxPexPeerCount = size_t{ 50U };

// ---

enum class EncryptionPreference : uint8_t
{
    Unknown,
    Yes,
    No
};

// ---

struct peer_request
{
    uint32_t index = 0;
    uint32_t offset = 0;
    uint32_t length = 0;

    [[nodiscard]] auto constexpr operator==(peer_request const& that) const noexcept
    {
        return this->index == that.index && this->offset == that.offset && this->length == that.length;
    }

    [[nodiscard]] static auto from_block(tr_torrent const& tor, tr_block_index_t block) noexcept
    {
        auto const loc = tor.block_loc(block);
        return peer_request{ loc.piece, loc.piece_offset, tor.block_size(block) };
    }
};

// ---

/* this is raw, unchanged data from the peer regarding
 * the current message that it's sending us. */
struct tr_incoming
{
    std::optional<uint32_t> length; // the full message payload length. Includes the +1 for id length
    std::optional<uint8_t> id; // the protocol message, e.g. BtPeerMsgs::Piece
    MessageBuffer payload;

    struct incoming_piece_data
    {
        explicit incoming_piece_data(uint32_t block_size)
            : buf{ std::make_unique<Cache::BlockData>(block_size) }
            , block_size_{ block_size }
        {
        }

        [[nodiscard]] bool add_span(size_t begin, size_t end)
        {
            if (begin > end || end > block_size_)
            {
                return false;
            }

            for (; begin < end; ++begin)
            {
                have_.set(begin);
            }

            return true;
        }

        [[nodiscard]] auto has_all() const noexcept
        {
            return have_.count() >= block_size_;
        }

        std::unique_ptr<Cache::BlockData> buf;

    private:
        std::bitset<tr_block_info::BlockSize> have_;
        uint32_t const block_size_;
    };

    std::unordered_map<tr_block_index_t, incoming_piece_data> blocks;
};

class tr_peerMsgsImpl;
// TODO: make these to be member functions
void gotError(tr_peerIo* io, tr_error const& err, void* vmsgs);
void peerPulse(void* vmsgs);
size_t protocolSendReject(tr_peerMsgsImpl* const msgs, struct peer_request const* req);
size_t protocolSendCancel(tr_peerMsgsImpl* msgs, struct peer_request const& req);
size_t protocolSendChoke(tr_peerMsgsImpl* msgs, bool choke);
size_t protocolSendHave(tr_peerMsgsImpl* msgs, tr_piece_index_t index);
size_t protocolSendPort(tr_peerMsgsImpl* msgs, tr_port port);
size_t protocolSendRequest(tr_peerMsgsImpl* msgs, struct peer_request const& req);
void sendInterest(tr_peerMsgsImpl* msgs, bool b);
void sendLtepHandshake(tr_peerMsgsImpl* msgs);
void tellPeerWhatWeHave(tr_peerMsgsImpl* msgs);
void updateDesiredRequestCount(tr_peerMsgsImpl* msgs);

#define myLogMacro(msgs, level, text) \
    do \
    { \
        if (tr_logLevelIsActive(level)) \
        { \
            tr_logAddMessage( \
                __FILE__, \
                __LINE__, \
                (level), \
                fmt::format("{:s} [{:s}]: {:s}", (msgs)->io->display_name(), (msgs)->user_agent().sv(), text), \
                (msgs)->torrent->name()); \
        } \
    } while (0)

#define logdbg(msgs, text) myLogMacro(msgs, TR_LOG_DEBUG, text)
#define logtrace(msgs, text) myLogMacro(msgs, TR_LOG_TRACE, text)
#define logwarn(msgs, text) myLogMacro(msgs, TR_LOG_WARN, text)

using ReadResult = std::pair<ReadState, size_t /*n_piece_data_bytes_read*/>;

/**
 * Low-level communication state information about a connected peer.
 *
 * This structure remembers the low-level protocol states that we're
 * in with this peer, such as active requests, pex messages, and so on.
 * Its fields are all private to peer-msgs.c.
 *
 * Data not directly involved with sending & receiving messages is
 * stored in tr_peer, where it can be accessed by both peermsgs and
 * the peer manager.
 *
 * @see tr_peer
 * @see tr_peer_info
 */
class tr_peerMsgsImpl final : public tr_peerMsgs
{
public:
    tr_peerMsgsImpl(
        tr_torrent* torrent_in,
        tr_peer_info* const peer_info_in,
        std::shared_ptr<tr_peerIo> io_in,
        tr_interned_string client,
        tr_peer_callback_bt callback,
        void* callback_data)
        : tr_peerMsgs{ torrent_in, peer_info_in, client, io_in->is_encrypted(), io_in->is_incoming(), io_in->is_utp() }
        , torrent{ torrent_in }
        , io{ std::move(io_in) }
        , have_{ torrent_in->piece_count() }
        , callback_{ callback }
        , callback_data_{ callback_data }
    {
        if (torrent->allows_pex())
        {
            pex_timer_ = session->timerMaker().create([this]() { send_pex(); });
            pex_timer_->start_repeating(SendPexInterval);
        }

        if (io->supports_ltep())
        {
            sendLtepHandshake(this);
        }

        tellPeerWhatWeHave(this);

        if (session->allowsDHT() && io->supports_dht())
        {
            protocolSendPort(this, session->udpPort());
        }

        io->set_callbacks(can_read, did_write, gotError, this);
        updateDesiredRequestCount(this);

        update_active();
    }

    tr_peerMsgsImpl(tr_peerMsgsImpl&&) = delete;
    tr_peerMsgsImpl(tr_peerMsgsImpl const&) = delete;
    tr_peerMsgsImpl& operator=(tr_peerMsgsImpl&&) = delete;
    tr_peerMsgsImpl& operator=(tr_peerMsgsImpl const&) = delete;

    ~tr_peerMsgsImpl() override
    {
        set_active(TR_UP, false);
        set_active(TR_DOWN, false);

        if (io)
        {
            io->clear();
        }
    }

    // ---

    [[nodiscard]] Speed get_piece_speed(uint64_t now, tr_direction dir) const override
    {
        return io->get_piece_speed(now, dir);
    }

    [[nodiscard]] size_t active_req_count(tr_direction dir) const noexcept override
    {
        switch (dir)
        {
        case TR_CLIENT_TO_PEER: // requests we sent
            return tr_peerMgrCountActiveRequestsToPeer(torrent, this);

        case TR_PEER_TO_CLIENT: // requests they sent
            return std::size(peer_requested_);

        default:
            TR_ASSERT(0);
            return {};
        }
    }

    [[nodiscard]] tr_socket_address socket_address() const override
    {
        return io->socket_address();
    }

    [[nodiscard]] std::string display_name() const override
    {
        return socket_address().display_name();
    }

    [[nodiscard]] tr_bitfield const& has() const noexcept override
    {
        return have_;
    }

    // ---

    void on_torrent_got_metainfo() noexcept override
    {
        invalidate_percent_done();

        update_active();
    }

    void invalidate_percent_done()
    {
        update_interest();
    }

    void cancel_block_request(tr_block_index_t block) override
    {
        protocolSendCancel(this, peer_request::from_block(*torrent, block));
    }

    void reject_all_requests()
    {
        auto& queue = peer_requested_;

        if (auto const must_send_rej = io->supports_fext(); must_send_rej)
        {
            for (auto const& req : queue)
            {
                protocolSendReject(this, &req);
            }
        }

        queue.clear();
    }

    void set_choke(bool peer_is_choked) override
    {
        time_t const now = tr_time();
        time_t const fibrillation_time = now - MinChokePeriodSec;

        if (chokeChangedAt > fibrillation_time)
        {
            // TODO logtrace(msgs, "Not changing choke to %d to avoid fibrillation", peer_is_choked);
        }
        else if (this->peer_is_choked() != peer_is_choked)
        {
            set_peer_choked(peer_is_choked);

            if (peer_is_choked)
            {
                reject_all_requests();
            }

            protocolSendChoke(this, peer_is_choked);
            chokeChangedAt = now;
            update_active(TR_CLIENT_TO_PEER);
        }
    }

    void pulse() override
    {
        peerPulse(this);
    }

    void on_piece_completed(tr_piece_index_t piece) override
    {
        protocolSendHave(this, piece);

        // since we have more pieces now, we might not be interested in this peer
        update_interest();
    }

    void set_interested(bool interested) override
    {
        if (client_is_interested() != interested)
        {
            set_client_interested(interested);
            sendInterest(this, interested);
            update_active(TR_PEER_TO_CLIENT);
        }
    }

    void update_interest()
    {
        // TODO -- might need to poke the mgr on startup
    }

    //

    [[nodiscard]] bool is_valid_request(peer_request const& req) const
    {
        return tr_torrentReqIsValid(torrent, req.index, req.offset, req.length);
    }

    void request_blocks(tr_block_span_t const* block_spans, size_t n_spans) override
    {
        TR_ASSERT(torrent->client_can_download());
        TR_ASSERT(client_is_interested());
        TR_ASSERT(!client_is_choked());

        for (auto const *span = block_spans, *span_end = span + n_spans; span != span_end; ++span)
        {
            for (auto [block, block_end] = *span; block < block_end; ++block)
            {
                // Note that requests can't cross over a piece boundary.
                // So if a piece isn't evenly divisible by the block size,
                // we need to split our block request info per-piece chunks.
                auto const byte_begin = torrent->block_loc(block).byte;
                auto const block_size = torrent->block_size(block);
                auto const byte_end = byte_begin + block_size;
                for (auto offset = byte_begin; offset < byte_end;)
                {
                    auto const loc = torrent->byte_loc(offset);
                    auto const left_in_block = block_size - loc.block_offset;
                    auto const left_in_piece = torrent->piece_size(loc.piece) - loc.piece_offset;
                    auto const req_len = std::min(left_in_block, left_in_piece);
                    protocolSendRequest(this, { loc.piece, loc.piece_offset, req_len });
                    offset += req_len;
                }
            }

            tr_peerMgrClientSentRequests(torrent, this, *span);
        }
    }

    // how many blocks could we request from this peer right now?
    [[nodiscard]] RequestLimit can_request() const noexcept override
    {
        auto const max_blocks = max_available_reqs();
        return RequestLimit{ max_blocks, max_blocks };
    }

    void send_pex();

    void publish(tr_peer_event const& peer_event)
    {
        if (callback_ != nullptr)
        {
            (*callback_)(this, peer_event, callback_data_);
        }
    }

private:
    [[nodiscard]] size_t max_available_reqs() const
    {
        if (torrent->is_done() || !torrent->has_metainfo() || client_is_choked() || !client_is_interested())
        {
            return 0;
        }

        // Get the rate limit we should use.
        // TODO: this needs to consider all the other peers as well...
        uint64_t const now = tr_time_msec();
        auto rate = get_piece_speed(now, TR_PEER_TO_CLIENT);
        if (torrent->uses_speed_limit(TR_PEER_TO_CLIENT))
        {
            rate = std::min(rate, torrent->speed_limit(TR_PEER_TO_CLIENT));
        }

        // honor the session limits, if enabled
        if (torrent->uses_session_limits())
        {
            if (auto const limit = torrent->session->active_speed_limit(TR_PEER_TO_CLIENT); limit)
            {
                rate = std::min(rate, *limit);
            }
        }

        // use this desired rate to figure out how
        // many requests we should send to this peer
        size_t constexpr Floor = 32;
        size_t constexpr Seconds = RequestBufSecs;
        size_t const estimated_blocks_in_period = (rate.base_quantity() * Seconds) / tr_block_info::BlockSize;
        size_t const ceil = reqq ? *reqq : 250;

        auto max_reqs = estimated_blocks_in_period;
        max_reqs = std::min(max_reqs, ceil);
        max_reqs = std::max(max_reqs, Floor);
        return max_reqs;
    }

    // ---

    void update_active()
    {
        update_active(TR_UP);
        update_active(TR_DOWN);
    }

    void update_active(tr_direction direction)
    {
        TR_ASSERT(tr_isDirection(direction));
        set_active(direction, calculate_active(direction));
    }

    [[nodiscard]] bool calculate_active(tr_direction direction) const
    {
        if (direction == TR_CLIENT_TO_PEER)
        {
            return peer_is_interested() && !peer_is_choked();
        }

        // TR_PEER_TO_CLIENT

        if (!torrent->has_metainfo())
        {
            return true;
        }

        auto const active = client_is_interested() && !client_is_choked();
        TR_ASSERT(!active || !torrent->is_done());
        return active;
    }

    // ---

    static void did_write(tr_peerIo* /*io*/, size_t bytes_written, bool was_piece_data, void* vmsgs);
    static ReadState can_read(tr_peerIo* io, void* vmsgs, size_t* piece);

public:
    bool peerSupportsPex = false;
    bool peerSupportsMetadataXfer = false;
    bool clientSentLtepHandshake = false;

    size_t desired_request_count = 0;

    uint8_t ut_pex_id = 0;
    uint8_t ut_metadata_id = 0;

    tr_port dht_port;

    EncryptionPreference encryption_preference = EncryptionPreference::Unknown;

    tr_torrent* const torrent;

    std::shared_ptr<tr_peerIo> const io;

    std::deque<peer_request> peer_requested_;

    std::array<std::vector<tr_pex>, NUM_TR_AF_INET_TYPES> pex;

    std::queue<int> peerAskedForMetadata;

    time_t clientSentAnythingAt = 0;

    time_t chokeChangedAt = 0;

    /* when we started batching the outMessages */
    // time_t outMessagesBatchedAt = 0;

    tr_incoming incoming = {};

    // if the peer supports the Extension Protocol in BEP 10 and
    // supplied a reqq argument, it's stored here.
    std::optional<size_t> reqq;

    std::unique_ptr<libtransmission::Timer> pex_timer_;

    tr_bitfield have_;

private:
    friend ReadResult process_peer_message(tr_peerMsgsImpl* msgs, uint8_t id, MessageReader& payload);
    friend void parseLtepHandshake(tr_peerMsgsImpl* msgs, MessageReader& payload);
    friend void parseUtMetadata(tr_peerMsgsImpl* msgs, MessageReader& payload_in);

    tr_peer_callback_bt const callback_;
    void* const callback_data_;

    // seconds between periodic send_pex() calls
    static auto constexpr SendPexInterval = 90s;
};

// ---

[[nodiscard]] constexpr bool messageLengthIsCorrect(tr_torrent const* const tor, uint8_t id, uint32_t len)
{
    switch (id)
    {
    case BtPeerMsgs::Choke:
    case BtPeerMsgs::Unchoke:
    case BtPeerMsgs::Interested:
    case BtPeerMsgs::NotInterested:
    case BtPeerMsgs::FextHaveAll:
    case BtPeerMsgs::FextHaveNone:
        return len == 1U;

    case BtPeerMsgs::Have:
    case BtPeerMsgs::FextSuggest:
    case BtPeerMsgs::FextAllowedFast:
        return len == 5U;

    case BtPeerMsgs::Bitfield:
        return !tor->has_metainfo() || len == 1 + ((tor->piece_count() + 7U) / 8U);

    case BtPeerMsgs::Request:
    case BtPeerMsgs::Cancel:
    case BtPeerMsgs::FextReject:
        return len == 13U;

    case BtPeerMsgs::Piece:
        len -= sizeof(id) + sizeof(uint32_t /*piece*/) + sizeof(uint32_t /*offset*/);
        return len <= tr_block_info::BlockSize;

    case BtPeerMsgs::Port:
        return len == 3U;

    case BtPeerMsgs::Ltep:
        return len >= 2U;

    default: // unrecognized message
        return false;
    }
}

namespace protocol_send_message_helpers
{
namespace
{

[[nodiscard]] constexpr auto get_param_length(uint8_t param) noexcept
{
    return sizeof(param);
}

[[nodiscard]] constexpr auto get_param_length(uint16_t param) noexcept
{
    return sizeof(param);
}

[[nodiscard]] constexpr auto get_param_length(uint32_t param) noexcept
{
    return sizeof(param);
}

template<typename T>
[[nodiscard]] TR_CONSTEXPR20 auto get_param_length(T const& param) noexcept
{
    return std::size(param);
}

// ---

void add_param(MessageWriter& buffer, uint8_t param) noexcept
{
    buffer.add_uint8(param);
}

void add_param(MessageWriter& buffer, uint16_t param) noexcept
{
    buffer.add_uint16(param);
}

void add_param(MessageWriter& buffer, uint32_t param) noexcept
{
    buffer.add_uint32(param);
}

template<typename T>
void add_param(MessageWriter& buffer, T const& param) noexcept
{
    buffer.add(param);
}

// ---

[[nodiscard]] std::string log_param(uint8_t param)
{
    return fmt::format(" {:d}", static_cast<int>(param));
}

[[nodiscard]] std::string log_param(uint16_t param)
{
    return fmt::format(" {:d}", static_cast<int>(param));
}

[[nodiscard]] std::string log_param(uint32_t param)
{
    return fmt::format(" {:d}", static_cast<int>(param));
}

template<typename T>
[[nodiscard]] std::string log_param(T const& /*unused*/)
{
    return " []";
}

template<typename... Args>
[[nodiscard]] std::string build_log_message(uint8_t type, Args const&... args)
{
    auto text = fmt::format("sending '{:s}'", BtPeerMsgs::debug_name(type));
    (text.append(log_param(args)), ...);
    return text;
}
} // namespace

template<typename... Args>
void build_peer_message(tr_peerMsgsImpl const* const msgs, MessageWriter& out, uint8_t type, Args const&... args)
{
    logtrace(msgs, build_log_message(type, args...));

    auto msg_len = sizeof(type);
    ((msg_len += get_param_length(args)), ...);
    out.add_uint32(msg_len);
    out.add_uint8(type);
    (add_param(out, args), ...);

    TR_ASSERT(messageLengthIsCorrect(msgs->torrent, type, msg_len));
}
} // namespace protocol_send_message_helpers

template<typename... Args>
size_t protocol_send_message(tr_peerMsgsImpl const* const msgs, uint8_t type, Args const&... args)
{
    using namespace protocol_send_message_helpers;

    auto out = MessageBuffer{};
    build_peer_message(msgs, out, type, args...);
    auto const n_bytes_added = std::size(out);
    msgs->io->write(out, type == BtPeerMsgs::Piece);
    return n_bytes_added;
}

size_t protocol_send_keepalive(tr_peerMsgsImpl* msgs)
{
    logtrace(msgs, "sending 'keepalive'");

    auto out = MessageBuffer{};
    out.add_uint32(0);

    auto const n_bytes_added = std::size(out);
    msgs->io->write(out, false);
    return n_bytes_added;
}

size_t protocolSendReject(tr_peerMsgsImpl* const msgs, struct peer_request const* req)
{
    TR_ASSERT(msgs->io->supports_fext());
    return protocol_send_message(msgs, BtPeerMsgs::FextReject, req->index, req->offset, req->length);
}

size_t protocolSendCancel(tr_peerMsgsImpl* const msgs, peer_request const& req)
{
    return protocol_send_message(msgs, BtPeerMsgs::Cancel, req.index, req.offset, req.length);
}

size_t protocolSendRequest(tr_peerMsgsImpl* const msgs, struct peer_request const& req)
{
    TR_ASSERT(msgs->is_valid_request(req));
    return protocol_send_message(msgs, BtPeerMsgs::Request, req.index, req.offset, req.length);
}

size_t protocolSendPort(tr_peerMsgsImpl* const msgs, tr_port port)
{
    return protocol_send_message(msgs, BtPeerMsgs::Port, port.host());
}

size_t protocolSendHave(tr_peerMsgsImpl* const msgs, tr_piece_index_t index)
{
    return protocol_send_message(msgs, BtPeerMsgs::Have, index);
}

size_t protocolSendChoke(tr_peerMsgsImpl* const msgs, bool choke)
{
    return protocol_send_message(msgs, choke ? BtPeerMsgs::Choke : BtPeerMsgs::Unchoke);
}

// --- INTEREST

void sendInterest(tr_peerMsgsImpl* msgs, bool b)
{
    protocol_send_message(msgs, b ? BtPeerMsgs::Interested : BtPeerMsgs::NotInterested);
}

std::optional<int> popNextMetadataRequest(tr_peerMsgsImpl* msgs)
{
    auto& reqs = msgs->peerAskedForMetadata;

    if (std::empty(reqs))
    {
        return {};
    }

    auto next = reqs.front();
    reqs.pop();
    return next;
}

// ---

void sendLtepHandshake(tr_peerMsgsImpl* msgs)
{
    if (msgs->clientSentLtepHandshake)
    {
        return;
    }

    logtrace(msgs, "sending an ltep handshake");
    msgs->clientSentLtepHandshake = true;

    /* decide if we want to advertise metadata xfer support (BEP 9) */
    bool const allow_metadata_xfer = msgs->torrent->is_public();

    /* decide if we want to advertise pex support */
    bool const allow_pex = msgs->torrent->allows_pex();

    auto val = tr_variant{};
    tr_variantInitDict(&val, 8);
    tr_variantDictAddBool(&val, TR_KEY_e, msgs->session->encryptionMode() != TR_CLEAR_PREFERRED);

    // If connecting to global peer, then use global address
    // Otherwise we are connecting to local peer, use bind address directly
    if (auto const addr = msgs->io->address().is_global_unicast_address() ? msgs->session->global_address(TR_AF_INET) :
                                                                            msgs->session->bind_address(TR_AF_INET);
        addr && !addr->is_any())
    {
        TR_ASSERT(addr->is_ipv4());
        tr_variantDictAddRaw(&val, TR_KEY_ipv4, &addr->addr.addr4, sizeof(addr->addr.addr4));
    }
    if (auto const addr = msgs->io->address().is_global_unicast_address() ? msgs->session->global_address(TR_AF_INET6) :
                                                                            msgs->session->bind_address(TR_AF_INET6);
        addr && !addr->is_any())
    {
        TR_ASSERT(addr->is_ipv6());
        tr_variantDictAddRaw(&val, TR_KEY_ipv6, &addr->addr.addr6, sizeof(addr->addr.addr6));
    }

    // https://www.bittorrent.org/beps/bep_0009.html
    // It also adds "metadata_size" to the handshake message (not the
    // "m" dictionary) specifying an integer value of the number of
    // bytes of the metadata.
    if (auto const info_dict_size = msgs->torrent->info_dict_size();
        allow_metadata_xfer && msgs->torrent->has_metainfo() && info_dict_size > 0)
    {
        tr_variantDictAddInt(&val, TR_KEY_metadata_size, info_dict_size);
    }

    // https://www.bittorrent.org/beps/bep_0010.html
    // Local TCP listen port. Allows each side to learn about the TCP
    // port number of the other side. Note that there is no need for the
    // receiving side of the connection to send this extension message,
    // since its port number is already known.
    tr_variantDictAddInt(&val, TR_KEY_p, msgs->session->advertisedPeerPort().host());

    // https://www.bittorrent.org/beps/bep_0010.html
    // An integer, the number of outstanding request messages this
    // client supports without dropping any. The default in in
    // libtorrent is 250.
    tr_variantDictAddInt(&val, TR_KEY_reqq, ReqQ);

    // https://www.bittorrent.org/beps/bep_0010.html
    // A string containing the compact representation of the ip address this peer sees
    // you as. i.e. this is the receiver's external ip address (no port is included).
    // This may be either an IPv4 (4 bytes) or an IPv6 (16 bytes) address.
    {
        auto buf = std::array<std::byte, TR_ADDRSTRLEN>{};
        auto const begin = std::data(buf);
        auto const end = msgs->io->address().to_compact(begin);
        auto const len = end - begin;
        TR_ASSERT(len == tr_address::CompactAddrBytes[0] || len == tr_address::CompactAddrBytes[1]);
        tr_variantDictAddRaw(&val, TR_KEY_yourip, begin, len);
    }

    // https://www.bittorrent.org/beps/bep_0010.html
    // Client name and version (as a utf-8 string). This is a much more
    // reliable way of identifying the client than relying on the
    // peer id encoding.
    tr_variantDictAddStrView(&val, TR_KEY_v, TR_NAME " " USERAGENT_PREFIX);

    // https://www.bittorrent.org/beps/bep_0021.html
    // A peer that is a partial seed SHOULD include an extra header in
    // the extension handshake 'upload_only'. Setting the value of this
    // key to 1 indicates that this peer is not interested in downloading
    // anything.
    tr_variantDictAddBool(&val, TR_KEY_upload_only, msgs->torrent->is_done());

    if (allow_metadata_xfer || allow_pex)
    {
        tr_variant* m = tr_variantDictAddDict(&val, TR_KEY_m, 2);

        if (allow_metadata_xfer)
        {
            tr_variantDictAddInt(m, TR_KEY_ut_metadata, UT_METADATA_ID);
        }

        if (allow_pex)
        {
            tr_variantDictAddInt(m, TR_KEY_ut_pex, UT_PEX_ID);
        }
    }

    protocol_send_message(msgs, BtPeerMsgs::Ltep, LtepMessages::Handshake, tr_variant_serde::benc().to_string(val));
}

void parseLtepHandshake(tr_peerMsgsImpl* msgs, MessageReader& payload)
{
    auto const handshake_sv = payload.to_string_view();

    auto var = tr_variant_serde::benc().inplace().parse(handshake_sv);
    if (!var || !var->holds_alternative<tr_variant::Map>())
    {
        logtrace(msgs, "GET  extended-handshake, couldn't get dictionary");
        return;
    }

    logtrace(msgs, fmt::format("here is the base64-encoded handshake: [{:s}]", tr_base64_encode(handshake_sv)));

    /* does the peer prefer encrypted connections? */
    auto pex = tr_pex{};
    auto& [addr, port] = pex.socket_address;
    if (auto e = int64_t{}; tr_variantDictFindInt(&*var, TR_KEY_e, &e))
    {
        msgs->encryption_preference = e != 0 ? EncryptionPreference::Yes : EncryptionPreference::No;

        if (msgs->encryption_preference == EncryptionPreference::Yes)
        {
            pex.flags |= ADDED_F_ENCRYPTION_FLAG;
        }
    }

    /* check supported messages for utorrent pex */
    msgs->peerSupportsPex = false;
    msgs->peerSupportsMetadataXfer = false;

    if (tr_variant* sub = nullptr; tr_variantDictFindDict(&*var, TR_KEY_m, &sub))
    {
        if (auto ut_pex = int64_t{}; tr_variantDictFindInt(sub, TR_KEY_ut_pex, &ut_pex))
        {
            msgs->peerSupportsPex = ut_pex != 0;
            msgs->ut_pex_id = static_cast<uint8_t>(ut_pex);
            logtrace(msgs, fmt::format("msgs->ut_pex is {:d}", static_cast<int>(msgs->ut_pex_id)));
        }

        if (auto ut_metadata = int64_t{}; tr_variantDictFindInt(sub, TR_KEY_ut_metadata, &ut_metadata))
        {
            msgs->peerSupportsMetadataXfer = ut_metadata != 0;
            msgs->ut_metadata_id = static_cast<uint8_t>(ut_metadata);
            logtrace(msgs, fmt::format("msgs->ut_metadata_id is {:d}", static_cast<int>(msgs->ut_metadata_id)));
        }

        if (auto ut_holepunch = int64_t{}; tr_variantDictFindInt(sub, TR_KEY_ut_holepunch, &ut_holepunch))
        {
            // Transmission doesn't support this extension yet.
            // But its presence does indicate µTP supports,
            // which we do care about...
            msgs->peer_info->set_utp_supported(true);
        }
    }

    /* look for metainfo size (BEP 9) */
    if (auto metadata_size = int64_t{}; tr_variantDictFindInt(&*var, TR_KEY_metadata_size, &metadata_size))
    {
        if (!tr_metadata_download::is_valid_metadata_size(metadata_size))
        {
            msgs->peerSupportsMetadataXfer = false;
        }
        else
        {
            msgs->torrent->maybe_start_metadata_transfer(metadata_size);
        }
    }

    /* look for upload_only (BEP 21) */
    if (auto upload_only = int64_t{}; tr_variantDictFindInt(&*var, TR_KEY_upload_only, &upload_only))
    {
        pex.flags |= ADDED_F_SEED_FLAG;
    }

    // https://www.bittorrent.org/beps/bep_0010.html
    // Client name and version (as a utf-8 string). This is a much more
    // reliable way of identifying the client than relying on the
    // peer id encoding.
    if (auto sv = std::string_view{}; tr_variantDictFindStrView(&*var, TR_KEY_v, &sv))
    {
        msgs->set_user_agent(tr_interned_string{ sv });
    }

    /* get peer's listening port */
    if (auto p = int64_t{}; tr_variantDictFindInt(&*var, TR_KEY_p, &p) && p > 0)
    {
        port.set_host(p);
        msgs->publish(tr_peer_event::GotPort(port));
        logtrace(msgs, fmt::format("peer's port is now {:d}", p));
    }

    std::byte const* addr_compact = nullptr;
    auto addr_len = size_t{};
    if (msgs->io->is_incoming() && tr_variantDictFindRaw(&*var, TR_KEY_ipv4, &addr_compact, &addr_len) &&
        addr_len == tr_address::CompactAddrBytes[TR_AF_INET])
    {
        std::tie(addr, std::ignore) = tr_address::from_compact_ipv4(addr_compact);
        tr_peerMgrAddPex(msgs->torrent, TR_PEER_FROM_LTEP, &pex, 1);
    }

    if (msgs->io->is_incoming() && tr_variantDictFindRaw(&*var, TR_KEY_ipv6, &addr_compact, &addr_len) &&
        addr_len == tr_address::CompactAddrBytes[TR_AF_INET6])
    {
        std::tie(addr, std::ignore) = tr_address::from_compact_ipv6(addr_compact);
        tr_peerMgrAddPex(msgs->torrent, TR_PEER_FROM_LTEP, &pex, 1);
    }

    /* get peer's maximum request queue size */
    if (auto reqq = int64_t{}; tr_variantDictFindInt(&*var, TR_KEY_reqq, &reqq))
    {
        msgs->reqq = reqq;
    }
}

void parseUtMetadata(tr_peerMsgsImpl* msgs, MessageReader& payload_in)
{
    int64_t msg_type = -1;
    int64_t piece = -1;
    int64_t total_size = 0;

    auto const tmp = payload_in.to_string_view();
    auto const* const msg_end = std::data(tmp) + std::size(tmp);

    auto serde = tr_variant_serde::benc();
    if (auto var = serde.inplace().parse(tmp); var)
    {
        (void)tr_variantDictFindInt(&*var, TR_KEY_msg_type, &msg_type);
        (void)tr_variantDictFindInt(&*var, TR_KEY_piece, &piece);
        (void)tr_variantDictFindInt(&*var, TR_KEY_total_size, &total_size);
    }

    logtrace(msgs, fmt::format("got ut_metadata msg: type {:d}, piece {:d}, total_size {:d}", msg_type, piece, total_size));

    if (msg_type == MetadataMsgType::Reject)
    {
        /* NOOP */
    }

    if (auto const piece_len = msg_end - serde.end();
        msg_type == MetadataMsgType::Data && piece * MetadataPieceSize + piece_len <= total_size)
    {
        msgs->torrent->set_metadata_piece(piece, serde.end(), piece_len);
    }

    if (msg_type == MetadataMsgType::Request)
    {
        if (piece >= 0 && msgs->torrent->has_metainfo() && msgs->torrent->is_public() &&
            std::size(msgs->peerAskedForMetadata) < MetadataReqQ)
        {
            msgs->peerAskedForMetadata.push(piece);
        }
        else
        {
            /* send a rejection message */
            auto v = tr_variant{};
            tr_variantInitDict(&v, 2);
            tr_variantDictAddInt(&v, TR_KEY_msg_type, MetadataMsgType::Reject);
            tr_variantDictAddInt(&v, TR_KEY_piece, piece);
            protocol_send_message(msgs, BtPeerMsgs::Ltep, msgs->ut_metadata_id, serde.to_string(v));
        }
    }
}

void parseUtPex(tr_peerMsgsImpl* msgs, MessageReader& payload)
{
    auto* const tor = msgs->torrent;
    if (!tor->allows_pex())
    {
        return;
    }

    if (auto var = tr_variant_serde::benc().inplace().parse(payload.to_string_view()); var)
    {
        uint8_t const* added = nullptr;
        auto added_len = size_t{};
        if (tr_variantDictFindRaw(&*var, TR_KEY_added, &added, &added_len))
        {
            uint8_t const* added_f = nullptr;
            auto added_f_len = size_t{};
            if (!tr_variantDictFindRaw(&*var, TR_KEY_added_f, &added_f, &added_f_len))
            {
                added_f_len = 0;
                added_f = nullptr;
            }

            auto pex = tr_pex::from_compact_ipv4(added, added_len, added_f, added_f_len);
            pex.resize(std::min(MaxPexPeerCount, std::size(pex)));
            tr_peerMgrAddPex(tor, TR_PEER_FROM_PEX, std::data(pex), std::size(pex));
        }

        if (tr_variantDictFindRaw(&*var, TR_KEY_added6, &added, &added_len))
        {
            uint8_t const* added_f = nullptr;
            auto added_f_len = size_t{};
            if (!tr_variantDictFindRaw(&*var, TR_KEY_added6_f, &added_f, &added_f_len))
            {
                added_f_len = 0;
                added_f = nullptr;
            }

            auto pex = tr_pex::from_compact_ipv6(added, added_len, added_f, added_f_len);
            pex.resize(std::min(MaxPexPeerCount, std::size(pex)));
            tr_peerMgrAddPex(tor, TR_PEER_FROM_PEX, std::data(pex), std::size(pex));
        }
    }
}

void parseLtep(tr_peerMsgsImpl* msgs, MessageReader& payload)
{
    TR_ASSERT(!std::empty(payload));

    auto const ltep_msgid = payload.to_uint8();

    if (ltep_msgid == LtepMessages::Handshake)
    {
        logtrace(msgs, "got ltep handshake");
        parseLtepHandshake(msgs, payload);

        if (msgs->io->supports_ltep())
        {
            sendLtepHandshake(msgs);
            msgs->send_pex();
        }
    }
    else if (ltep_msgid == UT_PEX_ID)
    {
        logtrace(msgs, "got ut pex");
        msgs->peerSupportsPex = true;
        parseUtPex(msgs, payload);
    }
    else if (ltep_msgid == UT_METADATA_ID)
    {
        logtrace(msgs, "got ut metadata");
        msgs->peerSupportsMetadataXfer = true;
        parseUtMetadata(msgs, payload);
    }
    else
    {
        logtrace(msgs, fmt::format("skipping unknown ltep message ({:d})", static_cast<int>(ltep_msgid)));
    }
}

ReadResult process_peer_message(tr_peerMsgsImpl* msgs, uint8_t id, MessageReader& payload);

[[nodiscard]] bool canAddRequestFromPeer(tr_peerMsgsImpl const* const msgs, struct peer_request const& req)
{
    if (msgs->peer_is_choked())
    {
        logtrace(msgs, "rejecting request from choked peer");
        return false;
    }

    if (std::size(msgs->peer_requested_) >= ReqQ)
    {
        logtrace(msgs, "rejecting request ... reqq is full");
        return false;
    }

    if (!tr_torrentReqIsValid(msgs->torrent, req.index, req.offset, req.length))
    {
        logtrace(msgs, "rejecting an invalid request.");
        return false;
    }

    if (!msgs->torrent->has_piece(req.index))
    {
        logtrace(msgs, "rejecting request for a piece we don't have.");
        return false;
    }

    return true;
}

void peerMadeRequest(tr_peerMsgsImpl* msgs, struct peer_request const* req)
{
    if (canAddRequestFromPeer(msgs, *req))
    {
        msgs->peer_requested_.emplace_back(*req);
    }
    else if (msgs->io->supports_fext())
    {
        protocolSendReject(msgs, req);
    }
}

int clientGotBlock(tr_peerMsgsImpl* msgs, std::unique_ptr<Cache::BlockData> block_data, tr_block_index_t block);

ReadResult read_piece_data(tr_peerMsgsImpl* msgs, MessageReader& payload)
{
    // <index><begin><block>
    auto const piece = payload.to_uint32();
    auto const offset = payload.to_uint32();
    auto const len = std::size(payload);

    auto const loc = msgs->torrent->piece_loc(piece, offset);
    auto const block = loc.block;
    auto const block_size = msgs->torrent->block_size(block);

    logtrace(msgs, fmt::format("got {:d} bytes for req {:d}:{:d}->{:d}", len, piece, offset, len));

    if (loc.block_offset + len > block_size)
    {
        logwarn(msgs, fmt::format("got unaligned piece {:d}:{:d}->{:d}", piece, offset, len));
        return { READ_ERR, len };
    }

    if (!tr_peerMgrDidPeerRequest(msgs->torrent, msgs, block))
    {
        logwarn(msgs, fmt::format("got unrequested piece {:d}:{:d}->{:d}", piece, offset, len));
        return { READ_ERR, len };
    }

    msgs->publish(tr_peer_event::GotPieceData(len));

    if (loc.block_offset == 0U && len == block_size) // simple case: one message has entire block
    {
        auto buf = std::make_unique<Cache::BlockData>(block_size);
        payload.to_buf(std::data(*buf), len);
        auto const ok = clientGotBlock(msgs, std::move(buf), block) == 0;
        return { ok ? READ_NOW : READ_ERR, len };
    }

    auto& blocks = msgs->incoming.blocks;
    auto& incoming_block = blocks.try_emplace(block, block_size).first->second;
    payload.to_buf(std::data(*incoming_block.buf) + loc.block_offset, len);

    if (!incoming_block.add_span(loc.block_offset, loc.block_offset + len))
    {
        return { READ_ERR, len }; // invalid span
    }

    if (!incoming_block.has_all())
    {
        return { READ_LATER, len }; // we don't have the full block yet
    }

    auto block_buf = std::move(incoming_block.buf);
    blocks.erase(block); // note: invalidates `incoming_block` local
    auto const ok = clientGotBlock(msgs, std::move(block_buf), block) == 0;
    return { ok ? READ_NOW : READ_ERR, len };
}

ReadResult process_peer_message(tr_peerMsgsImpl* msgs, uint8_t id, MessageReader& payload)
{
    bool const fext = msgs->io->supports_fext();

    auto ui32 = uint32_t{};

    logtrace(
        msgs,
        fmt::format(
            "got peer msg '{:s}' ({:d}) with payload len {:d}",
            BtPeerMsgs::debug_name(id),
            static_cast<int>(id),
            std::size(payload)));

    if (!messageLengthIsCorrect(msgs->torrent, id, sizeof(id) + std::size(payload)))
    {
        logdbg(
            msgs,
            fmt::format(
                "bad msg: '{:s}' ({:d}) with payload len {:d}",
                BtPeerMsgs::debug_name(id),
                static_cast<int>(id),
                std::size(payload)));
        msgs->publish(tr_peer_event::GotError(EMSGSIZE));
        return { READ_ERR, {} };
    }

    switch (id)
    {
    case BtPeerMsgs::Choke:
        logtrace(msgs, "got Choke");
        msgs->set_client_choked(true);

        if (!fext)
        {
            msgs->publish(tr_peer_event::GotChoke());
        }

        msgs->update_active(TR_PEER_TO_CLIENT);
        break;

    case BtPeerMsgs::Unchoke:
        logtrace(msgs, "got Unchoke");
        msgs->set_client_choked(false);
        msgs->update_active(TR_PEER_TO_CLIENT);
        updateDesiredRequestCount(msgs);
        break;

    case BtPeerMsgs::Interested:
        logtrace(msgs, "got Interested");
        msgs->set_peer_interested(true);
        msgs->update_active(TR_CLIENT_TO_PEER);
        break;

    case BtPeerMsgs::NotInterested:
        logtrace(msgs, "got Not Interested");
        msgs->set_peer_interested(false);
        msgs->update_active(TR_CLIENT_TO_PEER);
        break;

    case BtPeerMsgs::Have:
        ui32 = payload.to_uint32();
        logtrace(msgs, fmt::format("got Have: {:d}", ui32));

        if (msgs->torrent->has_metainfo() && ui32 >= msgs->torrent->piece_count())
        {
            msgs->publish(tr_peer_event::GotError(ERANGE));
            return { READ_ERR, {} };
        }

        /* a peer can send the same HAVE message twice... */
        if (!msgs->have_.test(ui32))
        {
            msgs->have_.set(ui32);
            msgs->publish(tr_peer_event::GotHave(ui32));
        }

        msgs->invalidate_percent_done();
        break;

    case BtPeerMsgs::Bitfield:
        logtrace(msgs, "got a bitfield");
        msgs->have_ = tr_bitfield{ msgs->torrent->has_metainfo() ? msgs->torrent->piece_count() : std::size(payload) * 8 };
        msgs->have_.set_raw(reinterpret_cast<uint8_t const*>(std::data(payload)), std::size(payload));
        msgs->publish(tr_peer_event::GotBitfield(&msgs->have_));
        msgs->invalidate_percent_done();
        break;

    case BtPeerMsgs::Request:
        {
            struct peer_request r;
            r.index = payload.to_uint32();
            r.offset = payload.to_uint32();
            r.length = payload.to_uint32();
            logtrace(msgs, fmt::format("got Request: {:d}:{:d}->{:d}", r.index, r.offset, r.length));
            peerMadeRequest(msgs, &r);
            break;
        }

    case BtPeerMsgs::Cancel:
        {
            struct peer_request r;
            r.index = payload.to_uint32();
            r.offset = payload.to_uint32();
            r.length = payload.to_uint32();
            msgs->cancels_sent_to_client.add(tr_time(), 1);
            logtrace(msgs, fmt::format("got a Cancel {:d}:{:d}->{:d}", r.index, r.offset, r.length));

            auto& requests = msgs->peer_requested_;
            if (auto iter = std::find(std::begin(requests), std::end(requests), r); iter != std::end(requests))
            {
                requests.erase(iter);

                // bep6: "Even when a request is cancelled, the peer
                // receiving the cancel should respond with either the
                // corresponding reject or the corresponding piece"
                if (fext)
                {
                    protocolSendReject(msgs, &r);
                }
            }
            break;
        }

    case BtPeerMsgs::Piece:
        return read_piece_data(msgs, payload);
        break;

    case BtPeerMsgs::Port:
        // https://www.bittorrent.org/beps/bep_0005.html
        // Peers supporting the DHT set the last bit of the 8-byte reserved flags
        // exchanged in the BitTorrent protocol handshake. Peer receiving a handshake
        // indicating the remote peer supports the DHT should send a PORT message.
        // It begins with byte 0x09 and has a two byte payload containing the UDP
        // port of the DHT node in network byte order.
        {
            logtrace(msgs, "Got a BtPeerMsgs::Port");

            auto const hport = payload.to_uint16();
            if (auto const dht_port = tr_port::from_host(hport); !std::empty(dht_port))
            {
                msgs->dht_port = dht_port;
                msgs->session->maybe_add_dht_node(msgs->io->address(), msgs->dht_port);
            }
        }
        break;

    case BtPeerMsgs::FextSuggest:
        logtrace(msgs, "Got a BtPeerMsgs::FextSuggest");

        if (fext)
        {
            auto const piece = payload.to_uint32();
            msgs->publish(tr_peer_event::GotSuggest(piece));
        }
        else
        {
            msgs->publish(tr_peer_event::GotError(EMSGSIZE));
            return { READ_ERR, {} };
        }

        break;

    case BtPeerMsgs::FextAllowedFast:
        logtrace(msgs, "Got a BtPeerMsgs::FextAllowedFast");

        if (fext)
        {
            auto const piece = payload.to_uint32();
            msgs->publish(tr_peer_event::GotAllowedFast(piece));
        }
        else
        {
            msgs->publish(tr_peer_event::GotError(EMSGSIZE));
            return { READ_ERR, {} };
        }

        break;

    case BtPeerMsgs::FextHaveAll:
        logtrace(msgs, "Got a BtPeerMsgs::FextHaveAll");

        if (fext)
        {
            msgs->have_.set_has_all();
            msgs->publish(tr_peer_event::GotHaveAll());
            msgs->invalidate_percent_done();
        }
        else
        {
            msgs->publish(tr_peer_event::GotError(EMSGSIZE));
            return { READ_ERR, {} };
        }

        break;

    case BtPeerMsgs::FextHaveNone:
        logtrace(msgs, "Got a BtPeerMsgs::FextHaveNone");

        if (fext)
        {
            msgs->have_.set_has_none();
            msgs->publish(tr_peer_event::GotHaveNone());
            msgs->invalidate_percent_done();
        }
        else
        {
            msgs->publish(tr_peer_event::GotError(EMSGSIZE));
            return { READ_ERR, {} };
        }

        break;

    case BtPeerMsgs::FextReject:
        {
            struct peer_request r;
            r.index = payload.to_uint32();
            r.offset = payload.to_uint32();
            r.length = payload.to_uint32();

            if (fext)
            {
                msgs->publish(
                    tr_peer_event::GotRejected(msgs->torrent->block_info(), msgs->torrent->piece_loc(r.index, r.offset).block));
            }
            else
            {
                msgs->publish(tr_peer_event::GotError(EMSGSIZE));
                return { READ_ERR, {} };
            }

            break;
        }

    case BtPeerMsgs::Ltep:
        logtrace(msgs, "Got a BtPeerMsgs::Ltep");
        parseLtep(msgs, payload);
        break;

    default:
        logtrace(msgs, fmt::format("peer sent us an UNKNOWN: {:d}", static_cast<int>(id)));
        break;
    }

    return { READ_NOW, {} };
}

/* returns 0 on success, or an errno on failure */
int clientGotBlock(tr_peerMsgsImpl* msgs, std::unique_ptr<Cache::BlockData> block_data, tr_block_index_t const block)
{
    TR_ASSERT(msgs != nullptr);

    tr_torrent const* const tor = msgs->torrent;
    auto const n_expected = msgs->torrent->block_size(block);

    if (!block_data)
    {
        logdbg(msgs, fmt::format("wrong block size: expected {:d}, got {:d}", n_expected, 0));
        return EMSGSIZE;
    }

    if (std::size(*block_data) != msgs->torrent->block_size(block))
    {
        logdbg(msgs, fmt::format("wrong block size: expected {:d}, got {:d}", n_expected, std::size(*block_data)));
        return EMSGSIZE;
    }

    logtrace(msgs, fmt::format("got block {:d}", block));

    if (!tr_peerMgrDidPeerRequest(msgs->torrent, msgs, block))
    {
        logdbg(msgs, "we didn't ask for this message...");
        return 0;
    }

    auto const loc = msgs->torrent->block_loc(block);
    if (msgs->torrent->has_piece(loc.piece))
    {
        logtrace(msgs, "we did ask for this message, but the piece is already complete...");
        return 0;
    }

    // NB: if writeBlock() fails the torrent may be paused.
    // If this happens, `msgs` will be a dangling pointer and must no longer be used.
    if (auto const err = msgs->session->cache->write_block(tor->id(), block, std::move(block_data)); err != 0)
    {
        return err;
    }

    msgs->blame.set(loc.piece);
    msgs->publish(tr_peer_event::GotBlock(tor->block_info(), block));

    return 0;
}

void tr_peerMsgsImpl::did_write(tr_peerIo* /*io*/, size_t bytes_written, bool was_piece_data, void* vmsgs)
{
    auto* const msgs = static_cast<tr_peerMsgsImpl*>(vmsgs);

    if (was_piece_data)
    {
        msgs->publish(tr_peer_event::SentPieceData(bytes_written));
    }

    peerPulse(msgs);
}

ReadState tr_peerMsgsImpl::can_read(tr_peerIo* io, void* vmsgs, size_t* piece)
{
    auto* msgs = static_cast<tr_peerMsgsImpl*>(vmsgs);

    // https://www.bittorrent.org/beps/bep_0003.html
    // Next comes an alternating stream of length prefixes and messages.
    // Messages of length zero are keepalives, and ignored.
    // All non-keepalive messages start with a single byte which gives their type.
    //
    // https://wiki.theory.org/BitTorrentSpecification
    // All of the remaining messages in the protocol take the form of
    // <length prefix><message ID><payload>. The length prefix is a four byte
    // big-endian value. The message ID is a single decimal byte.
    // The payload is message dependent.

    // read <length prefix>
    auto& current_message_len = msgs->incoming.length; // the full message payload length. Includes the +1 for id length
    if (!current_message_len)
    {
        auto message_len = uint32_t{};
        if (io->read_buffer_size() < sizeof(message_len))
        {
            return READ_LATER;
        }

        io->read_uint32(&message_len);

        // The keep-alive message is a message with zero bytes,
        // specified with the length prefix set to zero.
        // There is no message ID and no payload.
        if (message_len == 0U)
        {
            logtrace(msgs, "got KeepAlive");
            return READ_NOW;
        }

        current_message_len = message_len;
    }

    // read <message ID>
    auto& current_message_type = msgs->incoming.id;
    if (!current_message_type)
    {
        auto message_type = uint8_t{};
        if (io->read_buffer_size() < sizeof(message_type))
        {
            return READ_LATER;
        }

        io->read_uint8(&message_type);
        current_message_type = message_type;
    }

    // read <payload>
    auto& current_payload = msgs->incoming.payload;
    auto const full_payload_len = *current_message_len - sizeof(*current_message_type);
    auto n_left = full_payload_len - std::size(current_payload);
    auto const [buf, n_this_pass] = current_payload.reserve_space(std::min(n_left, io->read_buffer_size()));
    io->read_bytes(buf, n_this_pass);
    current_payload.commit_space(n_this_pass);
    n_left -= n_this_pass;
    logtrace(msgs, fmt::format("read {:d} payload bytes; {:d} left to go", n_this_pass, n_left));

    if (n_left > 0U)
    {
        return READ_LATER;
    }

    // The incoming message is now complete. After processing the message
    // with `process_peer_message()`, reset the peerMsgs' incoming
    // field so it's ready to receive the next message.

    auto const [read_state, n_piece_bytes_read] = process_peer_message(msgs, *current_message_type, current_payload);
    *piece = n_piece_bytes_read;

    current_message_len.reset();
    current_message_type.reset();
    current_payload.clear();

    return read_state;
}

// ---

void updateDesiredRequestCount(tr_peerMsgsImpl* msgs)
{
    msgs->desired_request_count = msgs->can_request().max_blocks;
}

void updateMetadataRequests(tr_peerMsgsImpl* msgs, time_t now)
{
    if (!msgs->peerSupportsMetadataXfer)
    {
        return;
    }

    if (auto const piece = msgs->torrent->get_next_metadata_request(now); piece)
    {
        auto tmp = tr_variant{};
        tr_variantInitDict(&tmp, 3);
        tr_variantDictAddInt(&tmp, TR_KEY_msg_type, MetadataMsgType::Request);
        tr_variantDictAddInt(&tmp, TR_KEY_piece, *piece);
        protocol_send_message(msgs, BtPeerMsgs::Ltep, msgs->ut_metadata_id, tr_variant_serde::benc().to_string(tmp));
    }
}

void updateBlockRequests(tr_peerMsgsImpl* msgs)
{
    auto* const tor = msgs->torrent;

    if (!tor->client_can_download())
    {
        return;
    }

    auto const n_active = tr_peerMgrCountActiveRequestsToPeer(tor, msgs);
    if (n_active >= msgs->desired_request_count)
    {
        return;
    }

    TR_ASSERT(msgs->client_is_interested());
    TR_ASSERT(!msgs->client_is_choked());

    auto const n_wanted = msgs->desired_request_count - n_active;
    if (auto const requests = tr_peerMgrGetNextRequests(tor, msgs, n_wanted); !std::empty(requests))
    {
        msgs->request_blocks(std::data(requests), std::size(requests));
    }
}

namespace peer_pulse_helpers
{
[[nodiscard]] size_t add_next_metadata_piece(tr_peerMsgsImpl* msgs)
{
    auto const piece = popNextMetadataRequest(msgs);

    if (!piece.has_value()) // no pending requests
    {
        return {};
    }

    auto data = msgs->torrent->get_metadata_piece(*piece);
    if (!data)
    {
        // send a reject
        auto tmp = tr_variant{};
        tr_variantInitDict(&tmp, 2);
        tr_variantDictAddInt(&tmp, TR_KEY_msg_type, MetadataMsgType::Reject);
        tr_variantDictAddInt(&tmp, TR_KEY_piece, *piece);
        return protocol_send_message(msgs, BtPeerMsgs::Ltep, msgs->ut_metadata_id, tr_variant_serde::benc().to_string(tmp));
    }

    // send the metadata
    auto tmp = tr_variant{};
    tr_variantInitDict(&tmp, 3);
    tr_variantDictAddInt(&tmp, TR_KEY_msg_type, MetadataMsgType::Data);
    tr_variantDictAddInt(&tmp, TR_KEY_piece, *piece);
    tr_variantDictAddInt(&tmp, TR_KEY_total_size, msgs->torrent->info_dict_size());
    return protocol_send_message(msgs, BtPeerMsgs::Ltep, msgs->ut_metadata_id, tr_variant_serde::benc().to_string(tmp), *data);
}

[[nodiscard]] size_t add_next_piece(tr_peerMsgsImpl* msgs, uint64_t now)
{
    if (std::empty(msgs->peer_requested_) || msgs->io->get_write_buffer_space(now) == 0U)
    {
        return {};
    }

    auto const req = msgs->peer_requested_.front();
    msgs->peer_requested_.pop_front();

    auto buf = std::array<uint8_t, tr_block_info::BlockSize>{};
    auto ok = msgs->is_valid_request(req) && msgs->torrent->has_piece(req.index);

    if (ok)
    {
        ok = msgs->torrent->ensure_piece_is_checked(req.index);

        if (!ok)
        {
            msgs->torrent->error().set_local_error(fmt::format("Please Verify Local Data! Piece #{:d} is corrupt.", req.index));
        }
    }

    if (ok)
    {
        ok = msgs->session->cache
                 ->read_block(*msgs->torrent, msgs->torrent->piece_loc(req.index, req.offset), req.length, std::data(buf)) == 0;
    }

    if (ok)
    {
        auto const piece_data = std::string_view{ reinterpret_cast<char const*>(std::data(buf)), req.length };
        return protocol_send_message(msgs, BtPeerMsgs::Piece, req.index, req.offset, piece_data);
    }

    if (msgs->io->supports_fext())
    {
        return protocolSendReject(msgs, &req);
    }

    return {};
}

[[nodiscard]] size_t fill_output_buffer(tr_peerMsgsImpl* msgs, time_t now_sec, uint64_t now_msec)
{
    auto n_bytes_written = size_t{};

    // fulfuill metadata requests
    for (;;)
    {
        auto const old_len = n_bytes_written;
        n_bytes_written += add_next_metadata_piece(msgs);
        if (old_len == n_bytes_written)
        {
            break;
        }
    }

    // fulfuill piece requests
    for (;;)
    {
        auto const old_len = n_bytes_written;
        n_bytes_written += add_next_piece(msgs, now_msec);
        if (old_len == n_bytes_written)
        {
            break;
        }
    }

    if (msgs != nullptr && msgs->clientSentAnythingAt != 0 && now_sec - msgs->clientSentAnythingAt > KeepaliveIntervalSecs)
    {
        n_bytes_written += protocol_send_keepalive(msgs);
    }

    return n_bytes_written;
}
} // namespace peer_pulse_helpers

void peerPulse(void* vmsgs)
{
    using namespace peer_pulse_helpers;

    auto* msgs = static_cast<tr_peerMsgsImpl*>(vmsgs);
    auto const now_sec = tr_time();
    auto const now_msec = tr_time_msec();

    updateDesiredRequestCount(msgs);
    updateBlockRequests(msgs);
    updateMetadataRequests(msgs, now_sec);

    for (;;)
    {
        if (fill_output_buffer(msgs, now_sec, now_msec) == 0U)
        {
            break;
        }
    }
}

void gotError(tr_peerIo* /*io*/, tr_error const& /*error*/, void* vmsgs)
{
    static_cast<tr_peerMsgsImpl*>(vmsgs)->publish(tr_peer_event::GotError(ENOTCONN));
}

void tellPeerWhatWeHave(tr_peerMsgsImpl* msgs)
{
    bool const fext = msgs->io->supports_fext();

    if (fext && msgs->torrent->has_all())
    {
        protocol_send_message(msgs, BtPeerMsgs::FextHaveAll);
    }
    else if (fext && msgs->torrent->has_none())
    {
        protocol_send_message(msgs, BtPeerMsgs::FextHaveNone);
    }
    else if (!msgs->torrent->has_none())
    {
        protocol_send_message(msgs, BtPeerMsgs::Bitfield, msgs->torrent->create_piece_bitfield());
    }
}

void tr_peerMsgsImpl::send_pex()
{
    // only send pex if both the torrent and peer support it
    if (!this->peerSupportsPex || !this->torrent->allows_pex())
    {
        return;
    }

    static auto constexpr MaxPexAdded = size_t{ 50 };
    static auto constexpr MaxPexDropped = size_t{ 50 };

    auto val = tr_variant{};
    tr_variantInitDict(&val, 3); /* ipv6 support: left as 3: speed vs. likelihood? */

    auto tmpbuf = small::vector<std::byte, 2048U>{};

    for (uint8_t i = 0; i < NUM_TR_AF_INET_TYPES; ++i)
    {
        static auto constexpr AddedMap = std::array{ TR_KEY_added, TR_KEY_added6 };
        static auto constexpr AddedFMap = std::array{ TR_KEY_added_f, TR_KEY_added6_f };
        static auto constexpr DroppedMap = std::array{ TR_KEY_dropped, TR_KEY_dropped6 };
        auto const ip_type = static_cast<tr_address_type>(i);

        auto& old_pex = pex[i];
        auto new_pex = tr_peerMgrGetPeers(this->torrent, ip_type, TR_PEERS_CONNECTED, MaxPexPeerCount);
        auto added = std::vector<tr_pex>{};
        added.reserve(std::size(new_pex));
        std::set_difference(
            std::begin(new_pex),
            std::end(new_pex),
            std::begin(old_pex),
            std::end(old_pex),
            std::back_inserter(added));
        auto dropped = std::vector<tr_pex>{};
        dropped.reserve(std::size(old_pex));
        std::set_difference(
            std::begin(old_pex),
            std::end(old_pex),
            std::begin(new_pex),
            std::end(new_pex),
            std::back_inserter(dropped));

        // Some peers give us error messages if we send
        // more than this many peers in a single pex message.
        // https://wiki.theory.org/BitTorrentPeerExchangeConventions
        added.resize(std::min(std::size(added), MaxPexAdded));
        dropped.resize(std::min(std::size(dropped), MaxPexDropped));

        logtrace(
            this,
            fmt::format(
                "pex: old {:s} peer count {:d}, new peer count {:d}, added {:d}, dropped {:d}",
                tr_ip_protocol_to_sv(ip_type),
                std::size(old_pex),
                std::size(new_pex),
                std::size(added),
                std::size(dropped)));

        // if there's nothing to send, then we're done
        if (std::empty(added) && std::empty(dropped))
        {
            continue;
        }

        // update msgs
        std::swap(old_pex, new_pex);

        // build the pex payload
        if (!std::empty(added))
        {
            // "added"
            tmpbuf.clear();
            tmpbuf.reserve(std::size(added) * tr_socket_address::CompactSockAddrBytes[i]);
            tr_pex::to_compact(std::back_inserter(tmpbuf), std::data(added), std::size(added));
            TR_ASSERT(std::size(tmpbuf) == std::size(added) * tr_socket_address::CompactSockAddrBytes[i]);
            tr_variantDictAddRaw(&val, AddedMap[i], std::data(tmpbuf), std::size(tmpbuf));

            // "added.f"
            tmpbuf.resize(std::size(added));
            auto* begin = std::data(tmpbuf);
            auto* walk = begin;
            for (auto const& p : added)
            {
                *walk++ = std::byte{ p.flags };
            }

            TR_ASSERT(static_cast<size_t>(walk - begin) == std::size(added));
            tr_variantDictAddRaw(&val, AddedFMap[i], begin, walk - begin);
        }

        if (!std::empty(dropped))
        {
            // "dropped"
            tmpbuf.clear();
            tmpbuf.reserve(std::size(dropped) * tr_socket_address::CompactSockAddrBytes[i]);
            tr_pex::to_compact(std::back_inserter(tmpbuf), std::data(dropped), std::size(dropped));
            TR_ASSERT(std::size(tmpbuf) == std::size(dropped) * tr_socket_address::CompactSockAddrBytes[i]);
            tr_variantDictAddRaw(&val, DroppedMap[i], std::data(tmpbuf), std::size(tmpbuf));
        }
    }

    protocol_send_message(this, BtPeerMsgs::Ltep, this->ut_pex_id, tr_variant_serde::benc().to_string(val));
}

} // namespace

tr_peerMsgs::tr_peerMsgs(
    tr_torrent const* tor,
    tr_peer_info* peer_info_in,
    tr_interned_string user_agent,
    bool connection_is_encrypted,
    bool connection_is_incoming,
    bool connection_is_utp)
    : tr_peer{ tor }
    , peer_info{ peer_info_in }
    , user_agent_{ user_agent }
    , connection_is_encrypted_{ connection_is_encrypted }
    , connection_is_incoming_{ connection_is_incoming }
    , connection_is_utp_{ connection_is_utp }
{
    peer_info->set_connected(tr_time());
    ++n_peers;
}

tr_peerMsgs::~tr_peerMsgs()
{
    peer_info->set_connected(tr_time(), false);
    [[maybe_unused]] auto const n_prev = n_peers--;
    TR_ASSERT(n_prev > 0U);
}

tr_peerMsgs* tr_peerMsgsNew(
    tr_torrent* const torrent,
    tr_peer_info* const peer_info,
    std::shared_ptr<tr_peerIo> io,
    tr_interned_string user_agent,
    tr_peer_callback_bt callback,
    void* callback_data)
{
    return new tr_peerMsgsImpl(torrent, peer_info, std::move(io), user_agent, callback, callback_data);
}
