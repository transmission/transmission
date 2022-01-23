// This file Copyright © 2009-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

class Speed
{
public:
    Speed() = default;

    double getKBps() const;

    [[nodiscard]] int getBps() const
    {
        return bytes_per_second_;
    }

    [[nodiscard]] bool isZero() const
    {
        return bytes_per_second_ == 0;
    }

    static Speed fromKBps(double KBps);

    static Speed fromBps(int Bps)
    {
        return Speed{ Bps };
    }

    void setBps(int Bps)
    {
        bytes_per_second_ = Bps;
    }

    Speed& operator+=(Speed const& that)
    {
        bytes_per_second_ += that.bytes_per_second_;
        return *this;
    }

    [[nodiscard]] Speed operator+(Speed const& that) const
    {
        return Speed{ getBps() + that.getBps() };
    }

    [[nodiscard]] bool operator<(Speed const& that) const
    {
        return getBps() < that.getBps();
    }

    [[nodiscard]] bool operator==(Speed const& that) const
    {
        return getBps() == that.getBps();
    }

    [[nodiscard]] bool operator!=(Speed const& that) const
    {
        return getBps() != that.getBps();
    }

private:
    explicit Speed(int bytes_per_second)
        : bytes_per_second_{ bytes_per_second }
    {
    }

    int bytes_per_second_ = {};
};
