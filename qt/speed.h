/*
 * This file Copyright (C) 2009-2014 Mnemosyne LLC
 *
 * It may be used under the GNU Public License v2 or v3 licenses,
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef QTR_SPEED_H
#define QTR_SPEED_H

#include "formatter.h"

class Speed
{
  private:
    int _Bps;
    Speed (int Bps): _Bps (Bps) {}

  public:
    Speed (): _Bps (0) {}
    double KBps () const;
    int Bps () const { return _Bps; }
    bool isZero () const { return _Bps == 0; }
    static Speed fromKBps (double KBps);
    static Speed fromBps (int Bps) { return Speed (Bps); }
    void setBps (int Bps) { _Bps = Bps; }
    Speed& operator+= (const Speed& that) { _Bps += that._Bps; return *this; }
    Speed operator+ (const Speed& that) const { return Speed (_Bps + that._Bps); }
    bool operator< (const Speed& that) const { return _Bps < that._Bps; }
};

#endif
