/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2006 Transmission authors and contributors
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

#define TC_ATTEMPT_NOREACH 1
#define TC_ATTEMPT_ERROR   2
#define TC_ATTEMPT_OK      4
    char           lastAttempt;

    tr_http_t    * http;

    int            bindPort;
    int            newPort;

    uint64_t       download;
    uint64_t       upload;
};

static tr_http_t * getQuery   ( tr_tracker_t * tc );
static void        readAnswer ( tr_tracker_t * tc, const char *, int );

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

    tc->lastAttempt = TC_ATTEMPT_NOREACH;

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
    const char   * data;
    int            len;

    if( ( NULL == tc->http ) && shouldConnect( tc ) )
    {
        if( tr_fdSocketWillCreate( tor->fdlimit, 1 ) )
        {
            return 0;
        }
        tc->dateTry = tr_date();
        tc->http = getQuery( tc );

        tr_inf( "Tracker: connecting to %s:%d (%s)",
                inf->trackerAddress, inf->trackerPort,
                tc->started ? "sending 'started'" :
                ( tc->completed ? "sending 'completed'" :
                  ( tc->stopped ? "sending 'stopped'" :
                    ( 0 < tc->newPort ? "sending 'stopped' to change port" :
                      "getting peers" ) ) ) );
    }

    if( NULL != tc->http )
    {
        switch( tr_httpPulse( tc->http, &data, &len ) )
        {
            case TR_WAIT:
                return 0;

            case TR_ERROR:
                tr_httpClose( tc->http );
                tr_fdSocketClosed( tor->fdlimit, 1 );
                tc->http    = NULL;
                tc->dateTry = tr_date();
                return 0;

            case TR_OK:
                readAnswer( tc, data, len );
                tr_httpClose( tc->http );
                tc->http = NULL;
                tr_fdSocketClosed( tor->fdlimit, 1 );
                break;
        }
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

    if( NULL != tc->http )
    {
        /* If we are already sendy a query at the moment, we need to
           reconnect */
        tr_httpClose( tc->http );
        tc->http = NULL;
        tr_fdSocketClosed( tor->fdlimit, 1 );
    }

    tc->started   = 0;
    tc->completed = 0;
    tc->stopped   = 1;

    /* Even if we have connected recently, reconnect right now */
    tc->dateTry = 0;
}

void tr_trackerClose( tr_tracker_t * tc )
{
    tr_torrent_t * tor = tc->tor;

    if( NULL != tc->http )
    {
        tr_httpClose( tc->http );
        tr_fdSocketClosed( tor->fdlimit, 1 );
    }
    free( tc );
}

static tr_http_t * getQuery( tr_tracker_t * tc )
{
    tr_torrent_t * tor = tc->tor;
    tr_info_t    * inf = &tor->info;

    char         * event;
    uint64_t       left;
    uint64_t       down;
    uint64_t       up;

    assert( tor->downloaded >= tc->download && tor->uploaded >= tc->upload );
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

    return tr_httpClient( TR_HTTP_GET, inf->trackerAddress,
                          inf->trackerPort,
                          "%s?"
                          "info_hash=%s&"
                          "peer_id=%s&"
                          "port=%d&"
                          "uploaded=%"PRIu64"&"
                          "downloaded=%"PRIu64"&"
                          "left=%"PRIu64"&"
                          "compact=1&"
                          "numwant=50&"
                          "key=%s"
                          "%s ",
                          inf->trackerAnnounce, tor->hashString, tc->id,
                          tc->bindPort, up, down, left, tor->key, event );
}

