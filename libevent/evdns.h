/*
 * Copyright (c) 2006 Niels Provos <provos@citi.umich.edu>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * The original DNS code is due to Adam Langley with heavy
 * modifications by Nick Mathewson.  Adam put his DNS software in the
 * public domain.  You can find his original copyright below.  Please,
 * aware that the code as part of libevent is governed by the 3-clause
 * BSD license above.
 *
 * This software is Public Domain. To view a copy of the public domain dedication,
 * visit http://creativecommons.org/licenses/publicdomain/ or send a letter to
 * Creative Commons, 559 Nathan Abbott Way, Stanford, California 94305, USA.
 *
 * I ask and expect, but do not require, that all derivative works contain an
 * attribution similar to:
 * 	Parts developed by Adam Langley <agl@imperialviolet.org>
 *
 * You may wish to replace the word "Parts" with something else depending on
 * the amount of original code.
 *
 * (Derivative works does not include programs which link against, run or include
 * the source verbatim in their source distributions)
 */

/*
 * Welcome, gentle reader
 *
 * Async DNS lookups are really a whole lot harder than they should be,
 * mostly stemming from the fact that the libc resolver has never been
 * very good at them. Before you use this library you should see if libc
 * can do the job for you with the modern async call getaddrinfo_a
 * (see http://www.imperialviolet.org/page25.html#e498). Otherwise,
 * please continue.
 *
 * This code is based on libevent and you must call event_init before
 * any of the APIs in this file. You must also seed the OpenSSL random
 * source if you are using OpenSSL for ids (see below).
 *
 * This library is designed to be included and shipped with your source
 * code. You statically link with it. You should also test for the
 * existence of strtok_r and define HAVE_STRTOK_R if you have it.
 *
 * The DNS protocol requires a good source of id numbers and these
 * numbers should be unpredictable for spoofing reasons. There are
 * three methods for generating them here and you must define exactly
 * one of them. In increasing order of preference:
 *
 * DNS_USE_GETTIMEOFDAY_FOR_ID:
 *   Using the bottom 16 bits of the usec result from gettimeofday. This
 *   is a pretty poor solution but should work anywhere.
 * DNS_USE_CPU_CLOCK_FOR_ID:
 *   Using the bottom 16 bits of the nsec result from the CPU's time
 *   counter. This is better, but may not work everywhere. Requires
 *   POSIX realtime support and you'll need to link against -lrt on
 *   glibc systems at least.
 * DNS_USE_OPENSSL_FOR_ID:
 *   Uses the OpenSSL RAND_bytes call to generate the data. You must
 *   have seeded the pool before making any calls to this library.
 *
 * The library keeps track of the state of nameservers and will avoid
 * them when they go down. Otherwise it will round robin between them.
 *
 * Quick start guide:
 *   #include "evdns.h"
 *   void callback(int result, char type, int count, int ttl,
 *		 void *addresses, void *arg);
 *   evdns_resolv_conf_parse(DNS_OPTIONS_ALL, "/etc/resolv.conf");
 *   evdns_resolve("www.hostname.com", 0, callback, NULL);
 *
 * When the lookup is complete the callback function is called. The
 * first argument will be one of the DNS_ERR_* defines in evdns.h.
 * Hopefully it will be DNS_ERR_NONE, in which case type will be
 * DNS_IPv4_A, count will be the number of IP addresses, ttl is the time
 * which the data can be cached for (in seconds), addresses will point
 * to an array of uint32_t's and arg will be whatever you passed to
 * evdns_resolve.
 *
 * Searching:
 *
 * In order for this library to be a good replacement for glibc's resolver it
 * supports searching. This involves setting a list of default domains, in
 * which names will be queried for. The number of dots in the query name
 * determines the order in which this list is used.
 *
 * Searching appears to be a single lookup from the point of view of the API,
 * although many DNS queries may be generated from a single call to
 * evdns_resolve. Searching can also drastically slow down the resolution
 * of names.
 *
 * To disable searching:
 *   1. Never set it up. If you never call evdns_resolv_conf_parse or
 *   evdns_search_add then no searching will occur.
 *
 *   2. If you do call evdns_resolv_conf_parse then don't pass
 *   DNS_OPTION_SEARCH (or DNS_OPTIONS_ALL, which implies it).
 *
 *   3. When calling evdns_resolve, pass the DNS_QUERY_NO_SEARCH flag.
 *
 * The order of searches depends on the number of dots in the name. If the
 * number is greater than the ndots setting then the names is first tried
 * globally. Otherwise each search domain is appended in turn.
 *
 * The ndots setting can either be set from a resolv.conf, or by calling
 * evdns_search_ndots_set.
 *
 * For example, with ndots set to 1 (the default) and a search domain list of
 * ["myhome.net"]:
 *  Query: www
 *  Order: www.myhome.net, www.
 *
 *  Query: www.abc
 *  Order: www.abc., www.abc.myhome.net
 *
 * API reference:
 *
 * int evdns_nameserver_add(unsigned long int address)
 *   Add a nameserver. The address should be an IP address in
 *   network byte order. The type of address is chosen so that
 *   it matches in_addr.s_addr.
 *   Returns non-zero on error.
 *
 * int evdns_nameserver_ip_add(const char *ip_as_string)
 *   This wraps the above function by parsing a string as an IP
 *   address and adds it as a nameserver.
 *   Returns non-zero on error
 *
 * int evdns_resolve(const char *name, int flags,
 *		      evdns_callback_type callback,
 *		      void *ptr)
 *   Resolve a name. The name parameter should be a DNS name.
 *   The flags parameter should be 0, or DNS_QUERY_NO_SEARCH
 *   which disables searching for this query. (see defn of
 *   searching above).
 *
 *   The callback argument is a function which is called when
 *   this query completes and ptr is an argument which is passed
 *   to that callback function.
 *
 *   Returns non-zero on error
 *
 * void evdns_search_clear()
 *   Clears the list of search domains
 *
 * void evdns_search_add(const char *domain)
 *   Add a domain to the list of search domains
 *
 * void evdns_search_ndots_set(int ndots)
 *   Set the number of dots which, when found in a name, causes
 *   the first query to be without any search domain.
 *
 * int evdns_count_nameservers(void)
 *   Return the number of configured nameservers (not necessarily the
 *   number of running nameservers).  This is useful for double-checking
 *   whether our calls to the various nameserver configuration functions
 *   have been successful.
 *
 * int evdns_clear_nameservers_and_suspend(void)
 *   Remove all currently configured nameservers, and suspend all pending
 *   resolves.  Resolves will not necessarily be re-attempted until
 *   evdns_resume() is called.
 *
 * int evdns_resume(void)
 *   Re-attempt resolves left in limbo after an earlier call to
 *   evdns_clear_nameservers_and_suspend().
 *
 * int evdns_config_windows_nameservers(void)
 *   Attempt to configure a set of nameservers based on platform settings on
 *   a win32 host.  Preferentially tries to use GetNetworkParams; if that fails,
 *   looks in the registry.  Returns 0 on success, nonzero on failure.
 *
 * int evdns_resolv_conf_parse(int flags, const char *filename)
 *   Parse a resolv.conf like file from the given filename.
 *
 *   See the man page for resolv.conf for the format of this file.
 *   The flags argument determines what information is parsed from
 *   this file:
 *     DNS_OPTION_SEARCH - domain, search and ndots options
 *     DNS_OPTION_NAMESERVERS - nameserver lines
 *     DNS_OPTION_MISC - timeout and attempts options
 *     DNS_OPTIONS_ALL - all of the above
 *   The following directives are not parsed from the file:
 *     sortlist, rotate, no-check-names, inet6, debug
 *
 *   Returns non-zero on error:
 *    0 no errors
 *    1 failed to open file
 *    2 failed to stat file
 *    3 file too large
 *    4 out of memory
 *    5 short read from file
 *    6 no nameservers in file
 *
 * Internals:
 *
 * Requests are kept in two queues. The first is the inflight queue. In
 * this queue requests have an allocated transaction id and nameserver.
 * They will soon be transmitted if they haven't already been.
 *
 * The second is the waiting queue. The size of the inflight ring is
 * limited and all other requests wait in waiting queue for space. This
 * bounds the number of concurrent requests so that we don't flood the
 * nameserver. Several algorithms require a full walk of the inflight
 * queue and so bounding its size keeps thing going nicely under huge
 * (many thousands of requests) loads.
 *
 * If a nameserver loses too many requests it is considered down and we
 * try not to use it. After a while we send a probe to that nameserver
 * (a lookup for google.com) and, if it replies, we consider it working
 * again. If the nameserver fails a probe we wait longer to try again
 * with the next probe.
 */

