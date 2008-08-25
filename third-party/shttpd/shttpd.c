/*
 * Copyright (c) 2004-2005 Sergey Lyubka <valenok@gmail.com>
 * All rights reserved
 *
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Sergey Lyubka wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.
 */

/*
 * Small and portable HTTP server, http://shttpd.sourceforge.net
 * $Id: shttpd.c,v 1.57 2008/08/23 21:00:38 drozd Exp $
 */

#include "defs.h"

time_t	_shttpd_current_time;	/* Current UTC time		*/
int	_shttpd_tz_offset;	/* Time zone offset from UTC	*/
int	_shttpd_exit_flag;	/* Program exit flag		*/

const struct vec _shttpd_known_http_methods[] = {
	{"GET",		3},
	{"POST",	4},
	{"PUT",		3},
	{"DELETE",	6},
	{"HEAD",	4},
	{NULL,		0}
};

/*
 * This structure tells how HTTP headers must be parsed.
 * Used by parse_headers() function.
 */
#define	OFFSET(x)	offsetof(struct headers, x)
static const struct http_header http_headers[] = {
	{16, HDR_INT,	 OFFSET(cl),		"Content-Length: "	},
	{14, HDR_STRING, OFFSET(ct),		"Content-Type: "	},
	{12, HDR_STRING, OFFSET(useragent),	"User-Agent: "		},
	{19, HDR_DATE,	 OFFSET(ims),		"If-Modified-Since: "	},
	{15, HDR_STRING, OFFSET(auth),		"Authorization: "	},
	{9,  HDR_STRING, OFFSET(referer),	"Referer: "		},
	{8,  HDR_STRING, OFFSET(cookie),	"Cookie: "		},
	{10, HDR_STRING, OFFSET(location),	"Location: "		},
	{8,  HDR_INT,	 OFFSET(status),	"Status: "		},
	{7,  HDR_STRING, OFFSET(range),		"Range: "		},
	{12, HDR_STRING, OFFSET(connection),	"Connection: "		},
	{19, HDR_STRING, OFFSET(transenc),	"Transfer-Encoding: "	},
	{0,  HDR_INT,	 0,			NULL			}
};

struct shttpd_ctx *init_ctx(const char *config_file, int argc, char *argv[]);
static void process_connection(struct conn *, int, int);

int
_shttpd_is_true(const char *str)
{
	static const char	*trues[] = {"1", "yes", "true", "jawohl", NULL};
	const char		**p;

	for (p = trues; *p != NULL; p++)
		if (str && !strcmp(str, *p))
			return (TRUE);

	return (FALSE);
}

static void
free_list(struct llhead *head, void (*dtor)(struct llhead *))
{
	struct llhead	*lp, *tmp;

	LL_FOREACH_SAFE(head, lp, tmp) {
		LL_DEL(lp);
		dtor(lp);
	}
}

static void
listener_destructor(struct llhead *lp)
{
	struct listener	*listener = LL_ENTRY(lp, struct listener, link);

	(void) closesocket(listener->sock);
	free(listener);
}

static void
registered_uri_destructor(struct llhead *lp)
{
	struct registered_uri *ruri = LL_ENTRY(lp, struct registered_uri, link);

	free((void *) ruri->uri);
	free(ruri);
}

static void
acl_destructor(struct llhead *lp)
{
	struct acl	*acl = LL_ENTRY(lp, struct acl, link);
	free(acl);
}

int
_shttpd_url_decode(const char *src, int src_len, char *dst, int dst_len)
{
	int	i, j, a, b;
#define	HEXTOI(x)  (isdigit(x) ? x - '0' : x - 'W')

	for (i = j = 0; i < src_len && j < dst_len - 1; i++, j++)
		switch (src[i]) {
		case '%':
			if (isxdigit(((unsigned char *) src)[i + 1]) &&
			    isxdigit(((unsigned char *) src)[i + 2])) {
				a = tolower(((unsigned char *)src)[i + 1]);
				b = tolower(((unsigned char *)src)[i + 2]);
				dst[j] = (HEXTOI(a) << 4) | HEXTOI(b);
				i += 2;
			} else {
				dst[j] = '%';
			}
			break;
		default:
			dst[j] = src[i];
			break;
		}

	dst[j] = '\0';	/* Null-terminate the destination */

	return (j);
}

static const char *
is_alias(struct shttpd_ctx *ctx, const char *uri,
		struct vec *a_uri, struct vec *a_path)
{
	const char	*p, *s = ctx->options[OPT_ALIASES];
	size_t		len;

	DBG(("is_alias: aliases [%s]", s == NULL ? "" : s));

	FOR_EACH_WORD_IN_LIST(s, len) {

		if ((p = memchr(s, '=', len)) == NULL || p >= s + len || p == s)
			continue;

		if (memcmp(uri, s, p - s) == 0) {
			a_uri->ptr = s;
			a_uri->len = p - s;
			a_path->ptr = ++p;
			a_path->len = (s + len) - p;
			return (s);
		}
	}

	return (NULL);
}

void
_shttpd_stop_stream(struct stream *stream)
{
	if (stream->io_class != NULL && stream->io_class->close != NULL)
		stream->io_class->close(stream);

	stream->io_class= NULL;
	stream->flags |= FLAG_CLOSED;
	stream->flags &= ~(FLAG_R | FLAG_W | FLAG_ALWAYS_READY);

	DBG(("%d %s stopped. %lu of content data, %d now in a buffer",
	    stream->conn->rem.chan.sock, 
	    stream->io_class ? stream->io_class->name : "(null)",
	    (unsigned long) stream->io.total, (int) io_data_len(&stream->io)));
}

/*
 * Setup listening socket on given port, return socket
 */
static int
shttpd_open_listening_port(int port)
{
	int		sock, on = 1;
	struct usa	sa;

#ifdef _WIN32
	{WSADATA data;	WSAStartup(MAKEWORD(2,2), &data);}
#endif /* _WIN32 */

	sa.len				= sizeof(sa.u.sin);
	sa.u.sin.sin_family		= AF_INET;
	sa.u.sin.sin_port		= htons((uint16_t) port);
	sa.u.sin.sin_addr.s_addr	= htonl(INADDR_ANY);

	if ((sock = socket(PF_INET, SOCK_STREAM, 6)) == -1)
		goto fail;
	if (_shttpd_set_non_blocking_mode(sock) != 0)
		goto fail;
	if (setsockopt(sock, SOL_SOCKET,
	    SO_REUSEADDR,(char *) &on, sizeof(on)) != 0)
		goto fail;
	if (bind(sock, &sa.u.sa, sa.len) < 0)
		goto fail;
	if (listen(sock, 128) != 0)
		goto fail;

#ifndef _WIN32
	(void) fcntl(sock, F_SETFD, FD_CLOEXEC);
#endif /* !_WIN32 */

	return (sock);
fail:
	if (sock != -1)
		(void) closesocket(sock);
	_shttpd_elog(E_LOG, NULL, "open_listening_port(%d): %s", port, strerror(errno));
	return (-1);
}

/*
 * Check whether full request is buffered Return headers length, or 0
 */
int
_shttpd_get_headers_len(const char *buf, size_t buflen)
{
	const char	*s, *e;
	int		len = 0;

	for (s = buf, e = s + buflen - 1; len == 0 && s < e; s++)
		/* Control characters are not allowed but >=128 is. */
		if (!isprint(* (unsigned char *) s) && *s != '\r' &&
		    *s != '\n' && * (unsigned char *) s < 128)
			len = -1;
		else if (s[0] == '\n' && s[1] == '\n')
			len = s - buf + 2;
		else if (s[0] == '\n' && &s[1] < e &&
		    s[1] == '\r' && s[2] == '\n')
			len = s - buf + 3;

	return (len);
}

/*
 * Send error message back to a client.
 */
void
_shttpd_send_server_error(struct conn *c, int status, const char *reason)
{
	struct llhead		*lp;
	struct error_handler	*e;

	LL_FOREACH(&c->ctx->error_handlers, lp) {
		e = LL_ENTRY(lp, struct error_handler, link);

		if (e->code == status) {
			if (c->loc.io_class != NULL &&
			    c->loc.io_class->close != NULL)
				c->loc.io_class->close(&c->loc);
			io_clear(&c->loc.io);
			_shttpd_setup_embedded_stream(c,
			    e->callback, e->callback_data);
			return;
		}
	}

	io_clear(&c->loc.io);
	c->loc.io.head = _shttpd_snprintf(c->loc.io.buf, c->loc.io.size,
	    "HTTP/1.1 %d %s\r\n"
	    "Content-Type: text/plain\r\n"
	    "Content-Length: 12\r\n"
	    "\r\n"
	    "Error: %03d\r\n",
	    status, reason, status);
	c->loc.content_len = 10;
	c->status = status;
	_shttpd_stop_stream(&c->loc);
}

