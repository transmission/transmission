/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2. Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#include <string.h> /* strlen() */

#include <event2/buffer.h>
#include <event2/event.h>

#include "transmission.h"
#include "bandwidth.h"
#include "cache.h"
#include "inout.h" /* tr_ioFindFileLocation() */
#include "list.h"
#include "peer-mgr.h"
#include "torrent.h"
#include "utils.h"
#include "web.h"
#include "webseed.h"

struct tr_webseed_task
{
    tr_session         * session;
    struct evbuffer    * content;
    struct tr_webseed  * webseed;
    tr_block_index_t     block;
    tr_piece_index_t     piece_index;
    uint32_t             piece_offset;
    uint32_t             length;
    int                  torrent_id;
};

struct tr_webseed
{
    tr_peer              parent;
    tr_bandwidth         bandwidth;
    tr_session         * session;
    tr_peer_callback   * callback;
    void               * callback_data;
    tr_list            * tasks;
    struct event       * timer;
    char               * base_url;
    size_t               base_url_len;
    int                  torrent_id;
    bool                 is_stopping;
    int                  consecutive_failures;
};

enum
{
    TR_IDLE_TIMER_MSEC = 2000,

    MAX_CONSECUIVE_FAILURES = 5
};

static void
webseed_free( struct tr_webseed * w )
{
    tr_torrent * tor = tr_torrentFindFromId( w->session, w->torrent_id );

    /* webseed destruct */
    event_free( w->timer );
    tr_bandwidthDestruct( &w->bandwidth );
    tr_free( w->base_url );

    /* parent class destruct */
    tr_peerDestruct( tor, &w->parent );

    tr_free( w );
}

/***
****
***/

static void
publish( tr_webseed * w, tr_peer_event * e )
{
    if( w->callback != NULL )
        w->callback( &w->parent, e, w->callback_data );
}

static void
fire_client_got_rej( tr_torrent * tor, tr_webseed * w, tr_block_index_t block )
{
    tr_peer_event e = TR_PEER_EVENT_INIT;
    e.eventType = TR_PEER_CLIENT_GOT_REJ;
    tr_torrentGetBlockLocation( tor, block, &e.pieceIndex, &e.offset, &e.length );
    publish( w, &e );
}

static void
fire_client_got_block( tr_torrent * tor, tr_webseed * w, tr_block_index_t block )
{
    tr_peer_event e = TR_PEER_EVENT_INIT;
    e.eventType = TR_PEER_CLIENT_GOT_BLOCK;
    tr_torrentGetBlockLocation( tor, block, &e.pieceIndex, &e.offset, &e.length );
    publish( w, &e );
}

static void
fire_client_got_data( tr_webseed * w, uint32_t length )
{
    tr_peer_event e = TR_PEER_EVENT_INIT;
    e.eventType = TR_PEER_CLIENT_GOT_DATA;
    e.length = length;
    e.wasPieceData = true;
    publish( w, &e );
}

/***
****
***/

static void
on_content_changed( struct evbuffer                * buf UNUSED,
                    const struct evbuffer_cb_info  * info,
                    void                           * vw )
{
    tr_webseed * w = vw;

    if( ( info->n_added > 0 ) && !w->is_stopping )
    {
        tr_bandwidthUsed( &w->bandwidth, TR_DOWN, info->n_added, true, tr_time_msec( ) );
        fire_client_got_data( w, info->n_added );
    }
}

static void task_request_next_chunk( struct tr_webseed_task * task );

static bool
webseed_has_tasks( const tr_webseed * w )
{
    return w->tasks != NULL;
}


static void
on_idle( tr_webseed * w )
{
    tr_torrent * tor = tr_torrentFindFromId( w->session, w->torrent_id );

    if( w->is_stopping && !webseed_has_tasks( w ) )
    {
        webseed_free( w );
    }
    else if( !w->is_stopping && tor
                             && tor->isRunning
                             && !tr_torrentIsSeed( tor )
                             && ( w->consecutive_failures < MAX_CONSECUIVE_FAILURES ) )
    {
        int i;
        int got = 0;
        const int max = tor->blockCountInPiece;
        const int want = max - tr_list_size( w->tasks );
        tr_block_index_t * blocks = NULL;

        if( want > 0 )
        {
            blocks = tr_new( tr_block_index_t, want );
            tr_peerMgrGetNextRequests( tor, &w->parent, want, blocks, &got );
        }

        for( i=0; i<got; ++i )
        {
            const tr_block_index_t b = blocks[i];
            struct tr_webseed_task * task = tr_new( struct tr_webseed_task, 1 );
            task->webseed = w;
            task->session = w->session;
            task->torrent_id = w->torrent_id;
            task->block = b;
            task->piece_index = tr_torBlockPiece( tor, b );
            task->piece_offset = ( tor->blockSize * b )
                                - ( tor->info.pieceSize * task->piece_index );
            task->length = tr_torBlockCountBytes( tor, b );
            task->content = evbuffer_new( );
            evbuffer_add_cb( task->content, on_content_changed, w );
            tr_list_append( &w->tasks, task );
            task_request_next_chunk( task );
        }

        tr_free( blocks );
    }
}


