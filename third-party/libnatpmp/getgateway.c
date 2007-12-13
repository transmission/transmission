#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#if defined(BSD) || defined(__APPLE__)

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

int
getdefaultgateway( struct in_addr * addr )
{
    uint8_t * buf;
    int len;

    buf = getroute( &len );
    if( NULL == buf )
    {
        fprintf(stderr, "failed to get default route (BSD)" );
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
            fprintf(stderr, "sysctl net.route.0.inet.flags.gateway failed (%s)",
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
        fprintf(stderr, "sysctl net.route.0.inet.flags.gateway failed (%s)",
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

#include <stdio.h>
#include <ctype.h>
#include <netinet/in.h>
#include <sys/param.h>

int getdefaultgateway(in_addr_t * addr)
{
	long d, g;
	char buf[256];
	int line = 0;
	FILE * f;
	char * p;
	f = fopen("/proc/net/route", "r");
	if(!f)
		return -1;
	while(fgets(buf, sizeof(buf), f)) {
		if(line > 0) {
			p = buf;
			while(*p && !isspace(*p))
				p++;
			while(*p && isspace(*p))
				p++;
			if(sscanf(p, "%lx%lx", &d, &g)==2) {
				if(d == 0) { /* default */
					*addr = g;
					fclose(f);
					return 0;
				}
			}
		}
		line++;
	}
	/* not found ! */
	if(f)
		fclose(f);
	return -1;
}

#else /* not BSD or Linux */

int getdefaultgateway( struct in_addr * addr  )
{
    printf( "don't know how to get default route on this platform" );
    return 1;
}

#endif
