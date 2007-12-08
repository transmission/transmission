/* $Id: getgateway.c,v 1.4 2007/11/22 18:01:37 nanard Exp $ */
/* libnatpmp
 * Copyright (c) 2007, Thomas BERNARD <miniupnp@free.fr>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE. */
#include <stdio.h>
#include <ctype.h>
#include <netinet/in.h>
#include <sys/param.h>
#if defined(BSD) || defined(__APPLE__)
#include <stdlib.h>
#include <sys/sysctl.h>
#include <sys/socket.h>
#include <net/route.h>
#endif
#include "getgateway.h"

#ifdef __linux__
int getdefaultgateway(in_addr_t * addr)
{
	long d, g;
	char buf[256];
	int line = 0;
	FILE * f;
	char * p;
	f = fopen("/proc/net/route", "r");
	if(!f)
		return -1;
	while(fgets(buf, sizeof(buf), f)) {
		if(line > 0) {
			p = buf;
			while(*p && !isspace(*p))
				p++;
			while(*p && isspace(*p))
				p++;
			if(sscanf(p, "%lx%lx", &d, &g)==2) {
				if(d == 0) { /* default */
					*addr = g;
					fclose(f);
					return 0;
				}
			}
		}
		line++;
	}
	/* not found ! */
	if(f)
		fclose(f);
	return -1;
}
#endif

#if defined(BSD) || defined(__APPLE__)

#define ROUNDUP(a) \
	((a) > 0 ? (1 + (((a) - 1) | (sizeof(long) - 1))) : sizeof(long))

int getdefaultgateway(in_addr_t * addr)
{
	int mib[] = {CTL_NET, PF_ROUTE, 0, AF_INET,
	             NET_RT_DUMP, 0, 0/*tableid*/};
	size_t l;
	char * buf, * p;
	struct rt_msghdr * rt;
	struct sockaddr * sa;
	struct sockaddr * sa_tab[RTAX_MAX];
	int i;
	int r = -1;
	if(sysctl(mib, sizeof(mib)/sizeof(int), 0, &l, 0, 0) < 0) {
		return -1;
	}
	if(l>0) {
		buf = malloc(l);
		if(sysctl(mib, sizeof(mib)/sizeof(int), buf, &l, 0, 0) < 0) {
			return -1;
		}
		for(p=buf; p<buf+l; p+=rt->rtm_msglen) {
			rt = (struct rt_msghdr *)p;
			sa = (struct sockaddr *)(rt + 1);
			for(i=0; i<RTAX_MAX; i++) {
				if(rt->rtm_addrs & (1 << i)) {
					sa_tab[i] = sa;
					sa = (struct sockaddr *)((char *)sa + ROUNDUP(sa->sa_len));
				} else {
					sa_tab[i] = NULL;
				}
			}
			if( ((rt->rtm_addrs & (RTA_DST|RTA_GATEWAY)) == (RTA_DST|RTA_GATEWAY))
              && sa_tab[RTAX_DST]->sa_family == AF_INET
              && sa_tab[RTAX_GATEWAY]->sa_family == AF_INET) {
				if(((struct sockaddr_in *)sa_tab[RTAX_DST])->sin_addr.s_addr == 0) {
					*addr = ((struct sockaddr_in *)(sa_tab[RTAX_GATEWAY]))->sin_addr.s_addr;
					r = 0;
				}
			}
		}
		free(buf);
	}
	return r;
}
#endif
