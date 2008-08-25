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
write_file(struct stream *stream, const void *buf, size_t len)
{
	struct stat	st;
	struct stream	*rem = &stream->conn->rem;
	int		n, fd = stream->chan.fd;

	assert(fd != -1);
	n = write(fd, buf, len);

	DBG(("put_file(%p, %d): %d bytes", (void *) stream, (int) len, n));

	if (n <= 0 || (rem->io.total >= (big_int_t) rem->content_len)) {
		(void) fstat(fd, &st);
		stream->io.head = stream->headers_len =
		    _shttpd_snprintf(stream->io.buf,
		    stream->io.size, "HTTP/1.1 %d OK\r\n"
		    "Content-Length: %lu\r\nConnection: close\r\n\r\n",
		    stream->conn->status, st.st_size);
		_shttpd_stop_stream(stream);
	}

	return (n);
}

static int
read_file(struct stream *stream, void *buf, size_t len)
{
#ifdef USE_SENDFILE
	struct	iovec	vec;
	struct	sf_hdtr	hd = {&vec, 1, NULL, 0}, *hdp = &hd;
	int		sock, fd, n;
	size_t		nbytes;
	off_t		sent;

	sock = stream->conn->rem.chan.sock;
	fd = stream->chan.fd;

	/* If this is the first call for this file, send the headers */
	vec.iov_base = stream->io.buf;
	vec.iov_len = stream->headers_len;
	if (stream->io.total > 0)
		hdp = NULL;

	nbytes = stream->content_len - stream->io.total;
	n = sendfile(fd, sock, lseek(fd, 0, SEEK_CUR), nbytes, hdp, &sent, 0);

	if (n == -1 && ERRNO != EAGAIN) {
		stream->flags &= ~FLAG_DONT_CLOSE;
		return (n);
	}

	stream->conn->ctx->out += sent;

	/* If we have sent the HTTP headers in this turn, clear them off */
	if (stream->io.total == 0) {
		assert(sent >= stream->headers_len);
		sent -= stream->headers_len;
		io_clear(&stream->io);
	}

	(void) lseek(fd, sent, SEEK_CUR);
	stream->io.total += sent;
	stream->flags |= FLAG_DONT_CLOSE;

	return (0);
#endif /* USE_SENDFILE */

	assert(stream->chan.fd != -1);
	return (read(stream->chan.fd, buf, len));
}

static void
close_file(struct stream *stream)
{
	assert(stream->chan.fd != -1);
	(void) close(stream->chan.fd);
}

void
_shttpd_get_file(struct conn *c, struct stat *stp)
{
	char		date[64], lm[64], etag[64], range[64] = "";
	size_t		n, status = 200;
	unsigned long	r1, r2;
	const char	*fmt = "%a, %d %b %Y %H:%M:%S GMT", *msg = "OK";
	big_int_t	cl; /* Content-Length */

	if (c->mime_type.len == 0)
		_shttpd_get_mime_type(c->ctx, c->uri,
		    strlen(c->uri), &c->mime_type); 
	cl = (big_int_t) stp->st_size;

	/* If Range: header specified, act accordingly */
	if (c->ch.range.v_vec.len > 0 &&
	    (n = sscanf(c->ch.range.v_vec.ptr,"bytes=%lu-%lu",&r1, &r2)) > 0) {
		status = 206;
		(void) lseek(c->loc.chan.fd, r1, SEEK_SET);
		cl = n == 2 ? r2 - r1 + 1: cl - r1;
		(void) _shttpd_snprintf(range, sizeof(range),
		    "Content-Range: bytes %lu-%lu/%lu\r\n",
		    r1, r1 + cl - 1, (unsigned long) stp->st_size);
		msg = "Partial Content";
	}

	/* Prepare Etag, Date, Last-Modified headers */
	(void) strftime(date, sizeof(date),
	    fmt, localtime(&_shttpd_current_time));
	(void) strftime(lm, sizeof(lm), fmt, localtime(&stp->st_mtime));
	(void) _shttpd_snprintf(etag, sizeof(etag), "%lx.%lx",
	    (unsigned long) stp->st_mtime, (unsigned long) stp->st_size);

	/*
	 * We do not do io_inc_head here, because it will increase 'total'
	 * member in io. We want 'total' to be equal to the content size,
	 * and exclude the headers length from it.
	 */
	c->loc.io.head = c->loc.headers_len = _shttpd_snprintf(c->loc.io.buf,
	    c->loc.io.size,
	    "HTTP/1.1 %d %s\r\n"
	    "Date: %s\r\n"
	    "Last-Modified: %s\r\n"
	    "Etag: \"%s\"\r\n"
	    "Content-Type: %.*s\r\n"
	    "Content-Length: %lu\r\n"
	    "Accept-Ranges: bytes\r\n"
	    "%s\r\n",
	    status, msg, date, lm, etag,
	    c->mime_type.len, c->mime_type.ptr, cl, range);

	c->status = status;
	c->loc.content_len = cl;
	c->loc.io_class = &_shttpd_io_file;
	c->loc.flags |= FLAG_R | FLAG_ALWAYS_READY;

	if (c->method == METHOD_HEAD)
		_shttpd_stop_stream(&c->loc);
}

const struct io_class	_shttpd_io_file =  {
	"file",
	read_file,
	write_file,
	close_file
};
