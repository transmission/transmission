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

static int
write_cgi(struct stream *stream, const void *buf, size_t len)
{
	assert(stream->chan.sock != -1);
	assert(stream->flags & FLAG_W);

	return (send(stream->chan.sock, buf, len, 0));
}

static int
read_cgi(struct stream *stream, void *buf, size_t len)
{
	struct headers	parsed;
	char		status[4];
	int		n;

	assert(stream->chan.sock != -1);
	assert(stream->flags & FLAG_R);

	stream->flags &= ~FLAG_DONT_CLOSE;

	n = recv(stream->chan.sock, buf, len, 0);

	if (stream->flags & FLAG_HEADERS_PARSED)
		return (n);

	if (n <= 0 && ERRNO != EWOULDBLOCK) {
		send_server_error(stream->conn, 500, "Error running CGI");
		return (n);
	}

	/*
	 * CGI script may output Status: and Location: headers, which
	 * may alter the status code. Buffer in headers, parse
	 * them, send correct status code and then forward all data
	 * from CGI script back to the remote end.
	 * Reply line was alredy appended to the IO buffer in
	 * decide_what_to_do(), with blank status code.
	 */

	stream->flags |= FLAG_DONT_CLOSE;
	io_inc_head(&stream->io, n);

	stream->headers_len = get_headers_len(stream->io.buf, stream->io.head);
	if (stream->headers_len < 0) {
		stream->flags &= ~FLAG_DONT_CLOSE;
		send_server_error(stream->conn, 500, "Bad headers sent");
		elog(E_LOG, stream->conn, "CGI script sent invalid headers: "
		    "[%.*s]", stream->io.head - CGI_REPLY_LEN,
		    stream->io.buf + CGI_REPLY_LEN);
		return (0);
	}

	/*
	 * If we did not received full headers yet, we must not send any
	 * data read from the CGI back to the client. Suspend sending by
	 * setting tail = head, which tells that there is no data in IO buffer
	 */

	if (stream->headers_len == 0) {
		stream->io.tail = stream->io.head;
		return (0);
	}

	/* Received all headers. Set status code for the connection. */
	(void) memset(&parsed, 0, sizeof(parsed));
	parse_headers(stream->io.buf, stream->headers_len, &parsed);
	stream->content_len = parsed.cl.v_big_int;
	stream->conn->status = (int) parsed.status.v_big_int;

	/* If script outputs 'Location:' header, set status code to 302 */
	if (parsed.location.v_vec.len > 0)
		stream->conn->status = 302;

	/*
	 * If script did not output neither 'Location:' nor 'Status' headers,
	 * set the default status code 200, which means 'success'.
	 */
	if (stream->conn->status == 0)
		stream->conn->status = 200;

	/* Append the status line to the beginning of the output */
	(void) my_snprintf(status, sizeof(status), "%3d", stream->conn->status);
	(void) memcpy(stream->io.buf + 9, status, 3);
	DBG(("read_cgi: content len %lu status %s",
	    stream->content_len, status));

	/* Next time, pass output directly back to the client */
	assert((big_int_t) stream->headers_len <= stream->io.total);
	stream->io.total -= stream->headers_len;
	stream->io.tail = 0;
	stream->flags |= FLAG_HEADERS_PARSED;

	/* Return 0 because we've already shifted the head */
	return (0);
}

static void
close_cgi(struct stream *stream)
{
	assert(stream->chan.sock != -1);
	(void) closesocket(stream->chan.sock);
}

const struct io_class	io_cgi =  {
	"cgi",
	read_cgi,
	write_cgi,
	close_cgi
};
