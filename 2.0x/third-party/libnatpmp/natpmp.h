/* $Id: natpmp.h,v 1.12 2009/12/19 12:00:00 nanard Exp $ */
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
#ifndef __NATPMP_H__
#define __NATPMP_H__

/* NAT-PMP Port as defined by the NAT-PMP draft */
#define NATPMP_PORT (5351)

#include <time.h>
#if !defined(_MSC_VER)
#include <sys/time.h>
#endif
#ifdef WIN32
#include <winsock2.h>
#if !defined(_MSC_VER)
#include <stdint.h>
#else
typedef unsigned long uint32_t;
typedef unsigned short uint16_t;
#endif
#define in_addr_t uint32_t
#include "declspec.h"
#else
#define LIBSPEC
#include <netinet/in.h>
#endif

typedef struct {
	int s;	/* socket */
	in_addr_t gateway;	/* default gateway (IPv4) */
	int has_pending_request;
	unsigned char pending_request[12];
	int pending_request_len;
	int try_number;
	struct timeval retry_time;
} natpmp_t;

typedef struct {
	uint16_t type;	/* NATPMP_RESPTYPE_* */
	uint16_t resultcode;	/* NAT-PMP response code */
	uint32_t epoch;	/* Seconds since start of epoch */
	union {
		struct {
			//in_addr_t addr;
			struct in_addr addr;
		} publicaddress;
		struct {
			uint16_t privateport;
			uint16_t mappedpublicport;
			uint32_t lifetime;
		} newportmapping;
	} pnu;
} natpmpresp_t;

/* possible values for type field of natpmpresp_t */
#define NATPMP_RESPTYPE_PUBLICADDRESS (0)
#define NATPMP_RESPTYPE_UDPPORTMAPPING (1)
#define NATPMP_RESPTYPE_TCPPORTMAPPING (2)

/* Values to pass to sendnewportmappingrequest() */
#define NATPMP_PROTOCOL_UDP (1)
#define NATPMP_PROTOCOL_TCP (2)

/* return values */
/* NATPMP_ERR_INVALIDARGS : invalid arguments passed to the function */
#define NATPMP_ERR_INVALIDARGS (-1)
/* NATPMP_ERR_SOCKETERROR : socket() failed. check errno for details */
#define NATPMP_ERR_SOCKETERROR (-2)
/* NATPMP_ERR_CANNOTGETGATEWAY : can't get default gateway IP */
#define NATPMP_ERR_CANNOTGETGATEWAY (-3)
/* NATPMP_ERR_CLOSEERR : close() failed. check errno for details */
#define NATPMP_ERR_CLOSEERR (-4)
/* NATPMP_ERR_RECVFROM : recvfrom() failed. check errno for details */
#define NATPMP_ERR_RECVFROM (-5)
/* NATPMP_ERR_NOPENDINGREQ : readnatpmpresponseorretry() called while
 * no NAT-PMP request was pending */
#define NATPMP_ERR_NOPENDINGREQ (-6)
/* NATPMP_ERR_NOGATEWAYSUPPORT : the gateway does not support NAT-PMP */
#define NATPMP_ERR_NOGATEWAYSUPPORT (-7)
/* NATPMP_ERR_CONNECTERR : connect() failed. check errno for details */
#define NATPMP_ERR_CONNECTERR (-8)
/* NATPMP_ERR_WRONGPACKETSOURCE : packet not received from the network gateway */
#define NATPMP_ERR_WRONGPACKETSOURCE (-9)
/* NATPMP_ERR_SENDERR : send() failed. check errno for details */
#define NATPMP_ERR_SENDERR (-10)
/* NATPMP_ERR_FCNTLERROR : fcntl() failed. check errno for details */
#define NATPMP_ERR_FCNTLERROR (-11)
/* NATPMP_ERR_GETTIMEOFDAYERR : gettimeofday() failed. check errno for details */
#define NATPMP_ERR_GETTIMEOFDAYERR (-12)

