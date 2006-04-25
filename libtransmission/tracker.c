/******************************************************************************
 * Copyright (c) 2005 Eric Petit
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

#include "transmission.h"

struct tr_tracker_s
{
    tr_torrent_t * tor;

    char         * id;

    char           started;
    char           completed;
    char           stopped;

    int            interval;
    int            seeders;
    int            leechers;
    int            hasManyPeers;

    uint64_t       dateTry;
    uint64_t       dateOk;

#define TC_STATUS_IDLE    1
#define TC_STATUS_RESOLVE 2
#define TC_STATUS_CONNECT 4
#define TC_STATUS_RECV    8
    char           status;

#define TC_ATTEMPT_NOREACH 1
#define TC_ATTEMPT_ERROR   2
#define TC_ATTEMPT_OK      4
    char           lastAttempt;

    tr_resolve_t * resolve;
    int            socket;
    uint8_t      * buf;
    int            size;
    int            pos;

    int            bindPort;
    int            newPort;

    uint64_t       download;
    uint64_t       upload;
};

static void sendQuery  ( tr_tracker_t * tc );
static void recvAnswer ( tr_tracker_t * tc );

tr_tracker_t * tr_trackerInit( tr_torrent_t * tor )
{
    tr_tracker_t * tc;

    tc           = calloc( 1, sizeof( tr_tracker_t ) );
    tc->tor      = tor;
    tc->id       = tor->id;

    tc->started  = 1;

    tc->interval = 300;
    tc->seeders  = -1;
    tc->leechers = -1;

    tc->status   = TC_STATUS_IDLE;
    tc->lastAttempt = TC_ATTEMPT_NOREACH;
    tc->size     = 1024;
    tc->buf      = malloc( tc->size );

    tc->bindPort = *(tor->bindPort);
    tc->newPort  = -1;

    tc->download = tor->downloaded;
    tc->upload   = tor->uploaded;

    return tc;
}

static int shouldConnect( tr_tracker_t * tc )
{
    uint64_t now = tr_date();

    /* Unreachable tracker, try 10 seconds before trying again */
    if( tc->lastAttempt == TC_ATTEMPT_NOREACH &&
        now < tc->dateTry + 10000 )
    {
        return 0;
    }

    /* The tracker rejected us (like 4XX code, unauthorized IP...),
       don't hammer it - we'll probably get the same answer next time
       anyway */
    if( tc->lastAttempt == TC_ATTEMPT_ERROR &&
        now < tc->dateTry + 1000 * tc->interval )
    {
        return 0;
    }

    /* Do we need to send an event? */
    if( tc->started || tc->completed || tc->stopped || 0 < tc->newPort )
    {
        return 1;
    }

    /* Should we try and get more peers? */
    if( now > tc->dateOk + 1000 * tc->interval )
    {
        return 1;
    }

    /* If there is quite a lot of people on this torrent, stress
       the tracker a bit until we get a decent number of peers */
    if( tc->hasManyPeers )
    {
        if( tc->tor->peerCount < 5 && now > tc->dateOk + 10000 )
        {
            return 1;
        }
        if( tc->tor->peerCount < 10 && now > tc->dateOk + 20000 )
        {
            return 1;
        }
        if( tc->tor->peerCount < 15 && now > tc->dateOk + 30000 )
        {
            return 1;
        }
    }

    return 0;
}

void tr_trackerChangePort( tr_tracker_t * tc, int port )
{
    tc->newPort = port;
}