static void readAnswer( tr_tracker_t * tc, const char * data, int len )
{
    tr_torrent_t * tor = tc->tor;
    int i;
    int code;
    benc_val_t   beAll;
    benc_val_t * bePeers, * beFoo;
    const uint8_t * body;
    int bodylen;
    int shouldfree;

    tc->dateTry = tr_date();
    code = tr_httpResponseCode( data, len );
    if( 0 > code )
    {
        /* We don't have a valid HTTP status line */
        tr_inf( "Tracker: invalid HTTP status line" );
        tc->lastAttempt = TC_ATTEMPT_NOREACH;
        return;
    }

    if( !TR_HTTP_STATUS_OK( code ) )
    {
        /* we didn't get a 2xx status code */
        tr_err( "Tracker: invalid HTTP status code: %i", code );
        tc->lastAttempt = TC_ATTEMPT_ERROR;
        return;
    }

    /* find the end of the http headers */
    body = (uint8_t *) tr_httpParse( data, len, NULL );
    if( NULL == body )
    {
        tr_err( "Tracker: could not find end of HTTP headers" );
        tc->lastAttempt = TC_ATTEMPT_NOREACH;
        return;
    }
    bodylen = len - (body - (const uint8_t*)data);

    /* Find and load the dictionary */
    shouldfree = 0;
    for( i = 0; i < bodylen; i++ )
    {
        if( !tr_bencLoad( &body[i], bodylen - i, &beAll, NULL ) )
        {
            shouldfree = 1;
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
        tr_err( "Tracker: no valid dictionary found in answer" );
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
    if( shouldfree )
    {
        tr_bencFree( &beAll );
    }
}

int tr_trackerScrape( tr_torrent_t * tor, int * seeders, int * leechers )
{
    tr_info_t * inf = &tor->info;

    tr_http_t  * http;
    const char * data, * body;
    int          datalen, bodylen;
    int          code, ii;
    benc_val_t   scrape, * val1, * val2;

    if( !tor->scrape[0] )
    {
        /* scrape not supported */
        return 1;
    }

    http = tr_httpClient( TR_HTTP_GET, inf->trackerAddress, inf->trackerPort,
                          "%s?info_hash=%s", tor->scrape, tor->hashString );

    data = NULL;
    while( NULL == data )
    {
        switch( tr_httpPulse( http, &data, &datalen ) )
        {
            case TR_WAIT:
                break;

            case TR_ERROR:
                tr_httpClose( http );
                return 1;

            case TR_OK:
                if( NULL == data || 0 >= datalen )
                {
                    tr_httpClose( http );
                    return 1;
                }
                break;
        }
        tr_wait( 10 );
    }

    code = tr_httpResponseCode( data, datalen );
    if( !TR_HTTP_STATUS_OK( code ) )
    {
        tr_httpClose( http );
        return 1;
    }

    body = tr_httpParse( data, datalen , NULL );
    if( NULL == body )
    {
        tr_httpClose( http );
        return 1;
    }
    bodylen = datalen - ( body - data );

    for( ii = 0; ii < bodylen - 8; ii++ )
    {
        if( !memcmp( body + ii, "d5:files", 8 ) )
        {
            break;
        }
    }
    if( ii >= bodylen - 8 )
    {
        tr_httpClose( http );
        return 1;
    }
    if( tr_bencLoad( body + ii, bodylen - ii, &scrape, NULL ) )
    {
        tr_httpClose( http );
        return 1;
    }

    val1 = tr_bencDictFind( &scrape, "files" );
    if( !val1 )
    {
        tr_bencFree( &scrape );
        tr_httpClose( http );
        return 1;
    }
    val1 = &val1->val.l.vals[1];
    if( !val1 )
    {
        tr_bencFree( &scrape );
        tr_httpClose( http );
        return 1;
    }
    val2 = tr_bencDictFind( val1, "complete" );
    if( !val2 )
    {
        tr_bencFree( &scrape );
        tr_httpClose( http );
        return 1;
    }
    *seeders = val2->val.i;
    val2 = tr_bencDictFind( val1, "incomplete" );
    if( !val2 )
    {
        tr_bencFree( &scrape );
        tr_httpClose( http );
        return 1;
    }
    *leechers = val2->val.i;
    tr_bencFree( &scrape );
    tr_httpClose( http );

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
