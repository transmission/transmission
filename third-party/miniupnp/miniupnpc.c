/* $Id: miniupnpc.c,v 1.52 2008/02/18 13:28:33 nanard Exp $ */
/* Project : miniupnp
 * Author : Thomas BERNARD
 * copyright (c) 2005-2007 Thomas Bernard
 * This software is subjet to the conditions detailed in the
 * provided LICENCE file. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef WIN32
#include <winsock2.h>
#include <Ws2tcpip.h>
#include <io.h>
#define snprintf _snprintf
#define strncasecmp memicmp
#define MAXHOSTNAMELEN 64
#else
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/param.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <netdb.h>
#define closesocket close
#endif
#include "miniupnpc.h"
#include "minissdpc.h"
#include "miniwget.h"
#include "minisoap.h"
#include "minixml.h"
#include "upnpcommands.h"

/* Uncomment the following to transmit the msearch from the same port
 * as the UPnP multicast port. With WinXP this seems to result in the
 * responses to the msearch being lost, thus if things dont work then
 * comment this out. */
/* #define TX_FROM_UPNP_PORT */

#ifdef WIN32
#define PRINT_SOCKET_ERROR(x)    printf("Socket error: %s, %d\n", x, WSAGetLastError());
#else
#define PRINT_SOCKET_ERROR(x) perror(x)
#endif

/* root description parsing */
void parserootdesc(const char * buffer, int bufsize, struct IGDdatas * data)
{
	struct xmlparser parser;
	/* xmlparser object */
	parser.xmlstart = buffer;
	parser.xmlsize = bufsize;
	parser.data = data;
	parser.starteltfunc = IGDstartelt;
	parser.endeltfunc = IGDendelt;
	parser.datafunc = IGDdata;
	parser.attfunc = 0;
	parsexml(&parser);
#ifndef NDEBUG
	printIGD(data);
#endif
}

/* Content-length: nnn */
static int getcontentlenfromline(const char * p, int n)
{
	static const char contlenstr[] = "content-length";
	const char * p2 = contlenstr;
	int a = 0;
	while(*p2)
	{
		if(n==0)
			return -1;
		if(*p2 != *p && *p2 != (*p + 32))
			return -1;
		p++; p2++; n--;
	}
	if(n==0)
		return -1;
	if(*p != ':')
		return -1;
	p++; n--;
	while(*p == ' ')
	{
		if(n==0)
			return -1;
		p++; n--;
	}
	while(*p >= '0' && *p <= '9')
	{
		if(n==0)
			return -1;
		a = (a * 10) + (*p - '0');
		p++; n--;
	}
	return a;
}

static void
getContentLengthAndHeaderLength(char * p, int n,
                                int * contentlen, int * headerlen)
{
	char * line;
	int linelen;
	int r;
	line = p;
	while(line < p + n)
	{
		linelen = 0;
		while(line[linelen] != '\r' && line[linelen] != '\r')
		{
			if(line+linelen >= p+n)
				return;
			linelen++;
		}
		r = getcontentlenfromline(line, linelen);
		if(r>0)
			*contentlen = r;
		line = line + linelen + 2;
		if(line[0] == '\r' && line[1] == '\n')
		{
			*headerlen = (line - p) + 2;
			return;
		}
	}
}

/* simpleUPnPcommand :
 * not so simple !
 * return values :
 *   0 - OK
 *  -1 - error */
