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

/*
 * Log function
 */
void
_shttpd_elog(int flags, struct conn *c, const char *fmt, ...)
{
	FILE	*fp = c == NULL ? NULL : c->ctx->error_log;
	va_list	ap;

	/* Print to stderr */
	if (c == NULL || !IS_TRUE(c->ctx, OPT_INETD)) {
		va_start(ap, fmt);
		(void) vfprintf(stderr, fmt, ap);
		(void) fputc('\n', stderr);
		va_end(ap);
	}

	if (fp != NULL && (flags & (E_FATAL | E_LOG))) {
		char	date[64];
		char	*buf = malloc( URI_MAX );
		int	len;

		strftime(date, sizeof(date), "%a %b %d %H:%M:%S %Y",
		    localtime(&_shttpd_current_time));

		len = _shttpd_snprintf(buf, URI_MAX,
		    "[%s] [error] [client %s] \"%s\" ",
		    date, c ? inet_ntoa(c->sa.u.sin.sin_addr) : "-",
		    c && c->request ? c->request : "-");

		va_start(ap, fmt);
		(void) vsnprintf(buf + len, URI_MAX - len, fmt, ap);
		va_end(ap);

		buf[URI_MAX - 1] = '\0';

		(void) fprintf(fp, "%s\n", buf);
		(void) fflush(fp);

		free(buf);
	}

	if (flags & E_FATAL)
		exit(EXIT_FAILURE);
}

void
_shttpd_log_access(FILE *fp, const struct conn *c)
{
	static const struct vec	dash = {"-", 1};

	const struct vec	*user;
	const struct vec	*referer;
	const struct vec	*user_agent;
	char			date[64], *buf, *q1, *q2;

	if (fp == NULL)
		return;

	user = &c->ch.user.v_vec;
	referer = &c->ch.referer.v_vec;
	user_agent = &c->ch.useragent.v_vec;
	q1 = q2 = "\"";

	if (user->len == 0)
		user = &dash;

	if (referer->len == 0) {
		referer = &dash;
		q1 = "";
	}

	if (user_agent->len == 0) {
		user_agent = &dash;
		q2 = "";
	}

	(void) strftime(date, sizeof(date), "%d/%b/%Y:%H:%M:%S",
			localtime(&c->birth_time));

	buf = malloc(URI_MAX);

	(void) _shttpd_snprintf(buf, URI_MAX,
	    "%s - %.*s [%s %+05d] \"%s\" %d %lu %s%.*s%s %s%.*s%s",
	    inet_ntoa(c->sa.u.sin.sin_addr), user->len, user->ptr,
	    date, _shttpd_tz_offset, c->request ? c->request : "-",
	    c->status, (unsigned long) c->loc.io.total,
	    q1, referer->len, referer->ptr, q1,
	    q2, user_agent->len, user_agent->ptr, q2);

	(void) fprintf(fp, "%s\n", buf);
	(void) fflush(fp);

	free(buf);
}
