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

#include "error.h"
#include "net.h" // tr_socket_t
#include "utils-ev.h"
#include "utils.h" // for tr_htonll(), tr_ntohll()

namespace libtransmission
{

class Buffer
{
public:
    class Iterator
    {
    public:
        using difference_type = long;
        using value_type = std::byte;
        using pointer = value_type*;
        using reference = value_type&;
        using iterator_category = std::random_access_iterator_tag;

        constexpr Iterator(evbuffer* buf, size_t offset)
            : buf_{ buf }
            , buf_offset_{ offset }
        {
        }

        [[nodiscard]] value_type& operator*() noexcept
        {
            auto& info = iov();
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
            }

            return *iov_;
        }

        mutable std::optional<IovInfo> iov_;

        evbuffer* buf_;
        size_t buf_offset_ = 0;
    };

    Buffer() = default;
    Buffer(Buffer&&) = default;
    Buffer(Buffer const&) = delete;
    Buffer& operator=(Buffer const&) = delete;
    Buffer& operator=(Buffer&&) = default;

    template<typename T>
    explicit Buffer(T const& data)
    {
        add(std::data(data), std::size(data));
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

    template<typename T>
    [[nodiscard]] TR_CONSTEXPR20 bool starts_with(T const& needle) const
    {
        auto const n_bytes = std::size(needle);
        auto const needle_begin = reinterpret_cast<std::byte const*>(std::data(needle));
        auto const needle_end = needle_begin + n_bytes;
        return n_bytes <= size() && std::equal(needle_begin, needle_end, cbegin());
    }

    [[nodiscard]] std::string to_string() const
    {
        auto str = std::string{};
        str.resize(size());
        evbuffer_copyout(buf_.get(), std::data(str), std::size(str));
        return str;
    }

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

    void drain(size_t n_bytes)
    {
        evbuffer_drain(buf_.get(), n_bytes);
    }

    void clear()
    {
        drain(size());
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

    [[nodiscard]] std::pair<std::byte*, size_t> pullup()
    {
        return { reinterpret_cast<std::byte*>(evbuffer_pullup(buf_.get(), -1)), size() };
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

    // Move all data from one buffer into another.
    // This is a destructive add: the source buffer is empty after this call.
    void add(Buffer& that)
    {
        evbuffer_add_buffer(buf_.get(), that.buf_.get());
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

    template<
        typename T,
        typename std::enable_if_t<
            std::is_same_v<T, char> || std::is_same_v<T, unsigned char> || std::is_same_v<T, std::byte>>* = nullptr>
    void push_back(T ch)
    {
        add(&ch, 1);
    }

    void add_port(tr_port const& port)
    {
        auto nport = port.network();
        add(&nport, sizeof(nport));
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
