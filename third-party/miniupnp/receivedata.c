/* $Id: receivedata.c,v 1.3 2012/03/05 19:42:47 nanard Exp $ */
/* Project : miniupnp
 * Website : http://miniupnp.free.fr/
 * Author : Thomas Bernard
 * Copyright (c) 2011-2012 Thomas Bernard
 * This software is subject to the conditions detailed in the
 * LICENCE file provided in this distribution. */

#include <stdio.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <unistd.h>
#if defined(__amigaos__) && !defined(__amigaos4__)
#define socklen_t int
#else /* #if defined(__amigaos__) && !defined(__amigaos4__) */
#include <sys/select.h>
#endif /* #else defined(__amigaos__) && !defined(__amigaos4__) */
#include <sys/socket.h>
#if !defined(__amigaos__) && !defined(__amigaos4__)
#include <poll.h>
#endif
#include <errno.h>
#define MINIUPNPC_IGNORE_EINTR
#endif

#ifdef _WIN32
#define PRINT_SOCKET_ERROR(x)    printf("Socket error: %s, %d\n", x, WSAGetLastError());
#else
#define PRINT_SOCKET_ERROR(x) perror(x)
#endif

#include "receivedata.h"

int
receivedata(int socket, char * data, int length, int timeout)
{
    int n;
#if !defined(_WIN32) && !defined(__amigaos__) && !defined(__amigaos4__)
	/* using poll */
    struct pollfd fds[1]; /* for the poll */
#ifdef MINIUPNPC_IGNORE_EINTR
    do {
#endif
        fds[0].fd = socket;
        fds[0].events = POLLIN;
        n = poll(fds, 1, timeout);
#ifdef MINIUPNPC_IGNORE_EINTR
    } while(n < 0 && errno == EINTR);
#endif
    if(n < 0) {
        PRINT_SOCKET_ERROR("poll");
        return -1;
    } else if(n == 0) {
		/* timeout */
        return 0;
    }
#else /* !defined(_WIN32) && !defined(__amigaos__) && !defined(__amigaos4__) */
	/* using select under _WIN32 and amigaos */
    fd_set socketSet;
    TIMEVAL timeval;
    FD_ZERO(&socketSet);
    FD_SET(socket, &socketSet);
    timeval.tv_sec = timeout / 1000;
    timeval.tv_usec = (timeout % 1000) * 1000;
    n = select(FD_SETSIZE, &socketSet, NULL, NULL, &timeval);
    if(n < 0) {
        PRINT_SOCKET_ERROR("select");
        return -1;
    } else if(n == 0) {
        return 0;
    }
#endif
	n = recv(socket, data, length, 0);
	if(n<0) {
		PRINT_SOCKET_ERROR("recv");
	}
	return n;
}


