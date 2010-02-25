/* $Id: wingettimeofday.c,v 1.3 2009/12/19 12:00:00 nanard Exp $ */
/* libnatpmp
 * Copyright (c) 2007-2008, Thomas BERNARD <miniupnp@free.fr>
 * http://miniupnp.free.fr/libnatpmp.html
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
#ifdef WIN32
#if defined(_MSC_VER)
struct timeval {
	long    tv_sec;
	long    tv_usec;
};
#else
#include <sys/time.h>
#endif

typedef struct _FILETIME {
    unsigned long dwLowDateTime;
    unsigned long dwHighDateTime;
} FILETIME;

void __stdcall GetSystemTimeAsFileTime(FILETIME*);
  
//int gettimeofday(struct timeval* p, void* tz /* IGNORED */);

int gettimeofday(struct timeval* p, void* tz /* IGNORED */) {
  union {
   long long ns100; /*time since 1 Jan 1601 in 100ns units */
   FILETIME ft;
  } _now;

	if(!p)
		return -1;
  GetSystemTimeAsFileTime( &(_now.ft) );
  p->tv_usec =(long)((_now.ns100 / 10LL) % 1000000LL );
  p->tv_sec = (long)((_now.ns100-(116444736000000000LL))/10000000LL);
  return 0;
}
#endif

