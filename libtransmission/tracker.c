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

typedef struct tr_announce_list_ptr_s tr_announce_list_ptr_t;
struct tr_announce_list_ptr_s
{
    tr_tracker_info_t * item;
    tr_announce_list_ptr_t * nextItem;
};

struct tr_tracker_s
{
    tr_torrent_t * tor;

    char         * id;
    char         * trackerid;
    
    const char   * trackerAddress;
    int            trackerPort;
    const char   * trackerAnnounce;
    char           trackerScrape[MAX_PATH_LENGTH];
    int            trackerCanScrape;
    
    tr_announce_list_ptr_t ** trackerAnnounceListPtr;

#define TC_CHANGE_NO        0
#define TC_CHANGE_NEXT      1
#define TC_CHANGE_NONEXT    2
#define TC_CHANGE_REDIRECT  4
    int            shouldChangeAnnounce;
    int            announceTier;
    int            announceTierLast;
    
    char         * redirectAddress;
    int            redirectAddressLen;
    char         * redirectScrapeAddress;
    int            redirectScrapeAddressLen;

    char           started;
    char           completed;
    char           stopped;

    int            interval;
    int            minInterval;
    int            scrapeInterval;
    int            seeders;
    int            leechers;
    int            hasManyPeers;
    int            complete;
    int            randOffset;
    
    int            completelyUnconnectable;

    uint64_t       dateTry;
    uint64_t       dateOk;
    uint64_t       dateScrape;
    int            lastScrapeFailed;

#define TC_ATTEMPT_NOREACH 1
#define TC_ATTEMPT_ERROR   2
#define TC_ATTEMPT_OK      4
    char           lastAttempt;
    int            scrapeNeeded;

    tr_http_t    * http;
    tr_http_t    * httpScrape;

    int            bindPort;
    int            newPort;
};

static int         announceToScrape ( char * announce, char * scrape );
static void        setAnnounce      ( tr_tracker_t * tc, tr_announce_list_ptr_t * announceItem );
static void        failureAnnouncing( tr_tracker_t * tc );
static tr_http_t * getQuery         ( tr_tracker_t * tc );
static tr_http_t * getScrapeQuery   ( tr_tracker_t * tc );
static void        readAnswer       ( tr_tracker_t * tc, const char *, int );
static void        readScrapeAnswer ( tr_tracker_t * tc, const char *, int );
static void        killHttp         ( tr_http_t ** http, tr_fd_t * fdlimit );

tr_tracker_t * tr_trackerInit( tr_torrent_t * tor )
{
    tr_info_t * inf = &tor->info;

    tr_tracker_t * tc;
    tr_announce_list_ptr_t * prev, * cur;
    int ii, jj;

    tc                 = calloc( 1, sizeof( tr_tracker_t ) );
    tc->tor            = tor;
    tc->id             = tor->id;

    tc->started        = 1;
    
    tc->shouldChangeAnnounce = TC_CHANGE_NO;
    tc->redirectAddress = NULL;

    tc->interval       = 300;
    tc->scrapeInterval = 600;

    tc->lastAttempt    = TC_ATTEMPT_NOREACH;

    tc->bindPort       = *(tor->bindPort);
    tc->newPort        = -1;
    
    tc->trackerAnnounceListPtr = calloc( sizeof( int ), inf->trackerTiers );
    for( ii = 0; ii < inf->trackerTiers; ii++ )
    {
        prev = NULL;
        for( jj = 0; jj < inf->trackerList[ii].count; jj++ )
        {
            cur = calloc( sizeof( tr_announce_list_ptr_t ), 1 );
            cur->item = &inf->trackerList[ii].list[jj];
            if( NULL == prev )
            {
                tc->trackerAnnounceListPtr[ii] = cur;
            }
            else
            {
                prev->nextItem = cur;
            }
            prev = cur;
        }
    }
    
    setAnnounce( tc, tc->trackerAnnounceListPtr[0] );

    return tc;
}