#ifndef EVENTDNS_H
#define EVENTDNS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Error codes 0-5 are as described in RFC 1035. */
#define DNS_ERR_NONE 0
/* The name server was unable to interpret the query */
#define DNS_ERR_FORMAT 1
/* The name server was unable to process this query due to a problem with the
 * name server */
#define DNS_ERR_SERVERFAILED 2
/* The domain name does not exist */
#define DNS_ERR_NOTEXIST 3
/* The name server does not support the requested kind of query */
#define DNS_ERR_NOTIMPL 4
/* The name server refuses to reform the specified operation for policy
 * reasons */
#define DNS_ERR_REFUSED 5
/* The reply was truncated or ill-formated */
#define DNS_ERR_TRUNCATED 65
/* An unknown error occurred */
#define DNS_ERR_UNKNOWN 66
/* Communication with the server timed out */
#define DNS_ERR_TIMEOUT 67
/* The request was canceled because the DNS subsystem was shut down. */
#define DNS_ERR_SHUTDOWN 68

#define DNS_IPv4_A 1
#define DNS_PTR 2
#define DNS_IPv6_AAAA 3

#define DNS_QUERY_NO_SEARCH 1

#define DNS_OPTION_SEARCH 1
#define DNS_OPTION_NAMESERVERS 2
#define DNS_OPTION_MISC 4
#define DNS_OPTIONS_ALL 7

