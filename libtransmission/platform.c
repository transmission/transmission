/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005 Transmission authors and contributors
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

#ifdef SYS_BEOS
  #include <fs_info.h>
  #include <FindDirectory.h>
#endif
#include <sys/types.h>
#include <dirent.h>

#include "transmission.h"

#if !defined( SYS_BEOS ) && !defined( __AMIGAOS4__ )

#include <pwd.h>

const char *
tr_getHomeDirectory( void )
{
    static char     homeDirectory[MAX_PATH_LENGTH];
    static int      init = 0;
    char          * envHome;
    struct passwd * pw;

    if( init )
    {
        return homeDirectory;
    }

    envHome = getenv( "HOME" );
    if( NULL == envHome )
    {
        pw = getpwuid( getuid() );
        endpwent();
        if( NULL == pw )
        {
            /* XXX need to handle this case */
            return NULL;
        }
        envHome = pw->pw_dir;
    }

    snprintf( homeDirectory, MAX_PATH_LENGTH, "%s", envHome );
    init = 1;

    return homeDirectory;
}

#else

const char *
tr_getHomeDirectory( void )
{
    /* XXX */
    return "";
}

#endif /* !SYS_BEOS && !__AMIGAOS4__ */

static void
tr_migrateResume( const char *oldDirectory, const char *newDirectory )
{
    DIR * dirh = opendir( oldDirectory );

    if( dirh != NULL )
    {
        struct dirent * dirp;

        while( ( dirp = readdir( dirh ) ) )
        {
            if( !strncmp( "resume.", dirp->d_name, 7 ) )
            {
                char o[MAX_PATH_LENGTH];
                char n[MAX_PATH_LENGTH];
                tr_buildPath( o, sizeof(o), oldDirectory, dirp->d_name, NULL );
                tr_buildPath( n, sizeof(n), newDirectory, dirp->d_name, NULL );
                rename( o, n );
            }
        }

        closedir( dirh );
    }
}

const char *
tr_getPrefsDirectory( void )
{
    static char   buf[MAX_PATH_LENGTH];
    static int    init = 0;
    static size_t buflen = sizeof(buf);
    const char* h;

    if( init )
        return buf;

    h = tr_getHomeDirectory();
#ifdef SYS_BEOS
    find_directory( B_USER_SETTINGS_DIRECTORY,
                    dev_for_path("/boot"), true, buf, buflen );
    strcat( buf, "/Transmission" );
#elif defined( SYS_DARWIN )
    tr_buildPath ( buf, buflen, h,
                  "Library", "Application Support", "Transmission", NULL );
#elif defined(__AMIGAOS4__)
    snprintf( buf, buflen, "PROGDIR:.transmission" );
#else
    tr_buildPath ( buf, buflen, h, ".transmission", NULL );
#endif

    tr_mkdir( buf );
    init = 1;

#ifdef SYS_DARWIN
    char old[MAX_PATH_LENGTH];
    tr_buildPath ( old, sizeof(old), h, ".transmission", NULL );
    tr_migrateResume( old, buf );
    rmdir( old );
#endif

    return buf;
}

const char *
tr_getCacheDirectory( void )
{
    static char buf[MAX_PATH_LENGTH];
    static int  init = 0;
    static const size_t buflen = sizeof(buf);
    const char * p;

    if( init )
        return buf;

    p = tr_getPrefsDirectory();
#ifdef SYS_BEOS
    tr_buildPath( buf, buflen, p, "Cache", NULL );
#elif defined( SYS_DARWIN )
    tr_buildPath( buf, buflen, tr_getHomeDirectory(),
                  "Library", "Caches", "Transmission", NULL );
#else
    tr_buildPath( buf, buflen, p, "cache", NULL );
#endif

    tr_mkdir( buf );
    init = 1;

    if( strcmp( p, buf ) )
        tr_migrateResume( p, buf );

    return buf;
}