static int announceToScrape( char * announce, char * scrape )
{   
    char * slash, * nextSlash;
    int pre, post;
    
    slash = strchr( announce, '/' );
    while( ( nextSlash = strchr( slash + 1, '/' ) ) )
    {
        slash = nextSlash;
    }
    slash++;
    
    if( !strncmp( slash, "announce", 8 ) )
    {
        pre  = (long) slash - (long) announce;
        post = strlen( announce ) - pre - 8;
        memcpy( scrape, announce, pre );
        sprintf( &scrape[pre], "scrape" );
        memcpy( &scrape[pre+6], &announce[pre+8], post );
        scrape[pre+6+post] = 0;
        
        return 1;
    }
    else
    {
        return 0;
    }
}

static void setAnnounce( tr_tracker_t * tc, tr_announce_list_ptr_t * announcePtr )
{
    tr_tracker_info_t * announceItem = announcePtr->item;
    
    tc->trackerAddress  = announceItem->address;
    tc->trackerPort     = announceItem->port;
    tc->trackerAnnounce = announceItem->announce;
    
    tc->trackerCanScrape = announceToScrape( announceItem->announce, tc->trackerScrape );
    
    /* Needs a new scrape */
    tc->seeders = -1;
    tc->leechers = -1;
    tc->complete = -1;
    tc->dateScrape = 0;
}

static void failureAnnouncing( tr_tracker_t * tc )
{
    tr_info_t * inf = &tc->tor->info;
    
    tc->shouldChangeAnnounce = tc->announceTier + 1 < inf->trackerTiers
                                || tc->announceTierLast + 1 < inf->trackerList[tc->announceTier].count
                                ? TC_CHANGE_NEXT : TC_CHANGE_NONEXT;
    
    if( tc->shouldChangeAnnounce == TC_CHANGE_NONEXT )
    {
        tc->completelyUnconnectable = 1;
    }
}

static int shouldConnect( tr_tracker_t * tc )
{
    tr_torrent_t * tor = tc->tor;
    uint64_t       now;
    
    /* Last tracker failed, try next */
    if( tc->shouldChangeAnnounce == TC_CHANGE_NEXT
        || tc->shouldChangeAnnounce == TC_CHANGE_REDIRECT )
    {
        return 1;
    }
    
    now = tr_date();

    /* Unreachable tracker, wait 10 seconds + random value before trying again */
    if( tc->lastAttempt == TC_ATTEMPT_NOREACH &&
        now < tc->dateTry + tc->randOffset + 10000 )
    {
        return 0;
    }

    /* The tracker rejected us (like 4XX code, unauthorized IP...),
       don't hammer it - we'll probably get the same answer next time
       anyway */
    if( tc->lastAttempt == TC_ATTEMPT_ERROR &&
        now < tc->dateTry + 1000 * tc->interval + tc->randOffset )
    {
        return 0;
    }

    /* Do we need to send an event? */
    if( tc->started || tc->completed || tc->stopped || 0 < tc->newPort )
    {
        return 1;
    }

    /* Should we try and get more peers? */
    if( now > tc->dateOk + 1000 * tc->interval + tc->randOffset )
    {
        return 1;
    }

    /* If there is quite a lot of people on this torrent, stress
       the tracker a bit until we get a decent number of peers */
    if( tc->hasManyPeers && !tr_cpIsSeeding( tor->completion ) )
    {
        /* reannounce in 10 seconds if we have less than 5 peers */
        if( tor->peerCount < 5 )
        {
            if( now > tc->dateOk + 1000 * MAX( 10, tc->minInterval ) )
            {
                return 1;
            }
        }
        /* reannounce in 20 seconds if we have less than 10 peers */
        else if( tor->peerCount < 10 )
        {
            if( now > tc->dateOk + 1000 * MAX( 20, tc->minInterval ) )
            {
                return 1;
            }
        }
        /* reannounce in 30 seconds if we have less than 15 peers */
        else if( tor->peerCount < 15 )
        {
            if( now > tc->dateOk + 1000 * MAX( 30, tc->minInterval ) )
            {
                return 1;
            }
        }
    }

    return 0;
}

