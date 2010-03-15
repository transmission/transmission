/*
 * This file Copyright (C) 2010 Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id:$
 */

#include <glib/gstdio.h> /* g_remove() */
#include <gtk/gtk.h>

#include <libtransmission/transmission.h>
#include <libtransmission/web.h> /* tr_webRun() */

#include "favicon.h"
#include "util.h" /* gtr_mkdir_with_parents(), gtr_idle_add() */

struct favicon_data
{
    GFunc func;
    gpointer data;
    char * host;
    char * contents;
    size_t len;
};

static char*
favicon_get_cache_dir( void )
{
    static char * dir = NULL;

    if( dir == NULL )
    {
        dir = g_build_filename( g_get_user_cache_dir(),
                                "transmission",
                                "favicons",
                                NULL );
        gtr_mkdir_with_parents( dir, 0777 );
    }

    return dir;
}

static char*
favicon_get_cache_filename( const char * host )
{
    return g_build_filename( favicon_get_cache_dir(), host, NULL );
}

static void
favicon_save_cache_file( const char * host, const void * data, size_t len )
{
    char * filename = favicon_get_cache_filename( host );
    g_file_set_contents( filename, data, len, NULL );
    g_free( filename );
}

static GdkPixbuf*
favicon_load_from_data( const void * data, size_t len )
{
    GdkPixbuf * pixbuf = NULL;

    if( len > 0 )
    {
        GInputStream * i = g_memory_input_stream_new_from_data( data, len, NULL );
        pixbuf = gdk_pixbuf_new_from_stream_at_scale( i, 16, 16, TRUE, NULL, NULL );
        g_object_unref( i );
    }

    return pixbuf;
}

static gboolean
favicon_web_done_idle_cb( gpointer vfav )
{
    struct favicon_data * fav = vfav;
    GdkPixbuf * pixbuf = favicon_load_from_data( fav->contents, fav->len );

    if( pixbuf != NULL )
        favicon_save_cache_file( fav->host, fav->contents, fav->len );

    fav->func( pixbuf, fav->data );

    g_free( fav->host );
    g_free( fav->contents );
    g_free( fav );
    return FALSE;
}

static void
favicon_web_done_cb( tr_session    * session UNUSED,
                     long            code UNUSED,
                     const void    * data,
                     size_t          len,
                     void          * vfav )
{
    struct favicon_data * fav = vfav;
    fav->contents = g_memdup( data, len );
    fav->len = len;

    gtr_idle_add( favicon_web_done_idle_cb, fav );
}
    
static GdkPixbuf*
favicon_load_from_file( const char * host )
{
    gsize len;
    char * data;
    char * path = favicon_get_cache_filename( host );
    GdkPixbuf * pixbuf = NULL;

    if( g_file_get_contents( path, &data, &len, NULL ) )
    {
        pixbuf = favicon_load_from_data( data, len );
        if( pixbuf == NULL ) /* bad file... delete it from the cache */
            g_remove( path );
        g_free( data );
    }

    g_free( path );
    return pixbuf;
}
    
void
gtr_get_favicon( tr_session  * session,
                 const char  * host, 
                 GFunc         pixbuf_ready_func, 
                 gpointer      pixbuf_ready_func_data )
{
    GdkPixbuf * pixbuf = favicon_load_from_file( host );

    if( pixbuf != NULL )
    {
        pixbuf_ready_func( pixbuf, pixbuf_ready_func_data );
    }
    else
    {
        struct favicon_data * data;
        char * url = g_strdup_printf( "http://%s/favicon.ico", host );

        g_debug( "trying favicon from \"%s\"", url );
        data  = g_new( struct favicon_data, 1 );
        data->func = pixbuf_ready_func;
        data->data = pixbuf_ready_func_data;
        data->host = g_strdup( host );
        tr_webRun( session, url, NULL, favicon_web_done_cb, data );

        g_free( url );
    }
}