/*
 * Convert month to the month number. Return -1 on error, or month number
 */
static int
montoi(const char *s)
{
	static const char *ar[] = {
		"Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
	};
	size_t	i;

	for (i = 0; i < sizeof(ar) / sizeof(ar[0]); i++)
		if (!strcmp(s, ar[i]))
			return (i);

	return (-1);
}

/*
 * Parse date-time string, and return the corresponding time_t value
 */
static time_t
date_to_epoch(const char *s)
{
	struct tm	tm, *tmp;
	char		mon[32];
	int		sec, min, hour, mday, month, year;

	(void) memset(&tm, 0, sizeof(tm));
	sec = min = hour = mday = month = year = 0;

	if (((sscanf(s, "%d/%3s/%d %d:%d:%d",
	    &mday, mon, &year, &hour, &min, &sec) == 6) ||
	    (sscanf(s, "%d %3s %d %d:%d:%d",
	    &mday, mon, &year, &hour, &min, &sec) == 6) ||
	    (sscanf(s, "%*3s, %d %3s %d %d:%d:%d",
	    &mday, mon, &year, &hour, &min, &sec) == 6) ||
	    (sscanf(s, "%d-%3s-%d %d:%d:%d",
	    &mday, mon, &year, &hour, &min, &sec) == 6)) &&
	    (month = montoi(mon)) != -1) {
		tm.tm_mday	= mday;
		tm.tm_mon	= month;
		tm.tm_year	= year;
		tm.tm_hour	= hour;
		tm.tm_min	= min;
		tm.tm_sec	= sec;
	}

	if (tm.tm_year > 1900)
		tm.tm_year -= 1900;
	else if (tm.tm_year < 70)
		tm.tm_year += 100;

	/* Set Daylight Saving Time field */
	tmp = localtime(&_shttpd_current_time);
	tm.tm_isdst = tmp->tm_isdst;

	return (mktime(&tm));
}

static void
remove_double_dots(char *s)
{
	char	*p = s;

	while (*s != '\0') {
		*p++ = *s++;
		if (s[-1] == '/' || s[-1] == '\\')
			while (*s == '.' || *s == '/' || *s == '\\')
				s++;
	}
	*p = '\0';
}

void
_shttpd_parse_headers(const char *s, int len, struct headers *parsed)
{
	const struct http_header	*h;
	union variant			*v;
	const char			*p, *e = s + len;

	DBG(("parsing headers (len %d): [%.*s]", len, len, s));

	/* Loop through all headers in the request */
	while (s < e) {

		/* Find where this header ends */
		for (p = s; p < e && *p != '\n'; ) p++;

		/* Is this header known to us ? */
		for (h = http_headers; h->len != 0; h++)
			if (e - s > h->len &&
			    !_shttpd_strncasecmp(s, h->name, h->len))
				break;

		/* If the header is known to us, store its value */
		if (h->len != 0) {

			/* Shift to where value starts */
			s += h->len;

			/* Find place to store the value */
			v = (union variant *) ((char *) parsed + h->offset);

			/* Fetch header value into the connection structure */
			if (h->type == HDR_STRING) {
				v->v_vec.ptr = s;
				v->v_vec.len = p - s;
				if (p[-1] == '\r' && v->v_vec.len > 0)
					v->v_vec.len--;
			} else if (h->type == HDR_INT) {
				v->v_big_int = strtoul(s, NULL, 10);
			} else if (h->type == HDR_DATE) {
				v->v_time = date_to_epoch(s);
			}
		}

		s = p + 1;	/* Shift to the next header */
	}
}

static const struct {
	const char	*extension;
	int		ext_len;
	const char	*mime_type;
} builtin_mime_types[] = {
	{"html",	4,	"text/html"			},
	{"htm",		3,	"text/html"			},
	{"txt",		3,	"text/plain"			},
	{"css",		3,	"text/css"			},
	{"ico",		3,	"image/x-icon"			},
	{"gif",		3,	"image/gif"			},
	{"jpg",		3,	"image/jpeg"			},
	{"jpeg",	4,	"image/jpeg"			},
	{"png",		3,	"image/png"			},
	{"svg",		3,	"image/svg+xml"			},
	{"torrent",	7,	"application/x-bittorrent"	},
	{"wav",		3,	"audio/x-wav"			},
	{"mp3",		3,	"audio/x-mp3"			},
	{"mid",		3,	"audio/mid"			},
	{"m3u",		3,	"audio/x-mpegurl"		},
	{"ram",		3,	"audio/x-pn-realaudio"		},
	{"ra",		2,	"audio/x-pn-realaudio"		},
	{"doc",		3,	"application/msword",		},
	{"exe",		3,	"application/octet-stream"	},
	{"zip",		3,	"application/x-zip-compressed"	},
	{"xls",		3,	"application/excel"		},
	{"tgz",		3,	"application/x-tar-gz"		},
	{"tar.gz",	6,	"application/x-tar-gz"		},
	{"tar",		3,	"application/x-tar"		},
	{"gz",		2,	"application/x-gunzip"		},
	{"arj",		3,	"application/x-arj-compressed"	},
	{"rar",		3,	"application/x-arj-compressed"	},
	{"rtf",		3,	"application/rtf"		},
	{"pdf",		3,	"application/pdf"		},
	{"swf",		3,	"application/x-shockwave-flash"	},
	{"mpg",		3,	"video/mpeg"			},
	{"mpeg",	4,	"video/mpeg"			},
	{"asf",		3,	"video/x-ms-asf"		},
	{"avi",		3,	"video/x-msvideo"		},
	{"bmp",		3,	"image/bmp"			},
	{NULL,		0,	NULL				}
};

void
_shttpd_get_mime_type(struct shttpd_ctx *ctx,
		const char *uri, int len, struct vec *vec)
{
	const char	*eq, *p = ctx->options[OPT_MIME_TYPES];
	int		i, n, ext_len;

	/* Firt, loop through the custom mime types if any */
	FOR_EACH_WORD_IN_LIST(p, n) {
		if ((eq = memchr(p, '=', n)) == NULL || eq >= p + n || eq == p)
			continue;
		ext_len = eq - p;
		if (len > ext_len && uri[len - ext_len - 1] == '.' &&
		    !_shttpd_strncasecmp(p, &uri[len - ext_len], ext_len)) {
			vec->ptr = eq + 1;
			vec->len = p + n - vec->ptr;
			return;
		}
	}

	/* If no luck, try built-in mime types */
	for (i = 0; builtin_mime_types[i].extension != NULL; i++) {
		ext_len = builtin_mime_types[i].ext_len;
		if (len > ext_len && uri[len - ext_len - 1] == '.' &&
		    !_shttpd_strncasecmp(builtin_mime_types[i].extension,
			    &uri[len - ext_len], ext_len)) {
			vec->ptr = builtin_mime_types[i].mime_type;
			vec->len = strlen(vec->ptr);
			return;
		}
	}

	/* Oops. This extension is unknown to us. Fallback to text/plain */
	vec->ptr = "text/plain";
	vec->len = strlen(vec->ptr);
}

/*
 * For given directory path, substitute it to valid index file.
 * Return 0 if index file has been found, -1 if not found
 */
static int
find_index_file(struct conn *c, char *path, size_t maxpath, struct stat *stp)
{
	char		buf[FILENAME_MAX];
	const char	*s = c->ctx->options[OPT_INDEX_FILES];
	size_t		len;

	FOR_EACH_WORD_IN_LIST(s, len) {
		/* path must end with '/' character */
		_shttpd_snprintf(buf, sizeof(buf), "%s%.*s", path, len, s);
		if (_shttpd_stat(buf, stp) == 0) {
			_shttpd_strlcpy(path, buf, maxpath);
			_shttpd_get_mime_type(c->ctx, s, len, &c->mime_type);
			return (0);
		}
	}

	return (-1);
}

/*
 * Try to open requested file, return 0 if OK, -1 if error.
 * If the file is given arguments using PATH_INFO mechanism,
 * initialize pathinfo pointer.
 */