int tr_trackerPulse( tr_tracker_t * tc )
{
    tr_torrent_t * tor = tc->tor;
    tr_info_t    * inf = &tor->info;
    uint64_t       now = tr_date();

    if( ( tc->status & TC_STATUS_IDLE ) && shouldConnect( tc ) )
    {
        tc->resolve = tr_netResolveInit( inf->trackerAddress );

        tr_inf( "Tracker: connecting to %s:%d (%s)",
                inf->trackerAddress, inf->trackerPort,
                tc->started ? "sending 'started'" :
                ( tc->completed ? "sending 'completed'" :
                  ( tc->stopped ? "sending 'stopped'" :
                    ( 0 < tc->newPort ? "sending 'stopped' to change port" :
                      "getting peers" ) ) ) );

        tc->status  = TC_STATUS_RESOLVE;
        tc->dateTry = tr_date();
    }

    if( tc->status & TC_STATUS_RESOLVE )
    {
        int ret;
        struct in_addr addr;

        ret = tr_netResolvePulse( tc->resolve, &addr );
        if( ret == TR_RESOLVE_WAIT )
        {
            return 0;
        }
        else
        {
            tr_netResolveClose( tc->resolve );
        }
        
        if( ret == TR_RESOLVE_ERROR )
        {
            tc->status = TC_STATUS_IDLE;
            return 0;
        }

        if( tr_fdSocketWillCreate( tor->fdlimit, 1 ) )
        {
            tc->status = TC_STATUS_IDLE;
            return 0;
        }

        tc->socket = tr_netOpen( addr, htons( inf->trackerPort ) );
        if( tc->socket < 0 )
        {
            tr_fdSocketClosed( tor->fdlimit, 1 );
            tc->status = TC_STATUS_IDLE;
            return 0;
        }

        tc->status = TC_STATUS_CONNECT;
    }

    if( tc->status & TC_STATUS_CONNECT )
    {
        /* We are connecting to the tracker. Try to send the query */
        sendQuery( tc );
    }

    if( tc->status & TC_STATUS_RECV )
    {
        /* Try to get something */
        recvAnswer( tc );
    }

    if( tc->status > TC_STATUS_IDLE && now > tc->dateTry + 60000 )
    {
        /* Give up if the request wasn't successful within 60 seconds */
        tr_inf( "Tracker: timeout reached (60 s)" );

        tr_netClose( tc->socket );
        tr_fdSocketClosed( tor->fdlimit, 1 );

        tc->status  = TC_STATUS_IDLE;
        tc->dateTry = tr_date();
    }

    return 0;
}

void tr_trackerCompleted( tr_tracker_t * tc )
{
    tc->started   = 0;
    tc->completed = 1;
    tc->stopped   = 0;
}

void tr_trackerStopped( tr_tracker_t * tc )
{
    tr_torrent_t * tor = tc->tor;

    if( tc->status > TC_STATUS_CONNECT )
    {
        /* If we are already sendy a query at the moment, we need to
           reconnect */
        tr_netClose( tc->socket );
        tr_fdSocketClosed( tor->fdlimit, 1 );
        tc->status = TC_STATUS_IDLE;
    }

    tc->started   = 0;
    tc->completed = 0;
    tc->stopped   = 1;

    /* Even if we have connected recently, reconnect right now */
    if( tc->status & TC_STATUS_IDLE )
    {
        tc->dateTry = 0;
    }
}

void tr_trackerClose( tr_tracker_t * tc )
{
    tr_torrent_t * tor = tc->tor;

    if( tc->status == TC_STATUS_RESOLVE )
    {
        tr_netResolveClose( tc->resolve );
    }
    else if( tc->status > TC_STATUS_RESOLVE )
    {
        tr_netClose( tc->socket );
        tr_fdSocketClosed( tor->fdlimit, 1 );
    }
    free( tc->buf );
    free( tc );
}

