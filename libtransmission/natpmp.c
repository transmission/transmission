/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2006 Transmission authors and contributors
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

#define PMP_PORT                5351
#define PMP_MCAST_ADDR          "224.0.0.1"
#define PMP_INITIAL_DELAY       250     /* ms, 1/4 second */
#define PMP_TOTAL_DELAY         120000  /* ms, 2 minutes */
#define PMP_VERSION             0
#define PMP_OPCODE_GETIP        0
#define PMP_OPCODE_ADDUDP       1
#define PMP_OPCODE_ADDTCP       2
#define PMP_LIFETIME            3600    /* secs, one hour */
#define PMP_RESPCODE_OK         0
#define PMP_RESPCODE_BADVERS    1
#define PMP_RESPCODE_REFUSED    2
#define PMP_RESPCODE_NETDOWN    3
#define PMP_RESPCODE_NOMEM      4
#define PMP_RESPCODE_BADOPCODE  5

#define PMP_OPCODE_FROM_RESPONSE( op )  ( 0x80 ^ (op) )
#define PMP_OPCODE_TO_RESPONSE( op )    ( 0x80 | (op) )
#define PMP_OPCODE_IS_RESPONSE( op )    ( 0x80 & (op) )
#define PMP_TOBUF16( buf, num ) ( *( (uint16_t *) (buf) ) = htons( (num) ) )
#define PMP_TOBUF32( buf, num ) ( *( (uint32_t *) (buf) ) = htonl( (num) ) )
#define PMP_FROMBUF16( buf )    ( htons( *( (uint16_t *) (buf) ) ) )
#define PMP_FROMBUF32( buf )    ( htonl( *( (uint32_t *) (buf) ) ) )

typedef struct tr_natpmp_uptime_s
{
    time_t   when;
    uint32_t uptime;
} tr_natpmp_uptime_t;

typedef struct tr_natpmp_req_s
{
    unsigned int         adding : 1;
    unsigned int         nobodyhome : 1;
    unsigned int         tmpfail : 1;
    int                  fd;
    int                  delay;
    uint64_t             retry;
    uint64_t             timeout;
    int                  port;
    tr_fd_t *            fdlimit;
    tr_natpmp_uptime_t * uptime;
} tr_natpmp_req_t;

struct tr_natpmp_s
{
#define PMP_STATE_IDLE          1
#define PMP_STATE_ADDING        2
#define PMP_STATE_DELETING      3
#define PMP_STATE_MAPPED        4
#define PMP_STATE_FAILED        5
#define PMP_STATE_NOBODYHOME    6
#define PMP_STATE_TMPFAIL       7
    char               state;
    unsigned int       active : 1;
    struct in_addr     dest;
    int                newport;
    int                mappedport;
    tr_fd_t         *  fdlimit;
    tr_lock_t          lock;
    uint64_t           renew;
    tr_natpmp_req_t *  req;
    tr_natpmp_uptime_t uptime;
    int                mcastfd;
};

static int
checktime( tr_natpmp_uptime_t * uptime, uint32_t seen );
static void
killsock( int * fd, tr_fd_t * fdlimit );
static tr_natpmp_req_t *
newreq( int adding, struct in_addr addr, int port, tr_fd_t * fdlimit,
        tr_natpmp_uptime_t * uptime );
static tr_tristate_t
pulsereq( tr_natpmp_req_t * req, uint64_t * renew );
static int
mcastsetup( tr_fd_t * fdlimit );
static void
mcastpulse( tr_natpmp_t * pmp );
static void
killreq( tr_natpmp_req_t ** req );
static int
sendrequest( int adding, int fd, int port );
static tr_tristate_t
readrequest( uint8_t * buf, int len, int adding, int port,
             tr_natpmp_uptime_t * uptime, uint64_t * renew, int * tmpfail );