static int shouldScrape( tr_tracker_t * tc )
{
    uint64_t now, interval;

    /* in process of changing tracker or scrape not supported */
    if( tc->shouldChangeAnnounce != TC_CHANGE_NO || !tc->trackerCanScrape || tc->stopped )
    {
        return 0;
    }

    now      = tr_date();
    interval = 1000 * MAX( tc->scrapeInterval, 60 );

    /* scrape half as often if there is no need to */
    if( !tc->scrapeNeeded && !tc->lastScrapeFailed )
    {
        interval *= 2;
    }

    return now > tc->dateScrape + interval;
}

void tr_trackerChangePort( tr_tracker_t * tc, int port )
{
    tc->newPort = port;
}

void tr_trackerPulse( tr_tracker_t * tc )
{
    tr_torrent_t * tor = tc->tor;
    tr_info_t    * inf = &tor->info;
    const char   * data;
    char         * address, * announce;
    int            len, i, port;
    tr_announce_list_ptr_t * announcePtr, * prevAnnouncePtr;

    if( ( NULL == tc->http ) && shouldConnect( tc ) )
    {
        tc->completelyUnconnectable = 0;
        tc->randOffset = tr_rand( 60000 );
        
        if( tr_fdSocketWillCreate( tor->fdlimit, 1 ) )
        {
            return;
        }
        tc->dateTry = tr_date();
        
        /* Use redirected address */
        if( tc->shouldChangeAnnounce == TC_CHANGE_REDIRECT )
        {
            if( !tr_httpParseUrl( tc->redirectAddress, tc->redirectAddressLen,
                                     &address, &port, &announce ) )
            {
                tr_err( "Tracker: redirected URL: %s:%d", address, port );
                tc->http = tr_httpClient( TR_HTTP_GET, address, port, announce );
                
                free( address );
                free( announce );
            }
            
            free( tc->redirectAddress );
            tc->redirectAddress = NULL;
        }
        else
        {
            /* Need to change to next address in list */
            if( tc->shouldChangeAnnounce == TC_CHANGE_NEXT )
            {
                tr_inf( "Tracker: failed to connect to %s, trying next", tc->trackerAddress );
                
                if( tc->announceTierLast + 1 < inf->trackerList[tc->announceTier].count )
                {
                    tc->announceTierLast++;
                    
                    announcePtr = tc->trackerAnnounceListPtr[tc->announceTier];
                    for( i = 0; i < tc->announceTierLast; i++ )
                    {
                        announcePtr = announcePtr->nextItem;
                    }
                }
                else
                {
                    tc->announceTierLast = 0;
                    tc->announceTier++;
                    
                    announcePtr = tc->trackerAnnounceListPtr[tc->announceTier];
                }
                
                tr_inf( "Tracker: tracker address set to %s", tc->trackerAnnounceListPtr[tc->announceTier]->item->address );
                setAnnounce( tc, announcePtr );
            }
            /* Need to change to first in list */
            else if( tc->announceTier != 0 || tc->announceTierLast != 0 )
            {
                /* Check if the last announce was successful and wasn't the first in the sublist */
                if( tc->shouldChangeAnnounce == TC_CHANGE_NO && tc->announceTierLast != 0 )
                {
                    announcePtr = tc->trackerAnnounceListPtr[tc->announceTier];
                    for( i = 0; i < tc->announceTierLast; i++ )
                    {
                        prevAnnouncePtr = announcePtr;
                        announcePtr = announcePtr->nextItem;
                    }
                    
                    /* Move address to front of tier in announce list */
                    prevAnnouncePtr->nextItem = announcePtr->nextItem;
                    announcePtr->nextItem =  tc->trackerAnnounceListPtr[tc->announceTier];
                    tc->trackerAnnounceListPtr[tc->announceTier] = announcePtr;
                }
                
                setAnnounce( tc, tc->trackerAnnounceListPtr[0] );
                tc->announceTier = 0;
                tc->announceTierLast = 0;
            }
            
            tc->http = getQuery( tc );

            tr_inf( "Tracker: connecting to %s:%d (%s)",
                    tc->trackerAddress, tc->trackerPort,
                    tc->started ? "sending 'started'" :
                    ( tc->completed ? "sending 'completed'" :
                      ( tc->stopped ? "sending 'stopped'" :
                        ( 0 < tc->newPort ? "sending 'stopped' to change port" :
                          "getting peers" ) ) ) );
        }
        
        tc->shouldChangeAnnounce = TC_CHANGE_NO;
    }

    if( NULL != tc->http )
    {
        switch( tr_httpPulse( tc->http, &data, &len ) )
        {
            case TR_WAIT:
                break;

            case TR_ERROR:
                killHttp( &tc->http, tor->fdlimit );
                tc->dateTry = tr_date();
                
                failureAnnouncing( tc );
                if ( tc->shouldChangeAnnounce == TC_CHANGE_NEXT )
                {
                    tr_trackerPulse( tc );
                    return;
                }
                
                break;

            case TR_OK:
                readAnswer( tc, data, len );
                killHttp( &tc->http, tor->fdlimit );
                
                /* Something happened to need to try next address */
                if ( tc->shouldChangeAnnounce == TC_CHANGE_NEXT
                    || tc->shouldChangeAnnounce == TC_CHANGE_REDIRECT )
                {
                    tr_trackerPulse( tc );
                    return;
                }
                
                break;
        }
    }
    
    if( ( NULL == tc->httpScrape ) && shouldScrape( tc ) )
    {
        if( tr_fdSocketWillCreate( tor->fdlimit, 1 ) )
        {
            return;
        }
        tc->dateScrape = tr_date();
        
        if ( tc->redirectScrapeAddress != NULL )
        {
            /* Use redirected address */
            if( !tr_httpParseUrl( tc->redirectScrapeAddress, tc->redirectScrapeAddressLen,
                                     &address, &port, &announce ) )
            {
                tr_err( "Scrape: redirected URL: %s:%d", address, port );
                tc->httpScrape = tr_httpClient( TR_HTTP_GET, address, port, announce );
                
                free( address );
                free( announce );
            }
            
            free( tc->redirectScrapeAddress );
            tc->redirectScrapeAddress = NULL;
        }
        else
        {
            tc->httpScrape = getScrapeQuery( tc );
            tr_inf( "Scrape: sent HTTP request to %s:%d%s", tc->trackerAddress, tc->trackerPort, tc->trackerScrape );
        }
    }

    if( NULL != tc->httpScrape )
    {
        switch( tr_httpPulse( tc->httpScrape, &data, &len ) )
        {
            case TR_WAIT:
                break;

            case TR_ERROR:
                killHttp( &tc->httpScrape, tor->fdlimit );
                tc->lastScrapeFailed = 1;
                break;

            case TR_OK:
                readScrapeAnswer( tc, data, len );
                killHttp( &tc->httpScrape, tor->fdlimit );
                break;
        }
    }

    return;
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

    /* If we are already sending a query at the moment, we need to
       reconnect */
    killHttp( &tc->http, tor->fdlimit );

    tc->started   = 0;
    tc->completed = 0;
    tc->stopped   = 1;

    /* Even if we have connected recently, reconnect right now */
    tc->dateTry = 0;
}