static int
get_path_info(struct conn *c, char *path, struct stat *stp)
{
	char	*p, *e;

	if (_shttpd_stat(path, stp) == 0)
		return (0);

	p = path + strlen(path);
	e = path + strlen(c->ctx->options[OPT_ROOT]) + 2;
	
	/* Strip directory parts of the path one by one */
	for (; p > e; p--)
		if (*p == '/') {
			*p = '\0';
			if (!_shttpd_stat(path, stp) && !S_ISDIR(stp->st_mode)) {
				c->path_info = p + 1;
				return (0);
			} else {
				*p = '/';
			}
		}

	return (-1);
}

static void
decide_what_to_do(struct conn *c)
{
	char		path[URI_MAX], buf[1024], *root;
	struct vec	alias_uri, alias_path;
	struct stat	st;
	int		rc;
	struct registered_uri	*ruri;

	DBG(("decide_what_to_do: [%s]", c->uri));

	if ((c->query = strchr(c->uri, '?')) != NULL)
		*c->query++ = '\0';

	_shttpd_url_decode(c->uri, strlen(c->uri), c->uri, strlen(c->uri) + 1);
	remove_double_dots(c->uri);
	
	root = c->ctx->options[OPT_ROOT];
	if (strlen(c->uri) + strlen(root) >= sizeof(path)) {
		_shttpd_send_server_error(c, 400, "URI is too long");
		return;
	}

	(void) _shttpd_snprintf(path, sizeof(path), "%s%s", root, c->uri);

	/* User may use the aliases - check URI for mount point */
	if (is_alias(c->ctx, c->uri, &alias_uri, &alias_path) != NULL) {
		(void) _shttpd_snprintf(path, sizeof(path), "%.*s%s",
		    alias_path.len, alias_path.ptr, c->uri + alias_uri.len);
		DBG(("using alias %.*s -> %.*s", alias_uri.len, alias_uri.ptr,
		    alias_path.len, alias_path.ptr));
	}

#if !defined(NO_AUTH)
	if (_shttpd_check_authorization(c, path) != 1) {
		_shttpd_send_authorization_request(c);
	} else
#endif /* NO_AUTH */
	if ((ruri = _shttpd_is_registered_uri(c->ctx, c->uri)) != NULL) {
		_shttpd_setup_embedded_stream(c,
		    ruri->callback, ruri->callback_data);
	} else
	if (strstr(path, HTPASSWD)) {
		/* Do not allow to view passwords files */
		_shttpd_send_server_error(c, 403, "Forbidden");
	} else
#if !defined(NO_AUTH)
	if ((c->method == METHOD_PUT || c->method == METHOD_DELETE) &&
	    (c->ctx->options[OPT_AUTH_PUT] == NULL ||
	     !_shttpd_is_authorized_for_put(c))) {
		_shttpd_send_authorization_request(c);
	} else
#endif /* NO_AUTH */
	if (c->method == METHOD_PUT) {
		c->status = _shttpd_stat(path, &st) == 0 ? 200 : 201;

		if (c->ch.range.v_vec.len > 0) {
			_shttpd_send_server_error(c, 501,
			    "PUT Range Not Implemented");
		} else if ((rc = _shttpd_put_dir(path)) == 0) {
			_shttpd_send_server_error(c, 200, "OK");
		} else if (rc == -1) {
			_shttpd_send_server_error(c, 500, "PUT Directory Error");
		} else if (c->rem.content_len == 0) {
			_shttpd_send_server_error(c, 411, "Length Required");
		} else if ((c->loc.chan.fd = _shttpd_open(path, O_WRONLY | O_BINARY |
		    O_CREAT | O_NONBLOCK | O_TRUNC, 0644)) == -1) {
			_shttpd_send_server_error(c, 500, "PUT Error");
		} else {
			DBG(("PUT file [%s]", c->uri));
			c->loc.io_class = &_shttpd_io_file;
			c->loc.flags |= FLAG_W | FLAG_ALWAYS_READY ;
		}
	} else if (c->method == METHOD_DELETE) {
		DBG(("DELETE [%s]", c->uri));
		if (_shttpd_remove(path) == 0)
			_shttpd_send_server_error(c, 200, "OK");
		else
			_shttpd_send_server_error(c, 500, "DELETE Error");
	} else if (get_path_info(c, path, &st) != 0) {
		_shttpd_send_server_error(c, 404, "Not Found");
	} else if (S_ISDIR(st.st_mode) && path[strlen(path) - 1] != '/') {
		(void) _shttpd_snprintf(buf, sizeof(buf),
			"Moved Permanently\r\nLocation: %s/", c->uri);
		_shttpd_send_server_error(c, 301, buf);
	} else if (S_ISDIR(st.st_mode) &&
	    find_index_file(c, path, sizeof(path) - 1, &st) == -1 &&
	    !IS_TRUE(c->ctx, OPT_DIR_LIST)) {
		_shttpd_send_server_error(c, 403, "Directory Listing Denied");
	} else if (S_ISDIR(st.st_mode) && IS_TRUE(c->ctx, OPT_DIR_LIST)) {
		if ((c->loc.chan.dir.path = _shttpd_strdup(path)) != NULL)
			_shttpd_get_dir(c);
		else
			_shttpd_send_server_error(c, 500, "GET Directory Error");
	} else if (S_ISDIR(st.st_mode) && !IS_TRUE(c->ctx, OPT_DIR_LIST)) {
		_shttpd_send_server_error(c, 403, "Directory listing denied");
#if !defined(NO_CGI)
	} else if (_shttpd_match_extension(path,
	    c->ctx->options[OPT_CGI_EXTENSIONS])) {
		if (c->method != METHOD_POST && c->method != METHOD_GET) {
			_shttpd_send_server_error(c, 501, "Bad method ");
		} else if ((_shttpd_run_cgi(c, path)) == -1) {
			_shttpd_send_server_error(c, 500, "Cannot exec CGI");
		} else {
			_shttpd_do_cgi(c);
		}
#endif /* NO_CGI */
#if !defined(NO_SSI)
	} else if (_shttpd_match_extension(path,
	    c->ctx->options[OPT_SSI_EXTENSIONS])) {
		if ((c->loc.chan.fd = _shttpd_open(path,
		    O_RDONLY | O_BINARY, 0644)) == -1) {
			_shttpd_send_server_error(c, 500, "SSI open error");
		} else {
			_shttpd_do_ssi(c);
		}
#endif /* NO_CGI */
	} else if (c->ch.ims.v_time && st.st_mtime <= c->ch.ims.v_time) {
		_shttpd_send_server_error(c, 304, "Not Modified");
	} else if ((c->loc.chan.fd = _shttpd_open(path,
	    O_RDONLY | O_BINARY, 0644)) != -1) {
		_shttpd_get_file(c, &st);
	} else {
		_shttpd_send_server_error(c, 500, "Internal Error");
	}
}

static int
set_request_method(struct conn *c)
{
	const struct vec	*v;

	/* Set the request method */
	for (v = _shttpd_known_http_methods; v->ptr != NULL; v++)
		if (!memcmp(c->rem.io.buf, v->ptr, v->len)) {
			c->method = v - _shttpd_known_http_methods;
			break;
		}

	return (v->ptr == NULL);
}

