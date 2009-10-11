/* $Id: miniwget.c,v 1.28 2009/10/10 19:15:35 nanard Exp $ */
/* Project : miniupnp
 * Author : Thomas Bernard
 * Copyright (c) 2005-2009 Thomas Bernard
 * This software is subject to the conditions detailed in the
 * LICENCE file provided in this distribution.
 * */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "miniupnpc.h"
#ifdef WIN32
#include <winsock2.h>
#include <io.h>
#define MAXHOSTNAMELEN 64
#define MIN(x,y) (((x)<(y))?(x):(y))
#define snprintf _snprintf
#define herror
#define socklen_t int
#else
#include <unistd.h>
#include <sys/param.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <time.h>
#define closesocket close
#define MINIUPNPC_IGNORE_EINTR
#endif
#if defined(__sun) || defined(sun)
#define MIN(x,y) (((x)<(y))?(x):(y))
#endif

#include "miniupnpcstrings.h"
#include "miniwget.h"

/* miniwget2() :
 * */
static void *
miniwget2(const char * url, const char * host,
		  unsigned short port, const char * path,
		  int * size, char * addr_str, int addr_str_len)
{
	char buf[2048];
    int s;
	struct sockaddr_in dest;
	struct hostent *hp;
	int n;
	int len;
	int sent;
#ifdef MINIUPNPC_SET_SOCKET_TIMEOUT
	struct timeval timeout;
#endif
	*size = 0;
	hp = gethostbyname(host);
	if(hp==NULL)
	{
		herror(host);
		return NULL;
	}
	/*  memcpy((char *)&dest.sin_addr, hp->h_addr, hp->h_length);  */
	memcpy(&dest.sin_addr, hp->h_addr, sizeof(dest.sin_addr));
	memset(dest.sin_zero, 0, sizeof(dest.sin_zero));
	s = socket(PF_INET, SOCK_STREAM, 0);
	if(s < 0)
	{
		perror("socket");
		return NULL;
	}
#ifdef MINIUPNPC_SET_SOCKET_TIMEOUT
	/* setting a 3 seconds timeout for the connect() call */
	timeout.tv_sec = 3;
	timeout.tv_usec = 0;
	if(setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(struct timeval)) < 0)
	{
		perror("setsockopt");
	}
	timeout.tv_sec = 3;
	timeout.tv_usec = 0;
	if(setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(struct timeval)) < 0)
	{
		perror("setsockopt");
	}
#endif
	dest.sin_family = AF_INET;
	dest.sin_port = htons(port);
	n = connect(s, (struct sockaddr *)&dest, sizeof(struct sockaddr_in));
#ifdef MINIUPNPC_IGNORE_EINTR
	while(n < 0 && errno == EINTR)
	{
		socklen_t len;
		fd_set wset;
		int err;
		FD_ZERO(&wset);
		FD_SET(s, &wset);
		if((n = select(s + 1, NULL, &wset, NULL, NULL)) == -1 && errno == EINTR)
			continue;
		/*len = 0;*/
		/*n = getpeername(s, NULL, &len);*/
		len = sizeof(err);
		if(getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &len) < 0) {
			perror("getsockopt");
			closesocket(s);
			return NULL;
		}
		if(err != 0) {
			errno = err;
			n = -1;
		}
	}
