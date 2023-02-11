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
#include <string>
#include <utility> // std::make_pair

#include "transmission.h"

#include "bandwidth.h"
#include "net.h" // tr_address
#include "peer-mse.h"
#include "peer-socket.h"
#include "tr-assert.h"
#include "tr-buffer.h"
#include "utils-ev.h"

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

auto inline constexpr PEER_IO_MAGIC_NUMBER = 206745;

namespace libtransmission::test
{

class HandshakeTest;

} // namespace libtransmission::test

class tr_peerIo final : public std::enable_shared_from_this<tr_peerIo>
{
    using DH = tr_message_stream_encryption::DH;
    using Filter = tr_message_stream_encryption::Filter;

public:
    ~tr_peerIo();

    // TODO: 8 constructor args is too many; maybe a builder object?
    static std::shared_ptr<tr_peerIo> newOutgoing(
        tr_session* session,
        tr_bandwidth* parent,
        struct tr_address const* addr,
        tr_port port,
        time_t current_time,
        tr_sha1_digest_t const& torrent_hash,
        bool is_seed,
        bool utp);

    static std::shared_ptr<tr_peerIo> newIncoming(
        tr_session* session,
        tr_bandwidth* parent,
        struct tr_address const* addr,
        tr_port port,
        time_t current_time,
        struct tr_peer_socket const socket);

    void clear();

    void readBytes(void* bytes, size_t byte_count);

    void readUint8(uint8_t* setme)
    {
        readBytes(setme, sizeof(uint8_t));
    }

    void readUint16(uint16_t* setme);
    void readUint32(uint32_t* setme);

    int reconnect();

    void setEnabled(tr_direction dir, bool is_enabled);

    [[nodiscard]] constexpr tr_address const& address() const noexcept
    {
        return addr_;
    }

    [[nodiscard]] constexpr std::pair<tr_address, tr_port> socketAddress() const noexcept
    {
        return std::make_pair(addr_, port_);
    }

    std::string addrStr() const;

    void readBufferDrain(size_t byte_count);

    [[nodiscard]] auto readBufferSize() const noexcept
    {
        return std::size(inbuf);
    }

    template<typename T>
    [[nodiscard]] auto readBufferStartsWith(T const& t) const noexcept
    {
        return inbuf.startsWith(t);
    }

    void readBufferAdd(void const* data, size_t n_bytes);

    size_t flushOutgoingProtocolMsgs(tr_error** error = nullptr);
    size_t flush(tr_direction dir, size_t byte_limit, tr_error** error = nullptr);

    void writeBytes(void const* bytes, size_t n_bytes, bool is_piece_data);

    // Write all the data from `buf`.
    // This is a destructive add: `buf` is empty after this call.
    void write(libtransmission::Buffer& buf, bool is_piece_data);

    [[nodiscard]] size_t getWriteBufferSpace(uint64_t now) const noexcept;

    [[nodiscard]] auto hasBandwidthLeft(tr_direction dir) noexcept
    {
        return bandwidth_.clamp(dir, 1024) > 0;
    }

    [[nodiscard]] auto getPieceSpeedBytesPerSecond(uint64_t now, tr_direction dir) noexcept
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

    [[nodiscard]] constexpr auto isIncoming() const noexcept
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

    using tr_can_read_cb = ReadState (*)(tr_peerIo* io, void* user_data, size_t* setme_piece_byte_count);
    using tr_did_write_cb = void (*)(tr_peerIo* io, size_t bytesWritten, bool wasPieceData, void* userData);
    using tr_net_error_cb = void (*)(tr_peerIo* io, short what, void* userData);
    void setCallbacks(tr_can_read_cb readcb, tr_did_write_cb writecb, tr_net_error_cb errcb, void* user_data);

    void clearCallbacks()
    {
        setCallbacks(nullptr, nullptr, nullptr, nullptr);
    }

    struct tr_peer_socket socket = {};

    tr_session* const session;

    time_t const time_created;

    tr_can_read_cb canRead = nullptr;
    tr_did_write_cb didWrite = nullptr;
    tr_net_error_cb gotError = nullptr;
    void* userData = nullptr;

    void call_error_callback(short what)
    {
        if (gotError != nullptr)
        {
            gotError(this, what, userData);
        }
    }

    libtransmission::Buffer inbuf;
    libtransmission::Buffer outbuf;

    std::deque<std::pair<size_t /*n_bytes*/, bool /*is_piece_data*/>> outbuf_info;

    libtransmission::evhelpers::event_unique_ptr event_read;
    libtransmission::evhelpers::event_unique_ptr event_write;

    short int pendingEvents = 0;

    tr_priority_t priority = TR_PRI_NORMAL;

    bool utp_supported_ = false;

    void decryptInit(bool is_incoming, DH const& dh, tr_sha1_digest_t const& info_hash)
    {
        filter_.decryptInit(is_incoming, dh, info_hash);
    }

    void decrypt(size_t buflen, void* buf)
    {
        filter_.decrypt(buflen, buf);
    }

    void encryptInit(bool is_incoming, DH const& dh, tr_sha1_digest_t const& info_hash)
    {
        filter_.encryptInit(is_incoming, dh, info_hash);
    }

    void encrypt(size_t buflen, void* buf)
    {
        filter_.encrypt(buflen, buf);
    }

    [[nodiscard]] bool isEncrypted() const noexcept
    {
        return filter_.is_active();
    }

    static void utpInit(struct_utp_context* ctx);

private:
    friend class libtransmission::test::HandshakeTest;

    // this is only public for testing purposes.
    // production code should use newOutgoing() or newIncoming()
    static std::shared_ptr<tr_peerIo> create(
        tr_session* session,
        tr_bandwidth* parent,
        tr_address const* addr,
        tr_port port,
        time_t current_time,
        tr_sha1_digest_t const* torrent_hash,
        bool is_incoming,
        bool is_seed,
        struct tr_peer_socket const socket);

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
        , torrent_hash_{ torrent_hash != nullptr ? *torrent_hash : tr_sha1_digest_t{} }
        , addr_{ addr }
        , port_{ port }
        , is_seed_{ is_seed }
        , is_incoming_{ is_incoming }
    {
    }

    tr_bandwidth bandwidth_;

    Filter filter_;

    tr_sha1_digest_t torrent_hash_;

    tr_address const addr_;
    tr_port const port_;

    bool const is_seed_;
    bool const is_incoming_;

    bool dht_supported_ = false;
    bool extended_protocol_supported_ = false;
    bool fast_extension_supported_ = false;
};

constexpr bool tr_isPeerIo(tr_peerIo const* io)
{
    return io != nullptr && tr_address_is_valid(&io->address());
}
