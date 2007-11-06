/* $Id: minissdpc.c,v 1.3 2007/09/01 23:34:12 nanard Exp $ */
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

struct UPNPDev *
getDevicesFromMiniSSDPD(const char * devtype, const char * socketpath)
{
	struct UPNPDev * tmp;
	struct UPNPDev * devlist = NULL;
	unsigned char buffer[512];
	ssize_t n;
	unsigned char * p;
	unsigned int i;
	unsigned int urlsize, stsize;
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
	buffer[1] = stsize;
	memcpy(buffer + 2, devtype, (int)buffer[1]);
	if(write(s, buffer, (int)buffer[1] + 2) < 0)
	{
		/*syslog(LOG_ERR, "write(): %m");*/
		perror("write()");
		close(s);
		return NULL;
	}
	n = read(s, buffer, sizeof(buffer));
	if(n<=0)
	{
		close(s);
		return NULL;
	}
	p = buffer + 1;
	for(i = 0; i < buffer[0]; i++)
	{
		urlsize = *(p++);
		stsize = p[urlsize];
		tmp = (struct UPNPDev *)malloc(sizeof(struct UPNPDev)+urlsize+stsize);
		tmp->pNext = devlist;
		tmp->descURL = tmp->buffer;
		tmp->st = tmp->buffer + 1 + urlsize;
		memcpy(tmp->buffer, p, urlsize);
		tmp->buffer[urlsize] = '\0';
		p += urlsize;
		p++;
		memcpy(tmp->buffer + urlsize + 1, p, stsize);
		tmp->buffer[urlsize+1+stsize] = '\0';
		devlist = tmp;
	}
	close(s);
	return devlist;
}

