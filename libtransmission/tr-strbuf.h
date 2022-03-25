// This file Copyright © 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <string_view>

#include <fmt/format.h>

/**
 * A memory buffer which uses a builtin array of N bytes,
 * but falls back to heap allocation when necessary.
 * Useful for building temp strings without heap allocation.
 *
 * `fmt::basic_memory_buffer` is final, so aggregate instead
 * of subclassing ¯\_(ツ)_/¯
 */
template<typename T, size_t N>
class tr_strbuf
{
private:
    fmt::basic_memory_buffer<T, N> buffer_;

public:
    using value_type = T;
    using const_reference = const T&;

    tr_strbuf() = default;

    auto& operator=(tr_strbuf&& other)
    {
        buffer_ = std::move(other.buffer_);
        return *this;
    }

    template<typename... Args>
    tr_strbuf(Args const&... args)
    {
        append(args...);
    }

    [[nodiscard]] constexpr auto begin()
    {
        return buffer_.begin();
    }

    [[nodiscard]] constexpr auto end()
    {
        return buffer_.end();
    }

    [[nodiscard]] constexpr auto begin() const
    {
        return buffer_.begin();
    }

    [[nodiscard]] constexpr auto end() const
    {
        return buffer_.end();
    }

    [[nodiscard]] constexpr auto& operator[](size_t pos)
    {
        return buffer_[pos];
    }

    [[nodiscard]] constexpr auto const& operator[](size_t pos) const
    {
        return buffer_[pos];
    }

    [[nodiscard]] constexpr auto size() const
    {
        return buffer_.size();
    }

    [[nodiscard]] constexpr bool empty() const
    {
        return size() == 0;
    }

    [[nodiscard]] constexpr auto* data()
    {
        return buffer_.data();
    }

    [[nodiscard]] constexpr auto const* data() const
    {
        return buffer_.data();
    }

    [[nodiscard]] constexpr auto const* c_str() const
    {
        return data();
    }

    [[nodiscard]] constexpr auto sv() const
    {
        return std::basic_string_view<T>{ data(), size() };
    }

    ///

    [[nodiscard]] constexpr bool ends_with(T const& x) const
    {
        auto const n = size();
        return n != 0 && data()[n - 1] == x;
    }

    template<typename ContiguousRange>
    [[nodiscard]] constexpr bool ends_with(ContiguousRange const& x) const
    {
        auto const x_len = std::size(x);
        auto const len = size();
        return len >= x_len && this->sv().substr(len - x_len) == x;
    }

    [[nodiscard]] constexpr bool ends_with(T const* x) const
    {
        return x != nullptr && ends_with(std::basic_string_view<T>(x));
    }

    ///

    [[nodiscard]] constexpr bool starts_with(T const& x) const
    {
        return !empty() && *data() == x;
    }

    template<typename ContiguousRange>
    [[nodiscard]] constexpr bool starts_with(ContiguousRange const& x) const
    {
        auto const x_len = std::size(x);
        return size() >= x_len && this->sv().substr(0, x_len) == x;
    }

    [[nodiscard]] constexpr bool starts_with(T const* x) const
    {
        return x != nullptr && starts_with(std::basic_string_view<T>(x));
    }

    ///

    void clear()
    {
        buffer_.clear();
        ensure_sz();
    }

    void resize(size_t n)
    {
        buffer_.resize(n);
        ensure_sz();
    }

    ///

    void append(T const& value)
    {
        buffer_.push_back(value);
        ensure_sz();
    }

    template<typename ContiguousRange>
    void append(ContiguousRange const& args)
    {
        buffer_.append(std::data(args), std::data(args) + std::size(args));
        ensure_sz();
    }

    void append(T const* sz_value)
    {
        if (sz_value != nullptr)
        {
            append(std::basic_string_view<T>{ sz_value });
        }
    }

    template<typename... Args>
    void append(Args const&... args)
    {
        (append(args), ...);
    }

    template<typename Arg>
    auto& operator+=(Arg const& arg)
    {
        append(arg);
        return *this;
    }

    ///

    template<typename... Args>
    void assign(Args const&... args)
    {
        clear();
        append(args...);
    }

    template<typename Arg>
    auto& operator=(Arg const& arg)
    {
        assign(arg);
        return *this;
    }

    ///

    template<typename... Args>
    void join(T delim, Args const&... args)
    {
        ((append(args), append(delim)), ...);
        resize(size() - 1);
    }

    template<typename ContiguousRange, typename... Args>
    void join(ContiguousRange const& delim, Args const&... args)
    {
        ((append(args), append(delim)), ...);
        resize(size() - std::size(delim));
    }

    template<typename... Args>
    void join(T const* sz_delim, Args const&... args)
    {
        join(std::basic_string_view<T>{ sz_delim }, args...);
    }

private:
    /**
     * Ensure that the buffer's string is zero-terminated, e.g. for
     * external APIs that require char* strings.
     *
     * Note that the added trailing '\0' does not increment size().
     * This is to ensure that strlen(buf.c_str()) == buf.size().
     */
    void ensure_sz()
    {
        auto const n = size();
        buffer_.reserve(n + 1);
        buffer_[n] = '\0';
    }
};

/**
 * Good for building short-term URLs.
 * The initial size is big enough to avoid heap allocs in most cases,
 * but that also makes it a poor choice for longer-term storage.
 * https://stackoverflow.com/a/417184
 */
using tr_urlbuf = tr_strbuf<char, 2000>;

/**
 * Good for building short-term filenames.
 * The initial size is big enough to avoid heap allocs in most cases,
 * but that also makes it a poor choice for longer-term storage.
 * https://stackoverflow.com/a/65174437
 */
using tr_pathbuf = tr_strbuf<char, 4096>;
