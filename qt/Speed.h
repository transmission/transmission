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
    Speed() :
        _Bps(0)
    {
    }

    double KBps() const;

    int Bps() const
    {
        return _Bps;
    }

    bool isZero() const
    {
        return _Bps == 0;
    }

    static Speed fromKBps(double KBps);

    static Speed fromBps(int Bps)
    {
        return Speed(Bps);
    }

    void setBps(int Bps)
    {
        _Bps = Bps;
    }

    Speed& operator +=(Speed const& that)
    {
        _Bps += that._Bps;
        return *this;
    }

    Speed operator +(Speed const& that) const
    {
        return Speed(_Bps + that._Bps);
    }

    bool operator <(Speed const& that) const
    {
        return _Bps < that._Bps;
    }

private:
    Speed(int Bps) :
        _Bps(Bps)
    {
    }

private:
    int _Bps;
};
