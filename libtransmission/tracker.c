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

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "transmission.h"
#include "bencode.h"
#include "bsdqueue.h"
#include "http.h"
#include "net.h"
#include "shared.h"

struct tclist
{
    tr_tracker_info_t   * tl_inf;
    int                   tl_badscrape;
    SLIST_ENTRY( tclist ) next;
};
SLIST_HEAD( tchead, tclist );

struct tr_tracker_s
{
    tr_torrent_t * tor;

    char         * id;
    char         * trackerid;

    struct tchead * tiers;
    size_t          tierCount;
    size_t          tierIndex;
    struct tclist * tcCur;

#define TC_CHANGE_NO        0
#define TC_CHANGE_NEXT      1
#define TC_CHANGE_NONEXT    2
#define TC_CHANGE_REDIRECT  4
    int            shouldChangeAnnounce;
    
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
    int            allUnreachIfError;
    int            lastError;

    uint64_t       dateTry;
    uint64_t       dateOk;
    uint64_t       dateScrape;
    int            lastScrapeFailed;
    int            scrapeNeeded;

    tr_http_t    * http;
    tr_http_t    * httpScrape;

    int            publicPort;
};

static void        setAnnounce      ( tr_tracker_t * tc, struct tclist * new );
static void        failureAnnouncing( tr_tracker_t * tc );
static tr_http_t * getQuery         ( tr_tracker_t * tc );
static tr_http_t * getScrapeQuery   ( tr_tracker_t * tc );
static void        readAnswer       ( tr_tracker_t * tc, const char *, int,
                                      int * peerCount, uint8_t ** peerCompact );
static void        readScrapeAnswer ( tr_tracker_t * tc, const char *, int );
static void        killHttp         ( tr_http_t ** http );
static int         shouldChangePort( tr_tracker_t * tc );
static uint8_t *   parseOriginalPeers( benc_val_t * bePeers, int * peerCount );

tr_tracker_t * tr_trackerInit( tr_torrent_t * tor )
{
    tr_info_t * inf = &tor->info;

    tr_tracker_t * tc;
    struct tclist * item;
    size_t ii, jj;

    tc = calloc( 1, sizeof *tc );
    if( NULL == tc )
    {
        return NULL;
    }

    tc->tor            = tor;
    tc->id             = tor->id;

    tc->started        = 1;
    
    tc->shouldChangeAnnounce = TC_CHANGE_NO;
    tc->redirectAddress = NULL;

    tc->interval       = 300;
    tc->scrapeInterval = 1200;

    tc->lastError      = 0;
    tc->allUnreachIfError = 1;

    tc->publicPort     = tor->publicPort;

    assert( 0 <= inf->trackerTiers );
    assert( sizeof( struct tchead ) == sizeof *tc->tiers );
    tc->tiers          = calloc( inf->trackerTiers, sizeof *tc->tiers );
    tc->tierCount      = inf->trackerTiers;
    for( ii = 0; tc->tierCount > ii; ii++ )
    {
        assert( 0 <= inf->trackerList[ii].count );
        SLIST_INIT( &tc->tiers[ii] );
        for( jj = inf->trackerList[ii].count; 0 < jj; jj-- )
        {
            item = calloc( 1, sizeof *item );
            if( NULL == item )
            {
                tr_trackerClose( tc );
                return NULL;
            }
            item->tl_inf = &inf->trackerList[ii].list[jj-1];
            SLIST_INSERT_HEAD( &tc->tiers[ii], item, next );
        }
    }

    setAnnounce( tc, SLIST_FIRST( &tc->tiers[0] ) );

    return tc;
}

static void setAnnounce( tr_tracker_t * tc, struct tclist * new )
{
    tc->tcCur = new;

    /* Needs a new scrape */
    tc->seeders = -1;
    tc->leechers = -1;
    tc->complete = -1;
    tc->dateScrape = 0;
}

static void failureAnnouncing( tr_tracker_t * tc )
{
    if( NULL != SLIST_NEXT( tc->tcCur, next ) ||
        tc->tierIndex + 1 < tc->tierCount )
    {
        tc->shouldChangeAnnounce = TC_CHANGE_NEXT;
    }
    else
    {
        tc->shouldChangeAnnounce = TC_CHANGE_NONEXT;
        tc->completelyUnconnectable = 1;
    }
}

