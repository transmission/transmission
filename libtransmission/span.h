/*
* This file Copyright (C) Mnemosyne LLC
*
* It may be used under the GNU GPL versions 2 or 3
* or any future license endorsed by Mnemosyne LLC.
*
*/

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

/// @brief Readonly non-owning view into a provided memory block with start pointer and size.
/// In C++20 this appears in standard library as std::span and remotely similar usage.
template<typename T>
class Span
{
public:
    Span(T const* ptr, size_t size)
        : ptr_{ ptr }
        , size_{ size }
    {
    }

    T const* begin() const
    {
        return this->ptr_;
    }

    T const* end() const
    {
        return this->ptr_ + this->size_;
    }

    [[nodiscard]] size_t size() const
    {
        return size_;
    }

private:
    T const* ptr_;
    size_t size_;
};