static void
parse_http_request(struct conn *c)
{
	char	*s, *e, *p, *start;
	int	uri_len, req_len, n;

	s = io_data(&c->rem.io);;
	req_len = c->rem.headers_len =
	    _shttpd_get_headers_len(s, io_data_len(&c->rem.io));

	if (req_len == 0 && io_space_len(&c->rem.io) == 0) {
		io_clear(&c->rem.io);
		_shttpd_send_server_error(c, 400, "Request is too big");
	}

	if (req_len == 0) {
		return;
	} else if (req_len < 16) {	/* Minimal: "GET / HTTP/1.0\n\n" */
		_shttpd_send_server_error(c, 400, "Bad request");
	} else if (set_request_method(c)) {
		_shttpd_send_server_error(c, 501, "Method Not Implemented");
	} else if ((c->request = _shttpd_strndup(s, req_len)) == NULL) {
		_shttpd_send_server_error(c, 500, "Cannot allocate request");
	}

	if (c->loc.flags & FLAG_CLOSED)
		return;

	io_inc_tail(&c->rem.io, req_len);

	DBG(("Conn %d: parsing request: [%.*s]", c->rem.chan.sock, req_len, s));
	c->rem.flags |= FLAG_HEADERS_PARSED;

	/* Set headers pointer. Headers follow the request line */
	c->headers = memchr(c->request, '\n', req_len);
	assert(c->headers != NULL);
	assert(c->headers < c->request + req_len);
	if (c->headers > c->request && c->headers[-1] == '\r')
		c->headers[-1] = '\0';
	*c->headers++ = '\0';

	/*
	 * Now make a copy of the URI, because it will be URL-decoded,
	 * and we need a copy of unmodified URI for the access log.
	 * First, we skip the REQUEST_METHOD and shift to the URI.
	 */
	for (p = c->request, e = p + req_len; *p != ' ' && p < e; p++);
	while (p < e && *p == ' ')
		p++;

	/* Now remember where URI starts, and shift to the end of URI */
	for (start = p; p < e && !isspace((unsigned char)*p); ) p++;
	uri_len = p - start;

	/* Skip space following the URI */
	while (p < e && *p == ' ')
		p++;

	/* Now comes the HTTP-Version in the form HTTP/<major>.<minor> */
	if (sscanf(p, "HTTP/%lu.%lu%n",
	    &c->major_version, &c->minor_version, &n) != 2 || p[n] != '\0') {
		_shttpd_send_server_error(c, 400, "Bad HTTP version");
	} else if (c->major_version > 1 ||
	    (c->major_version == 1 && c->minor_version > 1)) {
		_shttpd_send_server_error(c, 505, "HTTP version not supported");
	} else if (uri_len <= 0) {
		_shttpd_send_server_error(c, 400, "Bad URI");
	} else if ((c->uri = malloc(uri_len + 1)) == NULL) {
		_shttpd_send_server_error(c, 500, "Cannot allocate URI");
	} else {
		_shttpd_strlcpy(c->uri, (char *) start, uri_len + 1);
		_shttpd_parse_headers(c->headers,
		    (c->request + req_len) - c->headers, &c->ch);

		/* Remove the length of request from total, count only data */
		assert(c->rem.io.total >= (big_int_t) req_len);
		c->rem.io.total -= req_len;
		c->rem.content_len = c->ch.cl.v_big_int;
		decide_what_to_do(c);
	}
}

static void
add_socket(struct worker *worker, int sock, int is_ssl)
{
	struct shttpd_ctx	*ctx = worker->ctx;
	struct conn		*c;
	struct usa		sa;
	int			l = IS_TRUE(ctx, OPT_INETD) ? E_FATAL : E_LOG;
#if !defined(NO_SSL)
	SSL		*ssl = NULL;
#else
	is_ssl = is_ssl;	/* supress warnings */
#endif /* NO_SSL */

	sa.len = sizeof(sa.u.sin);
	(void) _shttpd_set_non_blocking_mode(sock);

	if (getpeername(sock, &sa.u.sa, &sa.len)) {
		_shttpd_elog(l, NULL, "add_socket: %s", strerror(errno));
#if !defined(NO_SSL)
	} else if (is_ssl && (ssl = SSL_new(ctx->ssl_ctx)) == NULL) {
		_shttpd_elog(l, NULL, "add_socket: SSL_new: %s", strerror(ERRNO));
		(void) closesocket(sock);
	} else if (is_ssl && SSL_set_fd(ssl, sock) == 0) {
		_shttpd_elog(l, NULL, "add_socket: SSL_set_fd: %s", strerror(ERRNO));
		(void) closesocket(sock);
		SSL_free(ssl);
#endif /* NO_SSL */
	} else if ((c = calloc(1, sizeof(*c) + 2 * URI_MAX)) == NULL) {
#if !defined(NO_SSL)
		if (ssl)
			SSL_free(ssl);
#endif /* NO_SSL */
		(void) closesocket(sock);
		_shttpd_elog(l, NULL, "add_socket: calloc: %s", strerror(ERRNO));
	} else {
		c->rem.conn	= c->loc.conn = c;
		c->ctx		= ctx;
		c->worker	= worker;
		c->sa		= sa;
		c->birth_time	= _shttpd_current_time;
		c->expire_time	= _shttpd_current_time + EXPIRE_TIME;

		(void) getsockname(sock, &sa.u.sa, &sa.len);
		c->loc_port = sa.u.sin.sin_port;

		_shttpd_set_close_on_exec(sock);

		c->loc.io_class	= NULL;
	
		c->rem.io_class	= &_shttpd_io_socket;
		c->rem.chan.sock = sock;

		/* Set IO buffers */
		c->loc.io.buf	= (char *) (c + 1);
		c->rem.io.buf	= c->loc.io.buf + URI_MAX;
		c->loc.io.size	= c->rem.io.size = URI_MAX;

#if !defined(NO_SSL)
		if (is_ssl) {
			c->rem.io_class	= &_shttpd_io_ssl;
			c->rem.chan.ssl.sock = sock;
			c->rem.chan.ssl.ssl = ssl;
			_shttpd_ssl_handshake(&c->rem);
		}
#endif /* NO_SSL */

		LL_TAIL(&worker->connections, &c->link);
		worker->num_conns++;
		
		DBG(("%s:%hu connected (socket %d)",
		    inet_ntoa(* (struct in_addr *) &sa.u.sin.sin_addr.s_addr),
		    ntohs(sa.u.sin.sin_port), sock));
	}
}

static struct worker *
first_worker(struct shttpd_ctx *ctx)
{
	return (LL_ENTRY(ctx->workers.next, struct worker, link));
}

static void
pass_socket(struct shttpd_ctx *ctx, int sock, int is_ssl)
{
	struct llhead	*lp;
	struct worker	*worker, *lazy;
	int		buf[3];

	lazy = first_worker(ctx);

	/* Find least busy worker */
	LL_FOREACH(&ctx->workers, lp) {
		worker = LL_ENTRY(lp, struct worker, link);
		if (worker->num_conns < lazy->num_conns)
			lazy = worker;
	}

	buf[0] = CTL_PASS_SOCKET;
	buf[1] = sock;
	buf[2] = is_ssl;

	(void) send(lazy->ctl[1], (void *) buf, sizeof(buf), 0);
}

static int
set_ports(struct shttpd_ctx *ctx, const char *p)
{
	int		sock, len, is_ssl, port;
	struct listener	*l;


	free_list(&ctx->listeners, &listener_destructor);

	FOR_EACH_WORD_IN_LIST(p, len) {

		is_ssl	= p[len - 1] == 's' ? 1 : 0;
		port	= atoi(p);

		if ((sock = shttpd_open_listening_port(port)) == -1) {
			_shttpd_elog(E_LOG, NULL, "cannot open port %d", port);
			goto fail;
		} else if (is_ssl && ctx->ssl_ctx == NULL) {
			(void) closesocket(sock);
			_shttpd_elog(E_LOG, NULL, "cannot add SSL socket, "
			    "please specify certificate file");
			goto fail;
		} else if ((l = calloc(1, sizeof(*l))) == NULL) {
			(void) closesocket(sock);
			_shttpd_elog(E_LOG, NULL, "cannot allocate listener");
			goto fail;
		} else {
			l->is_ssl = is_ssl;
			l->sock	= sock;
			l->ctx	= ctx;
			LL_TAIL(&ctx->listeners, &l->link);
			DBG(("shttpd_listen: added socket %d", sock));
		}
	}

	return (TRUE);
fail:
	free_list(&ctx->listeners, &listener_destructor);
	return (FALSE);
}

static void
read_stream(struct stream *stream)
{
	int	n, len;

	len = io_space_len(&stream->io);
	assert(len > 0);

	/* Do not read more that needed */
	if (stream->content_len > 0 &&
	    stream->io.total + len > stream->content_len)
		len = stream->content_len - stream->io.total;

	/* Read from underlying channel */
	assert(stream->io_class != NULL);
	n = stream->io_class->read(stream, io_space(&stream->io), len);

	if (n > 0)
		io_inc_head(&stream->io, n);
	else if (n == -1 && (ERRNO == EINTR || ERRNO == EWOULDBLOCK))
		n = n;	/* Ignore EINTR and EAGAIN */
	else if (!(stream->flags & FLAG_DONT_CLOSE))
		_shttpd_stop_stream(stream);

	DBG(("read_stream (%d %s): read %d/%d/%lu bytes (errno %d)",
	    stream->conn->rem.chan.sock,
	    stream->io_class ? stream->io_class->name : "(null)",
	    n, len, (unsigned long) stream->io.total, ERRNO));

	/*
	 * Close the local stream if everything was read
	 * XXX We do not close the remote stream though! It may be
	 * a POST data completed transfer, we do not want the socket
	 * to be closed.
	 */
	if (stream->content_len > 0 && stream == &stream->conn->loc) {
		assert(stream->io.total <= stream->content_len);
		if (stream->io.total == stream->content_len)
			_shttpd_stop_stream(stream);
	}

	stream->conn->expire_time = _shttpd_current_time + EXPIRE_TIME;
}