int simpleUPnPcommand(int s, const char * url, const char * service,
                      const char * action, struct UPNParg * args,
                      char * buffer, int * bufsize)
{
	struct sockaddr_in dest;
	char hostname[MAXHOSTNAMELEN+1];
	unsigned short port = 0;
	char * path;
	char soapact[128];
	char soapbody[2048];
	int soapbodylen;
	char * buf;
	int buffree;
    int n;
	int contentlen, headerlen;	/* for the response */
	snprintf(soapact, sizeof(soapact), "%s#%s", service, action);
	if(args==NULL)
	{
		/*soapbodylen = snprintf(soapbody, sizeof(soapbody),
						"<?xml version=\"1.0\"?>\r\n"
	    	              "<SOAP-ENV:Envelope "
						  "xmlns:SOAP-ENV=\"http://schemas.xmlsoap.org/soap/envelope/\" "
						  "SOAP-ENV:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
						  "<SOAP-ENV:Body>"
						  "<m:%s xmlns:m=\"%s\"/>"
						  "</SOAP-ENV:Body></SOAP-ENV:Envelope>"
					 	  "\r\n", action, service);*/
		soapbodylen = snprintf(soapbody, sizeof(soapbody),
						"<?xml version=\"1.0\"?>\r\n"
	    	              "<s:Envelope "
						  "xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
						  "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
						  "<s:Body>"
						  "<m:%s xmlns:m=\"%s\">"
						  "</m:%s>"
						  "</s:Body></s:Envelope>"
					 	  "\r\n", action, service, action);
	}
	else
	{
		char * p;
		const char * pe, * pv;
		soapbodylen = snprintf(soapbody, sizeof(soapbody),
						"<?xml version=\"1.0\"?>\r\n"
	    	            "<SOAP-ENV:Envelope "
						"xmlns:SOAP-ENV=\"http://schemas.xmlsoap.org/soap/envelope/\" "
						"SOAP-ENV:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
						"<SOAP-ENV:Body>"
						"<m:%s xmlns:m=\"%s\">",
						action, service);
		p = soapbody + soapbodylen;
		while(args->elt)
		{
			/* check that we are never overflowing the string... */
			if(soapbody + sizeof(soapbody) <= p + 100)
			{
				/* we keep a margin of at least 100 bytes */
				*bufsize = 0;
				return -1;
			}
			*(p++) = '<';
			pe = args->elt;
			while(*pe)
				*(p++) = *(pe++);
			*(p++) = '>';
			if((pv = args->val))
			{
				while(*pv)
					*(p++) = *(pv++);
			}
			*(p++) = '<';
			*(p++) = '/';
			pe = args->elt;
			while(*pe)
				*(p++) = *(pe++);
			*(p++) = '>';
			args++;
		}
		*(p++) = '<';
		*(p++) = '/';
		*(p++) = 'm';
		*(p++) = ':';
		pe = action;
		while(*pe)
			*(p++) = *(pe++);
		strncpy(p, "></SOAP-ENV:Body></SOAP-ENV:Envelope>\r\n",
		        soapbody + sizeof(soapbody) - p);
	}
	if(!parseURL(url, hostname, &port, &path)) return -1;
	if(s<0)
	{
		s = socket(PF_INET, SOCK_STREAM, 0);
		if(s<0)
		{
			PRINT_SOCKET_ERROR("socket");
			*bufsize = 0;
			return -1;
		}
		dest.sin_family = AF_INET;
		dest.sin_port = htons(port);
		dest.sin_addr.s_addr = inet_addr(hostname);
		if(connect(s, (struct sockaddr *)&dest, sizeof(struct sockaddr))<0)
		{
			PRINT_SOCKET_ERROR("connect");
			closesocket(s);
			*bufsize = 0;
			return -1;
		}
	}

	n = soapPostSubmit(s, path, hostname, port, soapact, soapbody);
	if(n<=0) {
#ifdef DEBUG
		printf("Error sending SOAP request\n");
#endif
		closesocket(s);
		return -1;
	}

	contentlen = -1;
	headerlen = -1;
	buf = buffer;
	buffree = *bufsize;
	*bufsize = 0;
	while ((n = ReceiveData(s, buf, buffree, 5000)) > 0) {
		buffree -= n;
		buf += n;
		*bufsize += n;
		getContentLengthAndHeaderLength(buffer, *bufsize,
		                                &contentlen, &headerlen);
#ifdef DEBUG
		printf("received n=%dbytes bufsize=%d ContLen=%d HeadLen=%d\n",
		       n, *bufsize, contentlen, headerlen);
#endif
		/* break if we received everything */
		if(contentlen > 0 && headerlen > 0 && *bufsize >= contentlen+headerlen)
			break;
	}
	
	closesocket(s);
	return 0;
}

