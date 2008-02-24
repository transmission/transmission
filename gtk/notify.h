/*
 * This file Copyright (C) 2008 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license. 
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 * 
 * $Id:$
 */

#ifndef __TR_NOTIFY_H__
#define __TR_NOTIFY_H__

#include "tr_torrent.h"

void tr_notify_init( void ); 
void tr_notify_send( TrTorrent * tor ); 

#endif
