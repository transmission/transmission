// This file Copyright 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstddef>
#include <memory>

#include <event2/buffer.h>

#include "error.h"
#include "net.h" // tr_socket_t
#include "utils.h"

namespace libtransmission
{

class Buffer
{
public:
    Buffer() = default;
    Buffer(Buffer const&) = delete;
    Buffer& operator=(Buffer const&) = delete;

    [[nodiscard]] auto size() const noexcept
    {
        return evbuffer_get_length(buf_.get());
    }

    template<typename T>
    [[nodiscard]] T const* peek(size_t n_bytes) const noexcept
    {
        if (n_bytes > size())
        {
            return nullptr;
        }

        return reinterpret_cast<T const*>(evbuffer_pullup(buf_.get(), n_bytes));
    }

    auto toBuf(void* tgt, size_t n_bytes)
    {
        return evbuffer_remove(buf_.get(), tgt, n_bytes);
    }

    [[nodiscard]] uint16_t toUint16()
    {
        auto tmp = uint16_t{};
        toBuf(&tmp, sizeof(tmp));
        return ntohs(tmp);
    }

    [[nodiscard]] uint32_t toUint32()
    {
        auto tmp = uint32_t{};
        toBuf(&tmp, sizeof(tmp));
        return ntohl(tmp);
    }

    void drain(size_t n_bytes)
    {
        evbuffer_drain(buf_.get(), n_bytes);
    }

    // -1 on error, 0 on eof, >0 on n bytes written
    ssize_t toSocket(tr_socket_t sockfd, size_t n_bytes, tr_error** error = nullptr)
    {
        EVUTIL_SET_SOCKET_ERROR(0);
        int const res = evbuffer_write_atmost(buf_.get(), sockfd, n_bytes);
        int const err = EVUTIL_SOCKET_ERROR();
        if (res == -1)
        {
            tr_error_set(error, err, tr_net_strerror(err));
        }
        return res;
    }

    // -1 on error, 0 on eof, >0 for num bytes read
    ssize_t fromSocket(tr_socket_t sockfd, size_t n_bytes, tr_error** error = nullptr)
    {
        EVUTIL_SET_SOCKET_ERROR(0);
        auto const res = evbuffer_read(buf_.get(), sockfd, static_cast<int>(n_bytes));
        int const err = EVUTIL_SOCKET_ERROR();
        if (res == -1)
        {
            tr_error_set(error, err, tr_net_strerror(err));
        }
        return res;
    }

    void fromBuf(void const* bytes, size_t n_bytes)
    {
        evbuffer_add(buf_.get(), bytes, n_bytes);
    }

    void fromUint8(uint8_t uch)
    {
        fromBuf(&uch, 1);
    }

    void fromUint16(uint16_t hs)
    {
        uint16_t const ns = htons(hs);
        fromBuf(&ns, sizeof(ns));
    }

    void fromHton16(uint16_t hs)
    {
        fromUint16(hs);
    }

    void fromUint32(uint32_t hl)
    {
        uint32_t const nl = htonl(hl);
        fromBuf(&nl, sizeof(nl));
    }

    void fromHton32(uint32_t hl)
    {
        fromUint32(hl);
    }

    void fromUint64(uint64_t hll)
    {
        uint64_t const nll = tr_htonll(hll);
        fromBuf(&nll, sizeof(nll));
    }

    void fromHton64(uint64_t hll)
    {
        fromUint64(hll);
    }

private:
    std::unique_ptr<evbuffer, void (*)(evbuffer*)> const buf_{ evbuffer_new(), evbuffer_free };
};

} // namespace libtransmission
