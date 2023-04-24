// This file Copyright © 2022-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstddef> // for std::byte
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <string_view>

#include <sfl/small_vector.hpp>

#include <event2/buffer.h>

#include <fmt/core.h>

#include "error.h"
#include "net.h" // tr_socket_t
#include "tr-assert.h"
#include "utils-ev.h"
#include "utils.h" // for tr_htonll(), tr_ntohll()

namespace libtransmission
{

template<typename value_type>
class BufferReader
{
public:
    virtual ~BufferReader() = default;
    virtual void drain(size_t n_bytes) = 0;
    [[nodiscard]] virtual size_t size() const noexcept = 0;
    [[nodiscard]] virtual value_type const* data() const = 0;

    [[nodiscard]] auto empty() const noexcept
    {
        return size() == 0U;
    }

    [[nodiscard]] auto* begin() noexcept
    {
        return data();
    }

    [[nodiscard]] auto const* begin() const
    {
        return data();
    }

    [[nodiscard]] auto* end()
    {
        return begin() + size();
    }

    [[nodiscard]] auto const* end() const
    {
        return begin() + size();
    }

    [[nodiscard]] auto to_string() const
    {
        return std::string{ reinterpret_cast<char const*>(data()), size() };
    }

    [[nodiscard]] auto to_string_view() const
    {
        return std::string_view{ reinterpret_cast<char const*>(data()), size() };
    }

    template<typename T>
    [[nodiscard]] bool starts_with(T const& needle) const
    {
        auto const n_bytes = std::size(needle);
        auto const needle_begin = reinterpret_cast<value_type const*>(std::data(needle));
        auto const needle_end = needle_begin + n_bytes;
        return n_bytes <= size() && std::equal(needle_begin, needle_end, data());
    }

    auto to_buf(void* tgt, size_t n_bytes)
    {
        n_bytes = std::min(n_bytes, size());
        std::copy_n(data(), n_bytes, reinterpret_cast<value_type*>(tgt));
        drain(n_bytes);
        return n_bytes;
    }

    [[nodiscard]] auto to_uint8()
    {
        auto tmp = uint8_t{};
        to_buf(&tmp, sizeof(tmp));
        return tmp;
    }

    [[nodiscard]] uint16_t to_uint16()
    {
        auto tmp = uint16_t{};
        to_buf(&tmp, sizeof(tmp));
        return ntohs(tmp);
    }

    [[nodiscard]] uint32_t to_uint32()
    {
        auto tmp = uint32_t{};
        to_buf(&tmp, sizeof(tmp));
        return ntohl(tmp);
    }

    [[nodiscard]] uint64_t to_uint64()
    {
        auto tmp = uint64_t{};
        to_buf(&tmp, sizeof(tmp));
        return tr_ntohll(tmp);
    }
};

template<typename value_type>
class BufferWriter
{
public:
    virtual ~BufferWriter() = default;
    virtual std::pair<value_type*, size_t> reserve_space(size_t n_bytes) = 0;
    virtual void commit_space(size_t n_bytes) = 0;

    void add(void const* span_begin, size_t span_len)
    {
        auto [buf, buflen] = reserve_space(span_len);
        std::copy_n(reinterpret_cast<value_type const*>(span_begin), span_len, buf);
        commit_space(span_len);
    }

    template<typename ContiguousContainer>
    void add(ContiguousContainer const& container)
    {
        add(std::data(container), std::size(container));
    }

    template<typename OneByteType>
    void push_back(OneByteType ch)
    {
        add(&ch, 1);
    }

    void add_uint8(uint8_t uch)
    {
        add(&uch, 1);
    }

    void add_uint16(uint16_t hs)
    {
        uint16_t const ns = htons(hs);
        add(&ns, sizeof(ns));
    }

    void add_hton16(uint16_t hs)
    {
        add_uint16(hs);
    }

    void add_uint32(uint32_t hl)
    {
        uint32_t const nl = htonl(hl);
        add(&nl, sizeof(nl));
    }

    void eadd_hton32(uint32_t hl)
    {
        add_uint32(hl);
    }

    void add_uint64(uint64_t hll)
    {
        uint64_t const nll = tr_htonll(hll);
        add(&nll, sizeof(nll));
    }

    void add_hton64(uint64_t hll)
    {
        add_uint64(hll);
    }

