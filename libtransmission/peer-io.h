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

namespace libtransmission::test
{

class HandshakeTest;

} // namespace libtransmission::test

class tr_peerIo final : public std::enable_shared_from_this<tr_peerIo>
{
    using DH = tr_message_stream_encryption::DH;
    using Filter = tr_message_stream_encryption::Filter;

public:
    using CanRead = ReadState (*)(tr_peerIo* io, void* user_data, size_t* setme_piece_byte_count);
    using DidWrite = void (*)(tr_peerIo* io, size_t bytesWritten, bool wasPieceData, void* userData);
    using GotError = void (*)(tr_peerIo* io, tr_error const& error, void* userData);

    tr_peerIo(
        tr_session* session_in,
        tr_sha1_digest_t const* info_hash,
        bool is_incoming,
        bool is_seed,
        tr_bandwidth* parent_bandwidth);

    ~tr_peerIo();

    static std::shared_ptr<tr_peerIo> newOutgoing(
        tr_session* session,
        tr_bandwidth* parent,
        tr_address const& addr,
        tr_port port,
        tr_sha1_digest_t const& info_hash,
        bool is_seed,
        bool utp);

    static std::shared_ptr<tr_peerIo> newIncoming(tr_session* session, tr_bandwidth* parent, tr_peer_socket socket);

    void set_socket(tr_peer_socket);

    [[nodiscard]] constexpr auto is_utp() const noexcept
    {
        return socket_.is_utp();
    }

    void clear();

    void readBytes(void* bytes, size_t byte_count);

    void readUint8(uint8_t* setme)
    {
        readBytes(setme, sizeof(uint8_t));
    }

    void readUint16(uint16_t* setme);
    void readUint32(uint32_t* setme);

    [[nodiscard]] bool reconnect();

    void setEnabled(tr_direction dir, bool is_enabled);

    [[nodiscard]] constexpr auto const& address() const noexcept
    {
        return socket_.address();
    }

    [[nodiscard]] constexpr auto socketAddress() const noexcept
    {
        return socket_.socketAddress();
    }

    [[nodiscard]] auto display_name() const
    {
        return socket_.display_name();
    }

    void readBufferDrain(size_t byte_count);

    [[nodiscard]] auto readBufferSize() const noexcept
    {
        return std::size(inbuf_);
    }

    template<typename T>
    [[nodiscard]] auto readBufferStartsWith(T const& t) const noexcept
    {
        return inbuf_.startsWith(t);
    }

    size_t flushOutgoingProtocolMsgs();
    size_t flush(tr_direction dir, size_t byte_limit);

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
        info_hash_ = hash;
    }

    [[nodiscard]] constexpr auto const& torrentHash() const noexcept
    {
        return info_hash_;
    }

    void setCallbacks(CanRead can_read, DidWrite did_write, GotError got_error, void* user_data);

    void clearCallbacks()
    {
        setCallbacks(nullptr, nullptr, nullptr, nullptr);
    }

    [[nodiscard]] constexpr auto priority() const noexcept
    {
        return priority_;
    }

    constexpr void set_priority(tr_priority_t priority)
    {
        priority_ = priority;
    }

    void call_error_callback(tr_error const& error)
    {
        if (got_error_ != nullptr)
        {
            got_error_(this, error, user_data_);
        }
    }

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

    void on_utp_state_change(int new_state);
    void on_utp_error(int errcode);
    void can_read_wrapper();

private:
    static constexpr auto RcvBuf = size_t{ 256 * 1024 };

    void close();

    friend class libtransmission::test::HandshakeTest;

    static void event_read_cb(evutil_socket_t fd, short /*event*/, void* vio);
    static void event_write_cb(evutil_socket_t fd, short /*event*/, void* vio);

    void event_enable(short event);
    void event_disable(short event);

    void did_write_wrapper(size_t bytes_transferred);

    size_t try_read(size_t max);
    size_t try_write(size_t max);

    // this is only public for testing purposes.
    // production code should use newOutgoing() or newIncoming()
    static std::shared_ptr<tr_peerIo> create(
        tr_session* session,
        tr_bandwidth* parent,
        tr_sha1_digest_t const* info_hash,
        bool is_incoming,
        bool is_seed);

    tr_peer_socket socket_ = {};

    tr_session* const session_;

    CanRead can_read_ = nullptr;
    DidWrite did_write_ = nullptr;
    GotError got_error_ = nullptr;
    void* user_data_ = nullptr;

    libtransmission::Buffer inbuf_;
    libtransmission::Buffer outbuf_;

    std::deque<std::pair<size_t /*n_bytes*/, bool /*is_piece_data*/>> outbuf_info_;

    libtransmission::evhelpers::event_unique_ptr event_read_;
    libtransmission::evhelpers::event_unique_ptr event_write_;

    short int pending_events_ = 0;

    tr_priority_t priority_ = TR_PRI_NORMAL;

    bool utp_supported_ = false;

    tr_bandwidth bandwidth_;

    Filter filter_;

    tr_sha1_digest_t info_hash_;

    bool const is_seed_;
    bool const is_incoming_;

    bool dht_supported_ = false;
    bool extended_protocol_supported_ = false;
    bool fast_extension_supported_ = false;
};

constexpr bool tr_isPeerIo(tr_peerIo const* io)
{
    return io != nullptr && io->address().is_valid();
}
