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

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <unistd.h> /* close */
#include <fcntl.h> /* fcntl */

#ifdef __BEOS__
    #include <netdb.h>
#endif

#include <sys/types.h>

#include "transmission.h"
#include "natpmp.h"
#include "net.h"
#include "utils.h"

#define PMP_PORT                5351
#define PMP_MCAST_ADDR          "224.0.0.1"
#define PMP_INITIAL_DELAY       250     /* ms, 1/4 second */
#define PMP_TOTAL_DELAY         120000  /* ms, 2 minutes */
#define PMP_VERSION             0
#define PMP_OPCODE_GETIP        0
#define PMP_OPCODE_ADDUDP       1
#define PMP_OPCODE_ADDTCP       2
#define PMP_LIFETIME            3600    /* secs, one hour */
#define PMP_RESULT_OK           0
#define PMP_RESULT_BADVERS      1
#define PMP_RESULT_REFUSED      2
#define PMP_RESULT_NETDOWN      3
#define PMP_RESULT_NOMEM        4
#define PMP_RESULT_BADOPCODE    5

#define PMP_OPCODE_FROM_RESPONSE( op )  ( 0x80 ^ (op) )
#define PMP_OPCODE_TO_RESPONSE( op )    ( 0x80 | (op) )
#define PMP_OPCODE_IS_RESPONSE( op )    ( 0x80 & (op) )
#define PMP_TOBUF16( buf, num ) ( *( (uint16_t *) (buf) ) = htons( (num) ) )
#define PMP_TOBUF32( buf, num ) ( *( (uint32_t *) (buf) ) = htonl( (num) ) )
#define PMP_FROMBUF16( buf )    ( htons( *( (uint16_t *) (buf) ) ) )
#define PMP_FROMBUF32( buf )    ( htonl( *( (uint32_t *) (buf) ) ) )

static int tr_getDefaultRoute( struct in_addr * addr );

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
    int                  askport;
    int                  gotport;
} tr_natpmp_req_t;

struct tr_natpmp
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
    unsigned int       mapped : 1;
    struct in_addr     dest;
    int                newport;
    int                mappedport;
    uint64_t           renew;
    tr_natpmp_req_t *  req;
    tr_natpmp_uptime_t uptime;
    int                mcastfd;
};

typedef struct tr_natpmp_parse_s
{
    unsigned int tmpfail : 1;
    uint32_t     seconds;
    uint16_t     port;
    uint32_t     lifetime;
}
tr_natpmp_parse_t;

static void
unmap( tr_natpmp * pmp );
static int
checktime( tr_natpmp_uptime_t * uptime, uint32_t seen );
static void
killsock( int * fd );
static tr_natpmp_req_t *
newreq( int adding, struct in_addr addr, int port );
static void
killreq( tr_natpmp_req_t ** req );
static void
resetreq( tr_natpmp_req_t * req );
static tr_tristate_t
pulsereq( tr_natpmp * req );
static int
sendreq( tr_natpmp_req_t * req );
static int
mcastsetup();
static void
mcastpulse( tr_natpmp * pmp );
static tr_tristate_t
parseresponse( uint8_t * buf, int len, int port, tr_natpmp_parse_t * parse );

tr_natpmp *
tr_natpmpInit()
{
    tr_natpmp * pmp;

    pmp = calloc( 1, sizeof( *pmp ) );
    if( NULL == pmp )
    {
        return NULL;
    }

    pmp->state       = PMP_STATE_IDLE;
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

    return pmp;
}

void
tr_natpmpStart( tr_natpmp * pmp )
{
    if( !pmp->active )
    {
        tr_inf( "starting nat-pmp" );
        pmp->active = 1;
        if( 0 > pmp->mcastfd )
        {
            pmp->mcastfd = mcastsetup();
        }
    }
}

void
tr_natpmpStop( tr_natpmp * pmp )
{
    if( pmp->active )
    {
        tr_inf( "stopping nat-pmp" );
        pmp->active = 0;
        killsock( &pmp->mcastfd );
        unmap( pmp );
    }
}

int
tr_natpmpStatus( tr_natpmp * pmp )
{
    int ret;
    
    if( !pmp->active )
    {
        ret = ( PMP_STATE_DELETING == pmp->state ?
                TR_NAT_TRAVERSAL_UNMAPPING : TR_NAT_TRAVERSAL_DISABLED );
    }
    else if( pmp->mapped )
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
                /* if pmp->state is PMP_STATE_MAPPED then pmp->mapped
                   should be true */
                assert( 0 );
                ret = TR_NAT_TRAVERSAL_ERROR;
                break;
        }
    }

    return ret;
}

void
tr_natpmpForwardPort( tr_natpmp * pmp, int port )
{
    tr_inf( "nat-pmp set port %i", port );
    pmp->newport = port;
}

