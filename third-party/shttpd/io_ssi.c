/*
 * Copyright (c) 2006,2007 Steven Johnson <sjohnson@sakuraindustries.com>
 * Copyright (c) 2007 Sergey Lyubka <valenok@gmail.com>
 * All rights reserved
 *
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Sergey Lyubka wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.
 */

#include "defs.h"

#define	CMDBUFSIZ	512		/* SSI command buffer size	*/
#define	NEST_MAX	6		/* Maximum nesting level	*/

struct ssi_func {
	struct llhead	link;
	void		*user_data;
	char		*name;
	shttpd_callback_t func;
};

struct ssi_inc {
	int		state;		/* Buffering state		*/
	int		cond;		/* Conditional state		*/
	FILE		*fp;		/* Icluded file stream		*/
	char		buf[CMDBUFSIZ];	/* SSI command buffer		*/
	size_t		nbuf;		/* Bytes in a command buffer	*/
	FILE		*pipe;		/* #exec stream			*/
	struct ssi_func	func;		/* #call function		*/
};

struct ssi {
	struct conn	*conn;		/* Connection we belong to	*/
	int		nest;		/* Current nesting level	*/
	struct ssi_inc	incs[NEST_MAX];	/* Nested includes		*/
};

enum { SSI_PASS, SSI_BUF, SSI_EXEC, SSI_CALL };
enum { SSI_GO, SSI_STOP };		/* Conditional states		*/

static const struct vec	st = {"<!--#", 5};

void
shttpd_register_ssi_func(struct shttpd_ctx *ctx, const char *name,
		shttpd_callback_t func, void *user_data)
{
	struct ssi_func	*e;

	if ((e = malloc(sizeof(*e))) != NULL) {
		e->name		= my_strdup(name);
		e->func		= func;
		e->user_data	= user_data;
		LL_TAIL(&ctx->ssi_funcs, &e->link);
	}
}

void
ssi_func_destructor(struct llhead *lp)
{
	struct ssi_func	*e = LL_ENTRY(lp, struct ssi_func, link);

	free(e->name);
	free(e);
}

static const struct ssi_func *
find_ssi_func(struct ssi *ssi, const char *name)
{
	struct ssi_func	*e;
	struct llhead	*lp;

	LL_FOREACH(&ssi->conn->ctx->ssi_funcs, lp) {
		e = LL_ENTRY(lp, struct ssi_func, link);
		if (!strcmp(name, e->name))
			return (e);
	}

	return (NULL);
}

static void
call(struct ssi *ssi, const char *name,
		struct shttpd_arg *arg, char *buf, int len)
{
	const struct ssi_func	*ssi_func;

	(void) memset(arg, 0, sizeof(*arg));

	/*
	 * SSI function may be called with parameters. These parameters
	 * are passed as arg->in.buf, arg->in.len vector.
	 */
	arg->in.buf = strchr(name, ' ');
	if (arg->in.buf != NULL) {
		*arg->in.buf++ = '\0';
		arg->in.len = strlen(arg->in.buf);
	}

	if ((ssi_func = find_ssi_func(ssi, name)) != NULL) {
		arg->priv = ssi->conn;
		arg->user_data = ssi_func->user_data;
		arg->out.buf = buf;
		arg->out.len = len;
		ssi_func->func(arg);
	}
}

static int
evaluate(struct ssi *ssi, const char *name)
{
	struct shttpd_arg	arg;

	call(ssi, name, &arg, NULL, 0);

	return (arg.flags & SHTTPD_SSI_EVAL_TRUE);
}

static void
pass(struct ssi_inc *inc, void *buf, int *n)
{
	if (inc->cond == SSI_GO) {
		(void) memcpy(buf, inc->buf, inc->nbuf);
		(*n) += inc->nbuf;
	}
	inc->nbuf = 0;
	inc->state = SSI_PASS;
}

static int
get_path(struct conn *conn, const char *src,
		int src_len, char *dst, int dst_len)
{
	static struct vec	accepted[] = {
		{"\"",		1},	/* Relative to webserver CWD	*/
		{"file=\"", 	6},	/* Relative to current URI	*/
		{"virtual=\"", 	9},	/* Relative to document root	*/
		{NULL,		0},
	};
	struct vec	*vec;
	const char	*p, *root = conn->ctx->options[OPT_ROOT];
	int		len;

	for (vec = accepted; vec->len > 0; vec++)
		if (src_len > vec->len && !memcmp(src, vec->ptr, vec->len)) {
			src += vec->len;
			src_len -= vec->len;
			if ((p = memchr(src, '"', src_len)) == NULL)
				break;
			if (vec->len == 6) {
				len = my_snprintf(dst, dst_len, "%s%c%s",
				    root, DIRSEP, conn->uri);
				while (len > 0 && dst[len] != '/')
					len--;
				dst += len;
				dst_len -= len;
			} else if (vec->len == 9) {
				len = my_snprintf(dst, dst_len, "%s%c",
				    root, DIRSEP);
				dst += len;
				dst_len -= len;
			}
			url_decode(src, p - src, dst, dst_len);
			return (1);
		}

	return (0);
}

