/* $Id: igd_desc_parse.c,v 1.7 2006/11/19 22:32:33 nanard Exp $ */
/* Project : miniupnp
 * http://miniupnp.free.fr/
 * Author : Thomas Bernard
 * Copyright (c) 2005 Thomas Bernard
 * This software is subject to the conditions detailed in the
 * LICENCE file provided in this distribution.
 * */
#include "igd_desc_parse.h"
#include <stdio.h>
#include <string.h>

/* Start element handler :
 * update nesting level counter and copy element name */
void IGDstartelt(void * d, const char * name, int l)
{
	struct IGDdatas * datas = (struct IGDdatas *)d;
	memcpy( datas->cureltname, name, l);
	datas->cureltname[l] = '\0';
	datas->level++;
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
		/*datas->state++; */
		/*
		if( datas->state < 1
			&& !strcmp(datas->servicetype,
				//	"urn:schemas-upnp-org:service:WANIPConnection:1") )
				"urn:schemas-upnp-org:service:WANCommonInterfaceConfig:1"))
			datas->state ++;
		*/
		if(0==strcmp(datas->servicetype_CIF,
				"urn:schemas-upnp-org:service:WANCommonInterfaceConfig:1"))
			datas->state = 2;
		if(0==strcmp(datas->servicetype,
				"urn:schemas-upnp-org:service:WANIPConnection:1") )
			datas->state = 3;
/*		if(0==strcmp(datas->servicetype,
				"urn:schemas-upnp-org:service:WANPPPConnection:1") )
			datas->state = 4; */
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
	else if(datas->state<=1)
	{
		if( !strcmp(datas->cureltname, "serviceType") )
			dstmember = datas->servicetype_CIF;
		else if( !strcmp(datas->cureltname, "controlURL") )
			dstmember = datas->controlurl_CIF;
		else if( !strcmp(datas->cureltname, "eventSubURL") )
			dstmember = datas->eventsuburl_CIF;
		else if( !strcmp(datas->cureltname, "SCPDURL") )
			dstmember = datas->scpdurl_CIF;
		else if( !strcmp(datas->cureltname, "deviceType") )
			dstmember = datas->devicetype_CIF;
	}
	else if(datas->state==2)
	{
		if( !strcmp(datas->cureltname, "serviceType") )
			dstmember = datas->servicetype;
		else if( !strcmp(datas->cureltname, "controlURL") )
			dstmember = datas->controlurl;
		else if( !strcmp(datas->cureltname, "eventSubURL") )
			dstmember = datas->eventsuburl;
		else if( !strcmp(datas->cureltname, "SCPDURL") )
			dstmember = datas->scpdurl;
		else if( !strcmp(datas->cureltname, "deviceType") )
			dstmember = datas->devicetype;
	}
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
	printf(" deviceType = %s\n", d->devicetype_CIF);
	printf(" serviceType = %s\n", d->servicetype_CIF);
	printf(" controlURL = %s\n", d->controlurl_CIF);
	printf(" eventSubURL = %s\n", d->eventsuburl_CIF);
	printf(" SCPDURL = %s\n", d->scpdurl_CIF);
	printf("WAN Connection Device :\n");
	printf(" deviceType = %s\n", d->devicetype);
	printf(" servicetype = %s\n", d->servicetype);
	printf(" controlURL = %s\n", d->controlurl);
	printf(" eventSubURL = %s\n", d->eventsuburl);
	printf(" SCPDURL = %s\n", d->scpdurl);
}


