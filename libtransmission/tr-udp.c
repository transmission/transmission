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

#include <string.h> /* memcmp(), memcpy(), memset() */
#include <stdlib.h> /* malloc(), free() */

#ifdef _WIN32
#include <io.h> /* dup2() */
#else
#include <unistd.h> /* dup2() */
#endif

#include <event2/event.h>

#include <libutp/utp.h>

#include "transmission.h"
#include "list.h"
#include "log.h"
#include "net.h"
#include "session.h"
#include "tr-assert.h"
#include "tr-dht.h"
#include "tr-utp.h"
#include "tr-udp.h"

/* Since we use a single UDP socket in order to implement multiple
   uTP sockets, try to set up huge buffers. */

#define RECV_BUFFER_SIZE (4 * 1024 * 1024)
#define SEND_BUFFER_SIZE (1 * 1024 * 1024)
#define SMALL_BUFFER_SIZE (32 * 1024)

static void set_socket_buffers(tr_socket_t fd, bool large)
{
    int size;
    int rbuf;
    int sbuf;
    int rc;
    socklen_t rbuf_len = sizeof(rbuf);
    socklen_t sbuf_len = sizeof(sbuf);
    char err_buf[512];

    size = large ? RECV_BUFFER_SIZE : SMALL_BUFFER_SIZE;
    rc = setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (void const*)&size, sizeof(size));

    if (rc < 0)
    {
        tr_logAddNamedError("UDP", "Failed to set receive buffer: %s", tr_net_strerror(err_buf, sizeof(err_buf), sockerrno));
    }

    size = large ? SEND_BUFFER_SIZE : SMALL_BUFFER_SIZE;
    rc = setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (void const*)&size, sizeof(size));

    if (rc < 0)
    {
        tr_logAddNamedError("UDP", "Failed to set send buffer: %s", tr_net_strerror(err_buf, sizeof(err_buf), sockerrno));
    }

    if (large)
    {
        rc = getsockopt(fd, SOL_SOCKET, SO_RCVBUF, (void*)&rbuf, &rbuf_len);

        if (rc < 0)
        {
            rbuf = 0;
        }

        rc = getsockopt(fd, SOL_SOCKET, SO_SNDBUF, (void*)&sbuf, &sbuf_len);

        if (rc < 0)
        {
            sbuf = 0;
        }

        if (rbuf < RECV_BUFFER_SIZE)
        {
            tr_logAddNamedError("UDP", "Failed to set receive buffer: requested %d, got %d", RECV_BUFFER_SIZE, rbuf);
#ifdef __linux__
            tr_logAddNamedInfo("UDP", "Please add the line \"net.core.rmem_max = %d\" to /etc/sysctl.conf", RECV_BUFFER_SIZE);
#endif
        }

        if (sbuf < SEND_BUFFER_SIZE)
        {
            tr_logAddNamedError("UDP", "Failed to set send buffer: requested %d, got %d", SEND_BUFFER_SIZE, sbuf);
#ifdef __linux__
            tr_logAddNamedInfo("UDP", "Please add the line \"net.core.wmem_max = %d\" to /etc/sysctl.conf", SEND_BUFFER_SIZE);
#endif
        }
    }
}

void tr_udpSetSocketBuffers(tr_session* session)
{
    bool utp = tr_sessionIsUTPEnabled(session);
    struct tr_bindinfo const* b;
    tr_list const* l;

    for (l = session->udp_sockets; l; l = l->next)
    {
	b = l->data;
        set_socket_buffers(b->socket, utp);
    }
}

static struct tr_bindinfo* find_socket(tr_session* session, int s)
{
    struct tr_bindinfo* b;
    tr_list const* l;

    for (l = session->udp_sockets; l; l = l->next)
    {
	b = l->data;
	if (b->socket == s)
	{
	    return b;
	}
    }

    return NULL;
}

static void event_callback(evutil_socket_t s, short type UNUSED, void* sv)
{
    TR_ASSERT(tr_isSession(sv));
    TR_ASSERT(type == EV_READ);

    int rc;
    socklen_t fromlen;
    unsigned char buf[4096];
    struct sockaddr_storage from;
    tr_session* ss = sv;

    fromlen = sizeof(from);
    rc = recvfrom(s, (void*)buf, 4096 - 1, 0, (struct sockaddr*)&from, &fromlen);

    /* Since most packets we receive here are ÂµTP, make quick inline
       checks for the other protocols.  The logic is as follows:
       - all DHT packets start with 'd';
       - all UDP tracker packets start with a 32-bit (!) "action", which
         is between 0 and 3;
       - the above cannot be ÂµTP packets, since these start with a 4-bit
         version number (1). */
    if (rc > 0)
    {
        if (buf[0] == 'd')
        {
            if (tr_sessionAllowsDHT(ss))
            {
                buf[rc] = '\0'; /* required by the DHT code */
                tr_dhtCallback(buf, rc, (struct sockaddr*)&from, fromlen, sv);
            }
        }
        else if (rc >= 8 && buf[0] == 0 && buf[1] == 0 && buf[2] == 0 && buf[3] <= 3)
        {
            rc = tau_handle_message(ss, buf, rc);

            if (rc == 0)
            {
                tr_logAddNamedDbg("UDP", "Couldn't parse UDP tracker packet.");
            }
        }
        else
        {
            struct tr_bindinfo* b;

            if (tr_sessionIsUTPEnabled(ss) && (b = find_socket(ss, s)))
            {
                rc = tr_utpPacket(buf, rc, (struct sockaddr*)&from, fromlen, b);

                if (rc == 0)
                {
                    tr_logAddNamedDbg("UDP", "Unexpected UDP packet");
                }
            }
        }
    }
}

