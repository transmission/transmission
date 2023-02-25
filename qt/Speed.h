// This file Copyright © 2009-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

class Speed
{
public:
    Speed() = default;

    double getKBps() const;

    [[nodiscard]] auto constexpr getBps() const noexcept
    {
        return bytes_per_second_;
    }

    [[nodiscard]] auto constexpr isZero() const noexcept
    {
        return bytes_per_second_ == 0;
    }

    static Speed fromKBps(double KBps);

    [[nodiscard]] static constexpr Speed fromBps(int Bps) noexcept
    {
        return Speed{ Bps };
    }

    void constexpr setBps(int Bps) noexcept
    {
        bytes_per_second_ = Bps;
    }

    constexpr Speed& operator+=(Speed const& that) noexcept
    {
        bytes_per_second_ += that.bytes_per_second_;
        return *this;
    }

    [[nodiscard]] auto constexpr operator+(Speed const& that) const noexcept
    {
        return Speed{ getBps() + that.getBps() };
    }

    [[nodiscard]] auto constexpr operator<(Speed const& that) const noexcept
    {
        return getBps() < that.getBps();
    }

    [[nodiscard]] auto constexpr operator==(Speed const& that) const noexcept
    {
        return getBps() == that.getBps();
    }

    [[nodiscard]] auto constexpr operator!=(Speed const& that) const noexcept
    {
        return getBps() != that.getBps();
    }

private:
    explicit constexpr Speed(int bytes_per_second) noexcept
        : bytes_per_second_{ bytes_per_second }
    {
    }

    int bytes_per_second_ = {};
};