void
tr_natpmpRemoveForwarding( tr_natpmp * pmp )
{
    tr_inf( "nat-pmp unset port" );
    pmp->newport = -1;
    unmap( pmp );
}

void
tr_natpmpClose( tr_natpmp * pmp )
{
    /* try to send at least one delete request if we have a port mapping */
    tr_natpmpStop( pmp );
    tr_natpmpPulse( pmp, NULL );

    killreq( &pmp->req );
    free( pmp );
}

void
tr_natpmpPulse( tr_natpmp * pmp, int * publicPort )
{
    if( 0 <= pmp->mcastfd )
    {
        mcastpulse( pmp );
    }

    if( NULL != publicPort )
    {
        *publicPort = -1;
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
                        pmp->req = newreq( 1, pmp->dest, pmp->newport );
                        if( NULL == pmp->req )
                        {
                            pmp->state = PMP_STATE_FAILED;
                            tr_dbg( "nat-pmp state add -> fail on req init" );
                        }
                    }
                }
                if( PMP_STATE_ADDING == pmp->state )
                {
                    switch( pulsereq( pmp ) )
                    {
                        case TR_NET_ERROR:
                            if( pmp->req->nobodyhome )
                            {
                                pmp->state = PMP_STATE_NOBODYHOME;
                                tr_dbg( "nat-pmp state add -> nobodyhome on pulse" );
                            }
                            else if( pmp->req->tmpfail )
                            {
                                pmp->state = PMP_STATE_TMPFAIL;
                                tr_dbg( "nat-pmp state add -> err on pulse" );
                                if( pmp->req->askport == pmp->newport )
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
                        case TR_NET_OK:
                            pmp->mappedport = pmp->req->gotport;
                            if( pmp->mappedport != pmp->newport &&
                                pmp->newport == pmp->req->askport )
                            {
                                pmp->newport = pmp->req->gotport;
                            }
                            killreq( &pmp->req );
                            pmp->state = PMP_STATE_MAPPED;
                            pmp->mapped = 1;
                            tr_dbg( "nat-pmp state add -> mapped with port %i",
                                    pmp->mappedport);
                            tr_inf( "nat-pmp mapped port %i", pmp->mappedport );
                            if( NULL != publicPort )
                            {
                                *publicPort = pmp->mappedport;
                            }
                            break;
                        case TR_NET_WAIT:
                            break;
                    }
                }
                break;

            case PMP_STATE_DELETING:
                if( NULL == pmp->req )
                {
                    assert( 0 < pmp->mappedport );
                    pmp->req = newreq( 0, pmp->dest, pmp->mappedport );
                    if( NULL == pmp->req )
                    {
                        pmp->state = PMP_STATE_FAILED;
                        tr_dbg( "nat-pmp state del -> fail on req init" );
                    }
                }
                if( PMP_STATE_DELETING == pmp->state )
                {
                    switch( pulsereq( pmp ) )
                    {
                        case TR_NET_ERROR:
                            if( pmp->req->nobodyhome )
                            {
                                pmp->mapped = 0;
                                pmp->state = PMP_STATE_NOBODYHOME;
                                tr_dbg( "nat-pmp state del -> nobodyhome on pulse" );
                            }
                            else if( pmp->req->tmpfail )
                            {
                                pmp->mapped = 0;
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
                        case TR_NET_OK:
                            tr_dbg( "nat-pmp state del -> idle with port %i",
                                    pmp->req->askport);
                            tr_inf( "nat-pmp unmapped port %i",
                                    pmp->req->askport );
                            pmp->mapped = 0;
                            pmp->mappedport = -1;
                            killreq( &pmp->req );
                            pmp->state = PMP_STATE_IDLE;
                            break;
                        case TR_NET_WAIT:
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
}

void
unmap( tr_natpmp * pmp )
{
    switch( pmp->state )
    {
        case PMP_STATE_IDLE:
            break;
        case PMP_STATE_ADDING:
            if( NULL == pmp->req )
            {
                pmp->state = PMP_STATE_IDLE;
                tr_dbg( "nat-pmp state add -> idle" );
            }
            else
            {
                pmp->mappedport = pmp->req->gotport;
                killreq( &pmp->req );
                pmp->state = PMP_STATE_DELETING;
                tr_dbg( "nat-pmp state add -> del" );
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
killsock( int * fd )
{
    if( 0 <= *fd )
    {
        tr_netClose( *fd );
        *fd = -1;
    }
}

static tr_natpmp_req_t *
newreq( int adding, struct in_addr addr, int port )
{
    tr_natpmp_req_t * ret;

    ret = calloc( 1, sizeof( *ret ) );
    if( NULL == ret )
    {
        return NULL;
    }

    ret->fd = tr_netOpenUDP( &addr, htons( PMP_PORT ), 1 );
    if( 0 > ret->fd )
    {
        free( ret );
        return NULL;
    }

    ret->adding  = adding;
    ret->askport = port;
    ret->gotport = port;
    resetreq( ret );
    if( sendreq( ret ) )
    {
        killreq( &ret );
        return NULL;
    }

    return ret;
}

static void
killreq( tr_natpmp_req_t ** req )
{
    if( NULL != *req )
    {
        killsock( &(*req)->fd );
        free( *req );
        *req = NULL;
    }
}

static void
resetreq( tr_natpmp_req_t * req )
{
    uint64_t now;

    now          = tr_date();
    req->delay   = PMP_INITIAL_DELAY;
    req->retry   = now;
    req->timeout = now + PMP_TOTAL_DELAY;
}

static tr_tristate_t
pulsereq( tr_natpmp * pmp )
{
    tr_natpmp_req_t  * req = pmp->req;
    struct sockaddr_in sin;
    uint8_t            buf[16];
    int                res;
    uint64_t           now;
    tr_tristate_t      ret;
    tr_natpmp_parse_t  parse;

    now = tr_date();
    /* check for timeout */
    if( now >= req->timeout )
    {
        tr_dbg( "nat-pmp request timed out" );
        req->nobodyhome = 1;
        return TR_NET_ERROR;
    }
    /* send another request  if it's been long enough */
    if( now >= req->retry && sendreq( req ) )
    {
        return TR_NET_ERROR;
    }

    /* check for incoming packets */
    res = tr_netRecvFrom( req->fd, buf, sizeof( buf ), &sin );
    if( TR_NET_BLOCK & res )
    {
        return TR_NET_WAIT;
    }
    else if( TR_NET_CLOSE & res )
    {
        if( ECONNRESET == sockerrno || ECONNREFUSED == sockerrno )
        {
            tr_dbg( "nat-pmp not supported by device" );
            req->nobodyhome = 1;
        }
        else
        {
            tr_inf( "error reading nat-pmp response (%s)", strerror( sockerrno ) );
        }
        return TR_NET_ERROR;
    }

    /* parse the packet */
    tr_dbg( "nat-pmp read %i byte response", res );
    ret = parseresponse( buf, res, req->askport, &parse );
    req->tmpfail = parse.tmpfail;
    /* check for device reset */
    if( checktime( &pmp->uptime, parse.seconds ) )
    {
        pmp->renew = 0;
        tr_inf( "detected nat-pmp device reset" );
        resetreq( req );
        ret = TR_NET_WAIT;
    }
    if( TR_NET_OK == ret && req->adding )
    {
        if( req->askport != parse.port )
        {
            tr_dbg( "nat-pmp received %i for public port instead of %i",
                    parse.port, req->askport );
            req->gotport = parse.port;
        }
        tr_dbg( "nat-pmp set renew to half of %u", parse.lifetime );
        pmp->renew = now + ( parse.lifetime / 2 * 1000 );
    }

    return ret;
}

static int
sendreq( tr_natpmp_req_t * req )
{
    uint8_t buf[12];
    int res;

    memset( buf, 0, sizeof( buf ) );
    buf[0] = PMP_VERSION;
    buf[1] = PMP_OPCODE_ADDTCP;
    PMP_TOBUF16( buf + 4, req->askport );
    if( req->adding )
    {
        PMP_TOBUF16( buf + 6, req->askport );
        PMP_TOBUF32( buf + 8, PMP_LIFETIME );
    }

    res = tr_netSend( req->fd, buf, sizeof( buf ) );
    if( TR_NET_CLOSE & res && EHOSTUNREACH == sockerrno )
    {
        res = TR_NET_BLOCK;
    }
    if( TR_NET_CLOSE & res )
    {
        tr_err( "failed to send nat-pmp request (%s)", strerror( sockerrno ) );
        return 1;
    }
    else if( !( TR_NET_BLOCK & res ) )
    {
        /* XXX is it all right to assume the entire thing is written? */
        req->retry  = tr_date() + req->delay;
        req->delay *= 2;
    }
    return 0;
}

static int
mcastsetup()
{
    int fd;
    struct in_addr addr;

    addr.s_addr = inet_addr( PMP_MCAST_ADDR );
    fd = tr_netMcastOpen( PMP_PORT, &addr );
    if( 0 > fd )
    {
        return -1;
    }

    tr_dbg( "nat-pmp create multicast socket %i", fd );

    return fd;
}

static void
mcastpulse( tr_natpmp * pmp )
{
    struct sockaddr_in sin;
    uint8_t            buf[16];
    int                res;
    char               dbgstr[INET_ADDRSTRLEN];
    tr_natpmp_parse_t  parse;

    res = tr_netRecvFrom( pmp->mcastfd, buf, sizeof( buf ), &sin );
    if( TR_NET_BLOCK & res )
    {
        return;
    }
    else if( TR_NET_CLOSE & res )
    {
        tr_err( "error reading nat-pmp multicast message" );
        killsock( &pmp->mcastfd );
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

    if( TR_NET_OK == parseresponse( buf, res, -1, &parse ) )
    {
        if( checktime( &pmp->uptime, parse.seconds ) )
        {
            pmp->renew = 0;
            tr_inf( "detected nat-pmp device reset" );
            if( NULL != pmp->req )
            {
                resetreq( pmp->req );
            }
        }
        if( PMP_STATE_NOBODYHOME == pmp->state )
        {
            tr_dbg( "nat-pmp state notfound -> idle" );
            pmp->state = PMP_STATE_IDLE;
        }
    }
}

static tr_tristate_t
parseresponse( uint8_t * buf, int len, int port, tr_natpmp_parse_t * parse )
{
    int version, respopcode, opcode, wantedopcode, rescode, privport;

    memset( parse, 0, sizeof( *parse ) );

    if( 8 > len )
    {
        tr_err( "read truncated %i byte nat-pmp response packet", len );
        return TR_NET_ERROR;
    }

    /* parse the first 8 bytes: version, opcode, and result code */
    version      = buf[0];
    respopcode   = buf[1];
    opcode       = PMP_OPCODE_FROM_RESPONSE( respopcode );
    wantedopcode = ( 0 < port ? PMP_OPCODE_ADDTCP : PMP_OPCODE_GETIP );
    rescode      = PMP_FROMBUF16( buf + 2 );

    if( PMP_VERSION != version )
    {
        tr_err( "unknown nat-pmp version %hhu", buf[0] );
        return TR_NET_ERROR;
    }
    if( !PMP_OPCODE_IS_RESPONSE( respopcode ) )
    {
        tr_dbg( "nat-pmp ignoring request packet" );
        return TR_NET_WAIT;
    }
    if( wantedopcode != opcode )
    {
        tr_err( "unknown nat-pmp opcode %hhu", opcode );
        return TR_NET_ERROR;
    }

    switch( rescode )
    {
        case PMP_RESULT_OK:
            break;
        case PMP_RESULT_REFUSED:
            tr_err( "nat-pmp mapping failed: refused/unauthorized/disabled" );
            parse->tmpfail = 1;
            return TR_NET_ERROR;
        case PMP_RESULT_NETDOWN:
            tr_err( "nat-pmp mapping failed: network down" );
            parse->tmpfail = 1;
            return TR_NET_ERROR;
        case PMP_RESULT_NOMEM:
            tr_err( "nat-pmp mapping refused: insufficient resources" );
            parse->tmpfail = 1;
            return TR_NET_ERROR;
        default:
            tr_err( "nat-pmp mapping refused: unknown result code: %hu",
                    rescode );
            return TR_NET_ERROR;
    }

    parse->seconds = PMP_FROMBUF32( buf + 4 );
    if( PMP_OPCODE_ADDTCP == opcode )
    {
        if( 16 > len )
        {
            tr_err( "read truncated %i byte nat-pmp response packet", len );
            return TR_NET_ERROR;
        }
        privport        = PMP_FROMBUF16( buf + 8 );
        parse->port     = PMP_FROMBUF16( buf + 10 );
        parse->lifetime = PMP_FROMBUF32( buf + 12 );

        if( port != privport )
        {
            tr_dbg( "nat-pmp ignoring message for port %i, expected port %i",
                    privport, port );
            return TR_NET_WAIT;
        }
    }

    return TR_NET_OK;
}

/***
****  tr_getDefaultRoute()
***/

#ifdef BSD

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/in.h> /* struct in_addr */
#include <sys/sysctl.h>
#include <net/route.h>

static uint8_t *
getroute( int * buflen );
static int
parseroutes( uint8_t * buf, int len, struct in_addr * addr );

static int
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
                    strerror( sockerrno ) );
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
                strerror( sockerrno ) );
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
        tr_err( "failed to create routing socket (%s)", strerror( sockerrno ) );
        return -1;
    }

    flags = fcntl( fd, F_GETFL );
    if( 0 > flags || 0 > fcntl( fd, F_SETFL, O_NONBLOCK | flags ) )
    {
        tr_err( "failed to set socket nonblocking (%s)", strerror( sockerrno ) );
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
        tr_err( "failed to write to routing socket (%s)", strerror( sockerrno ) );
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
            if( EAGAIN != sockerrno )
            {
                tr_err( "failed to read from routing socket (%s)",
                        strerror( sockerrno ) );
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
