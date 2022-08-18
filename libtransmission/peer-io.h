// This file Copyright © 2007-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

/**
***
**/

#include <cstddef> // size_t
#include <cstdint> // uintX_t
#include <ctime>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <utility> // std::make_pair

#include <event2/buffer.h>

#include "transmission.h"

#include "bandwidth.h"
#include "net.h" // tr_address
#include "peer-mse.h"
#include "peer-socket.h"
#include "tr-assert.h"

class tr_peerIo;
struct tr_bandwidth;
struct struct_utp_context;

/**
 * @addtogroup networked_io Networked IO
 * @{
 */

enum ReadState
{
    READ_NOW,
    READ_LATER,
    READ_ERR
};

using tr_can_read_cb = ReadState (*)(tr_peerIo* io, void* user_data, size_t* setme_piece_byte_count);

using tr_did_write_cb = void (*)(tr_peerIo* io, size_t bytesWritten, bool wasPieceData, void* userData);

using tr_net_error_cb = void (*)(tr_peerIo* io, short what, void* userData);

auto inline constexpr PEER_IO_MAGIC_NUMBER = 206745;

struct evbuffer_deleter
{
    void operator()(struct evbuffer* buf) const noexcept
    {
        evbuffer_free(buf);
    }
};

using tr_evbuffer_ptr = std::unique_ptr<evbuffer, evbuffer_deleter>;

class tr_peerIo
{
    using DH = tr_message_stream_encryption::DH;
    using Filter = tr_message_stream_encryption::Filter;

public:
    tr_peerIo(
        tr_session* session_in,
        tr_sha1_digest_t const* torrent_hash,
        bool is_incoming,
        tr_address const& addr,
        tr_port port,
        bool is_seed,
        time_t current_time,
        tr_bandwidth* parent_bandwidth)
        : session{ session_in }
        , time_created{ current_time }
        , bandwidth_{ parent_bandwidth }
        , addr_{ addr }
        , port_{ port }
        , is_seed_{ is_seed }
        , is_incoming_{ is_incoming }
    {
        if (torrent_hash != nullptr)
        {
            torrent_hash_ = *torrent_hash;
        }
    }

    [[nodiscard]] constexpr tr_address const& address() const noexcept
    {
        return addr_;
    }

    [[nodiscard]] constexpr std::pair<tr_address, tr_port> socketAddress() const noexcept
    {
        return std::make_pair(addr_, port_);
    }

    std::string addrStr() const;

    [[nodiscard]] auto getReadBuffer() noexcept
    {
        return inbuf.get();
    }

    void readBufferAdd(void const* data, size_t n_bytes);

    [[nodiscard]] auto hasBandwidthLeft(tr_direction dir) noexcept
    {
        return bandwidth_.clamp(dir, 1024) > 0;
    }

    [[nodiscard]] auto getPieceSpeed_Bps(uint64_t now, tr_direction dir) noexcept
    {
        return bandwidth_.getPieceSpeedBytesPerSecond(now, dir);
    }

    constexpr void enableFEXT(bool flag) noexcept
    {
        fast_extension_supported_ = flag;
    }

    [[nodiscard]] constexpr auto supportsFEXT() const noexcept
    {
        return fast_extension_supported_;
    }

    constexpr void enableLTEP(bool flag) noexcept
    {
        extended_protocol_supported_ = flag;
    }

    [[nodiscard]] constexpr auto supportsLTEP() const noexcept
    {
        return extended_protocol_supported_;
    }

    constexpr void enableDHT(bool flag) noexcept
    {
        dht_supported_ = flag;
    }

    [[nodiscard]] constexpr auto supportsDHT() const noexcept
    {
        return dht_supported_;
    }

    [[nodiscard]] constexpr auto supportsUTP() const noexcept
    {
        return utp_supported_;
    }

    [[nodiscard]] constexpr auto isSeed() const noexcept
    {
        return is_seed_;
    }