void tr_trackerClose( tr_tracker_t * tc )
{
    tr_torrent_t * tor = tc->tor;

    killHttp( &tc->http, tor->fdlimit );
    killHttp( &tc->httpScrape, tor->fdlimit );
    free( tc->trackerid );
    free( tc );
}

static tr_http_t * getQuery( tr_tracker_t * tc )
{
    tr_torrent_t * tor = tc->tor;

    char         * event, * trackerid, * idparam;
    uint64_t       left;
    uint64_t       down;
    uint64_t       up;
    char           start;
    int            numwant = 50;

    down = tor->downloadedCur;
    up   = tor->uploadedCur;
    if( tc->started )
    {
        event = "&event=started";
        down  = 0;
        up    = 0;

        if( 0 < tc->newPort )
        {
            tc->bindPort = tc->newPort;
            tc->newPort  = -1;
        }
    }
    else if( tc->completed )
    {
        event = "&event=completed";
    }
    else if( tc->stopped || 0 < tc->newPort )
    {
        event = "&event=stopped";
        numwant = 0;
    }
    else
    {
        event = "";
    }

    if( NULL == tc->trackerid )
    {
        trackerid = "";
        idparam   = "";
    }
    else
    {
        trackerid = tc->trackerid;
        idparam   = "&trackerid=";
    }

    start = ( strchr( tc->trackerAnnounce, '?' ) ? '&' : '?' );
    left  = tr_cpLeftBytes( tor->completion );

    return tr_httpClient( TR_HTTP_GET, tc->trackerAddress, tc->trackerPort,
                          "%s%c"
                          "info_hash=%s&"
                          "peer_id=%s&"
                          "port=%d&"
                          "uploaded=%"PRIu64"&"
                          "downloaded=%"PRIu64"&"
                          "left=%"PRIu64"&"
                          "compact=1&"
                          "numwant=%d&"
                          "key=%s"
                          "%s%s"
                          "%s",
                          tc->trackerAnnounce, start, tor->escapedHashString,
                          tc->id, tc->bindPort, up, down, left, numwant,
                          tor->key, idparam, trackerid, event );
}