#endif
	if(n<0)
	{
		perror("connect");
		closesocket(s);
		return NULL;
	}

	/* get address for caller ! */
	if(addr_str)
	{
		struct sockaddr_in saddr;
		socklen_t saddrlen;

		saddrlen = sizeof(saddr);
		if(getsockname(s, (struct sockaddr *)&saddr, &saddrlen) < 0)
		{
			perror("getsockname");
		}
		else
		{
#ifndef WIN32
			inet_ntop(AF_INET, &saddr.sin_addr, addr_str, addr_str_len);
#else
	/* using INT WINAPI WSAAddressToStringA(LPSOCKADDR, DWORD, LPWSAPROTOCOL_INFOA, LPSTR, LPDWORD);
     * But his function make a string with the port :  nn.nn.nn.nn:port */
/*		if(WSAAddressToStringA((SOCKADDR *)&saddr, sizeof(saddr),
                            NULL, addr_str, (DWORD *)&addr_str_len))
		{
		    printf("WSAAddressToStringA() failed : %d\n", WSAGetLastError());
		}*/
			strncpy(addr_str, inet_ntoa(saddr.sin_addr), addr_str_len);
#endif
		}
#ifdef DEBUG
		printf("address miniwget : %s\n", addr_str);
#endif
	}

	len = snprintf(buf, sizeof(buf),
                 "GET %s HTTP/1.1\r\n"
			     "Host: %s:%d\r\n"
				 "Connection: Close\r\n"
				 "User-Agent: " OS_STRING ", UPnP/1.0, MiniUPnPc/" MINIUPNPC_VERSION_STRING "\r\n"

				 "\r\n",
		    path, host, port);
	sent = 0;
	/* sending the HTTP request */
	while(sent < len)
	{
		n = send(s, buf+sent, len-sent, 0);
		if(n < 0)
		{
			perror("send");
			closesocket(s);
			return NULL;
		}
		else
		{
			sent += n;
		}
	}
	{
		int headers=1;
		char * respbuffer = NULL;
		int allreadyread = 0;
		/*while((n = recv(s, buf, 2048, 0)) > 0)*/
		while((n = ReceiveData(s, buf, 2048, 5000)) > 0)
		{
			if(headers)
			{
				int i=0;
				while(i<n-3)
				{
					/* searching for the end of the HTTP headers */
					if(buf[i]=='\r' && buf[i+1]=='\n'
					   && buf[i+2]=='\r' && buf[i+3]=='\n')
					{
						headers = 0;	/* end */
						if(i<n-4)
						{
							/* Copy the content into respbuffet */
							respbuffer = (char *)realloc((void *)respbuffer, 
														 allreadyread+(n-i-4));
							memcpy(respbuffer+allreadyread, buf + i + 4, n-i-4);
							allreadyread += (n-i-4);
						}
						break;
					}
					i++;
				}
			}
			else
			{
				respbuffer = (char *)realloc((void *)respbuffer, 
								 allreadyread+n);
				memcpy(respbuffer+allreadyread, buf, n);
				allreadyread += n;
			}
		}
		*size = allreadyread;
#ifdef DEBUG
		printf("%d bytes read\n", *size);
#endif
		closesocket(s);
		return respbuffer;
	}
}

/* parseURL()
 * arguments :
 *   url :		source string not modified
 *   hostname :	hostname destination string (size of MAXHOSTNAMELEN+1)
 *   port :		port (destination)
 *   path :		pointer to the path part of the URL 
 *
 * Return values :
 *    0 - Failure
 *    1 - Success         */
int parseURL(const char * url, char * hostname, unsigned short * port, char * * path)
{
	char * p1, *p2, *p3;
	p1 = strstr(url, "://");
	if(!p1)
		return 0;
	p1 += 3;
	if(  (url[0]!='h') || (url[1]!='t')
	   ||(url[2]!='t') || (url[3]!='p'))
		return 0;
	p2 = strchr(p1, ':');
	p3 = strchr(p1, '/');
	if(!p3)
		return 0;
	memset(hostname, 0, MAXHOSTNAMELEN + 1);
	if(!p2 || (p2>p3))
	{
		strncpy(hostname, p1, MIN(MAXHOSTNAMELEN, (int)(p3-p1)));
		*port = 80;
	}
	else
	{
		strncpy(hostname, p1, MIN(MAXHOSTNAMELEN, (int)(p2-p1)));
		*port = 0;
		p2++;
		while( (*p2 >= '0') && (*p2 <= '9'))
		{
			*port *= 10;
			*port += (unsigned short)(*p2 - '0');
			p2++;
		}
	}
	*path = p3;
	return 1;
}

void * miniwget(const char * url, int * size)
{
	unsigned short port;
	char * path;
	/* protocol://host:port/chemin */
	char hostname[MAXHOSTNAMELEN+1];
	*size = 0;
	if(!parseURL(url, hostname, &port, &path))
		return NULL;
	return miniwget2(url, hostname, port, path, size, 0, 0);
}

void * miniwget_getaddr(const char * url, int * size, char * addr, int addrlen)
{
	unsigned short port;
	char * path;
	/* protocol://host:port/chemin */
	char hostname[MAXHOSTNAMELEN+1];
	*size = 0;
	if(addr)
		addr[0] = '\0';
	if(!parseURL(url, hostname, &port, &path))
		return NULL;
	return miniwget2(url, hostname, port, path, size, addr, addrlen);
}