tr_natpmp_t *
tr_natpmpInit( tr_fd_t * fdlimit )
{
    tr_natpmp_t * pmp;

    pmp = calloc( 1, sizeof( *pmp ) );
    if( NULL == pmp )
    {
        return NULL;
    }

    pmp->state       = PMP_STATE_IDLE;
    pmp->fdlimit     = fdlimit;
    pmp->mcastfd     = -1;

    if( tr_getDefaultRoute( &pmp->dest ) || INADDR_ANY == pmp->dest.s_addr )
    {
        pmp->dest.s_addr = INADDR_NONE;
    }

    if( INADDR_NONE == pmp->dest.s_addr )
    {
        tr_dbg( "nat-pmp device is unknown" );
    }
    else
    {
        char addrstr[INET_ADDRSTRLEN];
        tr_netNtop( &pmp->dest, addrstr, sizeof( addrstr ) );
        tr_dbg( "nat-pmp device is %s", addrstr );
    }

    tr_lockInit( &pmp->lock );

    return pmp;
}

void
tr_natpmpStart( tr_natpmp_t * pmp )
{
    tr_lockLock( &pmp->lock );

    if( !pmp->active )
    {
        tr_inf( "starting nat-pmp" );
        pmp->active = 1;
        if( 0 > pmp->mcastfd )
        {
            pmp->mcastfd = mcastsetup( pmp->fdlimit );
        }
        /* XXX should I change state? */
    }

    tr_lockUnlock( &pmp->lock );
}

void
tr_natpmpStop( tr_natpmp_t * pmp )
{
    tr_lockLock( &pmp->lock );

    if( pmp->active )
    {
        tr_inf( "stopping nat-pmp" );
        pmp->active = 0;
        killsock( &pmp->mcastfd, pmp->fdlimit );
        switch( pmp->state )
        {
            case PMP_STATE_IDLE:
                break;
            case PMP_STATE_ADDING:
                pmp->state = PMP_STATE_IDLE;
                tr_dbg( "nat-pmp state add -> idle" );
                if( NULL != pmp->req )
                {
                    killreq( &pmp->req );
                    pmp->mappedport = pmp->req->port;
                    pmp->state = PMP_STATE_DELETING;
                    tr_dbg( "nat-pmp state idle -> del" );
                }
                break;
            case PMP_STATE_DELETING:
                break;
            case PMP_STATE_MAPPED:
                pmp->state = PMP_STATE_DELETING;
                tr_dbg( "nat-pmp state mapped -> del" );
                break;
            case PMP_STATE_FAILED:
            case PMP_STATE_NOBODYHOME:
            case PMP_STATE_TMPFAIL:
                break;
            default:
                assert( 0 );
                break;
        }
    }

    tr_lockUnlock( &pmp->lock );
}

int
tr_natpmpStatus( tr_natpmp_t * pmp )
{
    int ret;

    tr_lockLock( &pmp->lock );

    
    if( !pmp->active )
    {
        ret = ( PMP_STATE_DELETING == pmp->state ?
                TR_NAT_TRAVERSAL_UNMAPPING : TR_NAT_TRAVERSAL_DISABLED );
    }
    else if( 0 < pmp->mappedport )
    {
        ret = TR_NAT_TRAVERSAL_MAPPED;
    }
    else
    {
        switch( pmp->state )
        {
            case PMP_STATE_IDLE:
            case PMP_STATE_ADDING:
            case PMP_STATE_DELETING:
                ret = TR_NAT_TRAVERSAL_MAPPING;
                break;
            case PMP_STATE_FAILED:
            case PMP_STATE_TMPFAIL:
                ret = TR_NAT_TRAVERSAL_ERROR;
                break;
            case PMP_STATE_NOBODYHOME:
                ret = TR_NAT_TRAVERSAL_NOTFOUND;
                break;
            case PMP_STATE_MAPPED:
            default:
                assert( 0 );
                ret = TR_NAT_TRAVERSAL_ERROR;
                break;
        }
    }

    tr_lockUnlock( &pmp->lock );

    return ret;
}