static tr_http_t * getScrapeQuery( tr_tracker_t * tc )
{
    tr_torrent_t * tor = tc->tor;
    char           start;

    start = ( strchr( tc->trackerScrape, '?' ) ? '&' : '?' );

    return tr_httpClient( TR_HTTP_GET, tc->trackerAddress, tc->trackerPort,
                          "%s%c"
                          "info_hash=%s",
                          tc->trackerScrape, start, tor->escapedHashString );
}

static void readAnswer( tr_tracker_t * tc, const char * data, int len )
{
    tr_torrent_t * tor = tc->tor;
    int i;
    int code;
    benc_val_t   beAll;
    benc_val_t * bePeers, * beFoo;
    const uint8_t * body;
    int bodylen, shouldfree, scrapeNeeded;

    tc->dateTry = tr_date();
    code = tr_httpResponseCode( data, len );
    
    if( 0 > code )
    {
        /* We don't have a valid HTTP status line */
        tr_inf( "Tracker: invalid HTTP status line" );
        tc->lastAttempt = TC_ATTEMPT_NOREACH;
        failureAnnouncing( tc );
        return;
    }
    
    if( code == 301 || code == 302 )
    {
        tr_err( "Tracker: HTTP status code: %i", code );
        
        tr_http_header_t hdr[] = { { "Location", NULL, 0 }, { NULL, NULL, 0 } };
        
        tr_httpParse( data, len, hdr );
        
        char * address = calloc( sizeof( char ), hdr->len+1 );
        snprintf( address, hdr->len+1, "%s", hdr->data );
        
        tc->shouldChangeAnnounce = TC_CHANGE_REDIRECT;
        tc->redirectAddress = address;
        tc->redirectAddressLen = hdr->len;
        
        return;
    }

    if( !TR_HTTP_STATUS_OK( code ) )
    {
        /* we didn't get a 2xx status code */
        tr_err( "Tracker: invalid HTTP status code: %i", code );
        tc->lastAttempt = TC_ATTEMPT_ERROR;
        failureAnnouncing( tc );
        return;
    }

    /* find the end of the http headers */
    body = (uint8_t *) tr_httpParse( data, len, NULL );
    if( NULL == body )
    {
        tr_err( "Tracker: could not find end of HTTP headers" );
        tc->lastAttempt = TC_ATTEMPT_NOREACH;
        failureAnnouncing( tc );
        return;
    }
    bodylen = len - ( body - (const uint8_t*)data );

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
        failureAnnouncing( tc );
        return;
    }

    // tr_bencPrint( &beAll );

    if( ( bePeers = tr_bencDictFind( &beAll, "failure reason" ) ) )
    {
        tr_err( "Tracker: Error - %s", bePeers->val.s.s );
        tor->error |= TR_ETRACKER;
        snprintf( tor->trackerError, sizeof( tor->trackerError ),
                  "%s", bePeers->val.s.s );
        tc->lastAttempt = TC_ATTEMPT_ERROR;
        failureAnnouncing( tc );
        goto cleanup;
    }
    else if( ( bePeers = tr_bencDictFind( &beAll, "warning message" ) ) )
    {
        tr_err( "Tracker: Warning - %s", bePeers->val.s.s );
        snprintf( tor->trackerError, sizeof( tor->trackerError ),
                  "%s", bePeers->val.s.s );
    }
    else
    {
        tor->trackerError[0] = '\0';
    }

    tor->error &= ~TR_ETRACKER;
    tc->lastAttempt = TC_ATTEMPT_OK;

    /* Get the tracker interval, force to between
       10 sec and 5 mins */
    beFoo = tr_bencDictFind( &beAll, "interval" );
    if( !beFoo || TYPE_INT != beFoo->type )
    {
        tr_err( "Tracker: no 'interval' field" );
        goto cleanup;
    }

    tc->interval = beFoo->val.i;
    tr_inf( "Tracker: interval = %d seconds", tc->interval );

    tc->interval = MIN( tc->interval, 300 );
    tc->interval = MAX( 10, tc->interval );

    /* Get the tracker minimum interval, force to between
       10 sec and 5 mins  */
    beFoo = tr_bencDictFind( &beAll, "min interval" );
    if( beFoo && TYPE_INT == beFoo->type )
    {
        tc->minInterval = beFoo->val.i;
        tr_inf( "Tracker: min interval = %d seconds", tc->minInterval );

        tc->minInterval = MIN( tc->minInterval, 300 );
        tc->minInterval = MAX( 10, tc->minInterval );

        if( tc->interval < tc->minInterval )
        {
            tc->interval = tc->minInterval;
            tr_inf( "Tracker: 'interval' less than 'min interval', "
                    "using 'min interval'" );
        }
    }
    else
    {
        tc->minInterval = 0;
        tr_inf( "Tracker: no 'min interval' field" );
    }

    scrapeNeeded = 0;
    beFoo = tr_bencDictFind( &beAll, "complete" );
    if( beFoo && TYPE_INT == beFoo->type )
    {
        tc->seeders = beFoo->val.i;
    }
    else
    {
        scrapeNeeded = 1;
    }

    beFoo = tr_bencDictFind( &beAll, "incomplete" );
    if( beFoo && TYPE_INT == beFoo->type )
    {
        tc->leechers = beFoo->val.i;
    }
    else
    {
        scrapeNeeded = 1;
    }

    tc->scrapeNeeded = scrapeNeeded;
    if( !scrapeNeeded )
    {
        tc->hasManyPeers = ( tc->seeders + tc->leechers >= 50 );
    }

    beFoo = tr_bencDictFind( &beAll, "tracker id" );
    if( beFoo )
    {
        free( tc->trackerid );
        tc->trackerid = strdup( beFoo->val.s.s );
        tr_inf( "Tracker: tracker id = %s", tc->trackerid );
    }

    bePeers = tr_bencDictFind( &beAll, "peers" );
    if( !bePeers )
    {
        if( tc->stopped || 0 < tc->newPort )
        {
            goto nodict;
        }
        tr_err( "Tracker: no \"peers\" field" );
        failureAnnouncing( tc );
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
    }