static int shouldConnect( tr_tracker_t * tc )
{
    tr_torrent_t * tor = tc->tor;
    const uint64_t now = tr_date();
    
    /* Last tracker failed, try next */
    if( tc->shouldChangeAnnounce == TC_CHANGE_NEXT
        || tc->shouldChangeAnnounce == TC_CHANGE_REDIRECT )
    {
        return 1;
    }
    
    /* If last attempt was an error and it did not change trackers,
       then all must have been errors */
    if( tc->lastError )
    {
        /* Unreachable trackers, wait 10 seconds + random value before
           trying again */
        if( tc->allUnreachIfError )
        {
            if( now < tc->dateTry + tc->randOffset + 10000 )
            {
                return 0;
            }
        }
        /* The tracker rejected us (like 4XX code, unauthorized
            IP...), don't hammer it - we'll probably get the same
            answer next time anyway */
        else
        {
            if( now < tc->dateTry + 1000 * tc->interval + tc->randOffset )
            {
                return 0;
            }
            else
            {
                /* since starting at the top of the list, reset if any
                   were reached previously */
                tc->allUnreachIfError = 1;
            }
        }
    }

    /* Do we need to send an event? */
    if( tc->started || tc->completed || tc->stopped || shouldChangePort( tc ) )
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
    if( tc->hasManyPeers &&
        (tr_cpGetStatus ( tor->completion ) == TR_CP_INCOMPLETE ))
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

static int shouldScrape( const tr_tracker_t * tc )
{
    uint64_t now, interval;

    /* in process of changing tracker or scrape not supported */
    if( tc->shouldChangeAnnounce != TC_CHANGE_NO ||
        NULL == tc->tcCur->tl_inf->scrape || tc->tcCur->tl_badscrape ||
        tc->stopped )
    {
        return 0;
    }

    now      = tr_date();
    interval = 1000 * MAX( tc->scrapeInterval, 600 );

    /* scrape more often if needed */
    if( tc->scrapeNeeded || tc->lastScrapeFailed )
    {
        interval /= 2;
    }

    return now > tc->dateScrape + interval;
}

void
tr_trackerPulse( tr_tracker_t    * tc,
                 int             * peerCount,
                 uint8_t        ** peerCompact )
{
    const char   * data;
    char         * address, * announce;
    int            len, port;
    struct tclist * next;
    struct tchead * tier;

    if( tc == NULL )
        return;

    *peerCount = 0;
    *peerCompact = NULL;
    
    if( !tc->http && shouldConnect( tc ) )
    {
        tc->completelyUnconnectable = FALSE;
        
        tc->randOffset = tr_rand( 60000 );
        
        tc->dateTry = tr_date();
        
        /* Use redirected address */
        if( tc->shouldChangeAnnounce == TC_CHANGE_REDIRECT )
        {
            if( !tr_httpParseUrl( tc->redirectAddress, tc->redirectAddressLen,
                                     &address, &port, &announce ) )
            {
                tr_err( "Tracker: redirected URL: %s:%d", address, port );
                tc->http = tr_httpClient( TR_HTTP_GET, address, port,
                                          "%s", announce );
                
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
                tr_inf( "Tracker: failed to connect to %s:%i, trying next",
                        tc->tcCur->tl_inf->address, tc->tcCur->tl_inf->port );
                next = SLIST_NEXT( tc->tcCur, next );
                if( NULL == next )
                {
                    assert( tc->tierCount > tc->tierIndex + 1 );
                    tc->tierIndex++;
                    next = SLIST_FIRST( &tc->tiers[tc->tierIndex] );
                    /* XXX will there always be at least one tracker
                       in a tier? */
                }
                
                tr_inf( "Tracker: switching to tracker http://%s:%i%s",
                        next->tl_inf->address, next->tl_inf->port,
                        next->tl_inf->announce );
                setAnnounce( tc, next );
            }
            /* Need to change to first in list */
            else if( SLIST_FIRST( &tc->tiers[0] ) != tc->tcCur )
            {
                tier = &tc->tiers[tc->tierIndex];
                /* Check if the last announce was successful and
                   wasn't the first in the sublist */
                if( tc->shouldChangeAnnounce == TC_CHANGE_NO &&
                    SLIST_FIRST( tier ) != tc->tcCur )
                {
                    SLIST_REMOVE( tier, tc->tcCur, tclist, next );
                    SLIST_INSERT_HEAD( tier, tc->tcCur, next );
                }
                
                setAnnounce( tc, SLIST_FIRST( tier ) );
            }

            tc->http = getQuery( tc );

            tr_inf( "Tracker: connecting to %s:%d (%s)",
                    tc->tcCur->tl_inf->address, tc->tcCur->tl_inf->port,
                    tc->started ? "sending 'started'" :
                    ( tc->completed ? "sending 'completed'" :
                      ( tc->stopped ? "sending 'stopped'" :
                        ( shouldChangePort( tc ) ?
                          "sending 'stopped' to change port" :
                          "getting peers" ) ) ) );
        }
        
        tc->shouldChangeAnnounce = TC_CHANGE_NO;
    }

    if( tc->http )
    {
        switch( tr_httpPulse( tc->http, &data, &len ) )
        {
            case TR_NET_WAIT:
                break;

            case TR_NET_ERROR:
                killHttp( &tc->http );
                tc->dateTry = tr_date();
                
                failureAnnouncing( tc );
                
                tc->lastError = 1;
                break;

            case TR_NET_OK:
                readAnswer( tc, data, len, peerCount, peerCompact );
                killHttp( &tc->http );
                break;
        }
    }
    
    if( ( NULL == tc->httpScrape ) && shouldScrape( tc ) )
    {
        tc->dateScrape = tr_date();
        
        if ( tc->redirectScrapeAddress != NULL )
        {
            /* Use redirected address */
            if( !tr_httpParseUrl( tc->redirectScrapeAddress,
                                  tc->redirectScrapeAddressLen,
                                  &address, &port, &announce ) )
            {
                tr_err( "Scrape: redirected URL: %s:%d", address, port );
                tc->httpScrape = tr_httpClient( TR_HTTP_GET, address, port,
                                                "%s", announce );
                
                free( address );
                free( announce );
            }
            
            free( tc->redirectScrapeAddress );
            tc->redirectScrapeAddress = NULL;
        }
        else
        {
            tc->httpScrape = getScrapeQuery( tc );
            tr_inf( "Scrape: sent HTTP request for http://%s:%d%s",
                    tc->tcCur->tl_inf->address,
                    tc->tcCur->tl_inf->port,
                    tc->tcCur->tl_inf->scrape );
        }
    }

    if( NULL != tc->httpScrape )
    {
        switch( tr_httpPulse( tc->httpScrape, &data, &len ) )
        {
            case TR_NET_WAIT:
                break;

            case TR_NET_ERROR:
                killHttp( &tc->httpScrape );
                tc->lastScrapeFailed = 1;
                break;

            case TR_NET_OK:
                readScrapeAnswer( tc, data, len );
                killHttp( &tc->httpScrape );
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
    if( tc == NULL )
        return;

    /* If we are already sending a query at the moment, we need to
       reconnect */
    killHttp( &tc->http );

    tc->started   = 0;
    tc->completed = 0;
    tc->stopped   = 1;

    /* Even if we have connected recently, reconnect right now */
    tc->dateTry = 0;
}

void tr_trackerClose( tr_tracker_t * tc )
{
    size_t          ii;
    struct tclist * dead;

    if( tc == NULL )
        return;

    killHttp( &tc->http );
    killHttp( &tc->httpScrape );

    for( ii = 0; tc->tierCount > ii; ii++ )
    {
        while( !SLIST_EMPTY( &tc->tiers[ii] ) )
        {
            dead = SLIST_FIRST( &tc->tiers[ii] );
            SLIST_REMOVE_HEAD( &tc->tiers[ii], next );
            free( dead );
        }
    }
    free( tc->tiers );

    free( tc->trackerid );
    free( tc );
}

static tr_http_t * getQuery( tr_tracker_t * tc )
{
    tr_torrent_t * tor = tc->tor;
    tr_tracker_info_t * tcInf = tc->tcCur->tl_inf;

    char         * event, * trackerid, * idparam;
    uint64_t       left;
    char           start;
    int            numwant = 50;

    if( tc->started )
    {
        event = "&event=started";
       
        tr_torrentResetTransferStats( tor );

        if( shouldChangePort( tc ) )
        {
            tc->publicPort = tor->publicPort;
        }
    }
    else if( tc->completed )
    {
        event = "&event=completed";
    }
    else if( tc->stopped || shouldChangePort( tc ) )
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

    start = ( strchr( tcInf->announce, '?' ) ? '&' : '?' );
    left  = tr_cpLeftUntilComplete( tor->completion );

    return tr_httpClient( TR_HTTP_GET, tcInf->address, tcInf->port,
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
                          tcInf->announce, start, tor->escapedHashString,
                          tc->id, tc->publicPort, tor->uploadedCur, tor->downloadedCur,
                          left, numwant, tor->key, idparam, trackerid, event );
}

static tr_http_t * getScrapeQuery( tr_tracker_t * tc )
{
    tr_torrent_t * tor = tc->tor;
    tr_tracker_info_t * tcInf = tc->tcCur->tl_inf;
    char           start;

    start = ( strchr( tcInf->scrape, '?' ) ? '&' : '?' );

    return tr_httpClient( TR_HTTP_GET, tcInf->address, tcInf->port,
                          "%s%c"
                          "info_hash=%s",
                          tcInf->scrape, start, tor->escapedHashString );
}

static void readAnswer( tr_tracker_t * tc, const char * data, int len,
                        int * _peerCount, uint8_t ** _peerCompact )
{
    tr_torrent_t * tor = tc->tor;
    int i;
    int code;
    benc_val_t   beAll;
    benc_val_t * bePeers, * beFoo;
    const uint8_t * body;
    int bodylen, shouldfree, scrapeNeeded;
    char * address;
    int peerCount;
    uint8_t * peerCompact;

    *_peerCount = peerCount = 0;
    *_peerCompact = peerCompact = NULL;

    tc->dateTry = tr_date();
    code = tr_httpResponseCode( data, len );
    
    if( 0 > code )
    {
        /* We don't have a valid HTTP status line */
        tr_inf( "Tracker: invalid HTTP status line" );
        tc->lastError = 1;
        failureAnnouncing( tc );
        return;
    }
    
    if( code == 301 || code == 302 )
    {
        tr_http_header_t hdr[] = { { "Location", NULL, 0 }, { NULL, NULL, 0 } };

        tr_err( "Tracker: HTTP status code: %i", code );
        
        tr_httpParse( data, len, hdr );
        
        address = calloc( hdr->len+1, sizeof( char ) );
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
        tc->lastError = 1;
        tc->allUnreachIfError = 0;
        failureAnnouncing( tc );
        return;
    }

    /* find the end of the http headers */
    body = (uint8_t *) tr_httpParse( data, len, NULL );
    if( NULL == body )
    {
        tr_err( "Tracker: could not find end of HTTP headers" );
        tc->lastError = 1;
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
        if( tc->stopped || shouldChangePort( tc ) )
        {
            tc->lastError = 0;
            goto nodict;
        }
        tr_err( "Tracker: no valid dictionary found in answer" );
        tc->lastError = 1;
        tc->allUnreachIfError = 0;
        failureAnnouncing( tc );
        return;
    }

    /* tr_bencPrint( &beAll ); */

    if( ( bePeers = tr_bencDictFind( &beAll, "failure reason" ) ) )
    {
        tr_err( "Tracker: Error - %s", bePeers->val.s.s );
        tor->error = TR_ERROR_TC_ERROR;
        snprintf( tor->errorString, sizeof( tor->errorString ),
                  "%s", bePeers->val.s.s );
        tc->lastError = 1;
        tc->allUnreachIfError = 0;
        failureAnnouncing( tc );
        goto cleanup;
    }
    else if( ( bePeers = tr_bencDictFind( &beAll, "warning message" ) ) )
    {
        tr_err( "Tracker: Warning - %s", bePeers->val.s.s );
        tor->error = TR_ERROR_TC_WARNING;
        snprintf( tor->errorString, sizeof( tor->errorString ),
                  "%s", bePeers->val.s.s );
    }
    else if( tor->error & TR_ERROR_TC_MASK )
    {
        tor->error = TR_OK;
    }

    tc->lastError = 0;
    tc->allUnreachIfError = 0;

    /* Get the tracker interval */
    beFoo = tr_bencDictFind( &beAll, "interval" );
    if( !beFoo || TYPE_INT != beFoo->type )
    {
        tr_err( "Tracker: no 'interval' field" );
        goto cleanup;
    }

    tc->interval = beFoo->val.i;
    tr_inf( "Tracker: interval = %d seconds", tc->interval );
    tc->interval = MAX( 10, tc->interval );

    /* Get the tracker minimum interval */
    beFoo = tr_bencDictFind( &beAll, "min interval" );
    if( beFoo && TYPE_INT == beFoo->type )
    {
        tc->minInterval = beFoo->val.i;
        tr_inf( "Tracker: min interval = %d seconds", tc->minInterval );
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
        if( tc->stopped || shouldChangePort( tc ) )
        {
            goto nodict;
        }
        tr_err( "Tracker: no \"peers\" field" );
        failureAnnouncing( tc );
        goto cleanup;
    }

    if( TYPE_LIST == bePeers->type )
    {
        /* Original protocol */
        if( bePeers->val.l.count > 0 )
        {
            peerCompact = parseOriginalPeers( bePeers, &peerCount );
        }
    }
    else if( TYPE_STR == bePeers->type )
    {
        /* "Compact" extension */
        if( bePeers->val.s.i >= 6 )
        {
            peerCount = bePeers->val.s.i / 6;
            peerCompact = malloc( bePeers->val.s.i );
            memcpy( peerCompact, bePeers->val.s.s, bePeers->val.s.i );
        }
    }

    if( peerCount > 0 )
    {
        tr_inf( "Tracker: got %d peers", peerCount );
        if( peerCount >= 50 )
        {
            tc->hasManyPeers = 1;
        }
        *_peerCount = peerCount;
        *_peerCompact = peerCompact;
    }

nodict:
    /* Success */
    tc->started   = 0;
    tc->completed = 0;
    tc->dateOk    = tr_date();

    if( tc->stopped )
    {
        tc->stopped = 0;
    }
    else if( shouldChangePort( tc ) )
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
    char * address;

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
        tr_http_header_t hdr[] = { { "Location", NULL, 0 }, { NULL, NULL, 0 } };
        
        tr_err( "Scrape: HTTP status code: %i", code );

        tr_httpParse( data, len, hdr );
        
        address = calloc( hdr->len+1, sizeof( char ) );
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
        {
            tc->tcCur->tl_badscrape = 1;
        }
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
    if( !val1 || val1->type != TYPE_DICT || val1->val.l.count < 1 )
    {
        tr_bencFree( &scrape );
        return;
    }
    val1 = &val1->val.l.vals[1];
    
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
    
    val2 = tr_bencDictFind( &scrape, "flags" );
    if( val2 )
    {
        val2 = tr_bencDictFind( val2, "min_request_interval" );
        if( val2 )
        {
            tc->scrapeInterval = val2->val.i;
            tr_inf( "Scrape: min_request_interval = %d seconds", tc->scrapeInterval );
        }
    }
    
    tc->hasManyPeers = ( tc->seeders + tc->leechers >= 50 );
    
    tr_bencFree( &scrape );
}

int tr_trackerSeeders( const tr_tracker_t * tc )
{
    return tc ? tc->seeders : -1;
}

int tr_trackerLeechers( const tr_tracker_t * tc )
{
    return tc ? tc->leechers : -1;
}

int tr_trackerDownloaded( const tr_tracker_t * tc )
{
    return tc ? tc->complete : -1;
}

const tr_tracker_info_t * tr_trackerGet( const tr_tracker_t * tc )
{
    return tc ? tc->tcCur->tl_inf : NULL;
}

int tr_trackerCannotConnect( const tr_tracker_t * tc )
{
    return tc && tc->completelyUnconnectable;
}

uint64_t tr_trackerLastResponseDate ( const tr_tracker_t * tc )
{
    return tc ? tc->dateOk : 0;
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

    if( NULL == tc->tcCur->tl_inf->scrape || tc->tcCur->tl_badscrape )
    {
        return 1;
    }

    http = getScrapeQuery( tc );

    for( data = NULL; !data; tr_wait( 10 ) )
    {
        switch( tr_httpPulse( http, &data, &len ) )
        {
            case TR_NET_WAIT:
                break;

            case TR_NET_ERROR:
                goto scrapeDone;

            case TR_NET_OK:
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

static void killHttp( tr_http_t ** http )
{
    if( NULL != *http )
    {
        tr_httpClose( *http );
        *http = NULL;
    }
}

static int shouldChangePort( tr_tracker_t * tc )
{
    tr_torrent_t * tor = tc->tor;

    return ( tor->publicPort != tc->publicPort );
}

/* Convert to compact form */
static uint8_t *
parseOriginalPeers( benc_val_t * bePeers, int * peerCount )
{
    struct in_addr addr;
    in_port_t      port;
    uint8_t      * peerCompact;
    benc_val_t   * peer, * addrval, * portval;
    int            ii, count;

    assert( TYPE_LIST == bePeers->type );

    count  = 0;
    peerCompact = malloc( 6 * bePeers->val.l.count );
    if( NULL == peerCompact )
    {
        return NULL;
    }

    for( ii = 0; bePeers->val.l.count > ii; ii++ )
    {
        peer = &bePeers->val.l.vals[ii];
        addrval = tr_bencDictFind( peer, "ip" );
        if( NULL == addrval || TYPE_STR != addrval->type ||
            tr_netResolve( addrval->val.s.s, &addr ) )
        {
            continue;
        }
        memcpy( &peerCompact[6 * count], &addr, 4 );

        portval = tr_bencDictFind( peer, "port" );
        if( NULL == portval || TYPE_INT != portval->type ||
            0 > portval->val.i || 0xffff < portval->val.i )
        {
            continue;
        }
        port = htons( portval->val.i );
        memcpy( &peerCompact[6 * count + 4], &port, 2 );

        count++;
    }

    *peerCount = count;

    return peerCompact;
}
