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

#include <assert.h>
#include <string.h> /* memcmp (), memcpy (), memset () */
#include <stdlib.h> /* malloc (), free () */

#include <unistd.h> /* close () */

#include <event2/event.h>

#include <libutp/utp.h>

#include "transmission.h"
#include "log.h"
#include "net.h"
#include "session.h"
#include "tr-dht.h"
#include "tr-utp.h"
#include "tr-udp.h"

/* Since we use a single UDP socket in order to implement multiple
   uTP sockets, try to set up huge buffers. */

#define RECV_BUFFER_SIZE (4 * 1024 * 1024)
#define SEND_BUFFER_SIZE (1 * 1024 * 1024)
#define SMALL_BUFFER_SIZE (32 * 1024)

static void
set_socket_buffers (int fd, int large)
{
    int size, rbuf, sbuf, rc;
    socklen_t rbuf_len = sizeof (rbuf), sbuf_len = sizeof (sbuf);

    size = large ? RECV_BUFFER_SIZE : SMALL_BUFFER_SIZE;
    rc = setsockopt (fd, SOL_SOCKET, SO_RCVBUF, &size, sizeof (size));
    if (rc < 0)
        tr_logAddNamedError ("UDP", "Failed to set receive buffer: %s",
                tr_strerror (errno));

    size = large ? SEND_BUFFER_SIZE : SMALL_BUFFER_SIZE;
    rc = setsockopt (fd, SOL_SOCKET, SO_SNDBUF, &size, sizeof (size));
    if (rc < 0)
        tr_logAddNamedError ("UDP", "Failed to set send buffer: %s",
                tr_strerror (errno));

    if (large) {
        rc = getsockopt (fd, SOL_SOCKET, SO_RCVBUF, &rbuf, &rbuf_len);
        if (rc < 0)
            rbuf = 0;

        rc = getsockopt (fd, SOL_SOCKET, SO_SNDBUF, &sbuf, &sbuf_len);
        if (rc < 0)
            sbuf = 0;

        if (rbuf < RECV_BUFFER_SIZE) {
            tr_logAddNamedError ("UDP", "Failed to set receive buffer: requested %d, got %d",
                    RECV_BUFFER_SIZE, rbuf);
#ifdef __linux__
            tr_logAddNamedInfo ("UDP",
                    "Please add the line "
                    "\"net.core.rmem_max = %d\" to /etc/sysctl.conf",
                    RECV_BUFFER_SIZE);
#endif
        }

        if (sbuf < SEND_BUFFER_SIZE) {
            tr_logAddNamedError ("UDP", "Failed to set send buffer: requested %d, got %d",
                    SEND_BUFFER_SIZE, sbuf);
#ifdef __linux__
            tr_logAddNamedInfo ("UDP",
                    "Please add the line "
                    "\"net.core.wmem_max = %d\" to /etc/sysctl.conf",
                    SEND_BUFFER_SIZE);
#endif
        }
    }
}

void
tr_udpSetSocketBuffers (tr_session *session)
{
    bool utp = tr_sessionIsUTPEnabled (session);
    if (session->udp_socket >= 0)
        set_socket_buffers (session->udp_socket, utp);
    if (session->udp6_socket >= 0)
        set_socket_buffers (session->udp6_socket, utp);
}




/* BEP-32 has a rather nice explanation of why we need to bind to one
   IPv6 address, if I may say so myself. */

static void
rebind_ipv6 (tr_session *ss, bool force)
{
    bool is_default;
    const struct tr_address * public_addr;
    struct sockaddr_in6 sin6;
    const unsigned char *ipv6 = tr_globalIPv6 ();
    int s = -1, rc;
    int one = 1;

    /* We currently have no way to enable or disable IPv6 after initialisation.
       No way to fix that without some surgery to the DHT code itself. */
    if (ipv6 == NULL || (!force && ss->udp6_socket < 0)) {
        if (ss->udp6_bound) {
            free (ss->udp6_bound);
            ss->udp6_bound = NULL;
        }
        return;
    }

    if (ss->udp6_bound != NULL && memcmp (ipv6, ss->udp6_bound, 16) == 0)
        return;

    s = socket (PF_INET6, SOCK_DGRAM, 0);
    if (s < 0)
        goto fail;

#ifdef IPV6_V6ONLY
        /* Since we always open an IPv4 socket on the same port, this
           shouldn't matter.  But I'm superstitious. */
        setsockopt (s, IPPROTO_IPV6, IPV6_V6ONLY, &one, sizeof (one));
#endif

    memset (&sin6, 0, sizeof (sin6));
    sin6.sin6_family = AF_INET6;
    if (ipv6)
        memcpy (&sin6.sin6_addr, ipv6, 16);
    sin6.sin6_port = htons (ss->udp_port);
    public_addr = tr_sessionGetPublicAddress (ss, TR_AF_INET6, &is_default);
    if (public_addr && !is_default)
        sin6.sin6_addr = public_addr->addr.addr6;

    rc = bind (s, (struct sockaddr*)&sin6, sizeof (sin6));
    if (rc < 0)
        goto fail;

    if (ss->udp6_socket < 0) {
        ss->udp6_socket = s;
    } else {
        rc = dup2 (s, ss->udp6_socket);
        if (rc < 0)
            goto fail;
        close (s);
    }

    if (ss->udp6_bound == NULL)
        ss->udp6_bound = malloc (16);
    if (ss->udp6_bound)
        memcpy (ss->udp6_bound, ipv6, 16);

    return;

 fail:
    /* Something went wrong.  It's difficult to recover, so let's simply
       set things up so that we try again next time. */
    tr_logAddNamedError ("UDP", "Couldn't rebind IPv6 socket");
    if (s >= 0)
        close (s);
    if (ss->udp6_bound) {
        free (ss->udp6_bound);
        ss->udp6_bound = NULL;
    }
}