const char *
tr_getTorrentsDirectory( void )
{
    static char buf[MAX_PATH_LENGTH];
    static int  init = 0;
    static const size_t buflen = sizeof(buf);
    const char * p;

    if( init )
        return buf;

    p = tr_getPrefsDirectory ();

#ifdef SYS_BEOS
    tr_buildPath( buf, buflen, p, "Torrents", NULL );
#elif defined( SYS_DARWIN )
    tr_buildPath( buf, buflen, p, "Torrents", NULL );
#else
    tr_buildPath( buf, buflen, p, "torrents", NULL );
#endif

    tr_mkdir( buf );
    init = 1;
    return buf;
}

static void ThreadFunc( void * _t )
{
    tr_thread_t * t = _t;
    char* name = tr_strdup( t->name );

#ifdef SYS_BEOS
    /* This is required because on BeOS, SIGINT is sent to each thread,
       which kills them not nicely */
    signal( SIGINT, SIG_IGN );
#endif

    tr_dbg( "Thread '%s' started", name );
    t->func( t->arg );
    tr_dbg( "Thread '%s' exited", name );
    tr_free( name );
}

void tr_threadCreate( tr_thread_t * t,
                      void (*func)(void *), void * arg,
                      const char * name )
{
    t->func = func;
    t->arg  = arg;
    t->name = tr_strdup( name );
#ifdef SYS_BEOS
    t->thread = spawn_thread( (void *) ThreadFunc, name,
                              B_NORMAL_PRIORITY, t );
    resume_thread( t->thread );
#else
    pthread_create( &t->thread, NULL, (void *) ThreadFunc, t );
#endif
}

const tr_thread_t THREAD_EMPTY = { NULL, NULL, NULL, 0 };

void tr_threadJoin( tr_thread_t * t )
{
    if( t->func != NULL )
    {
#ifdef SYS_BEOS
        long exit;
        wait_for_thread( t->thread, &exit );
#else
        pthread_join( t->thread, NULL );
#endif
        tr_dbg( "Thread '%s' joined", t->name );
        tr_free( t->name );
        t->name = NULL;
        t->func = NULL;
    }
}

void tr_lockInit( tr_lock_t * l )
{
#ifdef SYS_BEOS
    *l = create_sem( 1, "" );
#else
    pthread_mutex_init( l, NULL );
#endif
}

void tr_lockClose( tr_lock_t * l )
{
#ifdef SYS_BEOS
    delete_sem( *l );
#else
    pthread_mutex_destroy( l );
#endif
}

int tr_lockTryLock( tr_lock_t * l )
{
#ifdef SYS_BEOS
    #error how is this done in beos
#else
    /* success on zero! */
    return pthread_mutex_trylock( l );
#endif
}

void tr_lockLock( tr_lock_t * l )
{
#ifdef SYS_BEOS
    acquire_sem( *l );
#else
    pthread_mutex_lock( l );
#endif
}

void tr_lockUnlock( tr_lock_t * l )
{
#ifdef SYS_BEOS
    release_sem( *l );
#else
    pthread_mutex_unlock( l );
#endif
}


void tr_condInit( tr_cond_t * c )
{
#ifdef SYS_BEOS
    *c = -1;
#else
    pthread_cond_init( c, NULL );
#endif
}

void tr_condWait( tr_cond_t * c, tr_lock_t * l )
{
#ifdef SYS_BEOS
    *c = find_thread( NULL );
    release_sem( *l );
    suspend_thread( *c );
    acquire_sem( *l );
    *c = -1;
#else
    pthread_cond_wait( c, l );
#endif
}

void tr_condSignal( tr_cond_t * c )
{
#ifdef SYS_BEOS
    while( *c != -1 )
    {
        thread_info info;
        get_thread_info( *c, &info );
        if( info.state == B_THREAD_SUSPENDED )
        {
            resume_thread( *c );
            break;
        }
        snooze( 5000 );
    }
#else
    pthread_cond_signal( c );
#endif
}
void tr_condBroadcast( tr_cond_t * c )
{
#ifdef SYS_BEOS
    #error how is this done in beos
#else
    pthread_cond_broadcast( c );
#endif
}

void tr_condClose( tr_cond_t * c )
{
#ifdef SYS_BEOS
    *c = -1; /* Shut up gcc */
#else
    pthread_cond_destroy( c );
#endif
}