static void sendQuery( tr_tracker_t * tc )
{
    tr_torrent_t * tor = tc->tor;
    tr_info_t    * inf = &tor->info;

    char     * event;
    uint64_t   left;
    int        ret;
    uint64_t   down;
    uint64_t   up;

    down = tor->downloaded - tc->download;
    up = tor->uploaded - tc->upload;
    if( tc->started )
    {
        event = "&event=started";
        down = up = 0;
        
        if( 0 < tc->newPort )
        {
            tc->bindPort = tc->newPort;
            tc->newPort = -1;
        }
    }
    else if( tc->completed )
    {
        event = "&event=completed";
    }
    else if( tc->stopped || 0 < tc->newPort )
    {
        event = "&event=stopped";
    }
    else
    {
        event = "";
    }

    left = tr_cpLeftBytes( tor->completion );

    ret = snprintf( (char *) tc->buf, tc->size,
            "GET %s?"
            "info_hash=%s&"
            "peer_id=%s&"
            "port=%d&"
            "uploaded=%lld&"
            "downloaded=%lld&"
            "left=%lld&"
            "compact=1&"
            "numwant=50&"
            "key=%s"
            "%s "
            "HTTP/1.1\r\n"
            "Host: %s\r\n"
            "User-Agent: Transmission/%d.%d\r\n"
            "Connection: close\r\n\r\n",
            inf->trackerAnnounce, tor->hashString, tc->id,
            tc->bindPort, up, down,
            left, tor->key, event, inf->trackerAddress,
            VERSION_MAJOR, VERSION_MINOR );

    ret = tr_netSend( tc->socket, tc->buf, ret );
    if( ret & TR_NET_CLOSE )
    {
        tr_inf( "Tracker: connection failed" );
        tr_netClose( tc->socket );
        tr_fdSocketClosed( tor->fdlimit, 1 );
        tc->status  = TC_STATUS_IDLE;
        tc->dateTry = tr_date();
    }
    else if( !( ret & TR_NET_BLOCK ) )
    {
        // printf( "Tracker: sent %s", tc->buf );
        tc->status = TC_STATUS_RECV;
        tc->pos    = 0;
    }
}