static void
web_response_func( tr_session    * session,
                   bool            did_connect UNUSED,
                   bool            did_timeout UNUSED,
                   long            response_code,
                   const void    * response UNUSED,
                   size_t          response_byte_count UNUSED,
                   void          * vtask )
{
    struct tr_webseed_task * t = vtask;
    tr_webseed * w = t->webseed;
    tr_torrent * tor = tr_torrentFindFromId( session, t->torrent_id );
    const int success = ( response_code == 206 );

    if( success )
        w->consecutive_failures = 0;
    else
        ++w->consecutive_failures;

    if( tor )
    {
        if( !success )
        {
            fire_client_got_rej( tor, w, t->block );

            tr_list_remove_data( &w->tasks, t );
            evbuffer_free( t->content );
            tr_free( t );
        }
        else
        {
            if( evbuffer_get_length( t->content ) < t->length )
            {
                task_request_next_chunk( t );
            }
            else
            {
                tr_cacheWriteBlock( session->cache, tor,
                                    t->piece_index, t->piece_offset, t->length,
                                    t->content );
                fire_client_got_block( tor, w, t->block );

                tr_list_remove_data( &w->tasks, t );
                evbuffer_free( t->content );
                tr_free( t );

                on_idle( w );
            }
        }
    }
}

static char*
make_url( tr_webseed * w, const tr_file * file )
{
    struct evbuffer * out = evbuffer_new( );

    evbuffer_add( out, w->base_url, w->base_url_len );

    /* if url ends with a '/', add the torrent name */
    if( w->base_url[w->base_url_len - 1] == '/' && file->name )
        tr_http_escape( out, file->name, strlen(file->name), false );

    return evbuffer_free_to_str( out );
}

static void
task_request_next_chunk( struct tr_webseed_task * t )
{
    tr_torrent * tor = tr_torrentFindFromId( t->session, t->torrent_id );
    if( tor != NULL )
    {
        char * url;
        char range[64];

        const tr_info * inf = tr_torrentInfo( tor );
        const uint32_t remain = t->length - evbuffer_get_length( t->content );

        const uint64_t total_offset = inf->pieceSize * t->piece_index
                                    + t->piece_offset
                                    + evbuffer_get_length( t->content );
        const tr_piece_index_t step_piece = total_offset / inf->pieceSize;
        const uint32_t step_piece_offset
                               = total_offset - ( inf->pieceSize * step_piece );

        tr_file_index_t file_index;
        uint64_t file_offset;
        const tr_file * file;
        uint32_t this_pass;

        tr_ioFindFileLocation( tor, step_piece, step_piece_offset,
                                    &file_index, &file_offset );
        file = &inf->files[file_index];
        this_pass = MIN( remain, file->length - file_offset );

        url = make_url( t->webseed, file );
        tr_snprintf( range, sizeof range, "%"PRIu64"-%"PRIu64,
                     file_offset, file_offset + this_pass - 1 );
        tr_webRunWithBuffer( t->session, url, range, NULL,
                             web_response_func, t, t->content );
        tr_free( url );
    }
}

bool
tr_webseedGetSpeed_Bps( const tr_webseed * w, uint64_t now, int * setme_Bps )
{
    const bool is_active = webseed_has_tasks( w );
    *setme_Bps = is_active ? tr_bandwidthGetPieceSpeed_Bps( &w->bandwidth, now, TR_DOWN ) : 0;
    return is_active;
}

bool
tr_webseedIsActive( const tr_webseed * w )
{
    int Bps = 0;
    return tr_webseedGetSpeed_Bps( w, tr_time_msec(), &Bps ) && ( Bps > 0 );
}

/***
****
***/

static void
webseed_timer_func( evutil_socket_t foo UNUSED, short bar UNUSED, void * vw )
{
    tr_webseed * w = vw;
    on_idle( w );
    tr_timerAddMsec( w->timer, TR_IDLE_TIMER_MSEC );
}

tr_webseed*
tr_webseedNew( struct tr_torrent  * tor,
               const char         * url,
               tr_peer_callback   * callback,
               void               * callback_data )
{
    tr_webseed * w = tr_new0( tr_webseed, 1 );
    tr_peer * peer = &w->parent;

    /* construct parent class */
    tr_peerConstruct( peer );
    peer->peerIsChoked = true;
    peer->clientIsInterested = !tr_torrentIsSeed( tor );
    peer->client = tr_strdup( "webseed" );
    tr_bitfieldSetHasAll( &peer->have );
    tr_peerUpdateProgress( tor, peer );

    w->torrent_id = tr_torrentId( tor );
    w->session = tor->session;
    w->base_url_len = strlen( url );
    w->base_url = tr_strndup( url, w->base_url_len );
    w->callback = callback;
    w->callback_data = callback_data;
    //tr_rcConstruct( &w->download_rate );
    tr_bandwidthConstruct( &w->bandwidth, tor->session, &tor->bandwidth );
    w->timer = evtimer_new( w->session->event_base, webseed_timer_func, w );
    tr_timerAddMsec( w->timer, TR_IDLE_TIMER_MSEC );
    return w;
}

void
tr_webseedFree( tr_webseed * w )
{
    if( w )
    {
        if( webseed_has_tasks( w ) )
            w->is_stopping = true;
        else
            webseed_free( w );
    }
}
