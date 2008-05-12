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

#if !defined(NO_CGI)
struct env_block {
	char	buf[ENV_MAX];		/* Environment buffer		*/
	int	len;			/* Space taken			*/
	char	*vars[CGI_ENV_VARS];	/* Point into the buffer	*/
	int	nvars;			/* Number of variables		*/
};

/*
 * UNIX socketpair() implementation. Why? Because Windows does not have it.
 * Return 0 on success, -1 on error.
 */
static int
my_socketpair(struct conn *c, int sp[2])
{
	struct sockaddr_in	sa;
	int			sock, ret = -1;
	socklen_t		len = sizeof(sa);

	(void) memset(&sa, 0, sizeof(sa));
	sa.sin_family 		= AF_INET;
	sa.sin_port		= htons(0);
	sa.sin_addr.s_addr	= htonl(INADDR_LOOPBACK);

	if ((sock = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		elog(E_LOG, c, "mysocketpair: socket(): %d", ERRNO);
	} else if (bind(sock, (struct sockaddr *) &sa, len) != 0) {
		elog(E_LOG, c, "mysocketpair: bind(): %d", ERRNO);
		(void) closesocket(sock);
	} else if (listen(sock, 1) != 0) {
		elog(E_LOG, c, "mysocketpair: listen(): %d", ERRNO);
		(void) closesocket(sock);
	} else if (getsockname(sock, (struct sockaddr *) &sa, &len) != 0) {
		elog(E_LOG, c, "mysocketpair: getsockname(): %d", ERRNO);
		(void) closesocket(sock);
	} else if ((sp[0] = socket(AF_INET, SOCK_STREAM, 6)) == -1) {
		elog(E_LOG, c, "mysocketpair: socket(): %d", ERRNO);
		(void) closesocket(sock);
	} else if (connect(sp[0], (struct sockaddr *) &sa, len) != 0) {
		elog(E_LOG, c, "mysocketpair: connect(): %d", ERRNO);
		(void) closesocket(sock);
		(void) closesocket(sp[0]);
	} else if ((sp[1] = accept(sock,(struct sockaddr *) &sa, &len)) == -1) {
		elog(E_LOG, c, "mysocketpair: accept(): %d", ERRNO);
		(void) closesocket(sock);
		(void) closesocket(sp[0]);
	} else {
		/* Success */
		ret = 0;
		(void) closesocket(sock);
	}

#ifndef _WIN32
	(void) fcntl(sp[0], F_SETFD, FD_CLOEXEC);
	(void) fcntl(sp[1], F_SETFD, FD_CLOEXEC);
#endif /* _WIN32*/

	return (ret);
}

static void
addenv(struct env_block *block, const char *fmt, ...)
{
	int	n, space;
	va_list	ap;

	space = sizeof(block->buf) - block->len - 2;
	assert(space >= 0);

	va_start(ap, fmt);
	n = vsnprintf(block->buf + block->len, space, fmt, ap);
	va_end(ap);

	if (n > 0 && n < space && block->nvars < CGI_ENV_VARS - 2) {
		block->vars[block->nvars++] = block->buf + block->len;
		block->len += n + 1;	/* Include \0 terminator */
	}
}

static void
add_http_headers_to_env(struct env_block *b, const char *s, int len)
{
	const char	*p, *v, *e = s + len;
	int		space, n, i, ch;

	/* Loop through all headers in the request */
	while (s < e) {

		/* Find where this header ends. Remember where value starts */
		for (p = s, v = NULL; p < e && *p != '\n'; p++)
			if (v == NULL && *p == ':') 
				v = p;

		/* 2 null terminators and "HTTP_" */
		space = (sizeof(b->buf) - b->len) - (2 + 5);
		assert(space >= 0);
	
		/* Copy header if enough space in the environment block */
		if (v > s && p > v + 2 && space > p - s) {

			/* Store var */
			if (b->nvars < (int) NELEMS(b->vars) - 1)
				b->vars[b->nvars++] = b->buf + b->len;

			(void) memcpy(b->buf + b->len, "HTTP_", 5);
			b->len += 5;

			/* Copy header name. Substitute '-' to '_' */
			n = v - s;
			for (i = 0; i < n; i++) {
				ch = s[i] == '-' ? '_' : s[i];
				b->buf[b->len++] = toupper(ch);
			}

			b->buf[b->len++] = '=';

			/* Copy header value */
			v += 2;
			n = p[-1] == '\r' ? (p - v) - 1 : p - v;
			for (i = 0; i < n; i++)
				b->buf[b->len++] = v[i];

			/* Null-terminate */
			b->buf[b->len++] = '\0';
		}

		s = p + 1;	/* Shift to the next header */
	}
}

static void
prepare_environment(const struct conn *c, const char *prog,
		struct env_block *blk)
{
	const struct headers	*h = &c->ch;
	const char		*s, *root = c->ctx->options[OPT_ROOT];
	size_t			len;

	blk->len = blk->nvars = 0;

	/* Prepare the environment block */
	addenv(blk, "%s", "GATEWAY_INTERFACE=CGI/1.1");
	addenv(blk, "%s", "SERVER_PROTOCOL=HTTP/1.1");
	addenv(blk, "%s", "REDIRECT_STATUS=200");	/* PHP */
	addenv(blk, "SERVER_PORT=%d", c->loc_port);
	addenv(blk, "SERVER_NAME=%s", c->ctx->options[OPT_AUTH_REALM]);
	addenv(blk, "SERVER_ROOT=%s", root);
	addenv(blk, "DOCUMENT_ROOT=%s", root);
	addenv(blk, "REQUEST_METHOD=%s", known_http_methods[c->method].ptr);
	addenv(blk, "REMOTE_ADDR=%s", inet_ntoa(c->sa.u.sin.sin_addr));
	addenv(blk, "REMOTE_PORT=%hu", ntohs(c->sa.u.sin.sin_port));
	addenv(blk, "REQUEST_URI=%s", c->uri);
	addenv(blk, "SCRIPT_NAME=%s", prog + strlen(root));
	addenv(blk, "SCRIPT_FILENAME=%s", prog);	/* PHP */
	addenv(blk, "PATH_TRANSLATED=%s", prog);

	if (h->ct.v_vec.len > 0)
		addenv(blk, "CONTENT_TYPE=%.*s", 
		    h->ct.v_vec.len, h->ct.v_vec.ptr);

	if (c->query != NULL)
		addenv(blk, "QUERY_STRING=%s", c->query);

	if (c->path_info != NULL)
		addenv(blk, "PATH_INFO=/%s", c->path_info);

	if (h->cl.v_big_int > 0)
		addenv(blk, "CONTENT_LENGTH=%lu", h->cl.v_big_int);

	if ((s = getenv("PATH")) != NULL)
		addenv(blk, "PATH=%s", s);

#ifdef _WIN32
	if ((s = getenv("COMSPEC")) != NULL)
		addenv(blk, "COMSPEC=%s", s);
	if ((s = getenv("SYSTEMROOT")) != NULL)
		addenv(blk, "SYSTEMROOT=%s", s);
#else
	if ((s = getenv("LD_LIBRARY_PATH")) != NULL)
		addenv(blk, "LD_LIBRARY_PATH=%s", s);
#endif /* _WIN32 */

	if ((s = getenv("PERLLIB")) != NULL)
		addenv(blk, "PERLLIB=%s", s);

	if (h->user.v_vec.len > 0) {
		addenv(blk, "REMOTE_USER=%.*s",
		    h->user.v_vec.len, h->user.v_vec.ptr);
		addenv(blk, "%s", "AUTH_TYPE=Digest");
	}

	/* Add user-specified variables */
	s = c->ctx->options[OPT_CGI_ENVIRONMENT];
	FOR_EACH_WORD_IN_LIST(s, len)
		addenv(blk, "%.*s", len, s);

	/* Add all headers as HTTP_* variables */
	add_http_headers_to_env(blk, c->headers,
	    c->rem.headers_len - (c->headers - c->request));

	blk->vars[blk->nvars++] = NULL;
	blk->buf[blk->len++] = '\0';

	assert(blk->nvars < CGI_ENV_VARS);
	assert(blk->len > 0);
	assert(blk->len < (int) sizeof(blk->buf));

	/* Debug stuff to view passed environment */
	DBG(("%s: %d vars, %d env size", prog, blk->nvars, blk->len));
	{
		int i;
		for (i = 0 ; i < blk->nvars; i++)
			DBG(("[%s]", blk->vars[i] ? blk->vars[i] : "null"));
	}
}

int
run_cgi(struct conn *c, const char *prog)
{
	struct env_block	blk;
	char			dir[FILENAME_MAX], *p;
	int			ret, pair[2];

	prepare_environment(c, prog, &blk);
	pair[0] = pair[1] = -1;

	/* CGI must be executed in its own directory */
	(void) my_snprintf(dir, sizeof(dir), "%s", prog);
	for (p = dir + strlen(dir) - 1; p > dir; p--)
		if (*p == '/') {
			*p++ = '\0';
			break;
		}
	
	if (my_socketpair(c, pair) != 0) {
		ret = -1;
	} else if (spawn_process(c, prog, blk.buf, blk.vars, pair[1], dir)) {
		ret = -1;
		(void) closesocket(pair[0]);
		(void) closesocket(pair[1]);
	} else {
		ret = 0;
		c->loc.chan.sock = pair[0];
	}

	return (ret);
}

void
do_cgi(struct conn *c)
{
	DBG(("running CGI: [%s]", c->uri));
	assert(c->loc.io.size > CGI_REPLY_LEN);
	memcpy(c->loc.io.buf, CGI_REPLY, CGI_REPLY_LEN);
	c->loc.io.head = c->loc.io.tail = c->loc.io.total = CGI_REPLY_LEN;
	c->loc.io_class = &io_cgi;
	c->loc.flags = FLAG_R;
	if (c->method == METHOD_POST)
		c->loc.flags |= FLAG_W;
}

#endif /* !NO_CGI */