static void recvAnswer( tr_tracker_t * tc )
{
    tr_torrent_t * tor = tc->tor;
    int ret;
    int i;
    benc_val_t   beAll;
    benc_val_t * bePeers, * beFoo;
    uint8_t * body;
    int bodylen;

    if( tc->pos == tc->size )
    {
        tc->size *= 2;
        tc->buf   = realloc( tc->buf, tc->size );
    }
    
    ret = tr_netRecv( tc->socket, &tc->buf[tc->pos],
                    tc->size - tc->pos );

    if( ret & TR_NET_BLOCK )
    {
        return;
    }
    if( !( ret & TR_NET_CLOSE ) )
    {
        // printf( "got %d bytes\n", ret );
        tc->pos += ret;
        return;
    }

    tr_netClose( tc->socket );
    tr_fdSocketClosed( tor->fdlimit, 1 );
    // printf( "connection closed, got total %d bytes\n", tc->pos );

    tc->status  = TC_STATUS_IDLE;
    tc->dateTry = tr_date();

    if( tc->pos < 12 || ( 0 != memcmp( tc->buf, "HTTP/1.0 ", 9 ) &&
                          0 != memcmp( tc->buf, "HTTP/1.1 ", 9 ) ) )
    {
        /* We don't have a complete HTTP status line */
        tr_inf( "Tracker: incomplete HTTP status line" );
        tc->lastAttempt = TC_ATTEMPT_NOREACH;
        return;
    }

    if( '2' != tc->buf[9] )
    {
        /* we didn't get a 2xx status code */
        tr_err( "Tracker: invalid HTTP status code: %c%c%c",
                tc->buf[9], tc->buf[10], tc->buf[11] );
        tc->lastAttempt = TC_ATTEMPT_ERROR;
        return;
    }

    /* find the end of the http headers */
    body = tr_memmem( tc->buf, tc->pos, "\015\012\015\012", 4 );
    if( NULL != body )
    {
        body += 4;
    }
    /* hooray for trackers that violate the HTTP spec */
    else if( NULL != ( body = tr_memmem( tc->buf, tc->pos, "\015\015", 2 ) ) ||
             NULL != ( body = tr_memmem( tc->buf, tc->pos, "\012\012", 2 ) ) )
    {
        body += 2;
    }
    else
    {
        tr_err( "Tracker: could not find end of HTTP headers" );
        tc->lastAttempt = TC_ATTEMPT_NOREACH;
        return;
    }
    bodylen = tc->pos - (body - tc->buf);

    /* Find the beginning of the dictionary */
    for( i = 0; i < bodylen; i++ )
    {
        if( body[i] == 'd' )
        {
            /* This must be it */
            break;
        }
    }

    if( i >= bodylen )
    {
        if( tc->stopped || 0 < tc->newPort )
        {
            tc->lastAttempt = TC_ATTEMPT_OK;
            goto nodict;
        }
        tr_err( "Tracker: no dictionary in answer" );
        tc->lastAttempt = TC_ATTEMPT_ERROR;
        return;
    }

    if( tr_bencLoad( &body[i], bodylen - i, &beAll, NULL ) )
    {
        tr_err( "Tracker: error parsing bencoded data" );
        tc->lastAttempt = TC_ATTEMPT_ERROR;
        return;
    }

    // tr_bencPrint( &beAll );

    if( ( bePeers = tr_bencDictFind( &beAll, "failure reason" ) ) )
    {
        tr_err( "Tracker: %s", bePeers->val.s.s );
        tor->error |= TR_ETRACKER;
        snprintf( tor->trackerError, sizeof( tor->trackerError ),
                  "%s", bePeers->val.s.s );
        tc->lastAttempt = TC_ATTEMPT_ERROR;
        goto cleanup;
    }

    tor->error &= ~TR_ETRACKER;
    tc->lastAttempt = TC_ATTEMPT_OK;

    if( !tc->interval )
    {
        /* Get the tracker interval, ignore it if it is not between
           10 sec and 5 mins */
        if( !( beFoo = tr_bencDictFind( &beAll, "interval" ) ) ||
            !( beFoo->type & TYPE_INT ) )
        {
            tr_err( "Tracker: no 'interval' field" );
            goto cleanup;
        }

        tc->interval = beFoo->val.i;
        tc->interval = MIN( tc->interval, 300 );
        tc->interval = MAX( 10, tc->interval );

        tr_inf( "Tracker: interval = %d seconds", tc->interval );
    }

    if( ( beFoo = tr_bencDictFind( &beAll, "complete" ) ) &&
        ( beFoo->type & TYPE_INT ) )
    {
        tc->seeders = beFoo->val.i;
    }
    if( ( beFoo = tr_bencDictFind( &beAll, "incomplete" ) ) &&
        ( beFoo->type & TYPE_INT ) )
    {
        tc->leechers = beFoo->val.i;
    }
    if( tc->seeders + tc->leechers >= 50 )
    {
        tc->hasManyPeers = 1;
    }

    if( !( bePeers = tr_bencDictFind( &beAll, "peers" ) ) )
    {
        if( tc->stopped || 0 < tc->newPort )
        {
            goto nodict;
        }
        tr_err( "Tracker: no \"peers\" field" );
        goto cleanup;
    }

    if( bePeers->type & TYPE_LIST )
    {
        char * ip;
        int    port;

        /* Original protocol */
        tr_inf( "Tracker: got %d peers", bePeers->val.l.count );

        for( i = 0; i < bePeers->val.l.count; i++ )
        {
            beFoo = tr_bencDictFind( &bePeers->val.l.vals[i], "ip" );
            if( !beFoo )
                continue;
            ip = beFoo->val.s.s;
            beFoo = tr_bencDictFind( &bePeers->val.l.vals[i], "port" );
            if( !beFoo )
                continue;
            port = beFoo->val.i;

            tr_peerAddOld( tor, ip, port );
        }

        if( bePeers->val.l.count >= 50 )
        {
            tc->hasManyPeers = 1;
        }
    }
    else if( bePeers->type & TYPE_STR )
    {
        struct in_addr addr;
        in_port_t      port;

        /* "Compact" extension */
        if( bePeers->val.s.i % 6 )
        {
            tr_err( "Tracker: \"peers\" of size %d",
                    bePeers->val.s.i );
            tr_lockUnlock( &tor->lock );
            goto cleanup;
        }

        tr_inf( "Tracker: got %d peers", bePeers->val.s.i / 6 );
        for( i = 0; i < bePeers->val.s.i / 6; i++ )
        {
            memcpy( &addr, &bePeers->val.s.s[6*i],   4 );
            memcpy( &port, &bePeers->val.s.s[6*i+4], 2 );

            tr_peerAddCompact( tor, addr, port );
        }

        if( bePeers->val.s.i / 6 >= 50 )
        {
            tc->hasManyPeers = 1;
        }
    }

nodict:
    /* Success */
    tc->started   = 0;
    tc->completed = 0;
    tc->dateOk    = tr_date();

    if( tc->stopped )
    {
        tor->status = TR_STATUS_STOPPED;
        tc->stopped = 0;
    }
    else if( 0 < tc->newPort )
    {
        tc->started  = 1;
        tc->download = tor->downloaded;
        tc->upload   = tor->uploaded;
    }

cleanup:
    tr_bencFree( &beAll );
}

