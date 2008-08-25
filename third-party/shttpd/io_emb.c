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

const char *
shttpd_version(void)
{
	return (VERSION);
}

static void
call_user(struct conn *c, struct shttpd_arg *arg, shttpd_callback_t func)
{
	arg->priv		= c;
	arg->state		= c->loc.chan.emb.state;
	arg->out.buf		= io_space(&c->loc.io);
	arg->out.len		= io_space_len(&c->loc.io);
	arg->out.num_bytes	= 0;
	arg->in.buf		= io_data(&c->rem.io);;
	arg->in.len		= io_data_len(&c->rem.io);
	arg->in.num_bytes	= 0;

	if (io_data_len(&c->rem.io) >= c->rem.io.size)
		arg->flags |= SHTTPD_POST_BUFFER_FULL;

	if (c->rem.content_len > 0 && c->rem.io.total < c->rem.content_len)
		arg->flags |= SHTTPD_MORE_POST_DATA;

	func(arg);

	io_inc_head(&c->loc.io, arg->out.num_bytes);
	io_inc_tail(&c->rem.io, arg->in.num_bytes);
	c->loc.chan.emb.state = arg->state;		/* Save state */

	/*
	 * If callback finished output, that means it did all cleanup.
	 * If the connection is terminated unexpectedly, we canna call
	 * the callback via the stream close() method from disconnect.
	 * However, if cleanup is already done, we set close() method to
	 * NULL, to prevent the call from disconnect().
	 */

	if (arg->flags & SHTTPD_END_OF_OUTPUT)
		c->loc.flags &= ~FLAG_DONT_CLOSE;
	else
		c->loc.flags |= FLAG_DONT_CLOSE;

	if (arg->flags & SHTTPD_SUSPEND)
		c->loc.flags |= FLAG_SUSPEND;
}

static int
do_embedded(struct stream *stream, void *buf, size_t len)
{
	struct shttpd_arg	arg;
	buf = NULL; len = 0;		/* Squash warnings */

	arg.user_data	= stream->conn->loc.chan.emb.data;
	arg.flags	= 0;

	call_user(stream->conn, &arg, (shttpd_callback_t)
			stream->conn->loc.chan.emb.func.v_func);

	return (0);
}

static void
close_embedded(struct stream *stream)
{
	struct shttpd_arg	arg;
	struct conn		*c = stream->conn;

	arg.flags	= SHTTPD_CONNECTION_ERROR;
	arg.user_data	= c->loc.chan.emb.data;

	/*
	 * Do not call the user function if SHTTPD_END_OF_OUTPUT was set,
	 * i.e. the callback already terminated correctly
	 */
	if (stream->flags & FLAG_DONT_CLOSE)
		call_user(stream->conn, &arg, (shttpd_callback_t)
		    c->loc.chan.emb.func.v_func);
}

size_t
shttpd_printf(struct shttpd_arg *arg, const char *fmt, ...)
{
	char		*buf = arg->out.buf + arg->out.num_bytes;
	int		buflen = arg->out.len - arg->out.num_bytes, len = 0;
	va_list		ap;

	if (buflen > 0) {
		va_start(ap, fmt);
		len = vsnprintf(buf, buflen, fmt, ap);
		va_end(ap);

		if (len < 0 || len > buflen)
			len = buflen;
		arg->out.num_bytes += len;
	}

	return (len);
}

const char *
shttpd_get_header(struct shttpd_arg *arg, const char *header_name)
{
	struct conn	*c = arg->priv;
	char		*p, *s, *e;
	size_t		len;

	p = c->headers;
	e = c->request + c->rem.headers_len;
	len = strlen(header_name);

	while (p < e) {
		if ((s = strchr(p, '\n')) != NULL)
			s[s[-1] == '\r' ? -1 : 0] = '\0';
		if (_shttpd_strncasecmp(header_name, p, len) == 0)
			return (p + len + 2);

		p += strlen(p) + 1;
	}

	return (NULL);
}

