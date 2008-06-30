/* $Id: getgateway.c,v 1.8 2008/06/30 14:15:40 nanard Exp $ */
/* libnatpmp
 * Copyright (c) 2007-2008, Thomas BERNARD <miniupnp@free.fr>
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
/* There is no portable method to get the default route gateway.
 * So below are three differents functions implementing this.
 * Parsing /proc/net/route is for linux.
 * sysctl is the way to access such informations on BSD systems.
 * Many systems should provide route information through raw PF_ROUTE
 * sockets. */
#ifdef __linux__
  #define USE_PROC_NET_ROUTE
#elif defined(__APPLE__)
  #define USE_SYSCTL_NET_ROUTE
#elif defined(BSD)
  /*#define USE_SYSCTL_NET_ROUTE*/
  #define USE_SOCKET_ROUTE
#elif (defined(sun) && defined(__SVR4))
  #define USE_SOCKET_ROUTE
#endif

#ifdef USE_SYSCTL_NET_ROUTE
#include <stdlib.h>
#include <sys/sysctl.h>
#include <sys/socket.h>
#include <net/route.h>
#endif
#ifdef USE_SOCKET_ROUTE
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <net/if.h>
#include <net/route.h>
#endif
#include "getgateway.h"

#define SUCCESS (0)
#define FAILED  (-1)

#ifdef USE_PROC_NET_ROUTE
int getdefaultgateway(in_addr_t * addr)
{
	long d, g;
	char buf[256];
	int line = 0;
	FILE * f;
	char * p;
	f = fopen("/proc/net/route", "r");
	if(!f)
		return FAILED;
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
					return SUCCESS;
				}
			}
		}
		line++;
	}
	/* default route not found ! */
	if(f)
		fclose(f);
	return FAILED;
}
#endif /* #ifdef USE_PROC_NET_ROUTE */


#ifdef USE_SYSCTL_NET_ROUTE

#define ROUNDUP(a) \
	((a) > 0 ? (1 + (((a) - 1) | (sizeof(long) - 1))) : sizeof(long))

int getdefaultgateway(in_addr_t * addr)
{
#if 0
	/* net.route.0.inet.dump.0.0 ? */
	int mib[] = {CTL_NET, PF_ROUTE, 0, AF_INET,
	             NET_RT_DUMP, 0, 0/*tableid*/};
#endif
	/* net.route.0.inet.flags.gateway */
	int mib[] = {CTL_NET, PF_ROUTE, 0, AF_INET,
	             NET_RT_FLAGS, RTF_GATEWAY};
	size_t l;
	char * buf, * p;
	struct rt_msghdr * rt;
	struct sockaddr * sa;
	struct sockaddr * sa_tab[RTAX_MAX];
	int i;
	int r = FAILED;
	if(sysctl(mib, sizeof(mib)/sizeof(int), 0, &l, 0, 0) < 0) {
		return FAILED;
	}
	if(l>0) {
		buf = malloc(l);
		if(sysctl(mib, sizeof(mib)/sizeof(int), buf, &l, 0, 0) < 0) {
			return FAILED;
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
					r = SUCCESS;
				}
			}
		}
		free(buf);
	}
	return r;
}
#endif /* #ifdef USE_SYSCTL_NET_ROUTE */


#ifdef USE_SOCKET_ROUTE
/* Thanks to Darren Kenny for this code */
#define NEXTADDR(w, u) \
        if (rtm_addrs & (w)) {\
            l = sizeof(struct sockaddr); memmove(cp, &(u), l); cp += l;\
        }

#define rtm m_rtmsg.m_rtm

struct {
  struct rt_msghdr m_rtm;
  char       m_space[512];
} m_rtmsg;

int getdefaultgateway(in_addr_t *addr)
{
  int s, seq, l, rtm_addrs, i;
  pid_t pid;
  struct sockaddr so_dst, so_mask;
  char *cp = m_rtmsg.m_space; 
  struct sockaddr *gate = NULL, *sa;
  struct rt_msghdr *msg_hdr;

  pid = getpid();
  seq = 0;
  rtm_addrs = RTA_DST | RTA_NETMASK;

  memset(&so_dst, 0, sizeof(so_dst));
  memset(&so_mask, 0, sizeof(so_mask));
  memset(&rtm, 0, sizeof(struct rt_msghdr));

  rtm.rtm_type = RTM_GET;
  rtm.rtm_flags = RTF_UP | RTF_GATEWAY;
  rtm.rtm_version = RTM_VERSION;
  rtm.rtm_seq = ++seq;
  rtm.rtm_addrs = rtm_addrs; 

  so_dst.sa_family = AF_INET;
  so_mask.sa_family = AF_INET;

  NEXTADDR(RTA_DST, so_dst);
  NEXTADDR(RTA_NETMASK, so_mask);

  rtm.rtm_msglen = l = cp - (char *)&m_rtmsg;

  s = socket(PF_ROUTE, SOCK_RAW, 0);

  if (write(s, (char *)&m_rtmsg, l) < 0) {
      close(s);
      return FAILED;
  }

  do {
    l = read(s, (char *)&m_rtmsg, sizeof(m_rtmsg));
  } while (l > 0 && (rtm.rtm_seq != seq || rtm.rtm_pid != pid));
                        
  close(s);

  msg_hdr = &rtm;

  cp = ((char *)(msg_hdr + 1));
  if (msg_hdr->rtm_addrs) {
    for (i = 1; i; i <<= 1)
      if (i & msg_hdr->rtm_addrs) {
        sa = (struct sockaddr *)cp;
        if (i == RTA_GATEWAY )
          gate = sa;

        cp += sizeof(struct sockaddr);
      }
  } else {
      return FAILED;
  }


  if (gate != NULL ) {
      *addr = ((struct sockaddr_in *)gate)->sin_addr.s_addr;
      return SUCCESS;
  } else {
      return FAILED;
  }
}
#endif /* #ifdef USE_SOCKET_ROUTE */

