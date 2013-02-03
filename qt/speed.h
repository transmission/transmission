/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
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
    Speed (): _Bps (0) { }
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
