/* $Id: minisoap.c,v 1.11 2007/05/19 13:13:08 nanard Exp $ */
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
	p = malloc(headerssize+bodysize);
	memcpy(p, headers, headerssize);
	memcpy(p+headerssize, body, bodysize);
	/*n = write(fd, p, headerssize+bodysize);*/
	n = send(fd, p, headerssize+bodysize, 0);
#ifdef WIN32
	shutdown(fd, SD_SEND);
#else
	shutdown(fd, SHUT_WR);	/*SD_SEND*/
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
	char headerbuf[1024];
	int headerssize;
	bodysize = (int)strlen(body);
	headerssize = snprintf(headerbuf, sizeof(headerbuf),
                       "POST %s HTTP/1.1\r\n"
	                   "HOST: %s:%d\r\n"
	                   "Content-length: %d\r\n"
					   "Content-Type: text/xml\r\n"
					   "SOAPAction: \"%s\"\r\n"
					   "Connection: Close\r\n"
					   "\r\n",
					   url, host, port, bodysize, action);
	return httpWrite(fd, body, bodysize, headerbuf, headerssize);
}