void
tr_natpmpForwardPort( tr_natpmp_t * pmp, int port )
{
    tr_lockLock( &pmp->lock );
    tr_inf( "nat-pmp set port %i", port );
    pmp->newport = port;
    tr_lockUnlock( &pmp->lock );
}

void
tr_natpmpClose( tr_natpmp_t * pmp )
{
    /* try to send at least one delete request if we have a port mapping */
    tr_natpmpStop( pmp );
    tr_natpmpPulse( pmp );

    tr_lockLock( &pmp->lock );
    killreq( &pmp->req );
    tr_lockClose( &pmp->lock );
    free( pmp );
}

void
tr_natpmpPulse( tr_natpmp_t * pmp )
{
    tr_lockLock( &pmp->lock );

    if( 0 <= pmp->mcastfd )
    {
        mcastpulse( pmp );
    }

    if( pmp->active || PMP_STATE_DELETING == pmp->state )
    {
        switch( pmp->state )
        {
            case PMP_STATE_IDLE:
            case PMP_STATE_TMPFAIL:
                if( 0 < pmp->newport )
                {
                    tr_dbg( "nat-pmp state %s -> add with port %i",
                            ( PMP_STATE_IDLE == pmp->state ? "idle" : "err" ),
                            pmp->newport );
                    pmp->state = PMP_STATE_ADDING;
                }
                break;

            case PMP_STATE_ADDING:
                if( NULL == pmp->req )
                {
                    if( 0 >= pmp->newport )
                    {
                        tr_dbg( "nat-pmp state add -> idle, no port" );
                        pmp->state = PMP_STATE_IDLE;
                    }
                    else if( INADDR_NONE == pmp->dest.s_addr )
                    {
                        tr_dbg( "nat-pmp state add -> fail, no default route" );
                        pmp->state = PMP_STATE_FAILED;
                    }
                    else
                    {
                        pmp->req = newreq( 1, pmp->dest, pmp->newport,
                                           pmp->fdlimit, &pmp->uptime );
                        if( NULL == pmp->req )
                        {
                            pmp->state = PMP_STATE_FAILED;
                            tr_dbg( "nat-pmp state add -> fail on req init" );
                        }
                    }
                }
                if( PMP_STATE_ADDING == pmp->state )
                {
                    switch( pulsereq( pmp->req, &pmp->renew ) )
                    {
                        case TR_ERROR:
                            if( pmp->req->nobodyhome )
                            {
                                pmp->state = PMP_STATE_NOBODYHOME;
                                tr_dbg( "nat-pmp state add -> nobodyhome on pulse" );
                            }
                            else if( pmp->req->tmpfail )
                            {
                                pmp->state = PMP_STATE_TMPFAIL;
                                tr_dbg( "nat-pmp state add -> err on pulse" );
                                if( pmp->req->port == pmp->newport )
                                {
                                    pmp->newport = 0;
                                }
                            }
                            else
                            {
                                pmp->state = PMP_STATE_FAILED;
                                tr_dbg( "nat-pmp state add -> fail on pulse" );
                            }
                            killreq( &pmp->req );
                            break;
                        case TR_OK:
                            pmp->mappedport = pmp->req->port;
                            killreq( &pmp->req );
                            pmp->state = PMP_STATE_MAPPED;
                            tr_dbg( "nat-pmp state add -> mapped with port %i",
                                    pmp->mappedport);
                            tr_inf( "nat-pmp mapped port %i", pmp->mappedport );
                            break;
                        case TR_WAIT:
                            break;
                    }
                }
                break;

            case PMP_STATE_DELETING:
                if( NULL == pmp->req )
                {
                    assert( 0 < pmp->mappedport );
                    pmp->req = newreq( 0, pmp->dest, pmp->mappedport,
                                       pmp->fdlimit, &pmp->uptime );
                    if( NULL == pmp->req )
                    {
                        pmp->state = PMP_STATE_FAILED;
                        tr_dbg( "nat-pmp state del -> fail on req init" );
                    }
                }
                if( PMP_STATE_DELETING == pmp->state )
                {
                    switch( pulsereq( pmp->req, &pmp->renew ) )
                    {
                        case TR_ERROR:
                            if( pmp->req->nobodyhome )
                            {
                                pmp->state = PMP_STATE_NOBODYHOME;
                                tr_dbg( "nat-pmp state del -> nobodyhome on pulse" );
                            }
                            else if( pmp->req->tmpfail )
                            {
                                pmp->state = PMP_STATE_TMPFAIL;
                                tr_dbg( "nat-pmp state del -> err on pulse" );
                                pmp->mappedport = -1;
                            }
                            else
                            {
                                pmp->state = PMP_STATE_FAILED;
                                tr_dbg( "nat-pmp state del -> fail on pulse" );
                            }
                            killreq( &pmp->req );
                            break;
                        case TR_OK:
                            tr_dbg( "nat-pmp state del -> idle with port %i",
                                    pmp->req->port);
                            tr_inf( "nat-pmp unmapped port %i", pmp->req->port );
                            pmp->mappedport = -1;
                            killreq( &pmp->req );
                            pmp->state = PMP_STATE_IDLE;
                            break;
                        case TR_WAIT:
                            break;
                    }
                }
                break;

            case PMP_STATE_MAPPED:
                if( pmp->newport != pmp->mappedport )
                {
                    tr_dbg( "nat-pmp state mapped -> del, port from %i to %i",
                            pmp->mappedport, pmp->newport );
                    pmp->state = PMP_STATE_DELETING;
                }
                else if( tr_date() > pmp->renew )
                {
                    pmp->state = PMP_STATE_ADDING;
                    tr_dbg( "nat-pmp state mapped -> add for renewal" );
                }
                break;

            case PMP_STATE_FAILED:
            case PMP_STATE_NOBODYHOME:
                break;

            default:
                assert( 0 );
                break;
        }
    }

    tr_lockUnlock( &pmp->lock );
}