int tr_trackerScrape( tr_torrent_t * tor, int * seeders, int * leechers )
{
    tr_info_t * inf = &tor->info;

    int s, i, ret;
    uint8_t buf[1024];
    benc_val_t scrape, * val1, * val2;
    struct in_addr addr;
    uint64_t date;
    int pos, len;

    if( !tor->scrape[0] )
    {
        /* scrape not supported */
        return 1;
    }

    if( tr_netResolve( inf->trackerAddress, &addr ) )
    {
        return 0;
    }
    s = tr_netOpen( addr, htons( inf->trackerPort ) );
    if( s < 0 )
    {
        return 1;
    }

    len = snprintf( (char *) buf, sizeof( buf ),
              "GET %s?info_hash=%s HTTP/1.1\r\n"
              "Host: %s\r\n"
              "Connection: close\r\n\r\n",
              tor->scrape, tor->hashString,
              inf->trackerAddress );

    for( date = tr_date();; )
    {
        ret = tr_netSend( s, buf, len );
        if( ret & TR_NET_CLOSE )
        {
            fprintf( stderr, "Could not connect to tracker\n" );
            tr_netClose( s );
            return 1;
        }
        else if( ret & TR_NET_BLOCK )
        {
            if( tr_date() > date + 10000 )
            {
                fprintf( stderr, "Could not connect to tracker\n" );
                tr_netClose( s );
                return 1;
            }
        }
        else
        {
            break;
        }
        tr_wait( 10 );
    }

    pos = 0;
    for( date = tr_date();; )
    {
        ret = tr_netRecv( s, &buf[pos], sizeof( buf ) - pos );
        if( ret & TR_NET_CLOSE )
        {
            break;
        }
        else if( ret & TR_NET_BLOCK )
        {
            if( tr_date() > date + 10000 )
            {
                fprintf( stderr, "Could not read from tracker\n" );
                tr_netClose( s );
                return 1;
            }
        }
        else
        {
            pos += ret;
        }
        tr_wait( 10 );
    }

    if( pos < 1 )
    {
        fprintf( stderr, "Could not read from tracker\n" );
        tr_netClose( s );
        return 1;
    }

    for( i = 0; i < pos - 8; i++ )
    {
        if( !memcmp( &buf[i], "d5:files", 8 ) )
        {
            break;
        }
    }
    if( i >= pos - 8 )
    {
        return 1;
    }
    if( tr_bencLoad( &buf[i], pos - i, &scrape, NULL ) )
    {
        return 1;
    }

    val1 = tr_bencDictFind( &scrape, "files" );
    if( !val1 )
    {
        return 1;
    }
    val1 = &val1->val.l.vals[1];
    if( !val1 )
    {
        return 1;
    }
    val2 = tr_bencDictFind( val1, "complete" );
    if( !val2 )
    {
        return 1;
    }
    *seeders = val2->val.i;
    val2 = tr_bencDictFind( val1, "incomplete" );
    if( !val2 )
    {
        return 1;
    }
    *leechers = val2->val.i;
    tr_bencFree( &scrape );

    return 0;
}

int tr_trackerSeeders( tr_tracker_t * tc )
{
    if( !tc )
    {
        return -1;
    }
    return tc->seeders;
}

int tr_trackerLeechers( tr_tracker_t * tc )
{
    if( !tc )
    {
        return -1;
    }
    return tc->leechers;
}