static void
write_stream(struct stream *from, struct stream *to)
{
	int	n, len;

	len = io_data_len(&from->io);
	assert(len > 0);

	/* TODO: should be assert on CAN_WRITE flag */
	n = to->io_class->write(to, io_data(&from->io), len);
	to->conn->expire_time = _shttpd_current_time + EXPIRE_TIME;
	DBG(("write_stream (%d %s): written %d/%d bytes (errno %d)",
	    to->conn->rem.chan.sock,
	    to->io_class ? to->io_class->name : "(null)", n, len, ERRNO));

	if (n > 0)
		io_inc_tail(&from->io, n);
	else if (n == -1 && (ERRNO == EINTR || ERRNO == EWOULDBLOCK))
		n = n;	/* Ignore EINTR and EAGAIN */
	else if (!(to->flags & FLAG_DONT_CLOSE))
		_shttpd_stop_stream(to);
}


static void
connection_desctructor(struct llhead *lp)
{
	struct conn		*c = LL_ENTRY(lp, struct conn, link);
	static const struct vec	vec = {"close", 5};
	int			do_close;

	DBG(("Disconnecting %d (%.*s)", c->rem.chan.sock,
	    c->ch.connection.v_vec.len, c->ch.connection.v_vec.ptr));

	if (c->request != NULL && c->ctx->access_log != NULL)
		_shttpd_log_access(c->ctx->access_log, c);

	/* In inetd mode, exit if request is finished. */
	if (IS_TRUE(c->ctx, OPT_INETD))
		exit(0);

	if (c->loc.io_class != NULL && c->loc.io_class->close != NULL)
		c->loc.io_class->close(&c->loc);

	/*
	 * Check the "Connection: " header before we free c->request
	 * If it its 'keep-alive', then do not close the connection
	 */
	do_close = (c->ch.connection.v_vec.len >= vec.len &&
	    !_shttpd_strncasecmp(vec.ptr,c->ch.connection.v_vec.ptr,vec.len)) ||
	    (c->major_version < 1 ||
	    (c->major_version >= 1 && c->minor_version < 1));

	if (c->request)
		free(c->request);
	if (c->uri)
		free(c->uri);

	/* Keep the connection open only if we have Content-Length set */
	if (!do_close && c->loc.content_len > 0) {
		c->loc.io_class = NULL;
		c->loc.flags = 0;
		c->loc.content_len = 0;
		c->rem.flags = FLAG_W | FLAG_R | FLAG_SSL_ACCEPTED;
		c->query = c->request = c->uri = c->path_info = NULL;
		c->mime_type.len = 0;
		(void) memset(&c->ch, 0, sizeof(c->ch));
		io_clear(&c->loc.io);
		c->birth_time = _shttpd_current_time;
		if (io_data_len(&c->rem.io) > 0)
			process_connection(c, 0, 0);
	} else {
		if (c->rem.io_class != NULL)
			c->rem.io_class->close(&c->rem);

		LL_DEL(&c->link);
		c->worker->num_conns--;
		assert(c->worker->num_conns >= 0);

		free(c);
	}
}

static void
worker_destructor(struct llhead *lp)
{
	struct worker	*worker = LL_ENTRY(lp, struct worker, link);

	free_list(&worker->connections, connection_desctructor);
	free(worker);
}

static int
is_allowed(const struct shttpd_ctx *ctx, const struct usa *usa)
{
	const struct acl	*acl;
	const struct llhead	*lp;
	int			allowed = '+';
	uint32_t		ip;

	LL_FOREACH(&ctx->acl, lp) {
		acl = LL_ENTRY(lp, struct acl, link);
		(void) memcpy(&ip, &usa->u.sin.sin_addr, sizeof(ip));
		if (acl->ip == (ntohl(ip) & acl->mask))
			allowed = acl->flag;
	}

	return (allowed == '+');
}

static void
add_to_set(int fd, fd_set *set, int *max_fd)
{
	FD_SET(fd, set);
	if (fd > *max_fd)
		*max_fd = fd;
}

static void
process_connection(struct conn *c, int remote_ready, int local_ready)
{
	/* Read from remote end if it is ready */
	if (remote_ready && io_space_len(&c->rem.io))
		read_stream(&c->rem);

	/* If the request is not parsed yet, do so */
	if (!(c->rem.flags & FLAG_HEADERS_PARSED))
		parse_http_request(c);

	DBG(("loc: %d [%.*s]", (int) io_data_len(&c->loc.io),
	    (int) io_data_len(&c->loc.io), io_data(&c->loc.io)));
	DBG(("rem: %d [%.*s]", (int) io_data_len(&c->rem.io),
	    (int) io_data_len(&c->rem.io), io_data(&c->rem.io)));

	/* Read from the local end if it is ready */
	if (local_ready && io_space_len(&c->loc.io))
		read_stream(&c->loc);

	if (io_data_len(&c->rem.io) > 0 && (c->loc.flags & FLAG_W) &&
	    c->loc.io_class != NULL && c->loc.io_class->write != NULL)
		write_stream(&c->rem, &c->loc);

	if (io_data_len(&c->loc.io) > 0 && c->rem.io_class != NULL)
		write_stream(&c->loc, &c->rem); 

	/* Check whether we should close this connection */
	if ((_shttpd_current_time > c->expire_time) ||
	    (c->rem.flags & FLAG_CLOSED) ||
	    ((c->loc.flags & FLAG_CLOSED) && !io_data_len(&c->loc.io)))
		connection_desctructor(&c->link);
}

static int
num_workers(const struct shttpd_ctx *ctx)
{
	char	*p = ctx->options[OPT_THREADS];
	return (p ? atoi(p) : 1);
}

static void
handle_connected_socket(struct shttpd_ctx *ctx,
		struct usa *sap, int sock, int is_ssl)
{
#if !defined(_WIN32)
	if (sock >= (int) FD_SETSIZE) {
		_shttpd_elog(E_LOG, NULL, "ctx %p: discarding "
		    "socket %d, too busy", ctx, sock);
		(void) closesocket(sock);
	} else
#endif /* !_WIN32 */
		if (!is_allowed(ctx, sap)) {
		_shttpd_elog(E_LOG, NULL, "%s is not allowed to connect",
		    inet_ntoa(sap->u.sin.sin_addr));
		(void) closesocket(sock);
	} else if (num_workers(ctx) > 1) {
		pass_socket(ctx, sock, is_ssl);
	} else {
		add_socket(first_worker(ctx), sock, is_ssl);
	}
}

static int
do_select(int max_fd, fd_set *read_set, fd_set *write_set, int milliseconds)
{
	struct timeval	tv;
	int		n;

	tv.tv_sec = milliseconds / 1000;
	tv.tv_usec = (milliseconds % 1000) * 1000;

	/* Check IO readiness */
	if ((n = select(max_fd + 1, read_set, write_set, NULL, &tv)) < 0) {
#ifdef _WIN32
		/*
		 * On windows, if read_set and write_set are empty,
		 * select() returns "Invalid parameter" error
		 * (at least on my Windows XP Pro). So in this case,
		 * we sleep here.
		 */
		Sleep(milliseconds);
#endif /* _WIN32 */
		DBG(("select: %d", ERRNO));
	}

	return (n);
}

static int
multiplex_worker_sockets(const struct worker *worker, int *max_fd,
		fd_set *read_set, fd_set *write_set)
{
	struct llhead	*lp;
	struct conn	*c;
	int		nowait = FALSE;

	/* Add control socket */
	add_to_set(worker->ctl[0], read_set, max_fd);

	/* Multiplex streams */
	LL_FOREACH(&worker->connections, lp) {
		c = LL_ENTRY(lp, struct conn, link);
		
		/* If there is a space in remote IO, check remote socket */
		if (io_space_len(&c->rem.io))
			add_to_set(c->rem.chan.fd, read_set, max_fd);

#if !defined(NO_CGI)
		/*
		 * If there is a space in local IO, and local endpoint is
		 * CGI, check local socket for read availability
		 */
		if (io_space_len(&c->loc.io) && (c->loc.flags & FLAG_R) &&
		    c->loc.io_class == &_shttpd_io_cgi)
			add_to_set(c->loc.chan.fd, read_set, max_fd);

		/*
		 * If there is some data read from remote socket, and
		 * local endpoint is CGI, check local for write availability
		 */
		if (io_data_len(&c->rem.io) && (c->loc.flags & FLAG_W) &&
		    c->loc.io_class == &_shttpd_io_cgi)
			add_to_set(c->loc.chan.fd, write_set, max_fd);
#endif /* NO_CGI */

		/*
		 * If there is some data read from local endpoint, check the
		 * remote socket for write availability
		 */
		if (io_data_len(&c->loc.io) && !(c->loc.flags & FLAG_SUSPEND))
			add_to_set(c->rem.chan.fd, write_set, max_fd);

		/*
		 * Set select wait interval to zero if FLAG_ALWAYS_READY set
		 */
		if (io_space_len(&c->loc.io) && (c->loc.flags & FLAG_R) &&
		    (c->loc.flags & FLAG_ALWAYS_READY))
			nowait = TRUE;
		
		if (io_data_len(&c->rem.io) && (c->loc.flags & FLAG_W) &&
		    (c->loc.flags & FLAG_ALWAYS_READY))
			nowait = TRUE;
	}

	return (nowait);
}

