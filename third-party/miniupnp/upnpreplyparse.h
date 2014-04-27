/* $Id: upnpreplyparse.h,v 1.17 2013/06/06 21:36:40 nanard Exp $ */
/* MiniUPnP project
 * http://miniupnp.free.fr/ or http://miniupnp.tuxfamily.org/
 * (c) 2006-2013 Thomas Bernard
 * This software is subject to the conditions detailed
 * in the LICENCE file provided within the distribution */

#ifndef UPNPREPLYPARSE_H_INCLUDED
#define UPNPREPLYPARSE_H_INCLUDED

#if defined(NO_SYS_QUEUE_H) || defined(_WIN32) || defined(__HAIKU__)
#include "bsdqueue.h"
#else
#include <sys/queue.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct NameValue {
    LIST_ENTRY(NameValue) entries;
    char name[64];
    char value[128];
};

struct NameValueParserData {
    LIST_HEAD(listhead, NameValue) head;
    char curelt[64];
	char * portListing;
	int portListingLength;
	int topelt;
	const char * cdata;
	int cdatalen;
};

/* ParseNameValue() */
void
ParseNameValue(const char * buffer, int bufsize,
               struct NameValueParserData * data);

/* ClearNameValueList() */
void
ClearNameValueList(struct NameValueParserData * pdata);

/* GetValueFromNameValueList() */
char *
GetValueFromNameValueList(struct NameValueParserData * pdata,
                          const char * Name);

#if 0
/* GetValueFromNameValueListIgnoreNS() */
char *
GetValueFromNameValueListIgnoreNS(struct NameValueParserData * pdata,
                                  const char * Name);
#endif

/* DisplayNameValueList() */
#ifdef DEBUG
void
DisplayNameValueList(char * buffer, int bufsize);
#endif

#ifdef __cplusplus
}
#endif

#endif

