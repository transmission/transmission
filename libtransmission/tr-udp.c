/*
Copyright (c) 2010 by Juliusz Chroboczek

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

*/

#include <unistd.h>
#include <assert.h>

#include <event2/event.h>

#include "transmission.h"
#include "net.h"
#include "session.h"
#include "tr-dht.h"
#include "tr-udp.h"

/* BEP-32 has a rather nice explanation of why we need to bind to one
   IPv6 address, if I may say so myself. */

static void
rebind_ipv6(tr_session *ss, tr_bool force)
{
    struct sockaddr_in6 sin6;
    const unsigned char *ipv6 = tr_globalIPv6();
    int s = -1, rc;
    int one = 1;

    /* We currently have no way to enable or disable IPv6 after initialisation.
       No way to fix that without some surgery to the DHT code itself. */
    if(ipv6 == NULL || (!force && ss->udp6_socket < 0)) {
        if(ss->udp6_bound) {
            free(ss->udp6_bound);
            ss->udp6_bound = NULL;
        }
        return;
    }

    if(ss->udp6_bound != NULL && memcmp(ipv6, ss->udp6_bound, 16) == 0)
        return;

    s = socket(PF_INET6, SOCK_DGRAM, 0);
    if(s < 0)
        goto fail;

#ifdef IPV6_V6ONLY
        /* Since we always open an IPv4 socket on the same port, this
           shouldn't matter.  But I'm superstitious. */
        setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, &one, sizeof(one));
#endif

    memset(&sin6, 0, sizeof(sin6));
    sin6.sin6_family = AF_INET6;
    if(ipv6)
        memcpy(&sin6.sin6_addr, ipv6, 16);
    sin6.sin6_port = htons(ss->udp_port);
    rc = bind(s, (struct sockaddr*)&sin6, sizeof(sin6));
    if(rc < 0)
        goto fail;

    if(ss->udp6_socket < 0) {
        ss->udp6_socket = s;
        s = -1;
    } else {
        rc = dup2(s, ss->udp6_socket);
        if(rc < 0)
            goto fail;
        close(s);
        s = -1;
    }

    if(ss->udp6_bound == NULL)
        ss->udp6_bound = malloc(16);
    if(ss->udp6_bound)
        memcpy(ss->udp6_bound, ipv6, 16);

    return;

 fail:
    /* Something went wrong.  It's difficult to recover, so let's simply
       set things up so that we try again next time. */
    tr_nerr("UDP", "Couldn't rebind IPv6 socket");
    if(s >= 0)
        close(s);
    if(ss->udp6_bound) {
        free(ss->udp6_bound);
        ss->udp6_bound = NULL;
    }
}

static void
event_callback(int s, short type, void *sv)
{
    tr_session *ss = (tr_session*)sv;
    unsigned char *buf;
    struct sockaddr_storage from;
    socklen_t fromlen;
    int rc;

    assert(tr_isSession(ss));
    assert(type == EV_READ);

    buf = malloc(4096);
    if(buf == NULL) {
        tr_nerr("UDP", "Couldn't allocate buffer");
        return;
    }

    fromlen = sizeof(from);
    rc = recvfrom(s, buf, 4096 - 1, 0,
                  (struct sockaddr*)&from, &fromlen);
    if(rc <= 0)
        return;

    if(buf[0] == 'd') {
        /* DHT packet. */
        buf[rc] = '\0';
        tr_dhtCallback(buf, rc, (struct sockaddr*)&from, fromlen, sv);
    } else {
        /* Probably a UTP packet. */
        /* Nothing yet. */
    }

    free(buf);
}    

void
tr_udpInit(tr_session *ss, const tr_address * addr)
{
    struct sockaddr_in sin;
    int rc;

    assert(ss->udp_socket < 0);
    assert(ss->udp6_socket < 0);

    ss->udp_port = tr_sessionGetPeerPort(ss);
    if(ss->udp_port <= 0)
        return;

    ss->udp_socket = socket(PF_INET, SOCK_DGRAM, 0);
    if(ss->udp_socket < 0) {
        tr_nerr("UDP", "Couldn't create IPv4 socket");
        goto ipv6;
    }

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    memcpy(&sin.sin_addr, &addr->addr.addr4, sizeof (struct in_addr));
    sin.sin_port = htons(ss->udp_port);
    rc = bind(ss->udp_socket, (struct sockaddr*)&sin, sizeof(sin));
    if(rc < 0) {
        tr_nerr("UDP", "Couldn't bind IPv4 socket");
        close(ss->udp_socket);
        ss->udp_socket = -1;
        goto ipv6;
    }
    ss->udp_event =
        event_new(NULL, ss->udp_socket, EV_READ | EV_PERSIST,
                  event_callback, ss);
    tr_nerr("UDP", "Couldn't allocate IPv4 event");
    /* Don't bother recovering for now. */

 ipv6:
    if(tr_globalIPv6())
        rebind_ipv6(ss, TRUE);
    if(ss->udp6_socket >= 0) {
        ss->udp6_event =
            event_new(NULL, ss->udp6_socket, EV_READ | EV_PERSIST,
                      event_callback, ss);
        if(ss->udp6_event == NULL)
            tr_nerr("UDP", "Couldn't allocate IPv6 event");
    }

    if(ss->isDHTEnabled)
        tr_dhtInit(ss);

    if(ss->udp_event)
        event_add(ss->udp_event, NULL);
    if(ss->udp6_event)
        event_add(ss->udp6_event, NULL);
}

void
tr_udpUninit(tr_session *ss)
{
    tr_dhtUninit(ss);

    if(ss->udp_socket >= 0) {
        tr_netCloseSocket( ss->udp_socket );
        ss->udp_socket = -1;
    }

    if(ss->udp_event) {
        event_free(ss->udp_event);
        ss->udp_event = NULL;
    }

    if(ss->udp6_socket >= 0) {
        tr_netCloseSocket( ss->udp6_socket );
        ss->udp6_socket = -1;
    }

    if(ss->udp6_event) {
        event_free(ss->udp6_event);
        ss->udp6_event = NULL;
    }

    if(ss->udp6_bound) {
        free(ss->udp6_bound);
        ss->udp6_bound = NULL;
    }
}