int
shttpd_join(struct shttpd_ctx *ctx,
		fd_set *read_set, fd_set *write_set, int *max_fd)
{
	struct llhead	*lp;
	struct listener	*l;
	int		nowait = FALSE;

	/* Add listening sockets to the read set */
	LL_FOREACH(&ctx->listeners, lp) {
		l = LL_ENTRY(lp, struct listener, link);
		add_to_set(l->sock, read_set, max_fd);
		DBG(("FD_SET(%d) (listening)", l->sock));
	}

	if (num_workers(ctx) == 1)
		nowait = multiplex_worker_sockets(first_worker(ctx), max_fd,
		    read_set, write_set);

	return (nowait);
}


static void
process_worker_sockets(struct worker *worker, fd_set *read_set)
{
	struct llhead	*lp, *tmp;
	int		cmd, skt[2], sock = worker->ctl[0];
	struct conn	*c;

	/* Check if new socket is passed to us over the control socket */
	if (FD_ISSET(worker->ctl[0], read_set))
		while (recv(sock, (void *) &cmd, sizeof(cmd), 0) == sizeof(cmd))
			switch (cmd) {
			case CTL_PASS_SOCKET:
				(void)recv(sock, (void *) &skt, sizeof(skt), 0);
				add_socket(worker, skt[0], skt[1]);
				break;
			case CTL_WAKEUP:
				(void)recv(sock, (void *) &c, sizeof(c), 0);
				c->loc.flags &= FLAG_SUSPEND;
				break;
			default:
				_shttpd_elog(E_FATAL, NULL, "ctx %p: ctl cmd %d",
				    worker->ctx, cmd);
				break;
			}

	/* Process all connections */
	LL_FOREACH_SAFE(&worker->connections, lp, tmp) {
		c = LL_ENTRY(lp, struct conn, link);
		process_connection(c, FD_ISSET(c->rem.chan.sock, read_set),
		    c->loc.io_class != NULL &&
		    ((c->loc.flags & FLAG_ALWAYS_READY)
#if !defined(NO_CGI)
		    || (c->loc.io_class == &_shttpd_io_cgi &&
		     FD_ISSET(c->loc.chan.fd, read_set))
#endif /* NO_CGI */
		    ));
	}
}

/*
 * One iteration of server loop. This is the core of the data exchange.
 */
void
shttpd_poll(struct shttpd_ctx *ctx, int milliseconds)
{
	struct llhead	*lp;
	struct listener	*l;
	fd_set		read_set, write_set;
	int		sock, max_fd = -1;
	struct usa	sa;

	_shttpd_current_time = time(0);
	FD_ZERO(&read_set);
	FD_ZERO(&write_set);

	if (shttpd_join(ctx, &read_set, &write_set, &max_fd))
		milliseconds = 0;

	if (do_select(max_fd, &read_set, &write_set, milliseconds) < 0)
		return;;

	/* Check for incoming connections on listener sockets */
	LL_FOREACH(&ctx->listeners, lp) {
		l = LL_ENTRY(lp, struct listener, link);
		if (!FD_ISSET(l->sock, &read_set))
			continue;
		do {
			sa.len = sizeof(sa.u.sin);
			if ((sock = accept(l->sock, &sa.u.sa, &sa.len)) != -1)
				handle_connected_socket(ctx,&sa,sock,l->is_ssl);
		} while (sock != -1);
	}

	if (num_workers(ctx) == 1)
		process_worker_sockets(first_worker(ctx), &read_set);
}

/*
 * Deallocate shttpd object, free up the resources
 */
void
shttpd_fini(struct shttpd_ctx *ctx)
{
	size_t	i;

	free_list(&ctx->workers, worker_destructor);
	free_list(&ctx->registered_uris, registered_uri_destructor);
	free_list(&ctx->acl, acl_destructor);
	free_list(&ctx->listeners, listener_destructor);
#if !defined(NO_SSI)
	free_list(&ctx->ssi_funcs, _shttpd_ssi_func_destructor);
#endif /* !NO_SSI */

	for (i = 0; i < NELEMS(ctx->options); i++)
		if (ctx->options[i] != NULL)
			free(ctx->options[i]);

	if (ctx->access_log)		(void) fclose(ctx->access_log);
	if (ctx->error_log)		(void) fclose(ctx->error_log);

	/* TODO: free SSL context */

	free(ctx);
}

/*
 * UNIX socketpair() implementation. Why? Because Windows does not have it.
 * Return 0 on success, -1 on error.
 */
int
shttpd_socketpair(int sp[2])
{
	struct sockaddr_in	sa;
	int			sock, ret = -1;
	socklen_t		len = sizeof(sa);

	sp[0] = sp[1] = -1;

	(void) memset(&sa, 0, sizeof(sa));
	sa.sin_family 		= AF_INET;
	sa.sin_port		= htons(0);
	sa.sin_addr.s_addr	= htonl(INADDR_LOOPBACK);

	if ((sock = socket(AF_INET, SOCK_STREAM, 0)) != -1 &&
	    !bind(sock, (struct sockaddr *) &sa, len) &&
	    !listen(sock, 1) &&
	    !getsockname(sock, (struct sockaddr *) &sa, &len) &&
	    (sp[0] = socket(AF_INET, SOCK_STREAM, 6)) != -1 &&
	    !connect(sp[0], (struct sockaddr *) &sa, len) &&
	    (sp[1] = accept(sock,(struct sockaddr *) &sa, &len)) != -1) {

		/* Success */
		ret = 0;
	} else {

		/* Failure, close descriptors */
		if (sp[0] != -1)
			(void) closesocket(sp[0]);
		if (sp[1] != -1)
			(void) closesocket(sp[1]);
	}

	(void) closesocket(sock);
	(void) _shttpd_set_non_blocking_mode(sp[0]);
	(void) _shttpd_set_non_blocking_mode(sp[1]);

#ifndef _WIN32
	(void) fcntl(sp[0], F_SETFD, FD_CLOEXEC);
	(void) fcntl(sp[1], F_SETFD, FD_CLOEXEC);
#endif /* _WIN32*/

	return (ret);
}

static int isbyte(int n) { return (n >= 0 && n <= 255); }

static int
set_inetd(struct shttpd_ctx *ctx, const char *flag)
{
	ctx = NULL; /* Unused */

	if (_shttpd_is_true(flag)) {
		shttpd_set_option(ctx, "ports", NULL);
		(void) freopen("/dev/null", "a", stderr);
		add_socket(first_worker(ctx), 0, 0);
	}

	return (TRUE);
}

static int
set_uid(struct shttpd_ctx *ctx, const char *uid)
{
	struct passwd	*pw;

	ctx = NULL; /* Unused */

#if !defined(_WIN32)
	if ((pw = getpwnam(uid)) == NULL)
		_shttpd_elog(E_FATAL, 0, "%s: unknown user [%s]", __func__, uid);
	else if (setgid(pw->pw_gid) == -1)
		_shttpd_elog(E_FATAL, NULL, "%s: setgid(%s): %s",
		    __func__, uid, strerror(errno));
	else if (setuid(pw->pw_uid) == -1)
		_shttpd_elog(E_FATAL, NULL, "%s: setuid(%s): %s",
		    __func__, uid, strerror(errno));
#endif /* !_WIN32 */
	return (TRUE);
}

