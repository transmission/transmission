// This file Copyright © 2022-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstddef>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <string_view>

#include <event2/buffer.h>

#include <fmt/core.h>

#include "error.h"
#include "net.h" // tr_socket_t
#include "tr-assert.h"
#include "utils-ev.h"
#include "utils.h" // for tr_htonll(), tr_ntohll()

namespace libtransmission
{

template<typename container_type, typename value_type>
class BufferReader
{
public:
    BufferReader(container_type* in)
        : in_{ in }
    {
        static_assert(sizeof(value_type) == 1);
    }

    [[nodiscard]] auto size() const noexcept
    {
        return std::size(*in_);
    }

    [[nodiscard]] value_type* data() noexcept
    {
        return std::data(*in_);
    }

    [[nodiscard]] value_type const* data() const noexcept
    {
        return std::data(*in_);
    }

    void drain(size_t n)
    {
        in_->drain(std::min(n, std::size(*in_)));
    }

    template<typename T>
    [[nodiscard]]  bool starts_with(T const& needle) const
    {
        auto const n_bytes = std::size(needle);
        auto const needle_begin = reinterpret_cast<value_type const*>(std::data(needle));
        auto const needle_end = needle_begin + n_bytes;
        return n_bytes <= std::size(*in_) && std::equal(needle_begin, needle_end, std::cbegin(*in_));
    }

    auto to_buf(void* tgt, size_t n_bytes)
    {
        n_bytes = std::min(n_bytes, std::size(*in_));
        std::copy_n(std::data(*in_), n_bytes, reinterpret_cast<value_type*>(tgt));
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

private:
    container_type* in_;
};

template<typename T, typename ValueType>
class BufferWriter
{
public:
    BufferWriter(T* out)
        : out_{ out }
    {
        static_assert(sizeof(ValueType) == 1);
    }