/* parseMSEARCHReply()
 * the last 4 arguments are filled during the parsing :
 *    - location/locationsize : "location:" field of the SSDP reply packet
 *    - st/stsize : "st:" field of the SSDP reply packet.
 * The strings are NOT null terminated */
static void
parseMSEARCHReply(const char * reply, int size,
                  const char * * location, int * locationsize,
			      const char * * st, int * stsize)
{
	int a, b, i;
	i = 0;
	a = i;	/* start of the line */
	b = 0;
	while(i<size)
	{
		switch(reply[i])
		{
		case ':':
				if(b==0)
				{
					b = i; /* end of the "header" */
					/*for(j=a; j<b; j++)
					{
						putchar(reply[j]);
					}
					*/
				}
				break;
		case '\x0a':
		case '\x0d':
				if(b!=0)
				{
					/*for(j=b+1; j<i; j++)
					{
						putchar(reply[j]);
					}
					putchar('\n');*/
					do { b++; } while(reply[b]==' ');
					if(0==strncasecmp(reply+a, "location", 8))
					{
						*location = reply+b;
						*locationsize = i-b;
					}
					else if(0==strncasecmp(reply+a, "st", 2))
					{
						*st = reply+b;
						*stsize = i-b;
					}
					b = 0;
				}
				a = i+1;
				break;
		default:
				break;
		}
		i++;
	}
}

/* port upnp discover : SSDP protocol */
#define PORT (1900)
#define UPNP_MCAST_ADDR "239.255.255.250"

/* upnpDiscover() :
 * return a chained list of all devices found or NULL if
 * no devices was found.
 * It is up to the caller to free the chained list
 * delay is in millisecond (poll) */