static int
set_acl(struct shttpd_ctx *ctx, const char *s)
{
	struct acl	*acl = NULL;
	char		flag;
	int		len, a, b, c, d, n, mask;

	/* Delete the old ACLs if any */
	free_list(&ctx->acl, acl_destructor);

	FOR_EACH_WORD_IN_LIST(s, len) {

		mask = 32;

		if (sscanf(s, "%c%d.%d.%d.%d%n",&flag,&a,&b,&c,&d,&n) != 5) {
			_shttpd_elog(E_FATAL, NULL, "[%s]: subnet must be "
			    "[+|-]x.x.x.x[/x]", s);
		} else if (flag != '+' && flag != '-') {
			_shttpd_elog(E_FATAL, NULL, "flag must be + or -: [%s]", s);
		} else if (!isbyte(a)||!isbyte(b)||!isbyte(c)||!isbyte(d)) {
			_shttpd_elog(E_FATAL, NULL, "bad ip address: [%s]", s);
		} else	if ((acl = malloc(sizeof(*acl))) == NULL) {
			_shttpd_elog(E_FATAL, NULL, "%s", "cannot malloc subnet");
		} else if (sscanf(s + n, "/%d", &mask) == 0) { 
			/* Do nothing, no mask specified */
		} else if (mask < 0 || mask > 32) {
			_shttpd_elog(E_FATAL, NULL, "bad subnet mask: %d [%s]", n, s);
		}

		acl->ip = (a << 24) | (b << 16) | (c << 8) | d;
		acl->mask = mask ? 0xffffffffU << (32 - mask) : 0;
		acl->flag = flag;
		LL_TAIL(&ctx->acl, &acl->link);
	}

	return (TRUE);
}

#ifndef NO_SSL
/*
 * Dynamically load SSL library. Set up ctx->ssl_ctx pointer.
 */
static int
set_ssl(struct shttpd_ctx *ctx, const char *pem)
{
	SSL_CTX		*CTX;
	void		*lib;
	struct ssl_func	*fp;
	int		retval = FALSE;

	/* Load SSL library dynamically */
	if ((lib = dlopen(SSL_LIB, RTLD_LAZY)) == NULL) {
		_shttpd_elog(E_LOG, NULL, "set_ssl: cannot load %s", SSL_LIB);
		return (FALSE);
	}

	for (fp = ssl_sw; fp->name != NULL; fp++)
		if ((fp->ptr.v_void = dlsym(lib, fp->name)) == NULL) {
			_shttpd_elog(E_LOG, NULL,"set_ssl: cannot find %s", fp->name);
			return (FALSE);
		}

	/* Initialize SSL crap */
	SSL_library_init();

	if ((CTX = SSL_CTX_new(SSLv23_server_method())) == NULL)
		_shttpd_elog(E_LOG, NULL, "SSL_CTX_new error");
	else if (SSL_CTX_use_certificate_file(CTX, pem, SSL_FILETYPE_PEM) == 0)
		_shttpd_elog(E_LOG, NULL, "cannot open %s", pem);
	else if (SSL_CTX_use_PrivateKey_file(CTX, pem, SSL_FILETYPE_PEM) == 0)
		_shttpd_elog(E_LOG, NULL, "cannot open %s", pem);
	else
		retval = TRUE;

	ctx->ssl_ctx = CTX;

	return (retval);
}
#endif /* NO_SSL */

static int
open_log_file(FILE **fpp, const char *path)
{
	int	retval = TRUE;

	if (*fpp != NULL)
		(void) fclose(*fpp);

	if (path == NULL) {
		*fpp = NULL;
	} else if ((*fpp = fopen(path, "a")) == NULL) {
		_shttpd_elog(E_LOG, NULL, "cannot open log file %s: %s",
		    path, strerror(errno));
		retval = FALSE;
	}

	return (retval);
}

static int set_alog(struct shttpd_ctx *ctx, const char *path) {
	return (open_log_file(&ctx->access_log, path));
}

static int set_elog(struct shttpd_ctx *ctx, const char *path) {
	return (open_log_file(&ctx->error_log, path));
}

static void show_cfg_page(struct shttpd_arg *arg);

static int
set_cfg_uri(struct shttpd_ctx *ctx, const char *uri)
{
	free_list(&ctx->registered_uris, &registered_uri_destructor);

	if (uri != NULL)
		shttpd_register_uri(ctx, uri, &show_cfg_page, ctx);

	return (TRUE);
}

static struct worker *
add_worker(struct shttpd_ctx *ctx)
{
	struct worker	*worker;

	if ((worker = calloc(1, sizeof(*worker))) == NULL)
		_shttpd_elog(E_FATAL, NULL, "Cannot allocate worker");
	LL_INIT(&worker->connections);
	worker->ctx = ctx;
	(void) shttpd_socketpair(worker->ctl);
	LL_TAIL(&ctx->workers, &worker->link);

	return (worker);
}

#if !defined(NO_THREADS)
static void
poll_worker(struct worker *worker, int milliseconds)
{
	fd_set		read_set, write_set;
	int		max_fd = -1;

	FD_ZERO(&read_set);
	FD_ZERO(&write_set);

	if (multiplex_worker_sockets(worker, &max_fd, &read_set, &write_set))
		milliseconds = 0;

	if (do_select(max_fd, &read_set, &write_set, milliseconds) < 0)
		return;;

	process_worker_sockets(worker, &read_set);
}

static void
worker_function(void *param)
{
	struct worker *worker = param;

	while (worker->exit_flag == 0)
		poll_worker(worker, 1000 * 10);

	free_list(&worker->connections, connection_desctructor);
	free(worker);
}

static int
set_workers(struct shttpd_ctx *ctx, const char *value)
{
	int		new_num, old_num;
	struct llhead	*lp, *tmp;
	struct worker	*worker;

       	new_num = atoi(value);
	old_num = 0;
	LL_FOREACH(&ctx->workers, lp)
		old_num++;

	if (new_num == 1) {
		if (old_num > 1)
			/* Stop old threads */
			LL_FOREACH_SAFE(&ctx->workers, lp, tmp) {
				worker = LL_ENTRY(lp, struct worker, link);
				LL_DEL(&worker->link);
				worker = LL_ENTRY(lp, struct worker, link);
				worker->exit_flag = 1;
			}
		(void) add_worker(ctx);
	} else {
		/* FIXME: we cannot here reduce the number of threads */
		while (new_num > 1 && new_num > old_num) {
			worker = add_worker(ctx);
			_beginthread(worker_function, 0, worker);
			old_num++;
		}
	}

	return (TRUE);
}
#endif /* NO_THREADS */

static const struct opt {
	int		index;		/* Index in shttpd_ctx		*/
	const char	*name;		/* Option name in config file	*/
	const char	*description;	/* Description			*/
	const char	*default_value;	/* Default option value		*/
	int (*setter)(struct shttpd_ctx *, const char *);
} known_options[] = {
	{OPT_ROOT, "root", "\tWeb root directory", ".", NULL},
	{OPT_INDEX_FILES, "index_files", "Index files", INDEX_FILES, NULL},
#ifndef NO_SSL
	{OPT_SSL_CERTIFICATE, "ssl_cert", "SSL certificate file", NULL,set_ssl},
#endif /* NO_SSL */
	{OPT_PORTS, "ports", "Listening ports", LISTENING_PORTS, set_ports},
	{OPT_DIR_LIST, "dir_list", "Directory listing", "yes", NULL},
	{OPT_CFG_URI, "cfg_uri", "Config uri", NULL, set_cfg_uri},
	{OPT_PROTECT, "protect", "URI to htpasswd mapping", NULL, NULL},
#ifndef NO_CGI
	{OPT_CGI_EXTENSIONS, "cgi_ext", "CGI extensions", CGI_EXT, NULL},
	{OPT_CGI_INTERPRETER, "cgi_interp", "CGI interpreter", NULL, NULL},
	{OPT_CGI_ENVIRONMENT, "cgi_env", "Additional CGI env vars", NULL, NULL},
#endif /* NO_CGI */
	{OPT_SSI_EXTENSIONS, "ssi_ext",	"SSI extensions", SSI_EXT, NULL},
#ifndef NO_AUTH
	{OPT_AUTH_REALM, "auth_realm", "Authentication domain name",REALM,NULL},
	{OPT_AUTH_GPASSWD, "auth_gpass", "Global passwords file", NULL, NULL},
	{OPT_AUTH_PUT, "auth_PUT", "PUT,DELETE auth file", NULL, NULL},
#endif /* !NO_AUTH */
#ifdef _WIN32
	{OPT_SERVICE, "service", "Manage WinNNT service (install"
	    "|uninstall)", NULL, _shttpd_set_nt_service},
	{OPT_HIDE, "systray", "Hide console, show icon on systray",
		"no", _shttpd_set_systray},
#else
	{OPT_INETD, "inetd", "Inetd mode", "no", set_inetd},
	{OPT_UID, "uid", "\tRun as user", NULL, set_uid},
#endif /* _WIN32 */
	{OPT_ACCESS_LOG, "access_log", "Access log file", NULL, set_alog},
	{OPT_ERROR_LOG, "error_log", "Error log file", NULL, set_elog},
	{OPT_MIME_TYPES, "mime_types", "Additional mime types list", NULL,NULL},
	{OPT_ALIASES, "aliases", "Path=URI mappings", NULL, NULL},
	{OPT_ACL, "acl", "\tAllow/deny IP addresses/subnets", NULL, set_acl},
#if !defined(NO_THREADS)
	{OPT_THREADS, "threads", "Number of worker threads", "1", set_workers},
#endif /* !NO_THREADS */
	{-1, NULL, NULL, NULL, NULL}
};