    void add_port(tr_port const& port)
    {
        auto nport = port.network();
        add(&nport, sizeof(nport));
    }
};

class Buffer
    : public BufferReader<std::byte>
    , public BufferWriter<std::byte>
{
public:
    using value_type = std::byte;

    Buffer() = default;
    Buffer(Buffer&& that) = default;
    Buffer& operator=(Buffer&& that) = default;

    template<typename T>
    explicit Buffer(T const& data)
    {
        add(data);
    }

    Buffer(Buffer const&) = delete;
    Buffer& operator=(Buffer const&) = delete;

    [[nodiscard]] size_t size() const noexcept override
    {
        return evbuffer_get_length(buf_.get());
    }

    void drain(size_t n_bytes) override
    {
        evbuffer_drain(buf_.get(), n_bytes);
    }

    // Returns the number of bytes written. Check `error` for error.
    size_t to_socket(tr_socket_t sockfd, size_t n_bytes, tr_error** error = nullptr)
    {
        EVUTIL_SET_SOCKET_ERROR(0);
        auto const res = evbuffer_write_atmost(buf_.get(), sockfd, n_bytes);
        auto const err = EVUTIL_SOCKET_ERROR();
        if (res >= 0)
        {
            return static_cast<size_t>(res);
        }
        tr_error_set(error, err, tr_net_strerror(err));
        return 0;
    }

    [[nodiscard]] value_type const* data() const noexcept override
    {
        return reinterpret_cast<value_type*>(evbuffer_pullup(buf_.get(), -1));
    }

    size_t add_socket(tr_socket_t sockfd, size_t n_bytes, tr_error** error = nullptr)
    {
        EVUTIL_SET_SOCKET_ERROR(0);
        auto const res = evbuffer_read(buf_.get(), sockfd, static_cast<int>(n_bytes));
        auto const err = EVUTIL_SOCKET_ERROR();

        if (res > 0)
        {
            return static_cast<size_t>(res);
        }

        if (res == 0)
        {
            tr_error_set_from_errno(error, ENOTCONN);
        }
        else
        {
            tr_error_set(error, err, tr_net_strerror(err));
        }

        return {};
    }

    virtual std::pair<value_type*, size_t> reserve_space(size_t n_bytes) override
    {
        evbuffer_iovec iov = {};
        evbuffer_reserve_space(buf_.get(), n_bytes, &iov, 1);
        return { static_cast<value_type*>(iov.iov_base), static_cast<size_t>(iov.iov_len) };
    }

    virtual void commit_space(size_t n_bytes) override
    {
        evbuffer_iovec iov = {};
        evbuffer_reserve_space(buf_.get(), n_bytes, &iov, 1);
        iov.iov_len = n_bytes;
        evbuffer_commit_space(buf_.get(), &iov, 1);
    }

private:
    evhelpers::evbuffer_unique_ptr buf_{ evbuffer_new() };
};

template<size_t N, typename value_type = std::byte>
class SmallBuffer
    : public BufferReader<value_type>
    , public BufferWriter<value_type>
{
public:
    SmallBuffer() = default;
    SmallBuffer(SmallBuffer&&) = delete;
    SmallBuffer(SmallBuffer const&) = delete;
    SmallBuffer& operator=(SmallBuffer&&) = delete;
    SmallBuffer& operator=(SmallBuffer const&) = delete;

    [[nodiscard]] size_t size() const noexcept override
    {
        return end_pos_ - begin_pos_;
    }

    [[nodiscard]] value_type const* data() const override
    {
        return std::data(buf_) + begin_pos_;
    }

    void drain(size_t n_bytes) override
    {
        begin_pos_ += std::min(n_bytes, size());

        if (begin_pos_ == end_pos_)
        {
            begin_pos_ = end_pos_ = 0U;
        }
    }

    virtual std::pair<value_type*, size_t> reserve_space(size_t n_bytes) override
    {
        buf_.resize(end_pos_ + n_bytes);
        return { &buf_[end_pos_], n_bytes };
    }

    virtual void commit_space(size_t n_bytes) override
    {
        end_pos_ += n_bytes;
    }

private:
    sfl::small_vector<value_type, N> buf_ = {};
    size_t begin_pos_ = {};
    size_t end_pos_ = {};
};

} // namespace libtransmission