    [[nodiscard]] constexpr auto const& bandwidth() const noexcept
    {
        return bandwidth_;
    }

    [[nodiscard]] constexpr auto& bandwidth() noexcept
    {
        return bandwidth_;
    }

    void setParent(tr_bandwidth* parent)
    {
        bandwidth_.setParent(parent);
    }

    [[nodiscard]] constexpr auto isIncoming() noexcept
    {
        return is_incoming_;
    }

    void setTorrentHash(tr_sha1_digest_t hash) noexcept
    {
        torrent_hash_ = hash;
    }

    [[nodiscard]] constexpr auto const& torrentHash() const noexcept
    {
        return torrent_hash_;
    }

    // TODO(ckerr): yikes, unlike other class' magic_numbers it looks
    // like this one isn't being used just for assertions, but also in
    // didWriteWrapper() to see if the tr_peerIo got freed during the
    // notify-consumed events. Fix this before removing this field.
    int magic_number = PEER_IO_MAGIC_NUMBER;

    struct tr_peer_socket socket = {};

    tr_session* const session;

    time_t const time_created;

    tr_can_read_cb canRead = nullptr;
    tr_did_write_cb didWrite = nullptr;
    tr_net_error_cb gotError = nullptr;
    void* userData = nullptr;

    tr_evbuffer_ptr const inbuf = tr_evbuffer_ptr{ evbuffer_new() };
    tr_evbuffer_ptr const outbuf = tr_evbuffer_ptr{ evbuffer_new() };

    std::deque<std::pair<size_t /*n_bytes*/, bool /*is_piece_data*/>> outbuf_info;

    struct event* event_read = nullptr;
    struct event* event_write = nullptr;

    // TODO: use std::shared_ptr instead of manual refcounting?
    int refCount = 1;

    short int pendingEvents = 0;

    tr_priority_t priority = TR_PRI_NORMAL;

    bool utp_supported_ = false;

    void decryptInit(bool is_incoming, DH const& dh, tr_sha1_digest_t const& info_hash)
    {
        filter().decryptInit(is_incoming, dh, info_hash);
    }

    void decrypt(size_t buflen, void* buf)
    {
        filter().decrypt(buflen, buf);
    }

    void encryptInit(bool is_incoming, DH const& dh, tr_sha1_digest_t const& info_hash)
    {
        filter().encryptInit(is_incoming, dh, info_hash);
    }

    void encrypt(size_t buflen, void* buf)
    {
        filter().encrypt(buflen, buf);
    }

    [[nodiscard]] bool isEncrypted() const noexcept
    {
        return filter_.get() != nullptr;
    }

private:
    tr_bandwidth bandwidth_;

    std::unique_ptr<tr_message_stream_encryption::Filter> filter_;

    Filter& filter()
    {
        if (!filter_)
        {
            filter_ = std::make_unique<Filter>();
        }

        return *filter_;
    }

    std::optional<tr_sha1_digest_t> torrent_hash_;

    tr_address const addr_;
    tr_port const port_;

    bool const is_seed_;
    bool const is_incoming_;

    bool dht_supported_ = false;
    bool extended_protocol_supported_ = false;
    bool fast_extension_supported_ = false;
};

/**
***
**/

void tr_peerIoUtpInit(struct_utp_context* ctx);

// TODO: 8 constructor args is too many; maybe a builder object?
tr_peerIo* tr_peerIoNewOutgoing(
    tr_session* session,
    tr_bandwidth* parent,
    struct tr_address const* addr,
    tr_port port,
    time_t current_time,
    tr_sha1_digest_t const& torrent_hash,
    bool is_seed,
    bool utp);

tr_peerIo* tr_peerIoNewIncoming(
    tr_session* session,
    tr_bandwidth* parent,
    struct tr_address const* addr,
    tr_port port,
    time_t current_time,
    struct tr_peer_socket const socket);

