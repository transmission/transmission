/* $Id: upnpcommands.h,v 1.10 2007/01/29 20:27:24 nanard Exp $ */
/* Miniupnp project : http://miniupnp.free.fr/
 * Author : Thomas Bernard
 * Copyright (c) 2005-2006 Thomas Bernard
 * This software is subject to the conditions detailed in the
 * LICENCE file provided within this distribution */
#ifndef __UPNPCOMMANDS_H__
#define __UPNPCOMMANDS_H__

#include "upnpreplyparse.h"
#include "declspec.h"

#ifdef __cplusplus
extern "C" {
#endif

LIBSPEC unsigned int
UPNP_GetTotalBytesSent(const char * controlURL,
					const char * servicetype);

LIBSPEC unsigned int
UPNP_GetTotalBytesReceived(const char * controlURL,
						const char * servicetype);

LIBSPEC unsigned int
UPNP_GetTotalPacketsSent(const char * controlURL,
					const char * servicetype);

LIBSPEC unsigned int
UPNP_GetTotalPacketsReceived(const char * controlURL,
					const char * servicetype);

LIBSPEC void
UPNP_GetStatusInfo(const char * controlURL,
			       const char * servicetype,
				   char * status,
				   unsigned int * uptime);

LIBSPEC void
UPNP_GetConnectionTypeInfo(const char * controlURL,
                           const char * servicetype,
						   char * connectionType);

/* UPNP_GetExternalIPAddress() call the corresponding UPNP method.
 * if the third arg is not null the value is copied to it.
 * at least 16 bytes must be available */
LIBSPEC void
UPNP_GetExternalIPAddress(const char * controlURL,
                          const char * servicetype,
                          char * extIpAdd);

LIBSPEC void
UPNP_GetLinkLayerMaxBitRates(const char* controlURL,
							const char* servicetype,
							unsigned int * bitrateDown,
							unsigned int * bitrateUp);

/* Returns zero if unable to add the port mapping, otherwise non-zero 
 * to indicate success */
LIBSPEC int
UPNP_AddPortMapping(const char * controlURL, const char * servicetype,
                    const char * extPort,
				    const char * inPort,
					const char * inClient,
					const char * desc,
                    const char * proto);

LIBSPEC void
UPNP_DeletePortMapping(const char * controlURL, const char * servicetype,
                       const char * extPort, const char * proto);

LIBSPEC void
UPNP_GetPortMappingNumberOfEntries(const char* controlURL, const char* servicetype, unsigned int * num);

/* UPNP_GetSpecificPortMappingEntry retrieves an existing port mapping
 * the result is returned in the intClient and intPort strings
 * please provide 16 and 6 bytes of data */
LIBSPEC void
UPNP_GetSpecificPortMappingEntry(const char * controlURL,
                                 const char * servicetype,
                                 const char * extPort,
                                 const char * proto,
                                 char * intClient,
                                 char * intPort);

LIBSPEC int
UPNP_GetGenericPortMappingEntry(const char * controlURL,
                                const char * servicetype,
								const char * index,
								char * extPort,
								char * intClient,
								char * intPort,
								char * protocol,
								char * desc,
								char * enabled,
								char * rHost,
								char * duration);

#ifdef __cplusplus
}
#endif

#endif