/* */
#define NATPMP_ERR_UNSUPPORTEDVERSION (-14)
#define NATPMP_ERR_UNSUPPORTEDOPCODE (-15)

/* Errors from the server : */
#define NATPMP_ERR_UNDEFINEDERROR (-49)
#define NATPMP_ERR_NOTAUTHORIZED (-51)
#define NATPMP_ERR_NETWORKFAILURE (-52)
#define NATPMP_ERR_OUTOFRESOURCES (-53)

/* NATPMP_TRYAGAIN : no data available for the moment. try again later */
#define NATPMP_TRYAGAIN (-100)

/* initnatpmp()
 * initialize a natpmp_t object
 * Return values :
 * 0 = OK
 * NATPMP_ERR_INVALIDARGS
 * NATPMP_ERR_SOCKETERROR
 * NATPMP_ERR_FCNTLERROR
 * NATPMP_ERR_CANNOTGETGATEWAY
 * NATPMP_ERR_CONNECTERR */
LIBSPEC int initnatpmp(natpmp_t * p);

/* closenatpmp()
 * close resources associated with a natpmp_t object
 * Return values :
 * 0 = OK
 * NATPMP_ERR_INVALIDARGS
 * NATPMP_ERR_CLOSEERR */
LIBSPEC int closenatpmp(natpmp_t * p);

/* sendpublicaddressrequest()
 * send a public address NAT-PMP request to the network gateway
 * Return values :
 * 2 = OK (size of the request)
 * NATPMP_ERR_INVALIDARGS
 * NATPMP_ERR_SENDERR */
LIBSPEC int sendpublicaddressrequest(natpmp_t * p);

/* sendnewportmappingrequest()
 * send a new port mapping NAT-PMP request to the network gateway
 * Arguments :
 * protocol is either NATPMP_PROTOCOL_TCP or NATPMP_PROTOCOL_UDP,
 * lifetime is in seconds.
 * To remove a port mapping, set lifetime to zero.
 * To remove all port mappings to the host, set lifetime and both ports
 * to zero.
 * Return values :
 * 12 = OK (size of the request)
 * NATPMP_ERR_INVALIDARGS
 * NATPMP_ERR_SENDERR */
LIBSPEC int sendnewportmappingrequest(natpmp_t * p, int protocol,
                              uint16_t privateport, uint16_t publicport,
							  uint32_t lifetime);

/* getnatpmprequesttimeout()
 * fills the timeval structure with the timeout duration of the
 * currently pending NAT-PMP request.
 * Return values :
 * 0 = OK
 * NATPMP_ERR_INVALIDARGS
 * NATPMP_ERR_GETTIMEOFDAYERR
 * NATPMP_ERR_NOPENDINGREQ */
LIBSPEC int getnatpmprequesttimeout(natpmp_t * p, struct timeval * timeout);

/* readnatpmpresponseorretry()
 * fills the natpmpresp_t structure if possible
 * Return values :
 * 0 = OK
 * NATPMP_TRYAGAIN
 * NATPMP_ERR_INVALIDARGS
 * NATPMP_ERR_NOPENDINGREQ
 * NATPMP_ERR_NOGATEWAYSUPPORT
 * NATPMP_ERR_RECVFROM
 * NATPMP_ERR_WRONGPACKETSOURCE
 * NATPMP_ERR_UNSUPPORTEDVERSION
 * NATPMP_ERR_UNSUPPORTEDOPCODE
 * NATPMP_ERR_NOTAUTHORIZED
 * NATPMP_ERR_NETWORKFAILURE
 * NATPMP_ERR_OUTOFRESOURCES
 * NATPMP_ERR_UNSUPPORTEDOPCODE
 * NATPMP_ERR_UNDEFINEDERROR */
LIBSPEC int readnatpmpresponseorretry(natpmp_t * p, natpmpresp_t * response);

#ifdef ENABLE_STRNATPMPERR
LIBSPEC const char * strnatpmperr(int t);
#endif

#endif
