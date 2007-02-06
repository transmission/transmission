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
pulsereq( tr_natpmp_t * req );
static int
sendreq( tr_natpmp_req_t * req );
static int
mcastsetup();
static void
mcastpulse( tr_natpmp_t * pmp );
static tr_tristate_t
parseresponse( uint8_t * buf, int len, int port, tr_natpmp_parse_t * parse );

tr_natpmp_t *
tr_natpmpInit()
{
    tr_natpmp_t * pmp;

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
tr_natpmpStart( tr_natpmp_t * pmp )
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
tr_natpmpStop( tr_natpmp_t * pmp )
{
    if( pmp->active )
    {
        tr_inf( "stopping nat-pmp" );
        pmp->active = 0;
        killsock( &pmp->mcastfd );
        switch( pmp->state )
        {
            case PMP_STATE_IDLE:
                break;
            case PMP_STATE_ADDING:
                pmp->state = PMP_STATE_IDLE;
                tr_dbg( "nat-pmp state add -> idle" );
                if( NULL != pmp->req )
                {
                    pmp->mappedport = pmp->req->gotport;
                    killreq( &pmp->req );
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
}

int
tr_natpmpStatus( tr_natpmp_t * pmp )
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
tr_natpmpForwardPort( tr_natpmp_t * pmp, int port )
{
    tr_inf( "nat-pmp set port %i", port );
    pmp->newport = port;
}

void
tr_natpmpClose( tr_natpmp_t * pmp )
{
    /* try to send at least one delete request if we have a port mapping */
    tr_natpmpStop( pmp );
    tr_natpmpPulse( pmp, NULL );

    killreq( &pmp->req );
    free( pmp );
}

void
tr_natpmpPulse( tr_natpmp_t * pmp, int * publicPort )
{
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

    if( NULL != publicPort )
    {
        *publicPort = -1;
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

    ret->fd = tr_netOpenUDP( addr, htons( PMP_PORT ), 1 );
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
pulsereq( tr_natpmp_t * pmp )
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
        if( ECONNRESET == errno || ECONNREFUSED == errno )
        {
            tr_dbg( "nat-pmp not supported by device" );
            req->nobodyhome = 1;
        }
        else
        {
            tr_inf( "error reading nat-pmp response (%s)", strerror( errno ) );
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

    bzero( buf, sizeof( buf ) );
    buf[0] = PMP_VERSION;
    buf[1] = PMP_OPCODE_ADDTCP;
    PMP_TOBUF16( buf + 4, req->askport );
    if( req->adding )
    {
        PMP_TOBUF16( buf + 6, req->askport );
        PMP_TOBUF32( buf + 8, PMP_LIFETIME );
    }

    res = tr_netSend( req->fd, buf, sizeof( buf ) );
    if( TR_NET_CLOSE & res && EHOSTUNREACH == errno )
    {
        res = TR_NET_BLOCK;
    }
    if( TR_NET_CLOSE & res )
    {
        tr_err( "failed to send nat-pmp request (%s)", strerror( errno ) );
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
    fd = tr_netMcastOpen( PMP_PORT, addr );
    if( 0 > fd )
    {
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

    bzero( parse, sizeof( *parse ) );

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
