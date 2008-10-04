/* $Id: minissdpc.c,v 1.4 2007/12/19 14:56:58 nanard Exp $ */
/* Project : miniupnp
 * Author : Thomas BERNARD
 * copyright (c) 2005-2007 Thomas Bernard
 * This software is subjet to the conditions detailed in the
 * provided LICENCE file. */
/*#include <syslog.h>*/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "minissdpc.h"
#include "miniupnpc.h"

#define DECODELENGTH(n, p) n = 0; \
                           do { n = (n << 7) | (*p & 0x7f); } \
                           while(*(p++)&0x80);
#define CODELENGTH(n, p) do { *p = (n & 0x7f) | ((n > 0x7f) ? 0x80 : 0); \
                              p++; n >>= 7; } while(n);

struct UPNPDev *
getDevicesFromMiniSSDPD(const char * devtype, const char * socketpath)
{
	struct UPNPDev * tmp;
	struct UPNPDev * devlist = NULL;
	unsigned char buffer[2048];
	ssize_t n;
	unsigned char * p;
	unsigned char * url;
	unsigned int i;
	unsigned int urlsize, stsize, usnsize, l;
	int s;
	struct sockaddr_un addr;

	s = socket(AF_UNIX, SOCK_STREAM, 0);
	if(s < 0)
	{
		/*syslog(LOG_ERR, "socket(unix): %m");*/
		perror("socket(unix)");
		return NULL;
	}
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, socketpath, sizeof(addr.sun_path));
	if(connect(s, (struct sockaddr *)&addr, sizeof(struct sockaddr_un)) < 0)
	{
		/*syslog(LOG_WARNING, "connect(\"%s\"): %m", socketpath);*/
		close(s);
		return NULL;
	}
	stsize = strlen(devtype);
	buffer[0] = 1;
	p = buffer + 1;
	l = stsize;	CODELENGTH(l, p);
	memcpy(p, devtype, stsize);
	p += stsize;
	if(write(s, buffer, p - buffer) < 0)
	{
		/*syslog(LOG_ERR, "write(): %m");*/
		perror("minissdpc.c: write()");
		close(s);
		return NULL;
	}
	n = read(s, buffer, sizeof(buffer));
	if(n<=0)
	{
		perror("minissdpc.c: read()");
		close(s);
		return NULL;
	}
	p = buffer + 1;
	for(i = 0; i < buffer[0]; i++)
	{
		if(p+2>=buffer+sizeof(buffer))
			break;
		DECODELENGTH(urlsize, p);
		if(p+urlsize+2>=buffer+sizeof(buffer))
			break;
		url = p;
		p += urlsize;
		DECODELENGTH(stsize, p);
		if(p+stsize+2>=buffer+sizeof(buffer))
			break;
		tmp = (struct UPNPDev *)malloc(sizeof(struct UPNPDev)+urlsize+stsize);
		tmp->pNext = devlist;
		tmp->descURL = tmp->buffer;
		tmp->st = tmp->buffer + 1 + urlsize;
		memcpy(tmp->buffer, url, urlsize);
		tmp->buffer[urlsize] = '\0';
		memcpy(tmp->buffer + urlsize + 1, p, stsize);
		p += stsize;
		tmp->buffer[urlsize+1+stsize] = '\0';
		devlist = tmp;
		/* added for compatibility with recent versions of MiniSSDPd 
		 * >= 2007/12/19 */
		DECODELENGTH(usnsize, p);
		p += usnsize;
		if(p>buffer + sizeof(buffer))
			break;
	}
	close(s);
	return devlist;
}

