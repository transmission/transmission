/*
 * This file Copyright (C) 2009 Charles Kerr <charles@transmissionbt.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id:$
 */
#ifdef WITH_INOTIFY
  #include <sys/inotify.h>
  #include <sys/select.h>
  #include <unistd.h> /* close */
#else
  #include <sys/types.h> /* stat */
  #include <sys/stat.h> /* stat */
  #include <dirent.h> /* readdir */
  #include <event.h> /* evbuffer */
#endif

#include <errno.h>
#include <string.h> /* strstr */

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h> /* tr_buildPath(), tr_inf() */
#include "watch.h"

struct dtr_watchdir
{
    tr_session * session;
    char * dir;
    dtr_watchdir_callback * callback;
#ifdef WITH_INOTIFY
    int inotify_fd;
#else /* readdir implementation */
    time_t lastTimeChecked;
    struct evbuffer * lastFiles;
#endif
};

/***
****  INOTIFY IMPLEMENTATION
***/

#if defined(WITH_INOTIFY)

/* how many inotify events to try to batch into a single read */
#define EVENT_BATCH_COUNT 50
/* size of the event structure, not counting name */
#define EVENT_SIZE  (sizeof (struct inotify_event))
/* reasonable guess as to size of 50 events */
#define BUF_LEN (EVENT_BATCH_COUNT * (EVENT_SIZE + 16) + 2048)

#define DTR_INOTIFY_MASK (IN_CREATE|IN_CLOSE_WRITE|IN_MOVED_TO)

static void
watchdir_new_impl( dtr_watchdir * w )
{
    int i;
    w->inotify_fd = inotify_init( );
    tr_inf( "Using inotify to watch directory \"%s\"", w->dir );
    i = inotify_add_watch( w->inotify_fd, w->dir, DTR_INOTIFY_MASK );
    if( i < 0 )
        tr_err( "Unable to watch \"%s\": %s", w->dir, strerror (errno) );
}
static void
watchdir_free_impl( dtr_watchdir * w )
{
    inotify_rm_watch( w->inotify_fd, DTR_INOTIFY_MASK );
    close( w->inotify_fd );
}
static void
watchdir_update_impl( dtr_watchdir * w )
{
    int ret;
    fd_set rfds;
    struct timeval time;
    const int fd = w->inotify_fd;

    /* timeout after one second */
    time.tv_sec = 1;
    time.tv_usec = 0;

    /* make the fd_set hold the inotify fd */
    FD_ZERO( &rfds );
    FD_SET( fd, &rfds );

    /* check for added files */
    ret = select( fd+1, &rfds, NULL, NULL, &time );
    if( ret < 0 ) {
        perror( "select" );
    } else if( !ret ) {
        /* timed out! */
    } else if( FD_ISSET( fd, &rfds ) ) {
        int i = 0;
        char buf[BUF_LEN];
        int len = read( fd, buf, sizeof( buf ) );
        while (i < len) {
            struct inotify_event * event = (struct inotify_event *) &buf[i];
            tr_inf( "Found new .torrent file \"%s\" in watchdir \"%s\"", event->name, w->dir );
            w->callback( w->session, w->dir, event->name );
            i += EVENT_SIZE +  event->len;
        }
    }
}

#else /* WITH_INOTIFY */

/***
****  READDIR IMPLEMENTATION
***/

#define WATCHDIR_POLL_INTERVAL_SECS 10

#define FILE_DELIMITER '\0'

static void
watchdir_new_impl( dtr_watchdir * w UNUSED )
{
    tr_inf( "Using readdir to watch directory \"%s\"", w->dir );
    w->lastFiles = evbuffer_new( );
}
static void
watchdir_free_impl( dtr_watchdir * w )
{
    evbuffer_free( w->lastFiles );
}
static void
add_file_to_list( struct evbuffer * buf, const char * filename, size_t len )
{
    const char delimiter = FILE_DELIMITER;
    evbuffer_add( buf, &delimiter, 1 );
    evbuffer_add( buf, filename, len );
    evbuffer_add( buf, &delimiter, 1 );
}
static tr_bool
is_file_in_list( struct evbuffer * buf, const char * filename, size_t len )
{
    tr_bool in_list;
    struct evbuffer * test = evbuffer_new( );
    add_file_to_list( test, filename, len );
    in_list = evbuffer_find( buf, EVBUFFER_DATA( test ), EVBUFFER_LENGTH( test ) );
    evbuffer_free( test );
    return in_list;
}
static void
watchdir_update_impl( dtr_watchdir * w )
{
    struct stat sb;
    DIR * odir;
    const time_t oldTime = w->lastTimeChecked;
    const char * dirname = w->dir;
    struct evbuffer * curFiles = evbuffer_new( );

    if ( ( oldTime + WATCHDIR_POLL_INTERVAL_SECS < time( NULL ) )
         && !stat( dirname, &sb )
         && S_ISDIR( sb.st_mode )
         && (( odir = opendir( dirname ))) )
    {
        struct dirent * d;

        for( d = readdir( odir ); d != NULL; d = readdir( odir ) )
        {
            size_t len;

            if( !d->d_name || *d->d_name=='.' ) /* skip dotfiles */
                continue;
            if( !strstr( d->d_name, ".torrent" ) ) /* skip non-torrents */
                continue;

            len = strlen( d->d_name );
            add_file_to_list( curFiles, d->d_name, len );

            /* if this file wasn't here last time, try adding it */
            if( !is_file_in_list( w->lastFiles, d->d_name, len ) ) {
                tr_inf( "Found new .torrent file \"%s\" in watchdir \"%s\"", d->d_name, w->dir );
                w->callback( w->session, w->dir, d->d_name );
            }
        }

        closedir( odir );
        w->lastTimeChecked = time( NULL );
        evbuffer_free( w->lastFiles );
        w->lastFiles = curFiles;
    }
}

#endif

/***
****
***/

dtr_watchdir*
dtr_watchdir_new( tr_session * session, const char * dir, dtr_watchdir_callback * callback )
{
    dtr_watchdir * w = tr_new0( dtr_watchdir, 1 );

    w->session = session;
    w->dir = tr_strdup( dir );
    w->callback = callback;

    watchdir_new_impl( w );

    return w;
}

void
dtr_watchdir_update( dtr_watchdir * w )
{
    if( w != NULL )
        watchdir_update_impl( w );
}

void
dtr_watchdir_free( dtr_watchdir * w )
{
    if( w != NULL )
    {
        watchdir_free_impl( w );
        tr_free( w->dir );
        tr_free( w );
    }
}