// this is only public for testing purposes.
// production code should use tr_peerIoNewOutgoing() or tr_peerIoNewIncoming()
tr_peerIo* tr_peerIoNew(
    tr_session* session,
    tr_bandwidth* parent,
    tr_address const* addr,
    tr_port port,
    time_t current_time,
    tr_sha1_digest_t const* torrent_hash,
    bool is_incoming,
    bool is_seed,
    struct tr_peer_socket const socket);

void tr_peerIoRefImpl(char const* file, int line, tr_peerIo* io);

#define tr_peerIoRef(io) tr_peerIoRefImpl(__FILE__, __LINE__, (io))

void tr_peerIoUnrefImpl(char const* file, int line, tr_peerIo* io);

#define tr_peerIoUnref(io) tr_peerIoUnrefImpl(__FILE__, __LINE__, (io))

constexpr bool tr_isPeerIo(tr_peerIo const* io)
{
    return io != nullptr && io->magic_number == PEER_IO_MAGIC_NUMBER && io->refCount >= 0 &&
        tr_address_is_valid(&io->address());
}

/**
***
**/

constexpr tr_session* tr_peerIoGetSession(tr_peerIo* io)
{
    TR_ASSERT(tr_isPeerIo(io));
    TR_ASSERT(io->session != nullptr);

    return io->session;
}

int tr_peerIoReconnect(tr_peerIo* io);

/**
***
**/

void tr_peerIoSetIOFuncs(tr_peerIo* io, tr_can_read_cb readcb, tr_did_write_cb writecb, tr_net_error_cb errcb, void* user_data);

void tr_peerIoClear(tr_peerIo* io);

/**
***
**/

void tr_peerIoWriteBytes(tr_peerIo* io, void const* writeme, size_t writeme_len, bool is_piece_data);

void tr_peerIoWriteBuf(tr_peerIo* io, struct evbuffer* buf, bool isPieceData);

/**
***
**/

void evbuffer_add_uint8(struct evbuffer* outbuf, uint8_t addme);
void evbuffer_add_uint16(struct evbuffer* outbuf, uint16_t hs);
void evbuffer_add_uint32(struct evbuffer* outbuf, uint32_t hl);
void evbuffer_add_uint64(struct evbuffer* outbuf, uint64_t hll);

static inline void evbuffer_add_hton_16(struct evbuffer* buf, uint16_t val)
{
    evbuffer_add_uint16(buf, val);
}

static inline void evbuffer_add_hton_32(struct evbuffer* buf, uint32_t val)
{
    evbuffer_add_uint32(buf, val);
}

static inline void evbuffer_add_hton_64(struct evbuffer* buf, uint64_t val)
{
    evbuffer_add_uint64(buf, val);
}

void tr_peerIoReadBytes(tr_peerIo* io, struct evbuffer* inbuf, void* bytes, size_t byteCount);

static inline void tr_peerIoReadUint8(tr_peerIo* io, struct evbuffer* inbuf, uint8_t* setme)
{
    tr_peerIoReadBytes(io, inbuf, setme, sizeof(uint8_t));
}

void tr_peerIoReadUint16(tr_peerIo* io, struct evbuffer* inbuf, uint16_t* setme);

void tr_peerIoReadUint32(tr_peerIo* io, struct evbuffer* inbuf, uint32_t* setme);

void tr_peerIoDrain(tr_peerIo* io, struct evbuffer* inbuf, size_t byteCount);

/**
***
**/

size_t tr_peerIoGetWriteBufferSpace(tr_peerIo const* io, uint64_t now);

void tr_peerIoBandwidthUsed(tr_peerIo* io, tr_direction direction, size_t byteCount, int isPieceData);

/**
***
**/

void tr_peerIoSetEnabled(tr_peerIo* io, tr_direction dir, bool isEnabled);

int tr_peerIoFlush(tr_peerIo* io, tr_direction dir, size_t byteLimit);

int tr_peerIoFlushOutgoingProtocolMsgs(tr_peerIo* io);

/* @} */
