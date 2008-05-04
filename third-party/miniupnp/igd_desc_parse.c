/* $Id: igd_desc_parse.c,v 1.8 2008/04/23 11:51:06 nanard Exp $ */
/* Project : miniupnp
 * http://miniupnp.free.fr/
 * Author : Thomas Bernard
 * Copyright (c) 2005-2008 Thomas Bernard
 * This software is subject to the conditions detailed in the
 * LICENCE file provided in this distribution.
 * */
#include "igd_desc_parse.h"
#include <stdio.h>
#include <string.h>

/* TODO : rewrite this code so it correctly handle descriptions with
 * both WANIPConnection and/or WANPPPConnection */

/* Start element handler :
 * update nesting level counter and copy element name */
void IGDstartelt(void * d, const char * name, int l)
{
	struct IGDdatas * datas = (struct IGDdatas *)d;
	memcpy( datas->cureltname, name, l);
	datas->cureltname[l] = '\0';
	datas->level++;
	if( (l==7) && !memcmp(name, "service", l) ) {
		datas->controlurl_tmp[0] = '\0';
		datas->eventsuburl_tmp[0] = '\0';
		datas->scpdurl_tmp[0] = '\0';
		datas->servicetype_tmp[0] = '\0';
	}
}

/* End element handler :
 * update nesting level counter and update parser state if
 * service element is parsed */
void IGDendelt(void * d, const char * name, int l)
{
	struct IGDdatas * datas = (struct IGDdatas *)d;
	datas->level--;
	/*printf("endelt %2d %.*s\n", datas->level, l, name);*/
	if( (l==7) && !memcmp(name, "service", l) )
	{
		/*
		if( datas->state < 1
			&& !strcmp(datas->servicetype,
				//	"urn:schemas-upnp-org:service:WANIPConnection:1") )
				"urn:schemas-upnp-org:service:WANCommonInterfaceConfig:1"))
			datas->state ++;
		*/
		if(0==strcmp(datas->servicetype_tmp,
				"urn:schemas-upnp-org:service:WANCommonInterfaceConfig:1")) {
			memcpy(datas->controlurl_CIF, datas->controlurl_tmp, MINIUPNPC_URL_MAXSIZE);
			memcpy(datas->eventsuburl_CIF, datas->eventsuburl_tmp, MINIUPNPC_URL_MAXSIZE);
			memcpy(datas->scpdurl_CIF, datas->scpdurl_tmp, MINIUPNPC_URL_MAXSIZE);
			memcpy(datas->servicetype_CIF, datas->servicetype_tmp, MINIUPNPC_URL_MAXSIZE);
		} else if(0==strcmp(datas->servicetype_tmp,
				"urn:schemas-upnp-org:service:WANIPConnection:1")
				 || 0==strcmp(datas->servicetype_tmp,
				"urn:schemas-upnp-org:service:WANPPPConnection:1") ) {
			memcpy(datas->controlurl, datas->controlurl_tmp, MINIUPNPC_URL_MAXSIZE);
			memcpy(datas->eventsuburl, datas->eventsuburl_tmp, MINIUPNPC_URL_MAXSIZE);
			memcpy(datas->scpdurl, datas->scpdurl_tmp, MINIUPNPC_URL_MAXSIZE);
			memcpy(datas->servicetype, datas->servicetype_tmp, MINIUPNPC_URL_MAXSIZE);
		}
	}
}

/* Data handler :
 * copy data depending on the current element name and state */
void IGDdata(void * d, const char * data, int l)
{
	struct IGDdatas * datas = (struct IGDdatas *)d;
	char * dstmember = 0;
	/*printf("%2d %s : %.*s\n",
           datas->level, datas->cureltname, l, data);	*/
	if( !strcmp(datas->cureltname, "URLBase") )
		dstmember = datas->urlbase;
	else if( !strcmp(datas->cureltname, "serviceType") )
		dstmember = datas->servicetype_tmp;
	else if( !strcmp(datas->cureltname, "controlURL") )
		dstmember = datas->controlurl_tmp;
	else if( !strcmp(datas->cureltname, "eventSubURL") )
		dstmember = datas->eventsuburl_tmp;
	else if( !strcmp(datas->cureltname, "SCPDURL") )
		dstmember = datas->scpdurl_tmp;
/*	else if( !strcmp(datas->cureltname, "deviceType") )
		dstmember = datas->devicetype_tmp;*/
	if(dstmember)
	{
		if(l>=MINIUPNPC_URL_MAXSIZE)
			l = MINIUPNPC_URL_MAXSIZE-1;
		memcpy(dstmember, data, l);
		dstmember[l] = '\0';
	}
}

void printIGD(struct IGDdatas * d)
{
	printf("urlbase = %s\n", d->urlbase);
	printf("WAN Device (Common interface config) :\n");
	/*printf(" deviceType = %s\n", d->devicetype_CIF);*/
	printf(" serviceType = %s\n", d->servicetype_CIF);
	printf(" controlURL = %s\n", d->controlurl_CIF);
	printf(" eventSubURL = %s\n", d->eventsuburl_CIF);
	printf(" SCPDURL = %s\n", d->scpdurl_CIF);
	printf("WAN Connection Device (IP or PPP Connection):\n");
	/*printf(" deviceType = %s\n", d->devicetype);*/
	printf(" servicetype = %s\n", d->servicetype);
	printf(" controlURL = %s\n", d->controlurl);
	printf(" eventSubURL = %s\n", d->eventsuburl);
	printf(" SCPDURL = %s\n", d->scpdurl);
}