struct UPNPDev * upnpDiscover(int delay, const char * multicastif,
                              const char * minissdpdsock)
{
	struct UPNPDev * tmp;
	struct UPNPDev * devlist = 0;
	int opt = 1;
	static const char MSearchMsgFmt[] = 
	"M-SEARCH * HTTP/1.1\r\n"
	"HOST: " UPNP_MCAST_ADDR ":" "1900" "\r\n"
	"ST: %s\r\n"
	"MAN: \"ssdp:discover\"\r\n"
	"MX: 3\r\n"
	"\r\n";
	static const char * const deviceList[] = {
		"urn:schemas-upnp-org:device:InternetGatewayDevice:1",
		"urn:schemas-upnp-org:service:WANIPConnection:1",
		"urn:schemas-upnp-org:service:WANPPPConnection:1",
		"upnp:rootdevice",
		0
	};
	int deviceIndex = 0;
	char bufr[1536];	/* reception and emission buffer */
	int sudp;
	int n;
	struct sockaddr_in sockudp_r, sockudp_w;

#ifndef WIN32
	/* first try to get infos from minissdpd ! */
	if(!minissdpdsock)
		minissdpdsock = "/var/run/minissdpd.sock";
	while(!devlist && deviceList[deviceIndex]) {
		devlist = getDevicesFromMiniSSDPD(deviceList[deviceIndex],
		                                  minissdpdsock);
		/* We return what we have found if it was not only a rootdevice */
		if(devlist && !strstr(deviceList[deviceIndex], "rootdevice"))
			return devlist;
		deviceIndex++;
	}
	deviceIndex = 0;
#endif
	/* fallback to direct discovery */
#ifdef WIN32
	sudp = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
#else
	sudp = socket(PF_INET, SOCK_DGRAM, 0);
#endif
	if(sudp < 0)
	{
		PRINT_SOCKET_ERROR("socket");
		return NULL;
	}
    /* reception */
    memset(&sockudp_r, 0, sizeof(struct sockaddr_in));
    sockudp_r.sin_family = AF_INET;
#ifdef TX_FROM_UPNP_PORT
    sockudp_r.sin_port = htons(PORT);
#endif
    sockudp_r.sin_addr.s_addr = INADDR_ANY;
    /* emission */
    memset(&sockudp_w, 0, sizeof(struct sockaddr_in));
    sockudp_w.sin_family = AF_INET;
    sockudp_w.sin_port = htons(PORT);
    sockudp_w.sin_addr.s_addr = inet_addr(UPNP_MCAST_ADDR);

#ifdef WIN32
	if (setsockopt(sudp, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof (opt)) < 0)
#else
	if (setsockopt(sudp, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof (opt)) < 0)
#endif
	{
		PRINT_SOCKET_ERROR("setsockopt");
		return NULL;
	}

	if(multicastif)
	{
		struct in_addr mc_if;
		mc_if.s_addr = inet_addr(multicastif);
    	sockudp_r.sin_addr.s_addr = mc_if.s_addr;
		if(setsockopt(sudp, IPPROTO_IP, IP_MULTICAST_IF, (const char *)&mc_if, sizeof(mc_if)) < 0)
		{
			PRINT_SOCKET_ERROR("setsockopt");
		}
	}

	/* Avant d'envoyer le paquet on bind pour recevoir la reponse */
    if (bind(sudp, (struct sockaddr *)&sockudp_r, sizeof(struct sockaddr_in)) != 0)
	{
        PRINT_SOCKET_ERROR("bind");
		closesocket(sudp);
		return NULL;
    }

	/* receiving SSDP response packet */
	for(n = 0;;)
	{
	if(n == 0)
	{
		/* sending the SSDP M-SEARCH packet */
		n = snprintf(bufr, sizeof(bufr),
		             MSearchMsgFmt, deviceList[deviceIndex++]);
		/*printf("Sending %s", bufr);*/
		n = sendto(sudp, bufr, n, 0,
		           (struct sockaddr *)&sockudp_w, sizeof(struct sockaddr_in));
		if (n < 0) {
			PRINT_SOCKET_ERROR("sendto");
			closesocket(sudp);
			return devlist;
		}
	}
	/* Waiting for SSDP REPLY packet to M-SEARCH */
	n = ReceiveData(sudp, bufr, sizeof(bufr), delay);
	if (n < 0) {
		/* error */
		closesocket(sudp);
		return devlist;
	} else if (n == 0) {
		/* no data or Time Out */
		if (devlist || (deviceList[deviceIndex] == 0)) {
			/* no more device type to look for... */
			closesocket(sudp);
			return devlist;
		}
	} else {
		const char * descURL=NULL;
		int urlsize=0;
		const char * st=NULL;
		int stsize=0;
        /*printf("%d byte(s) :\n%s\n", n, bufr);*/ /* affichage du message */
		parseMSEARCHReply(bufr, n, &descURL, &urlsize, &st, &stsize);
		if(st&&descURL)
		{
			/*printf("M-SEARCH Reply:\nST: %.*s\nLocation: %.*s\n",
			       stsize, st, urlsize, descURL); */
			tmp = (struct UPNPDev *)malloc(sizeof(struct UPNPDev)+urlsize+stsize);
			tmp->pNext = devlist;
			tmp->descURL = tmp->buffer;
			tmp->st = tmp->buffer + 1 + urlsize;
			memcpy(tmp->buffer, descURL, urlsize);
			tmp->buffer[urlsize] = '\0';
			memcpy(tmp->buffer + urlsize + 1, st, stsize);
			tmp->buffer[urlsize+1+stsize] = '\0';
			devlist = tmp;
		}
	}
	}
}