const char *
shttpd_get_env(struct shttpd_arg *arg, const char *env_name)
{
	struct conn	*c = arg->priv;
	struct vec	*vec;

	if (strcmp(env_name, "REQUEST_METHOD") == 0) {
		return (_shttpd_known_http_methods[c->method].ptr);
	} else if (strcmp(env_name, "REQUEST_URI") == 0) {
		return (c->uri);
	} else if (strcmp(env_name, "QUERY_STRING") == 0) {
		return (c->query);
	} else if (strcmp(env_name, "REMOTE_USER") == 0) {
		vec = &c->ch.user.v_vec;
		if (vec->len > 0) {
			((char *) vec->ptr)[vec->len] = '\0';
			return (vec->ptr);
		}
	} else if (strcmp(env_name, "REMOTE_ADDR") == 0) {
		return (inet_ntoa(c->sa.u.sin.sin_addr));/* FIXME NOT MT safe */
	}

	return (NULL);
}

void
shttpd_get_http_version(struct shttpd_arg *arg,
		unsigned long *major, unsigned long *minor)
{
	struct conn *c = arg->priv;
	
	*major = c->major_version;
	*minor = c->minor_version;
}

void
shttpd_register_uri(struct shttpd_ctx *ctx,
		const char *uri, shttpd_callback_t callback, void *data)
{
	struct registered_uri	*e;

	if ((e = malloc(sizeof(*e))) != NULL) {
		e->uri			= _shttpd_strdup(uri);
		e->callback.v_func	= (void (*)(void)) callback;
		e->callback_data	= data;
		LL_TAIL(&ctx->registered_uris, &e->link);
	}
}

int
shttpd_get_var(const char *var, const char *buf, int buf_len,
		char *value, int value_len)
{
	const char	*p, *e, *s;
	size_t		var_len;

	var_len = strlen(var);
	e = buf + buf_len;		/* End of QUERY_STRING buffer	*/

	/* buf is "var1=val1&var2=val2...". Find variable first */
	for (p = buf; p + var_len < e; p++)
		if ((p == buf || p[-1] == '&') &&
		    p[var_len] == '=' &&
		    !_shttpd_strncasecmp(var, p, var_len)) {

			/* Point 'p' to var value, 's' to the end of value */
			p += var_len + 1;	
			if ((s = memchr(p, '&', e - p)) == NULL)
				s = e;

			/* URL-decode value. Return result length */
			return (_shttpd_url_decode(p, s - p, value, value_len));
		}

	return (-1);
}

static int
match_regexp(const char *regexp, const char *text)
{
	if (*regexp == '\0')
		return (*text == '\0');

	if (*regexp == '*')
		do {
			if (match_regexp(regexp + 1, text))
				return (1);
		} while (*text++ != '\0');

	if (*text != '\0' && *regexp == *text)
		return (match_regexp(regexp + 1, text + 1));

	return (0);
}

struct registered_uri *
_shttpd_is_registered_uri(struct shttpd_ctx *ctx, const char *uri)
{
	struct llhead		*lp;
	struct registered_uri	*reg_uri;

	LL_FOREACH(&ctx->registered_uris, lp) {
		reg_uri = LL_ENTRY(lp, struct registered_uri, link);
		if (match_regexp(reg_uri->uri, uri))
			return (reg_uri);
	}

	return (NULL);
}

void
_shttpd_setup_embedded_stream(struct conn *c, union variant func, void *data)
{
	c->loc.chan.emb.state = NULL;
	c->loc.chan.emb.func = func;
	c->loc.chan.emb.data = data;
	c->loc.io_class = &_shttpd_io_embedded;
	c->loc.flags |= FLAG_R | FLAG_W |FLAG_ALWAYS_READY;
}

void
shttpd_handle_error(struct shttpd_ctx *ctx, int code,
		shttpd_callback_t func, void *data)
{
	struct error_handler	*e;

	if ((e = malloc(sizeof(*e))) != NULL) {
		e->code = code;
		e->callback.v_func = (void (*)(void)) func;
		e->callback_data = data;
		LL_TAIL(&ctx->error_handlers, &e->link);
	}
}

void
shttpd_wakeup(const void *priv)
{
	const struct conn	*conn = priv;
	char			buf[sizeof(int) + sizeof(void *)];
	int			cmd = CTL_WAKEUP;

#if 0
	conn->flags &= ~SHTTPD_SUSPEND;
#endif
	(void) memcpy(buf, &cmd, sizeof(cmd));
	(void) memcpy(buf + sizeof(cmd), conn, sizeof(conn));

	(void) send(conn->worker->ctl[1], buf, sizeof(buf), 0);
}

const struct io_class	_shttpd_io_embedded =  {
	"embedded",
	do_embedded,
	(int (*)(struct stream *, const void *, size_t)) do_embedded,
	close_embedded
};