cleanup:
    if( shouldfree )
    {
        tr_bencFree( &beAll );
    }
}

static void readScrapeAnswer( tr_tracker_t * tc, const char * data, int len )
{
    int code;
    const uint8_t * body;
    int bodylen, ii;
    benc_val_t scrape, * val1, * val2;

    code = tr_httpResponseCode( data, len );
    if( 0 > code )
    {
        /* We don't have a valid HTTP status line */
        tr_inf( "Scrape: invalid HTTP status line" );
        tc->lastScrapeFailed = 1;
        return;
    }
    
    if( code == 301 || code == 302 )
    {
        tr_err( "Scrape: HTTP status code: %i", code );
        
        tr_http_header_t hdr[] = { { "Location", NULL, 0 }, { NULL, NULL, 0 } };
        
        tr_httpParse( data, len, hdr );
        
        char * address = calloc( sizeof( char ), hdr->len+1 );
        snprintf( address, hdr->len+1, "%s", hdr->data );
        
        /* Needs a new scrape */
        tc->dateScrape = 0;
        
        tc->redirectScrapeAddress = address;
        tc->redirectScrapeAddressLen = hdr->len;
        
        return;
    }

    if( !TR_HTTP_STATUS_OK( code ) )
    {
        /* we didn't get a 2xx status code */
        tr_err( "Scrape: invalid HTTP status code: %i", code );
        if( TR_HTTP_STATUS_FAIL_CLIENT( code ) )
            tc->trackerCanScrape = 0;
        tc->lastScrapeFailed = 1;
        return;
    }

    /* find the end of the http headers */
    body = (uint8_t *) tr_httpParse( data, len, NULL );
    if( NULL == body )
    {
        tr_err( "Scrape: could not find end of HTTP headers" );
        tc->lastScrapeFailed = 1;
        return;
    }

    tc->lastScrapeFailed = 0;
    bodylen = len - ( body - (const uint8_t*)data );

    for( ii = 0; ii < bodylen; ii++ )
    {
        if( !tr_bencLoad( body + ii, bodylen - ii, &scrape, NULL ) )
        {
            break;
        }
    }
    if( ii >= bodylen )
    {
        return;
    }

    val1 = tr_bencDictFind( &scrape, "files" );
    if( !val1 )
    {
        tr_bencFree( &scrape );
        return;
    }
    val1 = &val1->val.l.vals[1];
    if( !val1 )
    {
        tr_bencFree( &scrape );
        return;
    }
    
    val2 = tr_bencDictFind( val1, "complete" );
    if( !val2 )
    {
        tr_bencFree( &scrape );
        return;
    }
    tc->seeders = val2->val.i;
    
    val2 = tr_bencDictFind( val1, "incomplete" );
    if( !val2 )
    {
        tr_bencFree( &scrape );
        return;
    }
    tc->leechers = val2->val.i;
    
    val2 = tr_bencDictFind( val1, "downloaded" );
    if( !val2 )
    {
        tr_bencFree( &scrape );
        return;
    }
    tc->complete = val2->val.i;
    
    val2 = tr_bencDictFind( val1, "flags" );
    if( val2 )
    {
        val2 = tr_bencDictFind( val2, "min_request_interval" );
        if( val2 )
        {
            tc->scrapeInterval = val2->val.i;
        }
    }
    
    tc->hasManyPeers = ( tc->seeders + tc->leechers >= 50 );
    
    tr_bencFree( &scrape );
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

int tr_trackerDownloaded( tr_tracker_t * tc )
{
    if( !tc )
    {
        return -1;
    }
    return tc->complete;
}

const char * tr_trackerAddress( tr_tracker_t * tc )
{
    if( !tc )
    {
        return NULL;
    }
    return tc->trackerAddress;
}

int tr_trackerPort( tr_tracker_t * tc )
{
    if( !tc )
    {
        return 0;
    }
    return tc->trackerPort;
}

const char * tr_trackerAnnounce( tr_tracker_t * tc )
{
    if( !tc )
    {
        return NULL;
    }
    return tc->trackerAnnounce;
}

int tr_trackerCannotConnect( tr_tracker_t * tc )
{
    if( !tc )
    {
        return 0;
    }
    return tc->completelyUnconnectable;
}

/* Blocking version */
int tr_trackerScrape( tr_torrent_t * tor, int * s, int * l, int * d )
{
    tr_tracker_t * tc;
    tr_http_t    * http;
    const char   * data;
    int            len;
    int            ret;
    
    tc = tr_trackerInit( tor );

    if( !tc->trackerCanScrape )
    {
        return 1;
    }

    http = getScrapeQuery( tc );

    for( data = NULL; !data; tr_wait( 10 ) )
    {
        switch( tr_httpPulse( http, &data, &len ) )
        {
            case TR_WAIT:
                break;

            case TR_ERROR:
                goto scrapeDone;

            case TR_OK:
                readScrapeAnswer( tc, data, len );
                goto scrapeDone;
        }
    }

scrapeDone:
    tr_httpClose( http );

    ret = 1;
    if( tc->seeders > -1 && tc->leechers > -1 && tc->complete > -1 )
    {
        *s = tc->seeders;
        *l = tc->leechers;
        *d = tc->complete;
        ret = 0;
    }

    tr_trackerClose( tc );
    return ret;
}

static void killHttp( tr_http_t ** http, tr_fd_t * fdlimit )
{
    if( NULL != *http )
    {
        tr_httpClose( *http );
        tr_fdSocketClosed( fdlimit, 1 );
        *http = NULL;
    }
}
