/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2007 Joshua Elsasser
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *****************************************************************************/

#ifndef TR_DAEMON_TORRENTS_H
#define TR_DAEMON_TORRENTS_H

#include <limits.h>

#include <libtransmission/transmission.h>

struct event_base;

void         torrent_init                ( struct event_base * );
int          torrent_add_file            ( const char *, const char *, int );
int          torrent_add_data            ( uint8_t *, size_t, const char *, int );
void         torrent_start               ( int );
void         torrent_stop                ( int );
void         torrent_remove              ( int );
const tr_info  * torrent_info          ( int );
const tr_stat  * torrent_stat          ( int );
int          torrent_lookup              ( const uint8_t * );
void       * torrent_iter                ( void *, int * );

void         torrent_exit                ( int );
void         torrent_set_autostart       ( int );
int          torrent_get_autostart       ( void );
void         torrent_set_port            ( int );
int          torrent_get_port            ( void );
void         torrent_set_pex             ( int );
int          torrent_get_pex             ( void );
void         torrent_enable_port_mapping ( int );
int          torrent_get_port_mapping    ( void );
void         torrent_set_uplimit         ( int );
int          torrent_get_uplimit         ( void );
void         torrent_set_downlimit       ( int );
int          torrent_get_downlimit       ( void );
void         torrent_set_directory       ( const char * );
const char * torrent_get_directory       ( void );

#endif /* TR_DAEMON_TORRENTS_H */