    void add(void const* span_begin, size_t span_len)
    {
        auto const* const begin = reinterpret_cast<ValueType const*>(span_begin);
        auto const* const end = begin + span_len;
        out_->insert(std::end(*out_), begin, end);
    }

#if 0
    template<typename container_type>
    void add(BufferReader<container_type, ValueType>& in)
    {
        auto const n_bytes = std::size(in);
        add(std::data(in), n_bytes);
        in.drain(n_bytes);
    }
#endif

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

private:
    T* out_;
};

class Buffer :
    public BufferReader<Buffer, std::byte>,
    public BufferWriter<Buffer, std::byte>
{
public:
    using value_type = std::byte;

    class Iterator
    {
    public:
        using difference_type = long;
        using pointer = value_type*;
        using reference = value_type&;
        using iterator_category = std::random_access_iterator_tag;

        constexpr Iterator(evbuffer* const buf, size_t offset)
            : buf_{ buf }
            , buf_offset_{ offset }
        {
        }

        [[nodiscard]] value_type& operator*() noexcept
        {
            auto& info = iov();
            TR_ASSERT(info.iov.iov_base != nullptr);
            TR_ASSERT(info.offset < info.iov.iov_len);
            return static_cast<value_type*>(info.iov.iov_base)[info.offset];
        }

        [[nodiscard]] value_type operator*() const noexcept
        {
            auto const& info = iov();
            return static_cast<value_type*>(info.iov.iov_base)[info.offset];
        }

        [[nodiscard]] constexpr Iterator operator+(size_t n_bytes)
        {
            return Iterator{ buf_, offset() + n_bytes };
        }

        [[nodiscard]] constexpr Iterator operator-(size_t n_bytes)
        {
            return Iterator{ buf_, offset() - n_bytes };
        }

        [[nodiscard]] constexpr auto operator-(Iterator const& that) const noexcept
        {
            return offset() - that.offset();
        }

        constexpr Iterator& operator++() noexcept
        {
            inc_offset(1U);
            return *this;
        }

        constexpr Iterator& operator+=(size_t n_bytes)
        {
            inc_offset(n_bytes);
            return *this;
        }

        constexpr Iterator& operator--() noexcept
        {
            dec_offset(1);
            return *this;
        }

        [[nodiscard]] constexpr bool operator==(Iterator const& that) const noexcept
        {
            return this->buf_ == that.buf_ && this->offset() == that.offset();
        }

        [[nodiscard]] constexpr bool operator!=(Iterator const& that) const noexcept
        {
            return !(*this == that);
        }

    private:
        struct IovInfo
        {
            evbuffer_iovec iov = {};
            size_t offset = 0;
        };

        [[nodiscard]] constexpr size_t offset() const noexcept
        {
            return buf_offset_;
        }

        constexpr void dec_offset(size_t increment)
        {
            buf_offset_ -= increment;

            if (iov_)
            {
                if (iov_->offset >= increment)
                {
                    iov_->offset -= increment;
                }
                else
                {
                    iov_.reset();
                }
            }
        }

        constexpr void inc_offset(size_t increment)
        {
            buf_offset_ += increment;

            if (iov_)
            {
                if (iov_->offset + increment < iov_->iov.iov_len)
                {
                    iov_->offset += increment;
                }
                else
                {
                    iov_.reset();
                }
            }
        }

        [[nodiscard]] IovInfo& iov() const noexcept
        {
            if (!iov_)
            {
                auto ptr = evbuffer_ptr{};
                auto iov = IovInfo{};
                evbuffer_ptr_set(buf_, &ptr, buf_offset_, EVBUFFER_PTR_SET);
                evbuffer_peek(buf_, std::numeric_limits<ev_ssize_t>::max(), &ptr, &iov.iov, 1);
                iov.offset = 0;
                iov_ = iov;

                fmt::print("{:s}:{:d} buffer {:p} buf_offset_ {:d} iov.ptr {:p} iov.len {:d}\n", __FILE__, __LINE__, fmt::ptr(buf_), buf_offset_, fmt::ptr(iov.iov.iov_base), iov.iov.iov_len);
            }

            return *iov_;
        }

        mutable std::optional<IovInfo> iov_;

        evbuffer* buf_;
        size_t buf_offset_ = 0;
    };

    Buffer()
        : BufferReader<Buffer, value_type>{ this }
        , BufferWriter<Buffer, value_type>{ this }
    {
    }

    Buffer(Buffer&& that)
        : BufferReader<Buffer, value_type>{ this }
        , BufferWriter<Buffer, value_type>{ this }
        , buf_{ std::move(that.buf_) }
    {
    }

    Buffer& operator=(Buffer&& that)
    {
        buf_ = std::move(that.buf_);
        return *this;
    }

    Buffer(Buffer const&) = delete;
    Buffer& operator=(Buffer const&) = delete;

    template<typename T>
    explicit Buffer(T const& data)
        : BufferReader<Buffer, value_type>{ this }
        , BufferWriter<Buffer, value_type>{ this }
    {
        add(data);
    }

    [[nodiscard]] auto size() const noexcept
    {
        return evbuffer_get_length(buf_.get());
    }

    [[nodiscard]] auto empty() const noexcept
    {
        return evbuffer_get_length(buf_.get()) == 0;
    }

    [[nodiscard]] auto begin() noexcept
    {
        fmt::print("{:s}:{:d} begin this {:p} evbuffer {:p}\n", __FILE__, __LINE__, fmt::ptr(this), fmt::ptr(buf_.get()));
        return Iterator{ buf_.get(), 0U };
    }

    [[nodiscard]] auto end() noexcept
    {
        return Iterator{ buf_.get(), size() };
    }

    [[nodiscard]] auto begin() const noexcept
    {
        return Iterator{ buf_.get(), 0U };
    }

    [[nodiscard]] auto end() const noexcept
    {
        return Iterator{ buf_.get(), size() };
    }

    [[nodiscard]] std::string to_string() const
    {
        auto str = std::string{};
        str.resize(size());
        evbuffer_copyout(buf_.get(), std::data(str), std::size(str));
        return str;
    }

#if 0
    auto to_buf(void* tgt, size_t n_bytes)
    {
        return evbuffer_remove(buf_.get(), tgt, n_bytes);
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
#endif

    void clear()
    {
        drain(size());
    }

    void drain(size_t n_bytes)
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

    [[nodiscard]] std::pair<value_type*, size_t> pullup()
    {
        return { reinterpret_cast<value_type*>(evbuffer_pullup(buf_.get(), -1)), size() };
    }

    [[nodiscard]] value_type* data()
    {
        return reinterpret_cast<value_type*>(evbuffer_pullup(buf_.get(), -1));
    }

    [[nodiscard]] value_type const* data() const
    {
        return reinterpret_cast<value_type*>(evbuffer_pullup(buf_.get(), -1));
    }

    [[nodiscard]] auto pullup_sv()
    {
        auto const [buf, buflen] = pullup();
        return std::string_view{ reinterpret_cast<char const*>(buf), buflen };
    }

    void reserve(size_t n_bytes)
    {
        evbuffer_expand(buf_.get(), n_bytes - size());
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

    template<typename T>
    void insert([[maybe_unused]] Iterator iter, T const* const begin, T const* const end)
    {
        TR_ASSERT(iter == this->end()); // tr_buffer only supports appending
        evbuffer_add(buf_.get(), begin, end - begin);
    }

private:
    evhelpers::evbuffer_unique_ptr buf_{ evbuffer_new() };

    [[nodiscard]] Iterator cbegin() const noexcept
    {
        return Iterator{ buf_.get(), 0U };
    }

    [[nodiscard]] Iterator cend() const noexcept
    {
        return Iterator{ buf_.get(), size() };
    }
};

} // namespace libtransmission

namespace std {
    template<>
    struct iterator_traits<libtransmission::Buffer::Iterator> {
        using difference_type = libtransmission::Buffer::Iterator::difference_type;
        using iterator_category = libtransmission::Buffer::Iterator::iterator_category;
        using value_type = libtransmission::Buffer::value_type;
        using pointer = value_type*;
        using reference = value_type&;
    };
}