static void
do_include(struct ssi *ssi)
{
	struct ssi_inc	*inc = ssi->incs + ssi->nest;
	char		buf[FILENAME_MAX];
	FILE		*fp;

	assert(inc->nbuf >= 13);

	if (inc->cond == SSI_STOP) {
		/* Do nothing - conditional FALSE */
	} else if (ssi->nest >= (int) NELEMS(ssi->incs) - 1) {
		elog(E_LOG, ssi->conn,
		    "ssi: #include: maximum nested level reached");
	} else if (!get_path(ssi->conn,
	    inc->buf + 13, inc->nbuf - 13, buf, sizeof(buf))) {
		elog(E_LOG, ssi->conn, "ssi: bad #include: [%.*s]",
		    inc->nbuf, inc->buf);
	} else if ((fp = fopen(buf, "r")) == NULL) {
		elog(E_LOG, ssi->conn, 
		    "ssi: fopen(%s): %s", buf, strerror(errno));
	} else {
		ssi->nest++;
		ssi->incs[ssi->nest].fp = fp;
		ssi->incs[ssi->nest].nbuf = 0;
		ssi->incs[ssi->nest].cond = SSI_GO;
	}
}

static char *
trim_spaces(struct ssi_inc *inc)
{
	char	*p = inc->buf + inc->nbuf - 2;

	/* Trim spaces from the right */
	*p-- = '\0';
	while (isspace(* (unsigned char *) p))
		*p-- = '\0';

	/* Shift pointer to the start of attributes */
	for (p = inc->buf; !isspace(* (unsigned char *) p); p++);
	while (*p && isspace(* (unsigned char *) p)) p++;

	return (p);
}

static void
do_if(struct ssi *ssi)
{
	struct ssi_inc	*inc = ssi->incs + ssi->nest;
	char		*name = trim_spaces(inc);

	inc->cond = evaluate(ssi, name) ? SSI_GO : SSI_STOP;
}

static void
do_elif(struct ssi *ssi)
{
	struct ssi_inc	*inc = ssi->incs + ssi->nest;
	char		*name = trim_spaces(inc);

	if (inc->cond == SSI_STOP && evaluate(ssi, name))
		inc->cond = SSI_GO;
	else
		inc->cond = SSI_STOP;
}
static void
do_endif(struct ssi *ssi)
{
	ssi->incs[ssi->nest].cond = SSI_GO;
}

static void
do_else(struct ssi *ssi)
{
	struct ssi_inc	*inc = ssi->incs + ssi->nest;

	inc->cond = inc->cond == SSI_GO ? SSI_STOP : SSI_GO;
}

static void
do_call2(struct ssi *ssi, char *buf, int len, int *n)
{
	struct ssi_inc	*inc = ssi->incs + ssi->nest;
	struct shttpd_arg	arg;

	call(ssi, inc->buf, &arg, buf, len);
	(*n) += arg.out.num_bytes;
	if (arg.flags & SHTTPD_END_OF_OUTPUT)
		inc->state = SSI_PASS;
}

static void
do_call(struct ssi *ssi, char *buf, int len, int *n)
{
	struct ssi_inc	*inc = ssi->incs + ssi->nest;
	char		*name = trim_spaces(inc);

	if (inc->cond == SSI_GO) {
		(void) memmove(inc->buf, name, strlen(name) + 1);
		inc->state = SSI_CALL;
		do_call2(ssi, buf, len, n);
	}
}

static void
do_exec2(struct ssi *ssi, char *buf, int len, int *n)
{
	struct ssi_inc	*inc = ssi->incs + ssi->nest;
	int		i, ch;

	for (i = 0; i < len; i++) {
		if ((ch = fgetc(inc->pipe)) == EOF) {
			inc->state = SSI_PASS;
			(void) pclose(inc->pipe);
			inc->pipe = NULL;
			break;
		}
		*buf++ = ch;
		(*n)++;
	}
}

static void
do_exec(struct ssi *ssi, char *buf, int len, int *n)
{
	struct ssi_inc	*inc = ssi->incs + ssi->nest;
	char		cmd[sizeof(inc->buf)], *e, *p;

	p = trim_spaces(inc);

	if (inc->cond == SSI_STOP) {
		/* Do nothing - conditional FALSE */
	} else if (*p != '"' || (e = strchr(p + 1, '"')) == NULL) {
		elog(E_LOG, ssi->conn, "ssi: bad exec(%s)", p);
	} else if (!url_decode(p + 1, e - p - 1, cmd, sizeof(cmd))) {
		elog(E_LOG, ssi->conn, "ssi: cannot url_decode: exec(%s)", p);
	} else if ((inc->pipe = popen(cmd, "r")) == NULL) {
		elog(E_LOG, ssi->conn, "ssi: popen(%s)", cmd);
	} else {
		inc->state = SSI_EXEC;
		do_exec2(ssi, buf, len, n);
	}
}