/* freeUPNPDevlist() should be used to
 * free the chained list returned by upnpDiscover() */
void freeUPNPDevlist(struct UPNPDev * devlist)
{
	struct UPNPDev * next;
	while(devlist)
	{
		next = devlist->pNext;
		free(devlist);
		devlist = next;
	}
}

static void
url_cpy_or_cat(char * dst, const char * src, int n)
{
	if(  (src[0] == 'h')
	   &&(src[1] == 't')
	   &&(src[2] == 't')
	   &&(src[3] == 'p')
	   &&(src[4] == ':')
	   &&(src[5] == '/')
	   &&(src[6] == '/'))
	{
		strncpy(dst, src, n);
	}
	else
	{
		int l = strlen(dst);
		if(src[0] != '/')
			dst[l++] = '/';
		if(l<=n)
			strncpy(dst + l, src, n - l);
	}
}

/* Prepare the Urls for usage...
 */
void GetUPNPUrls(struct UPNPUrls * urls, struct IGDdatas * data,
                 const char * descURL)
{
	char * p;
	int n1, n2, n3;
	n1 = strlen(data->urlbase);
	if(n1==0)
		n1 = strlen(descURL);
	n1 += 2;	/* 1 byte more for Null terminator, 1 byte for '/' if needed */
	n2 = n1; n3 = n1;
	n1 += strlen(data->scpdurl);
	n2 += strlen(data->controlurl);
	n3 += strlen(data->controlurl_CIF);

	urls->ipcondescURL = (char *)malloc(n1);
	urls->controlURL = (char *)malloc(n2);
	urls->controlURL_CIF = (char *)malloc(n3);
	/* maintenant on chope la desc du WANIPConnection */
	if(data->urlbase[0] != '\0')
		strncpy(urls->ipcondescURL, data->urlbase, n1);
	else
		strncpy(urls->ipcondescURL, descURL, n1);
	p = strchr(urls->ipcondescURL+7, '/');
	if(p) p[0] = '\0';
	strncpy(urls->controlURL, urls->ipcondescURL, n2);
	strncpy(urls->controlURL_CIF, urls->ipcondescURL, n3);
	
	url_cpy_or_cat(urls->ipcondescURL, data->scpdurl, n1);

	url_cpy_or_cat(urls->controlURL, data->controlurl, n2);

	url_cpy_or_cat(urls->controlURL_CIF, data->controlurl_CIF, n3);

#ifdef DEBUG
	printf("urls->ipcondescURL='%s' %d n1=%d\n", urls->ipcondescURL,
	       strlen(urls->ipcondescURL), n1);
	printf("urls->controlURL='%s' %d n2=%d\n", urls->controlURL,
	       strlen(urls->controlURL), n2);
	printf("urls->controlURL_CIF='%s' %d n3=%d\n", urls->controlURL_CIF,
	       strlen(urls->controlURL_CIF), n3);
#endif
}

void
FreeUPNPUrls(struct UPNPUrls * urls)
{
	if(!urls)
		return;
	free(urls->controlURL);
	urls->controlURL = 0;
	free(urls->ipcondescURL);
	urls->ipcondescURL = 0;
	free(urls->controlURL_CIF);
	urls->controlURL_CIF = 0;
}


int ReceiveData(int socket, char * data, int length, int timeout)
{
    int n;
#ifndef WIN32
    struct pollfd fds[1]; /* for the poll */
    fds[0].fd = socket;
    fds[0].events = POLLIN;
    n = poll(fds, 1, timeout);
    if(n < 0)
    {
        PRINT_SOCKET_ERROR("poll");
        return -1;
    }
    else if(n == 0)
    {
        return 0;
    }
#else
    fd_set socketSet;
    TIMEVAL timeval;
    FD_ZERO(&socketSet);
    FD_SET(socket, &socketSet);
    timeval.tv_sec = timeout / 1000;
    timeval.tv_usec = (timeout % 1000) * 1000;
    /*n = select(0, &socketSet, NULL, NULL, &timeval);*/
    n = select(FD_SETSIZE, &socketSet, NULL, NULL, &timeval);
    if(n < 0)
    {
        PRINT_SOCKET_ERROR("select");
        return -1;
    }
    else if(n == 0)
    {
        return 0;
    }    
#endif
	n = recv(socket, data, length, 0);
	if(n<0)
	{
		PRINT_SOCKET_ERROR("recv");
	}
	return n;
}

