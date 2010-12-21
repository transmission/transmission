/*
 * This file Copyright (C) 2008-2010 Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#ifndef GTR_NOTIFY_H
#define GTR_NOTIFY_H

#include "tr-torrent.h"

void gtr_notify_init( void );

void gtr_notify_send( TrTorrent * tor );

void gtr_notify_added( const char * name );

#endif
