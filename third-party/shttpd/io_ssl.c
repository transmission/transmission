/*
 * Copyright (c) 2004-2005 Sergey Lyubka <valenok@gmail.com>
 * All rights reserved
 *
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Sergey Lyubka wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.
 */

#include "defs.h"

#if !defined(NO_SSL)
struct ssl_func	ssl_sw[] = {
	{"SSL_free",			{0}},
	{"SSL_accept",			{0}},
	{"SSL_connect",			{0}},
	{"SSL_read",			{0}},
	{"SSL_write",			{0}},
	{"SSL_get_error",		{0}},
	{"SSL_set_fd",			{0}},
	{"SSL_new",			{0}},
	{"SSL_CTX_new",			{0}},
	{"SSLv23_server_method",	{0}},
	{"SSL_library_init",		{0}},
	{"SSL_CTX_use_PrivateKey_file",	{0}},
	{"SSL_CTX_use_certificate_file",{0}},
	{NULL,				{0}}
};

void
ssl_handshake(struct stream *stream)
{
	int	n;

	if ((n = SSL_accept(stream->chan.ssl.ssl)) == 0) {
		n = SSL_get_error(stream->chan.ssl.ssl, n);
		if (n != SSL_ERROR_WANT_READ && n != SSL_ERROR_WANT_WRITE)
			stream->flags |= FLAG_CLOSED;
		elog(E_LOG, stream->conn, "SSL_accept error %d", n);
	} else {
		DBG(("handshake: SSL accepted"));
		stream->flags |= FLAG_SSL_ACCEPTED;
	}
}

static int
read_ssl(struct stream *stream, void *buf, size_t len)
{
	int	nread = 0;

	assert(stream->chan.ssl.ssl != NULL);

	if (!(stream->flags & FLAG_SSL_ACCEPTED))
		ssl_handshake(stream);

	if (stream->flags & FLAG_SSL_ACCEPTED)
		nread = SSL_read(stream->chan.ssl.ssl, buf, len);

	return (nread);
}

static int
write_ssl(struct stream *stream, const void *buf, size_t len)
{
	assert(stream->chan.ssl.ssl != NULL);
	return (SSL_write(stream->chan.ssl.ssl, buf, len));
}

static void
close_ssl(struct stream *stream)
{
	assert(stream->chan.ssl.sock != -1);
	assert(stream->chan.ssl.ssl != NULL);
	(void) closesocket(stream->chan.ssl.sock);
	SSL_free(stream->chan.ssl.ssl);
}

const struct io_class	io_ssl =  {
	"ssl",
	read_ssl,
	write_ssl,
	close_ssl
};
#endif /* !NO_SSL */