static void
event_callback (evutil_socket_t s, short type UNUSED, void *sv)
{
    int rc;
    socklen_t fromlen;
    unsigned char buf[4096];
    struct sockaddr_storage from;
    tr_session *ss = sv;

    assert (tr_isSession (sv));
    assert (type == EV_READ);

    fromlen = sizeof (from);
    rc = recvfrom (s, buf, 4096 - 1, 0,
                (struct sockaddr*)&from, &fromlen);

    /* Since most packets we receive here are ÂµTP, make quick inline
       checks for the other protocols.  The logic is as follows:
       - all DHT packets start with 'd';
       - all UDP tracker packets start with a 32-bit (!) "action", which
         is between 0 and 3;
       - the above cannot be ÂµTP packets, since these start with a 4-bit
         version number (1). */
    if (rc > 0) {
        if (buf[0] == 'd') {
            if (tr_sessionAllowsDHT (ss)) {
                buf[rc] = '\0'; /* required by the DHT code */
                tr_dhtCallback (buf, rc, (struct sockaddr*)&from, fromlen, sv);
            }
        } else if (rc >= 8 &&
                   buf[0] == 0 && buf[1] == 0 && buf[2] == 0 && buf[3] <= 3) {
            rc = tau_handle_message (ss, buf, rc);
            if (!rc)
                tr_logAddNamedDbg ("UDP", "Couldn't parse UDP tracker packet.");
        } else {
            if (tr_sessionIsUTPEnabled (ss)) {
                rc = tr_utpPacket (buf, rc, (struct sockaddr*)&from, fromlen, ss);
                if (!rc)
                    tr_logAddNamedDbg ("UDP", "Unexpected UDP packet");
            }
        }
    }
}

void
tr_udpInit (tr_session *ss)
{
    bool is_default;
    const struct tr_address * public_addr;
    struct sockaddr_in sin;
    int rc;

    assert (ss->udp_socket < 0);
    assert (ss->udp6_socket < 0);

    ss->udp_port = tr_sessionGetPeerPort (ss);
    if (ss->udp_port <= 0)
        return;

    ss->udp_socket = socket (PF_INET, SOCK_DGRAM, 0);
    if (ss->udp_socket < 0) {
        tr_logAddNamedError ("UDP", "Couldn't create IPv4 socket");
        goto ipv6;
    }

    memset (&sin, 0, sizeof (sin));
    sin.sin_family = AF_INET;
    public_addr = tr_sessionGetPublicAddress (ss, TR_AF_INET, &is_default);
    if (public_addr && !is_default)
        memcpy (&sin.sin_addr, &public_addr->addr.addr4, sizeof (struct in_addr));
    sin.sin_port = htons (ss->udp_port);
    rc = bind (ss->udp_socket, (struct sockaddr*)&sin, sizeof (sin));
    if (rc < 0) {
        tr_logAddNamedError ("UDP", "Couldn't bind IPv4 socket");
        close (ss->udp_socket);
        ss->udp_socket = -1;
        goto ipv6;
    }
    ss->udp_event =
        event_new (ss->event_base, ss->udp_socket, EV_READ | EV_PERSIST,
                  event_callback, ss);
    if (ss->udp_event == NULL)
        tr_logAddNamedError ("UDP", "Couldn't allocate IPv4 event");

 ipv6:
    if (tr_globalIPv6 ())
        rebind_ipv6 (ss, true);
    if (ss->udp6_socket >= 0) {
        ss->udp6_event =
            event_new (ss->event_base, ss->udp6_socket, EV_READ | EV_PERSIST,
                      event_callback, ss);
        if (ss->udp6_event == NULL)
            tr_logAddNamedError ("UDP", "Couldn't allocate IPv6 event");
    }

    tr_udpSetSocketBuffers (ss);

    if (ss->isDHTEnabled)
        tr_dhtInit (ss);

    if (ss->udp_event)
        event_add (ss->udp_event, NULL);
    if (ss->udp6_event)
        event_add (ss->udp6_event, NULL);
}

void
tr_udpUninit (tr_session *ss)
{
    tr_dhtUninit (ss);

    if (ss->udp_socket >= 0) {
        tr_netCloseSocket (ss->udp_socket);
        ss->udp_socket = -1;
    }

    if (ss->udp_event) {
        event_free (ss->udp_event);
        ss->udp_event = NULL;
    }

    if (ss->udp6_socket >= 0) {
        tr_netCloseSocket (ss->udp6_socket);
        ss->udp6_socket = -1;
    }

    if (ss->udp6_event) {
        event_free (ss->udp6_event);
        ss->udp6_event = NULL;
    }

    if (ss->udp6_bound) {
        free (ss->udp6_bound);
        ss->udp6_bound = NULL;
    }
}
