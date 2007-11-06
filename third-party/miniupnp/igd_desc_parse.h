/* $Id: igd_desc_parse.h,v 1.5 2007/04/11 15:21:09 nanard Exp $ */
/* Project : miniupnp
 * http://miniupnp.free.fr/
 * Author : Thomas Bernard
 * Copyright (c) 2005 Thomas Bernard
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
	int state;
	char controlurl_CIF[MINIUPNPC_URL_MAXSIZE];
	char eventsuburl_CIF[MINIUPNPC_URL_MAXSIZE];
	char scpdurl_CIF[MINIUPNPC_URL_MAXSIZE];
	char servicetype_CIF[MINIUPNPC_URL_MAXSIZE];
	char devicetype_CIF[MINIUPNPC_URL_MAXSIZE];
	char controlurl[MINIUPNPC_URL_MAXSIZE];
	char eventsuburl[MINIUPNPC_URL_MAXSIZE];
	char scpdurl[MINIUPNPC_URL_MAXSIZE];
	char servicetype[MINIUPNPC_URL_MAXSIZE];
	char devicetype[MINIUPNPC_URL_MAXSIZE];
};

void IGDstartelt(void *, const char *, int);
void IGDendelt(void *, const char *, int);
void IGDdata(void *, const char *, int);
void printIGD(struct IGDdatas *);

#endif

