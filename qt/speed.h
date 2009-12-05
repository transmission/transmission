/*
 * This file Copyright (C) 2009 Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#ifndef QTR_SPEED_H
#define QTR_SPEED_H

class Speed
{
    private:
        double _kbps;
        Speed( double kbps ): _kbps(kbps) { }
    public:
        Speed( ): _kbps(0) { }
        double kbps( ) const { return _kbps; }
        double bps( ) const { return kbps()*1024.0; }
        bool isZero( ) const { return _kbps < 0.001; }
        static Speed fromKbps( double kbps ) { return Speed( kbps ); }
        static Speed fromBps( double bps ) { return Speed( bps/1024.0 ); }
        void setKbps( double kbps ) { _kbps = kbps; }
        void setBps( double bps ) { _kbps = bps/1024.0; }
        Speed operator+( const Speed& that ) const { return Speed( kbps() + that.kbps() ); }
        Speed& operator+=( const Speed& that ) { _kbps += that._kbps; return *this; }
        bool operator<( const Speed& that ) const { return kbps() < that.kbps(); }
};

#endif