#if defined( BSD )

#include <sys/sysctl.h>
#include <net/route.h>

static uint8_t *
getroute( int * buflen );
static int
parseroutes( uint8_t * buf, int len, struct in_addr * addr );

int
tr_getDefaultRoute( struct in_addr * addr )
{
    uint8_t * buf;
    int len;

    buf = getroute( &len );
    if( NULL == buf )
    {
        tr_err( "failed to get default route (BSD)" );
        return 1;
    }

    len = parseroutes( buf, len, addr );
    free( buf );

    return len;
}

#ifndef SA_SIZE
#define ROUNDUP( a, size ) \
    ( ( (a) & ( (size) - 1 ) ) ? ( 1 + ( (a) | ( (size) - 1 ) ) ) : (a) )
#define SA_SIZE( sap ) \
    ( sap->sa_len ? ROUNDUP( (sap)->sa_len, sizeof( u_long ) ) : \
                    sizeof( u_long ) )
#endif /* !SA_SIZE */
#define NEXT_SA( sap ) \
    (struct sockaddr *) ( (caddr_t) (sap) + ( SA_SIZE( (sap) ) ) )

static uint8_t *
getroute( int * buflen )
{
    int     mib[6];
    size_t  len;
    uint8_t * buf;

    mib[0] = CTL_NET;
    mib[1] = PF_ROUTE;
    mib[2] = 0;
    mib[3] = AF_INET;
    mib[4] = NET_RT_FLAGS;
    mib[5] = RTF_GATEWAY;

    if( sysctl( mib, 6, NULL, &len, NULL, 0 ) )
    {
        if( ENOENT != errno )
        {
            tr_err( "sysctl net.route.0.inet.flags.gateway failed (%s)",
                    strerror( errno ) );
        }
        *buflen = 0;
        return NULL;
    }

    buf = malloc( len );
    if( NULL == buf )
    {
        *buflen = 0;
        return NULL;
    }

    if( sysctl( mib, 6, buf, &len, NULL, 0 ) )
    {
        tr_err( "sysctl net.route.0.inet.flags.gateway failed (%s)",
                strerror( errno ) );
        free( buf );
        *buflen = 0;
        return NULL;
    }

    *buflen = len;

    return buf;
}

static int
parseroutes( uint8_t * buf, int len, struct in_addr * addr )
{
    uint8_t            * end;
    struct rt_msghdr   * rtm;
    struct sockaddr    * sa;
    struct sockaddr_in * sin;
    int                  ii;
    struct in_addr       dest, gw;

    end = buf + len;
    while( end > buf + sizeof( *rtm ) )
    {
        rtm = (struct rt_msghdr *) buf;
        buf += rtm->rtm_msglen;
        if( end >= buf )
        {
            dest.s_addr = INADDR_NONE;
            gw.s_addr   = INADDR_NONE;
            sa = (struct sockaddr *) ( rtm + 1 );

            for( ii = 0; ii < RTAX_MAX && (uint8_t *) sa < buf; ii++ )
            {
                if( buf < (uint8_t *) NEXT_SA( sa ) )
                {
                    break;
                }

                if( rtm->rtm_addrs & ( 1 << ii ) )
                {
                    if( AF_INET == sa->sa_family )
                    {
                        sin = (struct sockaddr_in *) sa;
                        switch( ii )
                        {
                            case RTAX_DST:
                                dest = sin->sin_addr;
                                break;
                            case RTAX_GATEWAY:
                                gw = sin->sin_addr;
                                break;
                        }
                    }
                    sa = NEXT_SA( sa );
                }
            }

            if( INADDR_ANY == dest.s_addr && INADDR_NONE != gw.s_addr )
            {
                *addr = gw;
                return 0;
            }
        }
    }

    return 1;
}

#elif defined( linux ) || defined( __linux ) || defined( __linux__ )

#include <linux/types.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#define SEQNUM 195909

static int
getsock( void );
static uint8_t *
getroute( int fd, unsigned int * buflen );
static int
parseroutes( uint8_t * buf, unsigned int len, struct in_addr * addr );

