/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

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

    Speed& operator +=(Speed const& that)
    {
        bytes_per_second_ += that.bytes_per_second_;
        return *this;
    }

    [[nodiscard]] Speed operator +(Speed const& that) const
    {
        return Speed{ getBps() + that.getBps() };
    }

    [[nodiscard]] bool operator <(Speed const& that) const
    {
        return getBps() < that.getBps();
    }

    [[nodiscard]] bool operator ==(Speed const& that) const
    {
        return getBps() == that.getBps();
    }

    [[nodiscard]] bool operator !=(Speed const& that) const
    {
        return getBps() != that.getBps();
    }

private:
    explicit Speed(int bytes_per_second) :
        bytes_per_second_{bytes_per_second}
    {
    }

private:
    int bytes_per_second_ = {};
};
