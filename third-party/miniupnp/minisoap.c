/* $Id: minisoap.c,v 1.15 2008/02/17 17:57:07 nanard Exp $ */
/* Project : miniupnp
 * Author : Thomas Bernard
 * Copyright (c) 2005 Thomas Bernard
 * This software is subject to the conditions detailed in the
 * LICENCE file provided in this distribution.
 *
 * Minimal SOAP implementation for UPnP protocol.
 */
#include <stdio.h>
#include <string.h>
#ifdef WIN32
#include <io.h>
#include <winsock2.h>
#define snprintf _snprintf
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#endif
#include "minisoap.h"

/* only for malloc */
#include <stdlib.h>

#ifdef WIN32
#define PRINT_SOCKET_ERROR(x)    printf("Socket error: %s, %d\n", x, WSAGetLastError());
#else
#define PRINT_SOCKET_ERROR(x) perror(x)
#endif

/* httpWrite sends the headers and the body to the socket
 * and returns the number of bytes sent */
static int
httpWrite(int fd, const char * body, int bodysize,
          const char * headers, int headerssize)
{
	int n = 0;
	/*n = write(fd, headers, headerssize);*/
	/*if(bodysize>0)
		n += write(fd, body, bodysize);*/
	/* Note : my old linksys router only took into account
	 * soap request that are sent into only one packet */
	char * p;
	/* TODO: AVOID MALLOC */
	p = malloc(headerssize+bodysize);
	if(!p)
	  return 0;
	memcpy(p, headers, headerssize);
	memcpy(p+headerssize, body, bodysize);
	/*n = write(fd, p, headerssize+bodysize);*/
	n = send(fd, p, headerssize+bodysize, 0);
	if(n<0) {
	  PRINT_SOCKET_ERROR("send");
	}
	/* disable send on the socket */
	/* draytek routers dont seems to like that... */
#if 0
#ifdef WIN32
	if(shutdown(fd, SD_SEND)<0) {
#else
	if(shutdown(fd, SHUT_WR)<0)	{ /*SD_SEND*/
#endif
		PRINT_SOCKET_ERROR("shutdown");
	}
#endif
	free(p);
	return n;
}

/* self explanatory  */
int soapPostSubmit(int fd,
                   const char * url,
				   const char * host,
				   unsigned short port,
				   const char * action,
				   const char * body)
{
	int bodysize;
	char headerbuf[512];
	int headerssize;
	char portstr[8];
	bodysize = (int)strlen(body);
	/* We are not using keep-alive HTTP connections.
	 * HTTP/1.1 needs the header Connection: close to do that.
	 * This is the default with HTTP/1.0 */
    /* Connection: Close is normally there only in HTTP/1.1 but who knows */
	portstr[0] = '\0';
	if(port != 80)
		snprintf(portstr, sizeof(portstr), ":%hu", port);
	headerssize = snprintf(headerbuf, sizeof(headerbuf),
                       "POST %s HTTP/1.1\r\n"
/*                       "POST %s HTTP/1.0\r\n"*/
	                   "Host: %s%s\r\n"
					   "User-Agent: POSIX, UPnP/1.0, miniUPnPc/1.0\r\n"
	                   "Content-Length: %d\r\n"
					   "Content-Type: text/xml\r\n"
					   "SOAPAction: \"%s\"\r\n"
					   "Connection: Close\r\n"
					   "Cache-Control: no-cache\r\n"	/* ??? */
					   "Pragma: no-cache\r\n"
					   "\r\n",
					   url, host, portstr, bodysize, action);
#ifdef DEBUG
	printf("SOAP request : headersize=%d bodysize=%d\n",
	       headerssize, bodysize);
	/*printf("%s", headerbuf);*/
#endif
	return httpWrite(fd, body, bodysize, headerbuf, headerssize);
}


