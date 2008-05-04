/* $Id: igd_desc_parse.h,v 1.6 2008/04/23 11:51:07 nanard Exp $ */
/* Project : miniupnp
 * http://miniupnp.free.fr/
 * Author : Thomas Bernard
 * Copyright (c) 2005-2008 Thomas Bernard
 * This software is subject to the conditions detailed in the
 * LICENCE file provided in this distribution.
 * */
#ifndef __IGD_DESC_PARSE_H__
#define __IGD_DESC_PARSE_H__

/* Structure to store the result of the parsing of UPnP
 * descriptions of Internet Gateway Devices */
#define MINIUPNPC_URL_MAXSIZE (128)
struct IGDdatas {
	char cureltname[MINIUPNPC_URL_MAXSIZE];
	char urlbase[MINIUPNPC_URL_MAXSIZE];
	int level;
	/*int state;*/
	/* "urn:schemas-upnp-org:service:WANCommonInterfaceConfig:1" */
	char controlurl_CIF[MINIUPNPC_URL_MAXSIZE];
	char eventsuburl_CIF[MINIUPNPC_URL_MAXSIZE];
	char scpdurl_CIF[MINIUPNPC_URL_MAXSIZE];
	char servicetype_CIF[MINIUPNPC_URL_MAXSIZE];
	/*char devicetype_CIF[MINIUPNPC_URL_MAXSIZE];*/
	/* "urn:schemas-upnp-org:service:WANIPConnection:1"
	 * "urn:schemas-upnp-org:service:WANPPPConnection:1" */
	char controlurl[MINIUPNPC_URL_MAXSIZE];
	char eventsuburl[MINIUPNPC_URL_MAXSIZE];
	char scpdurl[MINIUPNPC_URL_MAXSIZE];
	char servicetype[MINIUPNPC_URL_MAXSIZE];
	/*char devicetype[MINIUPNPC_URL_MAXSIZE];*/
	/* tmp */
	char controlurl_tmp[MINIUPNPC_URL_MAXSIZE];
	char eventsuburl_tmp[MINIUPNPC_URL_MAXSIZE];
	char scpdurl_tmp[MINIUPNPC_URL_MAXSIZE];
	char servicetype_tmp[MINIUPNPC_URL_MAXSIZE];
	/*char devicetype_tmp[MINIUPNPC_URL_MAXSIZE];*/
};

void IGDstartelt(void *, const char *, int);
void IGDendelt(void *, const char *, int);
void IGDdata(void *, const char *, int);
void printIGD(struct IGDdatas *);

#endif

