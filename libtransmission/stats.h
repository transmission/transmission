/*
 * This file Copyright (C) 2007-2008 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license. 
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id: peer-msgs.h 3818 2007-11-13 05:36:43Z charles $
 */

#ifndef TR_STATS_H
#define TR_STATS_H

void tr_statsInit( tr_handle * handle );

void tr_statsClose( tr_handle * handle );

void tr_statsAddUploaded( tr_handle * handle, uint32_t bytes );

void tr_statsAddDownloaded( tr_handle * handle, uint32_t bytes );

void tr_statsFileCreated( tr_handle * handle );

#endif