void tr_udpInit(tr_session* ss)
{
    struct tr_address const* public_addr;
    int rc, idx;

    ss->udp_port = tr_sessionGetPeerPort(ss);

    if (ss->udp_port <= 0)
    {
        return;
    }

    idx = 0;
    while ((public_addr = tr_sessionGetPublicAddress(ss, TR_AF_INET, idx++)))
    {
        struct sockaddr_in sin;
	struct tr_bindinfo* b;
	int sock;

        memset(&sin, 0, sizeof(sin));
        sin.sin_family = AF_INET;
        sin.sin_addr = public_addr->addr.addr4;
        sin.sin_port = htons(ss->udp_port);

        sock = socket(PF_INET, SOCK_DGRAM, 0);
        if (sock == TR_BAD_SOCKET)
	{
            tr_logAddNamedError("UDP", "Couldn't create IPv4 socket");
            break;
        }

        rc = bind(sock, (struct sockaddr*)&sin, sizeof(sin));
        if (rc < 0)
	{
            tr_logAddNamedError("UDP", "Couldn't bind IPv4 socket");
            tr_netCloseSocket(sock);
            continue;
        }

        b = tr_new(struct tr_bindinfo, 1);
        b->socket = sock;
        b->addr = public_addr;
        b->session = ss;
        b->ev = event_new(ss->event_base, sock, EV_READ | EV_PERSIST, event_callback, ss);
        if (b->ev == NULL)
	{
            tr_logAddNamedError("UDP", "Couldn't allocate IPv4 event");
	}
	else
	{
	    event_add(b->ev, NULL);
	}
        tr_list_append(&ss->udp_sockets, b);
    }

    idx = 0;
    while ((public_addr = tr_sessionGetPublicAddress(ss, TR_AF_INET6, idx++)))
    {
        struct sockaddr_in6 sin6;
	struct tr_bindinfo* b;
	int sock;

        memset(&sin6, 0, sizeof(sin6));
        sin6.sin6_family = AF_INET6;
        sin6.sin6_addr = public_addr->addr.addr6;
        sin6.sin6_port = htons(ss->udp_port);

        if (!tr_address_compare(public_addr, tr_address_default(TR_AF_INET6)))
	{
            const unsigned char *ipv6 = tr_globalIPv6();
            if (!ipv6)
	    {
		continue;
	    }
            memcpy(&sin6.sin6_addr, ipv6, sizeof(struct in6_addr));
        }

        sock = socket(PF_INET6, SOCK_DGRAM, 0);
        if (sock == TR_BAD_SOCKET)
	{
            tr_logAddNamedError("UDP", "Couldn't create IPv6 socket");
            break;
        }

        rc = bind(sock, (struct sockaddr*)&sin6, sizeof(sin6));
        if (rc < 0)
	{
            tr_logAddNamedError("UDP", "Couldn't bind IPv6 socket");
            tr_netCloseSocket(sock);
            continue;
        }

        b = tr_new(struct tr_bindinfo, 1);
        b->socket = sock;
        b->addr = public_addr;
        b->session = ss;
        b->ev = event_new(ss->event_base, sock, EV_READ | EV_PERSIST, event_callback, ss);
        if (b->ev == NULL)
	{
            tr_logAddNamedError("UDP", "Couldn't allocate IPv6 event");
	}
	else
	{
	    event_add(b->ev, NULL);
	}
        tr_list_append(&ss->udp_sockets, b);
    }

    tr_udpSetSocketBuffers(ss);

    if (ss->isDHTEnabled)
    {
        tr_dhtInit(ss);
    }
}

void tr_udpUninit(tr_session* ss)
{
    struct tr_bindinfo* b;

    tr_dhtUninit(ss);

    while ((b = tr_list_pop_front(&ss->udp_sockets)))
    {
        if (b->socket != TR_BAD_SOCKET)
	{
            tr_netCloseSocket(b->socket);
        }
        if (b->ev)
	{
            event_free(b->ev);
        }
        tr_free (b);
    }
}

struct tr_bindinfo* tr_udpGetSocket(tr_session* ss, int tr_af_type, int idx)
{
    tr_address const* a;

    while ((a = tr_sessionGetPublicAddress(ss, tr_af_type, idx)))
    {
	tr_list const* l;
        int count = 0;

        for (l = ss->udp_sockets; l; l = l->next)
	{
            struct tr_bindinfo* b = l->data;

            if (b->socket == TR_BAD_SOCKET)
	    {
                continue;
	    }
            if (b->addr == a)
	    {
                return b;
	    }
            if (b->addr->type == tr_af_type)
	    {
                count++;
	    }
        }
        if (count == 0 || idx >= 0)
	{
            break;
	}
    }

    return NULL;
}