/* 
 * The callback that contains the results from a lookup.
 * - type is either DNS_IPv4_A or DNS_PTR or DNS_IPv6_AAAA
 * - count contains the number of addresses of form type
 * - ttl is the number of seconds the resolution may be cached for.
 * - addresses needs to be cast according to type
 */
typedef void (*evdns_callback_type) (int result, char type, int count, int ttl, void *addresses, void *arg);

int evdns_init(void);
void evdns_shutdown(int fail_requests);
const char *evdns_err_to_string(int err);
int evdns_nameserver_add(unsigned long int address);
int evdns_count_nameservers(void);
int evdns_clear_nameservers_and_suspend(void);
int evdns_resume(void);
int evdns_nameserver_ip_add(const char *ip_as_string);
int evdns_resolve_ipv4(const char *name, int flags, evdns_callback_type callback, void *ptr);
int evdns_resolve_ipv6(const char *name, int flags, evdns_callback_type callback, void *ptr);
struct in_addr;
struct in6_addr;
int evdns_resolve_reverse(struct in_addr *in, int flags, evdns_callback_type callback, void *ptr);
int evdns_resolve_reverse_ipv6(struct in6_addr *in, int flags, evdns_callback_type callback, void *ptr);
int evdns_set_option(const char *option, const char *val, int flags);
int evdns_resolv_conf_parse(int flags, const char *);
#ifdef MS_WINDOWS
int evdns_config_windows_nameservers(void);
#endif
void evdns_search_clear(void);
void evdns_search_add(const char *domain);
void evdns_search_ndots_set(const int ndots);

typedef void (*evdns_debug_log_fn_type)(int is_warning, const char *msg);
void evdns_set_log_fn(evdns_debug_log_fn_type fn);

#define DNS_NO_SEARCH 1

#ifdef __cplusplus
}
#endif

/*
 * Structures and functions used to implement a DNS server.
 */

struct evdns_server_request {
	int flags;
	int nquestions;
	struct evdns_server_question **questions;
};
struct evdns_server_question {
	int type;
	int class;
	char name[1];
};
typedef void (*evdns_request_callback_fn_type)(struct evdns_server_request *, void *);
#define EVDNS_ANSWER_SECTION 0
#define EVDNS_AUTHORITY_SECTION 1
#define EVDNS_ADDITIONAL_SECTION 2

#define EVDNS_TYPE_A	   1
#define EVDNS_TYPE_NS	   2
#define EVDNS_TYPE_CNAME   5
#define EVDNS_TYPE_SOA	   6
#define EVDNS_TYPE_PTR	  12
#define EVDNS_TYPE_MX	  15
#define EVDNS_TYPE_TXT	  16
#define EVDNS_TYPE_AAAA	  28

#define EVDNS_QTYPE_AXFR 252
#define EVDNS_QTYPE_ALL	 255

#define EVDNS_CLASS_INET   1

struct evdns_server_port *evdns_add_server_port(int socket, int is_tcp, evdns_request_callback_fn_type callback, void *user_data);
void evdns_close_server_port(struct evdns_server_port *port);

int evdns_server_request_add_reply(struct evdns_server_request *req, int section, const char *name, int type, int class, int ttl, int datalen, int is_name, const char *data);
int evdns_server_request_add_a_reply(struct evdns_server_request *req, const char *name, int n, void *addrs, int ttl);
int evdns_server_request_add_aaaa_reply(struct evdns_server_request *req, const char *name, int n, void *addrs, int ttl);
int evdns_server_request_add_ptr_reply(struct evdns_server_request *req, struct in_addr *in, const char *inaddr_name, const char *hostname, int ttl);
int evdns_server_request_add_cname_reply(struct evdns_server_request *req, const char *name, const char *cname, int ttl);

int evdns_server_request_respond(struct evdns_server_request *req, int err);
int evdns_server_request_drop(struct evdns_server_request *req);
struct sockaddr;
int evdns_server_request_get_requesting_addr(struct evdns_server_request *_req, struct sockaddr *sa, int addr_len);

#endif  /* !EVENTDNS_H */