static const struct opt *
find_opt(const char *opt_name)
{
	int	i;

	for (i = 0; known_options[i].name != NULL; i++)
		if (!strcmp(opt_name, known_options[i].name))
			return (known_options + i);

	_shttpd_elog(E_FATAL, NULL, "no such option: [%s]", opt_name);

	/* UNREACHABLE */
	return (NULL);
}

int
shttpd_set_option(struct shttpd_ctx *ctx, const char *opt, const char *val)
{
	const struct opt	*o = find_opt(opt);
	int			retval = TRUE;

	/* Call option setter first, so it can use both new and old values */
	if (o->setter != NULL)
		retval = o->setter(ctx, val);

	/* Free old value if any */
	if (ctx->options[o->index] != NULL)
		free(ctx->options[o->index]);
	
	/* Set new option value */
	ctx->options[o->index] = val ? _shttpd_strdup(val) : NULL;

	return (retval);
}

static void
show_cfg_page(struct shttpd_arg *arg)
{
	struct shttpd_ctx	*ctx = arg->user_data;
	char			opt_name[20], value[BUFSIZ];
	const struct opt	*o;

	opt_name[0] = value[0] = '\0';

	if (!strcmp(shttpd_get_env(arg, "REQUEST_METHOD"), "POST")) {
		if (arg->flags & SHTTPD_MORE_POST_DATA)
			return;
		(void) shttpd_get_var("o", arg->in.buf, arg->in.len,
		    opt_name, sizeof(opt_name));
		(void) shttpd_get_var("v", arg->in.buf, arg->in.len,
		    value, sizeof(value));
		shttpd_set_option(ctx, opt_name, value[0] ? value : NULL);
	}

	shttpd_printf(arg, "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
	    "<html><body><h1>SHTTPD v. %s</h1>", shttpd_version());

	shttpd_printf(arg, "%s", "<table border=1"
	    "<tr><th>Option</th><th>Description</th>"
	    "<th colspan=2>Value</th></tr>");

	if (opt_name[0] != '\0' && value[0] != '\0')
		shttpd_printf(arg, "<p style='color: green'>Saved: %s=%s</p>",
		    opt_name, value[0] ? value : "NULL");


	for (o = known_options; o->name != NULL; o++) {
		shttpd_printf(arg,
		    "<form method=post><tr><td>%s</td><td>%s</td>"
		    "<input type=hidden name=o value='%s'>"
		    "<td><input type=text name=v value='%s'></td>"
		    "<td><input type=submit value=save></td></form></tr>",
		    o->name, o->description, o->name,
		    ctx->options[o->index] ? ctx->options[o->index] : "");
	}

	shttpd_printf(arg, "%s", "</table></body></html>");
	arg->flags |= SHTTPD_END_OF_OUTPUT;
}

/*
 * Show usage string and exit.
 */
void
_shttpd_usage(const char *prog)
{
	const struct opt	*o;

	(void) fprintf(stderr,
	    "SHTTPD version %s (c) Sergey Lyubka\n"
	    "usage: %s [options] [config_file]\n", VERSION, prog);

#if !defined(NO_AUTH)
	fprintf(stderr, "  -A <htpasswd_file> <realm> <user> <passwd>\n");
#endif /* NO_AUTH */

	for (o = known_options; o->name != NULL; o++) {
		(void) fprintf(stderr, "  -%s\t%s", o->name, o->description);
		if (o->default_value != NULL)
			fprintf(stderr, " (default: %s)", o->default_value);
		fputc('\n', stderr);
	}

	exit(EXIT_FAILURE);
}

static void
set_opt(struct shttpd_ctx *ctx, const char *opt, const char *value)
{
	const struct opt	*o;

	o = find_opt(opt);
	if (ctx->options[o->index] != NULL)
		free(ctx->options[o->index]);
	ctx->options[o->index] = _shttpd_strdup(value);
}

static void
process_command_line_arguments(struct shttpd_ctx *ctx, char *argv[])
{
	const char		*config_file = CONFIG_FILE;
	char			line[BUFSIZ], opt[BUFSIZ],
				val[BUFSIZ], path[FILENAME_MAX], *p;
	FILE			*fp;
	size_t			i, line_no = 0;

	/* First find out, which config file to open */
	for (i = 1; argv[i] != NULL && argv[i][0] == '-'; i += 2)
		if (argv[i + 1] == NULL)
			_shttpd_usage(argv[0]);

	if (argv[i] != NULL && argv[i + 1] != NULL) {
		/* More than one non-option arguments are given w*/
		_shttpd_usage(argv[0]);
	} else if (argv[i] != NULL) {
		/* Just one non-option argument is given, this is config file */
		config_file = argv[i];
	} else {
		/* No config file specified. Look for one where shttpd lives */
		if ((p = strrchr(argv[0], DIRSEP)) != 0) {
			_shttpd_snprintf(path, sizeof(path), "%.*s%s",
			    p - argv[0] + 1, argv[0], config_file);
			config_file = path;
		}
	}

	fp = fopen(config_file, "r");

	/* If config file was set in command line and open failed, exit */
	if (fp == NULL && argv[i] != NULL)
		_shttpd_elog(E_FATAL, NULL, "cannot open config file %s: %s",
		    config_file, strerror(errno));

	if (fp != NULL) {

		_shttpd_elog(E_LOG, NULL, "Loading config file %s", config_file);

		/* Loop over the lines in config file */
		while (fgets(line, sizeof(line), fp) != NULL) {

			line_no++;

			/* Ignore empty lines and comments */
			if (line[0] == '#' || line[0] == '\n')
				continue;

			if (sscanf(line, "%s %[^\n#]", opt, val) != 2)
				_shttpd_elog(E_FATAL, NULL, "line %d in %s is invalid",
				    line_no, config_file);

			set_opt(ctx, opt, val);
		}

		(void) fclose(fp);
	}

	/* Now pass through the command line options */
	for (i = 1; argv[i] != NULL && argv[i][0] == '-'; i += 2)
		set_opt(ctx, &argv[i][1], argv[i + 1]);
}

struct shttpd_ctx *
shttpd_init(int argc, char *argv[])
{
	struct shttpd_ctx	*ctx;
	struct tm		*tm;
	const struct opt	*o;

	if ((ctx = calloc(1, sizeof(*ctx))) == NULL)
		_shttpd_elog(E_FATAL, NULL, "cannot allocate shttpd context");

	LL_INIT(&ctx->registered_uris);
	LL_INIT(&ctx->error_handlers);
	LL_INIT(&ctx->acl);
	LL_INIT(&ctx->ssi_funcs);
	LL_INIT(&ctx->listeners);
	LL_INIT(&ctx->workers);

	/* Initialize options. First pass: set default option values */
	for (o = known_options; o->name != NULL; o++)
		ctx->options[o->index] = o->default_value ?
			_shttpd_strdup(o->default_value) : NULL;

	/* Second and third passes: config file and argv */
	if (argc > 0 && argv != NULL)
		process_command_line_arguments(ctx, argv);

	/* Call setter functions */
	for (o = known_options; o->name != NULL; o++)
		if (o->setter && ctx->options[o->index] != NULL)
			if (o->setter(ctx, ctx->options[o->index]) == FALSE) {
				shttpd_fini(ctx);
				return (NULL);
			}

	_shttpd_current_time = time(NULL);
	tm = localtime(&_shttpd_current_time);
	_shttpd_tz_offset = 0;

	if (num_workers(ctx) == 1)
		(void) add_worker(ctx);
#if 0
	tm->tm_gmtoff - 3600 * (tm->tm_isdst > 0 ? 1 : 0);
#endif

#ifdef _WIN32
	{WSADATA data;	WSAStartup(MAKEWORD(2,2), &data);}
#endif /* _WIN32 */

	return (ctx);
}
