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

#ifndef TR_DAEMON_CLIENT_H
#define TR_DAEMON_CLIENT_H

struct event_base;
struct strlist;

struct cl_info
{
    int          id;
    const char * name;
    const char * hash;
    int64_t      size;
};

struct cl_stat
{
    int          id;
    const char * state;
    int64_t      eta;
    int64_t      done;
    int64_t      ratedown;
    int64_t      rateup;
    int64_t      totaldown;
    int64_t      totalup;
    const char * error;
    const char * errmsg;
};

typedef void ( * cl_infofunc )( const struct cl_info * );
typedef void ( * cl_statfunc )( const struct cl_stat * );

int  client_init     ( struct event_base * );
int  client_new_sock ( const char * );
int  client_new_cmd  ( char * const * );
int  client_quit     ( void );
int  client_addfiles ( struct strlist * );
int  client_port     ( int );
int  client_automap  ( int );
int  client_pex      ( int );
int  client_downlimit( int );
int  client_uplimit  ( int );
int  client_dir      ( const char * );
int  client_crypto   ( const char * );
int  client_start    ( size_t, const int * );
int  client_stop     ( size_t, const int * );
int  client_remove   ( size_t, const int * );
int  client_list     ( cl_infofunc );
int  client_info     ( cl_infofunc );
int  client_hashids  ( cl_infofunc );
int  client_status   ( cl_statfunc );

#endif /* TR_DAEMON_CLIENT_H */