int
tr_getDefaultRoute( struct in_addr * addr )
{
    int fd, ret;
    unsigned int len;
    uint8_t * buf;

    ret = 1;
    fd = getsock();
    if( 0 <= fd )
    {
        while( ret )
        {
            buf = getroute( fd, &len );
            if( NULL == buf )
            {
                break;
            }
            ret = parseroutes( buf, len, addr );
            free( buf );
        }
        close( fd );
    }

    if( ret )
    {
        tr_err( "failed to get default route (Linux)" );
    }

    return ret;
}

static int
getsock( void )
{
    int fd, flags;
    struct
    {
        struct nlmsghdr nlh;
        struct rtgenmsg rtg;
    } req;
    struct sockaddr_nl snl;

    fd = socket( PF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE );
    if( 0 > fd )
    {
        tr_err( "failed to create routing socket (%s)", strerror( errno ) );
        return -1;
    }

    flags = fcntl( fd, F_GETFL );
    if( 0 > flags || 0 > fcntl( fd, F_SETFL, O_NONBLOCK | flags ) )
    {
        tr_err( "failed to set socket nonblocking (%s)", strerror( errno ) );
        close( fd );
        return -1;
    }

    bzero( &snl, sizeof( snl ) );
    snl.nl_family = AF_NETLINK;

    bzero( &req, sizeof( req ) );
    req.nlh.nlmsg_len = NLMSG_LENGTH( sizeof( req.rtg ) );
    req.nlh.nlmsg_type = RTM_GETROUTE;
    req.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    req.nlh.nlmsg_seq = SEQNUM;
    req.nlh.nlmsg_pid = 0;
    req.rtg.rtgen_family = AF_INET;

    if( 0 > sendto( fd, &req, sizeof( req ), 0,
                    (struct sockaddr *) &snl, sizeof( snl ) ) )
    {
        tr_err( "failed to write to routing socket (%s)", strerror( errno ) );
        close( fd );
        return -1;
    }

    return fd;
}

static uint8_t *
getroute( int fd, unsigned int * buflen )
{
    void             * buf;
    unsigned int       len;
    ssize_t            res;
    struct sockaddr_nl snl;
    socklen_t          slen;

    len = 8192;
    buf = calloc( 1, len );
    if( NULL == buf )
    {
        *buflen = 0;
        return NULL;
    }

    for( ;; )
    {
        bzero( &snl, sizeof( snl ) );
        slen = sizeof( snl );
        res = recvfrom( fd, buf, len, 0, (struct sockaddr *) &snl, &slen );
        if( 0 > res )
        {
            if( EAGAIN != errno )
            {
                tr_err( "failed to read from routing socket (%s)",
                        strerror( errno ) );
            }
            free( buf );
            *buflen = 0;
            return NULL;
        }
        if( slen < sizeof( snl ) || AF_NETLINK != snl.nl_family )
        {
            tr_err( "bad address" );
            free( buf );
            *buflen = 0;
            return NULL;
        }

        if( 0 == snl.nl_pid )
        {
            break;
        }
    }

    *buflen = res;

    return buf;
}

