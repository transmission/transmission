// This file Copyright 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstddef>
#include <limits>
#include <memory>
#include <vector>

#include <iostream>

#include <event2/buffer.h>

#include "error.h"
#include "net.h" // tr_socket_t
#include "utils.h"

namespace libtransmission
{

class Buffer
{
public:
    using Iovec = evbuffer_iovec;

    class Iterator
    {
    public:
        Iterator(evbuffer* buf, size_t offset)
            : buf_{ buf }
        {
            setOffset(offset);
        }

        [[nodiscard]] std::byte& operator*() noexcept
        {
            std::cerr << __FILE__ << ':' << __LINE__ << " deref" << std::endl;
            return *reinterpret_cast<std::byte*>(iov_.iov_base);
        }

        [[nodiscard]] std::byte operator*() const noexcept
        {
            std::cerr << __FILE__ << ':' << __LINE__ << " const deref" << std::endl;
            return *reinterpret_cast<std::byte*>(iov_.iov_base);
        }

        Iterator& operator++() noexcept
        {
            if (iov_.iov_len > 0)
            {
                iov_.iov_base = reinterpret_cast<std::byte*>(iov_.iov_base) + 1;
                --iov_.iov_len;
                ++offset_;
            }
            else
            {
                setOffset(offset_ + 1);
            }
            return *this;
        }

        [[nodiscard]] constexpr bool operator==(Iterator const& that) const noexcept
        {
            return offset_ == that.offset_;
        }

        [[nodiscard]] constexpr bool operator!=(Iterator const& that) const noexcept
        {
            return !(*this == that);
        }

    private:
        void setOffset(size_t offset)
        {
            offset_ = offset;
            auto ptr = evbuffer_ptr{};
            evbuffer_ptr_set(buf_, &ptr, offset, EVBUFFER_PTR_SET);
            evbuffer_peek(buf_, std::numeric_limits<ev_ssize_t>::max(), &ptr, &iov_, 1);
        }

        evbuffer* const buf_;
        Iovec iov_ = {};
        size_t offset_ = 0;
    };

    Buffer() = default;
    Buffer(Buffer const&) = delete;
    Buffer& operator=(Buffer const&) = delete;

    [[nodiscard]] auto size() const noexcept
    {
        return evbuffer_get_length(buf_.get());
    }

    [[nodiscard]] auto empty() const noexcept
    {
        return size() != 0U;
    }

    [[nodiscard]] auto vecs(size_t n_bytes) const
    {
        auto chains = std::vector<Iovec>(evbuffer_peek(buf_.get(), n_bytes, nullptr, nullptr, 0));
        evbuffer_peek(buf_.get(), n_bytes, nullptr, std::data(chains), std::size(chains));
        return chains;
    }

    [[nodiscard]] auto vecs() const
    {
        return vecs(size());
    }

    [[nodiscard]] auto begin() noexcept
    {
        return Iterator(buf_.get(), 0U);
    }

    [[nodiscard]] auto end() noexcept
    {
        return Iterator(buf_.get(), 0U);
    }

    [[nodiscard]] auto cbegin() const noexcept
    {
        return Iterator(buf_.get(), 0U);
    }

    [[nodiscard]] auto cend() const noexcept
    {
        return Iterator(buf_.get(), 0U);
    }

    template<typename T>
    [[nodiscard]] bool startsWith(T const& needle) const
    {
        auto const n_bytes = std::size(needle);
        return n_bytes <= size() && std::equal(std::begin(needle), std::end(needle), cbegin());
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
    ssize_t addSocket(tr_socket_t sockfd, size_t n_bytes, tr_error** error = nullptr)
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

    void add(Buffer&& that)
    {
        evbuffer_add_buffer(buf_.get(), that.buf_.get());
    }

    void add(void const* bytes, size_t n_bytes)
    {
        evbuffer_add(buf_.get(), bytes, n_bytes);
    }

    template<typename T>
    void add(T const& data)
    {
        add(std::data(data), std::size(data));
    }

    void* reserve(size_t n_bytes)
    {
        struct evbuffer_iovec vec
        {
        };
        evbuffer_reserve_space(buf_.get(), static_cast<ev_ssize_t>(n_bytes), &vec, 1);
        return vec.iov_base;
    }

    void commit(size_t n_bytes)
    {
        auto vec = evbuffer_iovec{ reserve(n_bytes), n_bytes };
        evbuffer_commit_space(buf_.get(), &vec, 1);
    }

    void addUint8(uint8_t uch)
    {
        add(&uch, 1);
    }

    void addUint16(uint16_t hs)
    {
        uint16_t const ns = htons(hs);
        add(&ns, sizeof(ns));
    }

    void addHton16(uint16_t hs)
    {
        addUint16(hs);
    }

    void addUint32(uint32_t hl)
    {
        uint32_t const nl = htonl(hl);
        add(&nl, sizeof(nl));
    }

    void addHton32(uint32_t hl)
    {
        addUint32(hl);
    }

    void addUint64(uint64_t hll)
    {
        uint64_t const nll = tr_htonll(hll);
        add(&nll, sizeof(nll));
    }

    void addHton64(uint64_t hll)
    {
        addUint64(hll);
    }

private:
    std::unique_ptr<evbuffer, void (*)(evbuffer*)> const buf_{ evbuffer_new(), evbuffer_free };
};

} // namespace libtransmission
