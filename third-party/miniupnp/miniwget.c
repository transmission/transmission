/* $Id: miniwget.c,v 1.37 2010/04/12 20:39:42 nanard Exp $ */
/* Project : miniupnp
 * Author : Thomas Bernard
 * Copyright (c) 2005-2010 Thomas Bernard
 * This software is subject to the conditions detailed in the
 * LICENCE file provided in this distribution. */
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "miniupnpc.h"
#ifdef WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <io.h>
#define MAXHOSTNAMELEN 64
#define MIN(x,y) (((x)<(y))?(x):(y))
#define snprintf _snprintf
#define socklen_t int
#else /* #ifdef WIN32 */
#include <unistd.h>
#include <sys/param.h>
#if defined(__amigaos__) && !defined(__amigaos4__)
#define socklen_t int
#else /* #if defined(__amigaos__) && !defined(__amigaos4__) */
#include <sys/select.h>
#endif /* #else defined(__amigaos__) && !defined(__amigaos4__) */
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#define closesocket close
/* defining MINIUPNPC_IGNORE_EINTR enable the ignore of interruptions
 * during the connect() call */
#define MINIUPNPC_IGNORE_EINTR
#endif /* #else WIN32 */
#if defined(__sun) || defined(sun)
#define MIN(x,y) (((x)<(y))?(x):(y))
#endif

#include "miniupnpcstrings.h"
#include "miniwget.h"
#include "connecthostport.h"

/* miniwget3() :
 * do all the work.
 * Return NULL if something failed. */
static void *
miniwget3(const char * url, const char * host,
		  unsigned short port, const char * path,
		  int * size, char * addr_str, int addr_str_len, const char * httpversion)
{
	char buf[2048];
    int s;
	int n;
	int len;
	int sent;

	*size = 0;
	s = connecthostport(host, port);
	if(s < 0)
		return NULL;

	/* get address for caller ! */
	if(addr_str)
	{
		struct sockaddr saddr;
		socklen_t saddrlen;

		saddrlen = sizeof(saddr);
		if(getsockname(s, &saddr, &saddrlen) < 0)
		{
			perror("getsockname");
		}
		else
		{
#if defined(__amigaos__) && !defined(__amigaos4__)
	/* using INT WINAPI WSAAddressToStringA(LPSOCKADDR, DWORD, LPWSAPROTOCOL_INFOA, LPSTR, LPDWORD);
     * But his function make a string with the port :  nn.nn.nn.nn:port */
/*		if(WSAAddressToStringA((SOCKADDR *)&saddr, sizeof(saddr),
                            NULL, addr_str, (DWORD *)&addr_str_len))
		{
		    printf("WSAAddressToStringA() failed : %d\n", WSAGetLastError());
		}*/
			strncpy(addr_str, inet_ntoa(((struct sockaddr_in *)&saddr)->sin_addr), addr_str_len);
#else
			/*inet_ntop(AF_INET, &saddr.sin_addr, addr_str, addr_str_len);*/
			n = getnameinfo(&saddr, saddrlen,
			                addr_str, addr_str_len,
			                NULL, 0,
			                NI_NUMERICHOST | NI_NUMERICSERV);
			if(n != 0) {
#ifdef WIN32
				fprintf(stderr, "getnameinfo() failed : %d\n", n);
#else
				fprintf(stderr, "getnameinfo() failed : %s\n", gai_strerror(n));
#endif
			}
#endif
		}
#ifdef DEBUG
		printf("address miniwget : %s\n", addr_str);
#endif
	}

	len = snprintf(buf, sizeof(buf),
                 "GET %s HTTP/%s\r\n"
			     "Host: %s:%d\r\n"
				 "Connection: Close\r\n"
				 "User-Agent: " OS_STRING ", UPnP/1.0, MiniUPnPc/" MINIUPNPC_VERSION_STRING "\r\n"

				 "\r\n",
			   path, httpversion, host, port);
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
		/* TODO : in order to support HTTP/1.1, chunked transfer encoding
		 *        must be supported. That means parsing of headers must be
		 *        added.                                                   */
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

/* miniwget2() :
 * Call miniwget3(); retry with HTTP/1.1 if 1.0 fails. */
static void *
miniwget2(const char * url, const char * host,
		  unsigned short port, const char * path,
		  int * size, char * addr_str, int addr_str_len)
{
	char * respbuffer;

	respbuffer = miniwget3(url, host, port, path, size, addr_str, addr_str_len, "1.0");
	if (*size == 0)
	{
#ifdef DEBUG
		printf("Retrying with HTTP/1.1\n");
#endif
		free(respbuffer);
		respbuffer = miniwget3(url, host, port, path, size, addr_str, addr_str_len, "1.1");
	}
	return respbuffer;
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