static int
parseroutes( uint8_t * buf, unsigned int len, struct in_addr * addr )
{
    struct nlmsghdr * nlm;
    struct nlmsgerr * nle;
    struct rtmsg    * rtm;
    struct rtattr   * rta;
    int               rtalen;
    struct in_addr    gw, dst;

    nlm = ( struct nlmsghdr * ) buf;
    while( NLMSG_OK( nlm, len ) )
    {
        gw.s_addr = INADDR_ANY;
        dst.s_addr = INADDR_ANY;
        if( NLMSG_ERROR == nlm->nlmsg_type )
        {
            nle = (struct nlmsgerr *) NLMSG_DATA( nlm );
            if( NLMSG_LENGTH( NLMSG_ALIGN( sizeof( struct nlmsgerr ) ) ) >
                nlm->nlmsg_len )
            {
                tr_err( "truncated netlink error" );
            }
            else
            {
                tr_err( "netlink error (%s)", strerror( nle->error ) );
            }
            return 1;
        }
        else if( RTM_NEWROUTE == nlm->nlmsg_type && SEQNUM == nlm->nlmsg_seq &&
                 getpid() == (pid_t) nlm->nlmsg_pid &&
                 NLMSG_LENGTH( sizeof( struct rtmsg ) ) <= nlm->nlmsg_len )
        {
            rtm = NLMSG_DATA( nlm );
            rta = RTM_RTA( rtm );
            rtalen = RTM_PAYLOAD( nlm );

            while( RTA_OK( rta, rtalen ) )
            {
                if( sizeof( struct in_addr ) <= RTA_PAYLOAD( rta ) )
                {
                    switch( rta->rta_type )
                    {
                        case RTA_GATEWAY:
                            memcpy( &gw, RTA_DATA( rta ), sizeof( gw ) );
                            break;
                        case RTA_DST:
                            memcpy( &dst, RTA_DATA( rta ), sizeof( dst ) );
                            break;
                    }
                }
                rta = RTA_NEXT( rta, rtalen );
            }
        }

        if( INADDR_NONE != gw.s_addr && INADDR_ANY != gw.s_addr &&
            INADDR_ANY == dst.s_addr )
        {
            *addr = gw;
            return 0;
        }

        nlm = NLMSG_NEXT( nlm, len );
    }

    return 1;
}

#else /* not BSD or Linux */

int
tr_getDefaultRoute( struct in_addr * addr UNUSED )
{
    tr_inf( "don't know how to get default route on this platform" );
    return 1;
}

#endif

/***
****
***/

static void
tr_rwSignal( tr_rwlock_t * rw )
{
  if ( rw->wantToWrite )
    tr_condSignal( &rw->writeCond );
  else if ( rw->wantToRead )
    tr_condBroadcast( &rw->readCond );
}

void
tr_rwInit ( tr_rwlock_t * rw )
{
    memset( rw, 0, sizeof(tr_rwlock_t) );
    tr_lockInit( &rw->lock );
    tr_condInit( &rw->readCond );
    tr_condInit( &rw->writeCond );
}

void
tr_rwReaderLock( tr_rwlock_t * rw )
{
    tr_lockLock( &rw->lock );
    rw->wantToRead++;
    while( rw->haveWriter || rw->wantToWrite )
        tr_condWait( &rw->readCond, &rw->lock );
    rw->wantToRead--;
    rw->readCount++;
    tr_lockUnlock( &rw->lock );
}

int
tr_rwReaderTrylock( tr_rwlock_t * rw )
{
    int ret = FALSE;
    tr_lockLock( &rw->lock );
    if ( !rw->haveWriter && !rw->wantToWrite ) {
        rw->readCount++;
        ret = TRUE;
    }
    tr_lockUnlock( &rw->lock );
    return ret;

}

void
tr_rwReaderUnlock( tr_rwlock_t * rw )
{
    tr_lockLock( &rw->lock );
    --rw->readCount;
    if( !rw->readCount )
        tr_rwSignal( rw );
    tr_lockUnlock( &rw->lock );
}

void
tr_rwWriterLock( tr_rwlock_t * rw )
{
    tr_lockLock( &rw->lock );
    rw->wantToWrite++;
    while( rw->haveWriter || rw->readCount )
        tr_condWait( &rw->writeCond, &rw->lock );
    rw->wantToWrite--;
    rw->haveWriter = TRUE;
    tr_lockUnlock( &rw->lock );
}

int
tr_rwWriterTrylock( tr_rwlock_t * rw )
{
    int ret = FALSE;
    tr_lockLock( &rw->lock );
    if( !rw->haveWriter && !rw->readCount )
        ret = rw->haveWriter = TRUE;
    tr_lockUnlock( &rw->lock );
    return ret;
}
void
tr_rwWriterUnlock( tr_rwlock_t * rw )
{
    tr_lockLock( &rw->lock );
    rw->haveWriter = FALSE;
    tr_rwSignal( rw );
    tr_lockUnlock( &rw->lock );
}

void
tr_rwClose( tr_rwlock_t * rw )
{
    tr_condClose( &rw->writeCond );
    tr_condClose( &rw->readCond );
    tr_lockClose( &rw->lock );
}