static int
checktime( tr_natpmp_uptime_t * uptime, uint32_t cursecs )
{
    time_t   now;
    int      ret;
    uint32_t estimated;

    now = time( NULL );
    ret = 0;
    if( 0 < uptime->when )
    {
        estimated = ( ( now - uptime->when ) * 7 / 8 ) + uptime->uptime;
        if( estimated > cursecs )
        {
            ret = 1;
        }
    }

    uptime->when   = now;
    uptime->uptime = cursecs;

    return ret;
}

static void
killsock( int * fd, tr_fd_t * fdlimit )
{
    if( 0 <= *fd )
    {
        tr_netClose( *fd );
        *fd = -1;
        tr_fdSocketClosed( fdlimit, 0 );
    }
}

static tr_natpmp_req_t *
newreq( int adding, struct in_addr addr, int port, tr_fd_t * fdlimit,
        tr_natpmp_uptime_t * uptime )
{
    tr_natpmp_req_t * ret;
    uint64_t          now;

    ret = calloc( 1, sizeof( *ret ) );
    if( NULL == ret )
    {
        goto err;
    }
    ret->fd = -1;
    if( tr_fdSocketWillCreate( fdlimit, 0 ) )
    {
        goto err;
    }
    ret->fd = tr_netOpenUDP( addr, htons( PMP_PORT ) );
    if( 0 > ret->fd )
    {
        goto err;
    }
    if( sendrequest( adding, ret->fd, port ) )
    {
        goto err;
    }

    now          = tr_date();
    ret->adding  = adding;
    ret->delay   = PMP_INITIAL_DELAY;
    ret->retry   = now + PMP_INITIAL_DELAY;
    ret->timeout = now + PMP_TOTAL_DELAY;
    ret->port    = port;
    ret->fdlimit = fdlimit;
    ret->uptime  = uptime;

    return ret;

  err:
    if( NULL != ret )
    {
        killsock( &ret->fd, fdlimit );
    }
    free( ret );

    return NULL;
}