static const struct ssi_cmd {
	struct vec	vec;
	void (*func)();
} known_ssi_commands [] = {
	{{"include ",	8}, do_include	},
	{{"if ",	3}, do_if	},
	{{"elif ",	5}, do_elif	},
	{{"else",	4}, do_else	},
	{{"endif",	5}, do_endif	},
	{{"call ",	5}, do_call	},
	{{"exec ",	5}, do_exec	},
	{{NULL,		0}, NULL	}
};

static void
do_command(struct ssi *ssi, char *buf, size_t len, int *n)
{
	struct ssi_inc		*inc = ssi->incs + ssi->nest;
	const struct ssi_cmd	*cmd;

	assert(len > 0);
	assert(inc->nbuf <= len);
	inc->state = SSI_PASS;

	for (cmd = known_ssi_commands; cmd->func != NULL; cmd++)
		if (inc->nbuf > (size_t) st.len + cmd->vec.len &&
		    !memcmp(inc->buf + st.len, cmd->vec.ptr, cmd->vec.len)) {
			cmd->func(ssi, buf, len, n);
			break;
		}

	if (cmd->func == NULL)
		pass(inc, buf, n);

	inc->nbuf = 0;
}

static int
read_ssi(struct stream *stream, void *vbuf, size_t len)
{
	struct ssi	*ssi = stream->conn->ssi;
	struct ssi_inc	*inc = ssi->incs + ssi->nest;
	char		*buf = vbuf;
	int		ch = EOF, n = 0;

again:

	if (inc->state == SSI_CALL)
		do_call2(ssi, buf, len, &n);
	else if (inc->state == SSI_EXEC)
		do_exec2(ssi, buf, len, &n);

	while (n + inc->nbuf < len && (ch = fgetc(inc->fp)) != EOF)
	
		switch (inc->state) {

		case SSI_PASS:
			if (ch == '<') {
				inc->nbuf = 0;
				inc->buf[inc->nbuf++] = ch;
				inc->state = SSI_BUF;
			} else if (inc->cond == SSI_GO) {
				buf[n++] = ch;
			}
			break;

		/*
		 * We are buffering whole SSI command, until closing "-->".
		 * That means that when do_command() is called, we can rely
		 * on that full command with arguments is buffered in and
		 * there is no need for streaming.
		 * Restrictions:
		 *  1. The command must fit in CMDBUFSIZ
		 *  2. HTML comments inside the command ? Not sure about this.
		 */
		case SSI_BUF:
			if (inc->nbuf >= sizeof(inc->buf) - 1) {
				pass(inc, buf + n, &n);
			} else if (ch == '>' &&
			    !memcmp(inc->buf + inc->nbuf - 2, "--", 2)) {
				do_command(ssi, buf + n, len - n, &n);
				inc = ssi->incs + ssi->nest;
			} else {
				inc->buf[inc->nbuf++] = ch;

				/* If not SSI tag, pass it */
				if (inc->nbuf <= (size_t) st.len &&
				    memcmp(inc->buf, st.ptr, inc->nbuf) != 0)
					pass(inc, buf + n, &n);
			}
			break;

		case SSI_EXEC:
		case SSI_CALL:
			break;

		default:
			/* Never happens */
			abort();
			break;
		}

	if (ssi->nest > 0 && n + inc->nbuf < len && ch == EOF) {
		(void) fclose(inc->fp);
		inc->fp = NULL;
		ssi->nest--;
		inc--;
		goto again;
	}
	
	return (n);
}

static void
close_ssi(struct stream *stream)
{
	struct ssi	*ssi = stream->conn->ssi;
	size_t		i;

	for (i = 0; i < NELEMS(ssi->incs); i++) {
		if (ssi->incs[i].fp != NULL)
			(void) fclose(ssi->incs[i].fp);
		if (ssi->incs[i].pipe != NULL)
			(void) pclose(ssi->incs[i].pipe);
	}

	free(ssi);
}

void
do_ssi(struct conn *c)
{
	char		date[64];
	struct ssi	*ssi;

	(void) strftime(date, sizeof(date), "%a, %d %b %Y %H:%M:%S GMT",
	    localtime(&current_time));

	c->loc.io.head = c->loc.headers_len = my_snprintf(c->loc.io.buf,
	    c->loc.io.size,
	    "HTTP/1.1 200 OK\r\n"
	    "Date: %s\r\n"
	    "Content-Type: text/html\r\n"
	    "Connection: close\r\n\r\n",
	    date);

	c->status = 200;
	c->loc.io_class = &io_ssi;
	c->loc.flags |= FLAG_R | FLAG_ALWAYS_READY;

	if (c->method == METHOD_HEAD) {
		stop_stream(&c->loc);
	} else if ((ssi = calloc(1, sizeof(struct ssi))) == NULL) {
		send_server_error(c, 500, "Cannot allocate SSI descriptor");
	} else {
		ssi->incs[0].fp = fdopen(c->loc.chan.fd, "r");
		ssi->conn = c;
		c->ssi = ssi;
	}
}

const struct io_class	io_ssi =  {
	"ssi",
	read_ssi,
	NULL,
	close_ssi
};
