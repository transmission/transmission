// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <algorithm> // for std::copy_n
#include <cstddef>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>

#include <small/vector.hpp>

#include "libtransmission/error.h"
#include "libtransmission/net.h" // tr_socket_t
#include "libtransmission/tr-assert.h"
#include "libtransmission/utils.h" // for tr_htonll(), tr_ntohll()

namespace libtransmission
{

template<typename value_type>
class BufferReader
{
public:
    virtual ~BufferReader() = default;
    virtual void drain(size_t n_bytes) = 0;
    [[nodiscard]] virtual size_t size() const noexcept = 0;
    [[nodiscard]] virtual value_type const* data() const noexcept = 0;

    [[nodiscard]] auto empty() const noexcept
    {
        return size() == 0;
    }

    [[nodiscard]] auto const* begin() const noexcept
    {
        return data();
    }

    [[nodiscard]] auto const* end() const noexcept
    {
        return begin() + size();
    }

    template<typename T>
    [[nodiscard]] TR_CONSTEXPR20 bool starts_with(T const& needle) const
    {
        auto const n_bytes = std::size(needle);
        auto const needle_begin = reinterpret_cast<value_type const*>(std::data(needle));
        auto const needle_end = needle_begin + n_bytes;
        return n_bytes <= size() && std::equal(needle_begin, needle_end, data());
    }

    [[nodiscard]] auto to_string_view() const
    {
        return std::string_view{ reinterpret_cast<char const*>(data()), size() };
    }

    [[nodiscard]] auto to_string() const
    {
        return std::string{ to_string_view() };
    }

    void to_buf(void* tgt, size_t n_bytes)
    {
        n_bytes = std::min(n_bytes, size());
        std::copy_n(data(), n_bytes, static_cast<value_type*>(tgt));
        drain(n_bytes);
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

    // Returns the number of bytes written. Check `error` for error.
    size_t to_socket(tr_socket_t sockfd, size_t n_bytes, tr_error* error = nullptr)
    {
        n_bytes = std::min(n_bytes, size());

        if (n_bytes == 0U)
        {
            return {};
        }

        if (auto const n_sent = send(sockfd, reinterpret_cast<char const*>(data()), n_bytes, 0); n_sent >= 0U)
        {
            drain(n_sent);
            return n_sent;
        }

        if (error != nullptr)
        {
            auto const err = sockerrno;
            error->set(err, tr_net_strerror(err));
        }

        return {};
    }

    void clear()
    {
        drain(size());
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
        add_uint16_n(htons(hs));
    }

    void add_uint16_n(uint16_t ns)
    {
        add(&ns, sizeof(ns));
    }

    void add_uint32(uint32_t hl)
    {
        add_uint32_n(htonl(hl));
    }

    void add_uint32_n(uint32_t nl)
    {
        add(&nl, sizeof(nl));
    }

    void add_uint64(uint64_t hll)
    {
        add_uint64_n(tr_htonll(hll));
    }

    void add_uint64_n(uint64_t nll)
    {
        add(&nll, sizeof(nll));
    }

    void add_port(tr_port port)
    {
        auto nport = port.network();
        add(&nport, sizeof(nport));
    }

    void add_address(tr_address const& addr)
    {
        switch (addr.type)
        {
        case TR_AF_INET:
            add(&addr.addr.addr4.s_addr, sizeof(addr.addr.addr4.s_addr));
            break;
        case TR_AF_INET6:
            add(&addr.addr.addr6.s6_addr, sizeof(addr.addr.addr6.s6_addr));
            break;
        default:
            TR_ASSERT_MSG(false, "invalid type");
            break;
        }
    }

    size_t add_socket(tr_socket_t sockfd, size_t n_bytes, tr_error* error = nullptr)
    {
        auto const [buf, buflen] = reserve_space(n_bytes);
        n_bytes = std::min(n_bytes, buflen);
        TR_ASSERT(n_bytes > 0U);
        auto const n_read = recv(sockfd, reinterpret_cast<char*>(buf), n_bytes, 0);
        auto const err = sockerrno;

        if (n_read > 0)
        {
            commit_space(n_read);
            return static_cast<size_t>(n_read);
        }

        // When a stream socket peer has performed an orderly shutdown,
        // the return value will be 0 (the traditional "end-of-file" return).
        if (error != nullptr)
        {
            if (n_read == 0)
            {
                error->set_from_errno(ENOTCONN);
            }
            else
            {
                error->set(err, tr_net_strerror(err));
            }
        }

        return {};
    }
};

template<size_t N, typename value_type = std::byte, typename GrowthFactor = std::ratio<3, 2>>
class StackBuffer final
    : public BufferReader<value_type>
    , public BufferWriter<value_type>
{
public:
    StackBuffer() = default;
    StackBuffer(StackBuffer&&) = delete;
    StackBuffer(StackBuffer const&) = delete;
    StackBuffer& operator=(StackBuffer&&) = delete;
    StackBuffer& operator=(StackBuffer const&) = delete;

    template<typename ContiguousContainer>
    explicit StackBuffer(ContiguousContainer const& data)
    {
        BufferWriter<value_type>::add(data);
    }

    [[nodiscard]] size_t size() const noexcept override
    {
        return end_pos_ - begin_pos_;
    }

    [[nodiscard]] value_type const* data() const noexcept override
    {
        return std::data(buf_) + begin_pos_;
    }

    void drain(size_t n_bytes) override
    {
        begin_pos_ += std::min(n_bytes, size());

        if (begin_pos_ == end_pos_) // empty; reuse the buf
        {
            begin_pos_ = end_pos_ = 0U;
        }
    }

    std::pair<value_type*, size_t> reserve_space(size_t n_bytes) override
    {
        if (auto const free_at_end = buf_.size() - end_pos_; free_at_end < n_bytes)
        {
            if (auto const total_free = begin_pos_ + free_at_end; total_free >= n_bytes)
            {
                // move data so that all free space is at the end
                auto const size = this->size();
                std::copy(data(), data() + size, std::data(buf_));
                begin_pos_ = 0;
                end_pos_ = size;
            }
            else // even `total_free` is not enough, so resize
            {
                buf_.resize(end_pos_ + n_bytes);
            }
        }

        return { buf_.data() + end_pos_, n_bytes };
    }

    void commit_space(size_t n_bytes) override
    {
        end_pos_ += n_bytes;
    }

private:
    small::vector<value_type, N, std::allocator<value_type>, std::true_type, size_t, GrowthFactor> buf_ = {};
    size_t begin_pos_ = {};
    size_t end_pos_ = {};
};

} // namespace libtransmission
