/* $Id: miniupnpc.h,v 1.15 2007/10/16 15:07:32 nanard Exp $ */
/* Project: miniupnp
 * http://miniupnp.free.fr/
 * Author: Thomas Bernard
 * Copyright (c) 2005-2006 Thomas Bernard
 * This software is subjects to the conditions detailed
 * in the LICENCE file provided within this distribution */
#ifndef __MINIUPNPC_H__
#define __MINIUPNPC_H__

#include "declspec.h"
#include "igd_desc_parse.h"

#ifdef __cplusplus
extern "C" {
#endif

struct UPNParg { const char * elt; const char * val; };

int simpleUPnPcommand(int, const char *, const char *,
                      const char *, struct UPNParg *,
                      char *, int *);

struct UPNPDev {
	struct UPNPDev * pNext;
	char * descURL;
	char * st;
	char buffer[2];
};

/* discover UPnP devices on the network */
LIBSPEC struct UPNPDev * upnpDiscover(int delay, const char * multicastif);
/* free returned list from above function */
LIBSPEC void freeUPNPDevlist(struct UPNPDev * devlist);

LIBSPEC void parserootdesc(const char *, int, struct IGDdatas *);

/* structure used to get fast access to urls
 * controlURL: controlURL of the WANIPConnection
 * ipcondescURL: url of the description of the WANIPConnection
 * controlURL_CIF: controlURL of the WANCOmmonInterfaceConfig
 */
struct UPNPUrls {
	char * controlURL;
	char * ipcondescURL;
	char * controlURL_CIF;
};

/* UPNP_GetValidIGD() :
 * return values :
 *     0 = NO IGD found
 *     1 = A valid connected IGD has been found
 *     2 = A valid IGD has been found but it reported as
 *         not connected
 *     3 = an UPnP device has been found but was not recognized as an IGD
 *
 * In any non zero return case, the urls and data structures
 * passed as parameters are set. Donc forget to call FreeUPNPUrls(urls) to
 * free allocated memory.
 */
LIBSPEC int
UPNP_GetValidIGD(struct UPNPDev * devlist,
                 struct UPNPUrls * urls,
				 struct IGDdatas * data,
				 char * lanaddr, int lanaddrlen);

/* UPNP_GetIGDFromUrl()
 * Used when skipping the discovery process.
 * return value :
 *   0 - Not ok
 *   1 - OK */
LIBSPEC int
UPNP_GetIGDFromUrl(const char * rootdescurl,
                   struct UPNPUrls * urls,
                   struct IGDdatas * data,
                   char * lanaddr, int lanaddrlen);

LIBSPEC void GetUPNPUrls(struct UPNPUrls *, struct IGDdatas *, const char *);

LIBSPEC void FreeUPNPUrls(struct UPNPUrls *);

/* Reads data from the specified socket. 
 * Returns the number of bytes read if successful, zero if no bytes were 
 * read or if we timed out. Returns negative if there was an error. */
int ReceiveData(int socket, char * data, int length, int timeout);

#ifdef __cplusplus
}
#endif

#endif