int
UPNPIGD_IsConnected(struct UPNPUrls * urls, struct IGDdatas * data)
{
	char status[64];
	unsigned int uptime;
	status[0] = '\0';
	UPNP_GetStatusInfo(urls->controlURL, data->servicetype,
	                   status, &uptime, NULL);
	if(0 == strcmp("Connected", status))
	{
		return 1;
	}
	else
		return 0;
}


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
int
UPNP_GetValidIGD(struct UPNPDev * devlist,
                 struct UPNPUrls * urls,
				 struct IGDdatas * data,
				 char * lanaddr, int lanaddrlen)
{
	char * descXML;
	int descXMLsize = 0;
	struct UPNPDev * dev;
	int ndev = 0;
	int state; /* state 1 : IGD connected. State 2 : IGD. State 3 : anything */
	if(!devlist)
	{
#ifdef DEBUG
		printf("Empty devlist\n");
#endif
		return 0;
	}
	for(state = 1; state <= 3; state++)
	{
		for(dev = devlist; dev; dev = dev->pNext)
		{
			/* we should choose an internet gateway device.
		 	* with st == urn:schemas-upnp-org:device:InternetGatewayDevice:1 */
			descXML = miniwget_getaddr(dev->descURL, &descXMLsize,
			   	                        lanaddr, lanaddrlen);
			if(descXML)
			{
				ndev++;
				memset(data, 0, sizeof(struct IGDdatas));
				memset(urls, 0, sizeof(struct UPNPUrls));
				parserootdesc(descXML, descXMLsize, data);
				free(descXML);
				descXML = NULL;
				if(0==strcmp(data->servicetype_CIF,
				   "urn:schemas-upnp-org:service:WANCommonInterfaceConfig:1")
				   || state >= 3 )
				{
				  GetUPNPUrls(urls, data, dev->descURL);

#ifdef DEBUG
				  printf("UPNPIGD_IsConnected(%s) = %d\n",
				     urls->controlURL,
			         UPNPIGD_IsConnected(urls, data));
#endif
				  if((state >= 2) || UPNPIGD_IsConnected(urls, data))
					return state;
				  FreeUPNPUrls(urls);
				}
				memset(data, 0, sizeof(struct IGDdatas));
			}
#ifdef DEBUG
			else
			{
				printf("error getting XML description %s\n", dev->descURL);
			}
#endif
		}
	}
	return 0;
}

/* UPNP_GetIGDFromUrl()
 * Used when skipping the discovery process.
 * return value :
 *   0 - Not ok
 *   1 - OK */
int
UPNP_GetIGDFromUrl(const char * rootdescurl,
                   struct UPNPUrls * urls,
                   struct IGDdatas * data,
                   char * lanaddr, int lanaddrlen)
{
	char * descXML;
	int descXMLsize = 0;
	descXML = miniwget_getaddr(rootdescurl, &descXMLsize,
	   	                       lanaddr, lanaddrlen);
	if(descXML) {
		memset(data, 0, sizeof(struct IGDdatas));
		memset(urls, 0, sizeof(struct UPNPUrls));
		parserootdesc(descXML, descXMLsize, data);
		free(descXML);
		descXML = NULL;
		GetUPNPUrls(urls, data, rootdescurl);
		return 1;
	} else {
		return 0;
	}
}