static tr_tristate_t
pulsereq( tr_natpmp_req_t * req, uint64_t * renew )
{
    struct sockaddr_in sin;
    uint8_t            buf[16];
    int                res, tmpfail;
    uint64_t           now;
    tr_tristate_t      ret;

    now = tr_date();

    if( now >= req->timeout )
    {
        tr_dbg( "nat-pmp request timed out" );
        req->nobodyhome = 1;
        return TR_ERROR;
    }

    if( now >= req->retry )
    {
        if( sendrequest( req->adding, req->fd, req->port ) )
        {
            return TR_ERROR;
        }
        req->delay *= 2;
        req->timeout = now + req->delay;
    }

    res = tr_netRecvFrom( req->fd, buf, sizeof( buf ), &sin );
    if( TR_NET_BLOCK & res )
    {
        return TR_WAIT;
    }
    else if( TR_NET_CLOSE & res )
    {
        if( ECONNRESET == errno || ECONNREFUSED == errno )
        {
            tr_dbg( "nat-pmp not supported by device" );
            req->nobodyhome = 1;
        }
        else
        {
            tr_inf( "error reading nat-pmp response (%s)", strerror( errno ) );
        }
        return TR_ERROR;
    }

    tr_dbg( "nat-pmp read %i byte response", res );

    ret = readrequest( buf, res, req->adding, req->port, req->uptime, renew,
                       &tmpfail );
    req->tmpfail = ( tmpfail ? 1 : 0 );
    return ret;
}

static int
mcastsetup( tr_fd_t * fdlimit )
{
    int fd;
    struct in_addr addr;

    if( tr_fdSocketWillCreate( fdlimit, 0 ) )
    {
        return -1;
    }

    addr.s_addr = inet_addr( PMP_MCAST_ADDR );
    fd = tr_netMcastOpen( PMP_PORT, addr );
    if( 0 > fd )
    {
        tr_fdSocketClosed( fdlimit, 0 );
        return -1;
    }

    tr_dbg( "nat-pmp create multicast socket %i", fd );

    return fd;
}

static void
mcastpulse( tr_natpmp_t * pmp )
{
    struct sockaddr_in sin;
    uint8_t            buf[16];
    int                res;
    char               dbgstr[INET_ADDRSTRLEN];

    res = tr_netRecvFrom( pmp->mcastfd, buf, sizeof( buf ), &sin );
    if( TR_NET_BLOCK & res )
    {
        return;
    }
    else if( TR_NET_CLOSE & res )
    {
        tr_err( "error reading nat-pmp multicast message" );
        killsock( &pmp->mcastfd, pmp->fdlimit );
        return;
    }

    tr_netNtop( &sin.sin_addr, dbgstr, sizeof( dbgstr ) );
    tr_dbg( "nat-pmp read %i byte multicast packet from %s", res, dbgstr );

    if( pmp->dest.s_addr != sin.sin_addr.s_addr )
    {
        tr_dbg( "nat-pmp ignoring multicast packet from unknown host %s",
                dbgstr );
        return;
    }

    if( TR_OK == readrequest( buf, res, 0, -1, &pmp->uptime, &pmp->renew, NULL ) &&
        PMP_STATE_NOBODYHOME == pmp->state )
    {
        tr_dbg( "nat-pmp state notfound -> idle" );
        pmp->state = PMP_STATE_IDLE;
    }
}

static void
killreq( tr_natpmp_req_t ** req )
{
    if( NULL != *req )
    {
        killsock( &(*req)->fd, (*req)->fdlimit );
        free( *req );
        *req = NULL;
    }
}

