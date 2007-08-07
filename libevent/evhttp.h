/*
 * Copyright (c) 2000-2004 Niels Provos <provos@citi.umich.edu>
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
#ifndef _EVHTTP_H_
#define _EVHTTP_H_

#ifdef __cplusplus
extern "C" {
#endif

#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#undef WIN32_LEAN_AND_MEAN
#endif

/*
 * Basic support for HTTP serving.
 *
 * As libevent is a library for dealing with event notification and most
 * interesting applications are networked today, I have often found the
 * need to write HTTP code.  The following prototypes and definitions provide
 * an application with a minimal interface for making HTTP requests and for
 * creating a very simple HTTP server.
 */

/* Response codes */
#define HTTP_OK			200
#define HTTP_NOCONTENT		204
#define HTTP_MOVEPERM		301
#define HTTP_MOVETEMP		302
#define HTTP_NOTMODIFIED	304
#define HTTP_BADREQUEST		400
#define HTTP_NOTFOUND		404
#define HTTP_SERVUNAVAIL	503

struct evhttp;
struct evhttp_request;
struct evkeyvalq;

/* Start an HTTP server on the specified address and port */
struct evhttp *evhttp_start(const char *address, u_short port);

/*
 * Free the previously create HTTP server.  Works only if no requests are
 * currently being served.
 */
void evhttp_free(struct evhttp* http);

/* Set a callback for a specified URI */
void evhttp_set_cb(struct evhttp *, const char *,
    void (*)(struct evhttp_request *, void *), void *);

/* Removes the callback for a specified URI */
int evhttp_del_cb(struct evhttp *, const char *);

/* Set a callback for all requests that are not caught by specific callbacks */
void evhttp_set_gencb(struct evhttp *,
    void (*)(struct evhttp_request *, void *), void *);

void evhttp_set_timeout(struct evhttp *, int timeout_in_secs);

/* Request/Response functionality */

void evhttp_send_error(struct evhttp_request *, int, const char *);
void evhttp_send_reply(struct evhttp_request *, int, const char *,
    struct evbuffer *);

/* Low-level response interface, for streaming/chunked replies */
void evhttp_send_reply_start(struct evhttp_request *, int, const char *);
void evhttp_send_reply_chunk(struct evhttp_request *, struct evbuffer *);
void evhttp_send_reply_end(struct evhttp_request *);
	
/* Interfaces for making requests */
enum evhttp_cmd_type { EVHTTP_REQ_GET, EVHTTP_REQ_POST, EVHTTP_REQ_HEAD };

enum evhttp_request_kind { EVHTTP_REQUEST, EVHTTP_RESPONSE };

/* 
 * the request structure that a server receives.
 * WARNING: expect this structure to change.  I will try to provide
 * reasonable accessors.
 */
struct evhttp_request {
	TAILQ_ENTRY(evhttp_request) next;

	/* the connection object that this request belongs to */
	struct evhttp_connection *evcon;
	int flags;
#define EVHTTP_REQ_OWN_CONNECTION	0x0001	
#define EVHTTP_PROXY_REQUEST		0x0002
	
	struct evkeyvalq *input_headers;
	struct evkeyvalq *output_headers;

	/* address of the remote host and the port connection came from */
	char *remote_host;
	u_short remote_port;

	enum evhttp_request_kind kind;
	enum evhttp_cmd_type type;

	char *uri;			/* uri after HTTP request was parsed */

	char major;			/* HTTP Major number */
	char minor;			/* HTTP Minor number */
	
	int got_firstline;
	int response_code;		/* HTTP Response code */
	char *response_code_line;	/* Readable response */

	struct evbuffer *input_buffer;	/* read data */
	int ntoread;
	int chunked;

	struct evbuffer *output_buffer;	/* outgoing post or data */

	/* Callback */
	void (*cb)(struct evhttp_request *, void *);
	void *cb_arg;

	/* 
	 * Chunked data callback - call for each completed chunk if
	 * specified.  If not specified, all the data is delivered via
	 * the regular callback.
	 */
	void (*chunk_cb)(struct evhttp_request *, void *);
};

/* 
 * Creates a new request object that needs to be filled in with the request
 * parameters.  The callback is executed when the request completed or an
 * error occurred.
 */
struct evhttp_request *evhttp_request_new(
	void (*cb)(struct evhttp_request *, void *), void *arg);

/* enable delivery of chunks to requestor */
void evhttp_request_set_chunked_cb(struct evhttp_request *,
    void (*cb)(struct evhttp_request *, void *));

/* Frees the request object and removes associated events. */
void evhttp_request_free(struct evhttp_request *req);

/*
 * A connection object that can be used to for making HTTP requests.  The
 * connection object tries to establish the connection when it is given an
 * http request object.
 */
struct evhttp_connection *evhttp_connection_new(
	const char *address, unsigned short port);

/* Frees an http connection */
void evhttp_connection_free(struct evhttp_connection *evcon);

/* Sets the timeout for events related to this connection */
void evhttp_connection_set_timeout(struct evhttp_connection *evcon,
    int timeout_in_secs);

/* Sets the retry limit for this connection - -1 repeats indefnitely */
void evhttp_connection_set_retries(struct evhttp_connection *evcon,
    int retry_max);

/* Set a callback for connection close. */
void evhttp_connection_set_closecb(struct evhttp_connection *evcon,
    void (*)(struct evhttp_connection *, void *), void *);

/* Get the remote address and port associated with this connection. */
void evhttp_connection_get_peer(struct evhttp_connection *evcon,
    char **address, u_short *port);

/* The connection gets ownership of the request */
int evhttp_make_request(struct evhttp_connection *evcon,
    struct evhttp_request *req,
    enum evhttp_cmd_type type, const char *uri);

const char *evhttp_request_uri(struct evhttp_request *req);

/* Interfaces for dealing with HTTP headers */

const char *evhttp_find_header(const struct evkeyvalq *, const char *);
int evhttp_remove_header(struct evkeyvalq *, const char *);
int evhttp_add_header(struct evkeyvalq *, const char *, const char *);
void evhttp_clear_headers(struct evkeyvalq *);

/* Miscellaneous utility functions */
char *evhttp_encode_uri(const char *uri);
char *evhttp_decode_uri(const char *uri);
void evhttp_parse_query(const char *uri, struct evkeyvalq *);
char *evhttp_htmlescape(const char *html);
#ifdef __cplusplus
}
#endif

#endif /* _EVHTTP_H_ */