static int
sendrequest( int adding, int fd, int port )
{
    uint8_t buf[12];
    int res;

    buf[0] = PMP_VERSION;
    buf[1] = PMP_OPCODE_ADDTCP;
    buf[2] = 0;
    buf[3] = 0;
    PMP_TOBUF16( buf + 4, port );
    if( adding )
    {
        PMP_TOBUF16( buf + 6, port );
        PMP_TOBUF32( buf + 8, PMP_LIFETIME );
    }
    else
    {
        PMP_TOBUF16( buf + 6, 0 );
        PMP_TOBUF32( buf + 8, 0 );
    }

    res = tr_netSend( fd, buf, sizeof( buf ) );
    /* XXX is it all right to assume the entire thing is written? */

    /* XXX I should handle blocking here */

    return ( ( TR_NET_CLOSE | TR_NET_BLOCK ) & res  ? 1 : 0 );
}

static tr_tristate_t
readrequest( uint8_t * buf, int len, int adding, int port,
             tr_natpmp_uptime_t * uptime, uint64_t * renew, int * tmpfail )
{
    uint8_t            version, opcode, wantedopcode;
    uint16_t           rescode, privport, pubport;
    uint32_t           seconds, lifetime;

    assert( !adding || NULL != tmpfail );
    if( NULL != tmpfail )
    {
        *tmpfail = 0;
    }
    if( 4 > len )
    {
        tr_err( "read truncated %i byte nat-pmp response packet", len );
        return TR_ERROR;
    }
    version      = buf[0];
    opcode       = buf[1];
    rescode      = PMP_FROMBUF16( buf + 2 );
    wantedopcode = ( 0 < port ? PMP_OPCODE_ADDTCP : PMP_OPCODE_GETIP );

    if( !PMP_OPCODE_IS_RESPONSE( opcode ) )
    {
        tr_dbg( "nat-pmp ignoring request packet" );
        return TR_WAIT;
    }
    opcode = PMP_OPCODE_FROM_RESPONSE( opcode );

    if( PMP_VERSION != version )
    {
        tr_err( "bad nat-pmp version %hhu", buf[0] );
        return TR_ERROR;
    }
    if( wantedopcode != opcode )
    {
        tr_err( "bad nat-pmp opcode %hhu", opcode );
        return TR_ERROR;
    }
    switch( rescode )
    {
        case PMP_RESPCODE_OK:
            break;
        case PMP_RESPCODE_REFUSED:
        case PMP_RESPCODE_NETDOWN:
        case PMP_RESPCODE_NOMEM:
            if( NULL != tmpfail )
            {
                *tmpfail = 1;
            }
            /* fallthrough */
        default:
            tr_err( "bad nat-pmp result code %hu", rescode );
            return TR_ERROR;
    }

    if( 8 > len )
    {
        tr_err( "read truncated %i byte nat-pmp response packet", len );
        return TR_ERROR;
    }
    seconds = PMP_FROMBUF32( buf + 4 );

    if( checktime( uptime, seconds ) )
    {
        *renew = 0;
        tr_inf( "detected nat-pmp device reset" );
        /* XXX should reset retry counter here */
        return TR_WAIT;
    }

    if( 0 <= port )
    {
        assert( PMP_OPCODE_ADDTCP == wantedopcode );
        if( 16 > len )
        {
            tr_err( "read truncated %i byte nat-pmp response packet", len );
            return TR_ERROR;
        }
        privport = PMP_FROMBUF16( buf + 8 );
        pubport  = PMP_FROMBUF16( buf + 10 );
        lifetime = PMP_FROMBUF32( buf + 12 );

        if( port != privport )
        {
            /* private port doesn't match, ignore it */
            tr_dbg( "nat-pmp ignoring message for port %i, expected port %i",
                    privport, port );
            return TR_WAIT;
        }

        if( adding )
        {
            if( port != pubport )
            {
                *tmpfail = 1;
                /* XXX should just start announcing the pub port we're given */
                return TR_ERROR;
            }
            tr_dbg( "nat-pmp set renew to half of %u", lifetime );
            *renew = tr_date() + ( lifetime / 2 * 1000 );
        }
    }

    return TR_OK;
}
