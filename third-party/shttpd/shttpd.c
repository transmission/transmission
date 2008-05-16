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
 * $Id: shttpd.c,v 1.28 2008/02/17 21:45:09 drozd Exp $
 */

#include "defs.h"

time_t		current_time;	/* Current UTC time		*/
int		tz_offset;	/* Time zone offset from UTC	*/

const struct vec known_http_methods[] = {
	{"GET",		3},
	{"POST",	4},
	{"PUT",		3},
	{"DELETE",	6},
	{"HEAD",	4},
	{NULL,		0}
};

struct listener {
	struct llhead	link;
	struct shttpd_ctx *ctx;		/* Context that socket belongs	*/
	int		sock;		/* Listening socket		*/
	int		is_ssl;		/* Should be SSL-ed		*/
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
url_decode(const char *src, int src_len, char *dst, int dst_len)
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
stop_stream(struct stream *stream)
{
	if (stream->io_class != NULL && stream->io_class->close != NULL)
		stream->io_class->close(stream);

	stream->io_class= NULL;
	stream->flags |= FLAG_CLOSED;
	stream->flags &= ~(FLAG_R | FLAG_W | FLAG_ALWAYS_READY);

	DBG(("%d %s stopped. %lu of content data, %d now in a buffer",
	    stream->conn->rem.chan.sock, 
	    stream->io_class ? stream->io_class->name : "(null)",
	    (unsigned long) stream->io.total, io_data_len(&stream->io)));
}

/*
 * Setup listening socket on given port, return socket
 */
static int
open_listening_port(int port)
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
	if (set_non_blocking_mode(sock) != 0)
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
	elog(E_LOG, NULL, "open_listening_port(%d): %s", port, strerror(errno));
	return (-1);
}

/*
 * Check whether full request is buffered Return headers length, or 0
 */
int
get_headers_len(const char *buf, size_t buflen)
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
send_server_error(struct conn *c, int status, const char *reason)
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
			setup_embedded_stream(c, e->callback, e->callback_data);
			return;
		}
	}

	io_clear(&c->loc.io);
	c->loc.io.head = my_snprintf(c->loc.io.buf, c->loc.io.size,
	    "HTTP/1.1 %d %s\r\n"
	    "Content-Type: text/plain\r\n"
	    "Content-Length: 12\r\n"
	    "\r\n"
	    "Error: %03d\r\n",
	    status, reason, status);
	c->loc.content_len = 10;
	c->status = status;
	stop_stream(&c->loc);
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
	tmp = localtime(&current_time);
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
parse_headers(const char *s, int len, struct headers *parsed)
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
			    !my_strncasecmp(s, h->name, h->len))
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
get_mime_type(struct shttpd_ctx *ctx, const char *uri, int len, struct vec *vec)
{
	const char	*eq, *p = ctx->options[OPT_MIME_TYPES];
	int		i, n, ext_len;

	/* Firt, loop through the custom mime types if any */
	FOR_EACH_WORD_IN_LIST(p, n) {
		if ((eq = memchr(p, '=', n)) == NULL || eq >= p + n || eq == p)
			continue;
		ext_len = eq - p;
		if (len > ext_len && uri[len - ext_len - 1] == '.' &&
		    !my_strncasecmp(p, &uri[len - ext_len], ext_len)) {
			vec->ptr = eq + 1;
			vec->len = p + n - vec->ptr;
			return;
		}
	}

	/* If no luck, try built-in mime types */
	for (i = 0; builtin_mime_types[i].extension != NULL; i++) {
		ext_len = builtin_mime_types[i].ext_len;
		if (len > ext_len && uri[len - ext_len - 1] == '.' &&
		    !my_strncasecmp(builtin_mime_types[i].extension,
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
		my_snprintf(buf, sizeof(buf), "%s%c%.*s",path, DIRSEP, len, s);
		if (my_stat(buf, stp) == 0) {
			my_strlcpy(path, buf, maxpath);
			get_mime_type(c->ctx, s, len, &c->mime_type);
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

	if (my_stat(path, stp) == 0)
		return (0);

	p = path + strlen(path);
	e = path + strlen(c->ctx->options[OPT_ROOT]) + 2;
	
	/* Strip directory parts of the path one by one */
	for (; p > e; p--)
		if (*p == '/') {
			*p = '\0';
			if (!my_stat(path, stp) && !S_ISDIR(stp->st_mode)) {
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

	url_decode(c->uri, strlen(c->uri), c->uri, strlen(c->uri) + 1);
	remove_double_dots(c->uri);
	
	root = c->ctx->options[OPT_ROOT];
	if (strlen(c->uri) + strlen(root) >= sizeof(path)) {
		send_server_error(c, 400, "URI is too long");
		return;
	}

	(void) my_snprintf(path, sizeof(path), "%s%s", root, c->uri);

	/* User may use the aliases - check URI for mount point */
	if (is_alias(c->ctx, c->uri, &alias_uri, &alias_path) != NULL) {
		(void) my_snprintf(path, sizeof(path), "%.*s%s",
		    alias_path.len, alias_path.ptr, c->uri + alias_uri.len);
		DBG(("using alias %.*s -> %.*s", alias_uri.len, alias_uri.ptr,
		    alias_path.len, alias_path.ptr));
	}

#if !defined(NO_AUTH)
	if (check_authorization(c, path) != 1) {
		send_authorization_request(c);
	} else
#endif /* NO_AUTH */
	if ((ruri = is_registered_uri(c->ctx, c->uri)) != NULL) {
		setup_embedded_stream(c, ruri->callback, ruri->callback_data);
	} else
	if (strstr(path, HTPASSWD)) {
		/* Do not allow to view passwords files */
		send_server_error(c, 403, "Forbidden");
	} else
#if !defined(NO_AUTH)
	if ((c->method == METHOD_PUT || c->method == METHOD_DELETE) &&
	    (c->ctx->options[OPT_AUTH_PUT] == NULL ||
	     !is_authorized_for_put(c))) {
		send_authorization_request(c);
	} else
#endif /* NO_AUTH */
	if (c->method == METHOD_PUT) {
		c->status = my_stat(path, &st) == 0 ? 200 : 201;

		if (c->ch.range.v_vec.len > 0) {
			send_server_error(c, 501, "PUT Range Not Implemented");
		} else if ((rc = put_dir(path)) == 0) {
			send_server_error(c, 200, "OK");
		} else if (rc == -1) {
			send_server_error(c, 500, "PUT Directory Error");
		} else if (c->rem.content_len == 0) {
			send_server_error(c, 411, "Length Required");
		} else if ((c->loc.chan.fd = my_open(path, O_WRONLY | O_BINARY |
		    O_CREAT | O_NONBLOCK | O_TRUNC, 0644)) == -1) {
			send_server_error(c, 500, "PUT Error");
		} else {
			DBG(("PUT file [%s]", c->uri));
			c->loc.io_class = &io_file;
			c->loc.flags |= FLAG_W | FLAG_ALWAYS_READY ;
		}
	} else if (c->method == METHOD_DELETE) {
		DBG(("DELETE [%s]", c->uri));
		if (my_remove(path) == 0)
			send_server_error(c, 200, "OK");
		else
			send_server_error(c, 500, "DELETE Error");
	} else if (get_path_info(c, path, &st) != 0) {
		send_server_error(c, 404, "Not Found");
	} else if (S_ISDIR(st.st_mode) && path[strlen(path) - 1] != '/') {
		(void) my_snprintf(buf, sizeof(buf),
			"Moved Permanently\r\nLocation: %s/", c->uri);
		send_server_error(c, 301, buf);
	} else if (S_ISDIR(st.st_mode) &&
	    find_index_file(c, path, sizeof(path) - 1, &st) == -1 &&
	    !IS_TRUE(c->ctx, OPT_DIR_LIST)) {
		send_server_error(c, 403, "Directory Listing Denied");
	} else if (S_ISDIR(st.st_mode) && IS_TRUE(c->ctx, OPT_DIR_LIST)) {
		if ((c->loc.chan.dir.path = my_strdup(path)) != NULL)
			get_dir(c);
		else
			send_server_error(c, 500, "GET Directory Error");
	} else if (S_ISDIR(st.st_mode) && !IS_TRUE(c->ctx, OPT_DIR_LIST)) {
		send_server_error(c, 403, "Directory listing denied");
#if !defined(NO_CGI)
	} else if (match_extension(path, c->ctx->options[OPT_CGI_EXTENSIONS])) {
		if (c->method != METHOD_POST && c->method != METHOD_GET) {
			send_server_error(c, 501, "Bad method ");
		} else if ((run_cgi(c, path)) == -1) {
			send_server_error(c, 500, "Cannot exec CGI");
		} else {
			do_cgi(c);
		}
#endif /* NO_CGI */
#if !defined(NO_SSI)
	} else if (match_extension(path, c->ctx->options[OPT_SSI_EXTENSIONS])) {
		if ((c->loc.chan.fd = my_open(path,
		    O_RDONLY | O_BINARY, 0644)) == -1) {
			send_server_error(c, 500, "SSI open error");
		} else {
			do_ssi(c);
		}
#endif /* NO_CGI */
	} else if (c->ch.ims.v_time && st.st_mtime <= c->ch.ims.v_time) {
		send_server_error(c, 304, "Not Modified");
	} else if ((c->loc.chan.fd = my_open(path,
	    O_RDONLY | O_BINARY, 0644)) != -1) {
		get_file(c, &st);
	} else {
		send_server_error(c, 500, "Internal Error");
	}
}

static int
set_request_method(struct conn *c)
{
	const struct vec	*v;

	/* Set the request method */
	for (v = known_http_methods; v->ptr != NULL; v++)
		if (!memcmp(c->rem.io.buf, v->ptr, v->len)) {
			c->method = v - known_http_methods;
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
	    get_headers_len(s, io_data_len(&c->rem.io));

	if (req_len == 0 && io_space_len(&c->rem.io) == 0) {
		io_clear(&c->rem.io);
		send_server_error(c, 400, "Request is too big");
	}

	io_inc_tail(&c->rem.io, req_len);

	if (req_len == 0) {
		return;
	} else if (req_len < 16) {	/* Minimal: "GET / HTTP/1.0\n\n" */
		send_server_error(c, 400, "Bad request");
	} else if (set_request_method(c)) {
		send_server_error(c, 501, "Method Not Implemented");
	} else if ((c->request = my_strndup(s, req_len)) == NULL) {
		send_server_error(c, 500, "Cannot allocate request");
	}

	if (c->loc.flags & FLAG_CLOSED)
		return;

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
		send_server_error(c, 400, "Bad HTTP version");
	} else if (c->major_version > 1 ||
	    (c->major_version == 1 && c->minor_version > 1)) {
		send_server_error(c, 505, "HTTP version not supported");
	} else if (uri_len <= 0) {
		send_server_error(c, 400, "Bad URI");
	} else if ((c->uri = malloc(uri_len + 1)) == NULL) {
		send_server_error(c, 500, "Cannot allocate URI");
	} else {
		my_strlcpy(c->uri, (char *) start, uri_len + 1);
		parse_headers(c->headers,
		    (c->request + req_len) - c->headers, &c->ch);

		/* Remove the length of request from total, count only data */
		assert(c->rem.io.total >= (big_int_t) req_len);
		c->rem.io.total -= req_len;
		c->rem.content_len = c->ch.cl.v_big_int;
		decide_what_to_do(c);
	}
}

void
shttpd_add_socket(struct shttpd_ctx *ctx, int sock, int is_ssl)
{
	struct conn	*c;
	struct usa	sa;
	int		l = IS_TRUE(ctx, OPT_INETD) ? E_FATAL : E_LOG;
#if !defined(NO_SSL)
	SSL		*ssl = NULL;
#endif /* NO_SSL */

	sa.len = sizeof(sa.u.sin);
	(void) set_non_blocking_mode(sock);

	if (getpeername(sock, &sa.u.sa, &sa.len)) {
		elog(l, NULL, "add_socket: %s", strerror(errno));
#if !defined(NO_SSL)
	} else if (is_ssl && (ssl = SSL_new(ctx->ssl_ctx)) == NULL) {
		elog(l, NULL, "add_socket: SSL_new: %s", strerror(ERRNO));
		(void) closesocket(sock);
	} else if (is_ssl && SSL_set_fd(ssl, sock) == 0) {
		elog(l, NULL, "add_socket: SSL_set_fd: %s", strerror(ERRNO));
		(void) closesocket(sock);
		SSL_free(ssl);
#endif /* NO_SSL */
	} else if ((c = calloc(1, sizeof(*c) + 2 * URI_MAX)) == NULL) {
#if !defined(NO_SSL)
		if (ssl)
			SSL_free(ssl);
#endif /* NO_SSL */
		(void) closesocket(sock);
		elog(l, NULL, "add_socket: calloc: %s", strerror(ERRNO));
	} else {
		ctx->nrequests++;
		c->rem.conn = c->loc.conn = c;
		c->ctx		= ctx;
		c->sa		= sa;
		c->birth_time	= current_time;
		c->expire_time	= current_time + EXPIRE_TIME;

		(void) getsockname(sock, &sa.u.sa, &sa.len);
		c->loc_port = sa.u.sin.sin_port;

		set_close_on_exec(sock);

		c->loc.io_class	= NULL;
	
		c->rem.io_class	= &io_socket;
		c->rem.chan.sock = sock;

		/* Set IO buffers */
		c->loc.io.buf	= (char *) (c + 1);
		c->rem.io.buf	= c->loc.io.buf + URI_MAX;
		c->loc.io.size	= c->rem.io.size = URI_MAX;

#if !defined(NO_SSL)
		if (is_ssl) {
			c->rem.io_class	= &io_ssl;
			c->rem.chan.ssl.sock = sock;
			c->rem.chan.ssl.ssl = ssl;
			ssl_handshake(&c->rem);
		}
#endif /* NO_SSL */

		EnterCriticalSection(&ctx->mutex);
		LL_TAIL(&ctx->connections, &c->link);
		ctx->nactive++;
		LeaveCriticalSection(&ctx->mutex);
		
		DBG(("%s:%hu connected (socket %d)",
		    inet_ntoa(* (struct in_addr *) &sa.u.sin.sin_addr.s_addr),
		    ntohs(sa.u.sin.sin_port), sock));
	}
}

int
shttpd_active(struct shttpd_ctx *ctx)
{
	return (ctx->nactive);
}

/*
 * Setup a listening socket on given port. Return opened socket or -1
 */
int
shttpd_listen(struct shttpd_ctx *ctx, int port, int is_ssl)
{
	struct listener	*l;
	int		sock;

	if ((sock = open_listening_port(port)) == -1) {
		elog(E_FATAL, NULL, "cannot open port %d", port);
	} else if ((l = calloc(1, sizeof(*l))) == NULL) {
		(void) closesocket(sock);
		elog(E_FATAL, NULL, "cannot allocate listener");
	} else if (is_ssl && ctx->ssl_ctx == NULL) {
		(void) closesocket(sock);
		elog(E_FATAL, NULL, "cannot add SSL socket, "
		    "please specify certificate file");
	} else {
		l->is_ssl = is_ssl;
		l->sock	= sock;
		l->ctx	= ctx;
		LL_TAIL(&ctx->listeners, &l->link);
		DBG(("shttpd_listen: added socket %d", sock));
	}

	return (sock);
}

int
shttpd_accept(int lsn_sock, int milliseconds)
{
	struct timeval	tv;
	struct usa	sa;
	fd_set		read_set;
	int		sock = -1;
	
	tv.tv_sec	= milliseconds / 1000;
	tv.tv_usec	= milliseconds % 1000;
	sa.len		= sizeof(sa.u.sin);
	FD_ZERO(&read_set);
	FD_SET(lsn_sock, &read_set);
	
	if (select(lsn_sock + 1, &read_set, NULL, NULL, &tv) == 1)
		sock = accept(lsn_sock, &sa.u.sa, &sa.len);

	return (sock);
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
	n = stream->nread_last = stream->io_class->read(stream,
	    io_space(&stream->io), len);

	if (n > 0)
		io_inc_head(&stream->io, n);
	else if (n == -1 && (ERRNO == EINTR || ERRNO == EWOULDBLOCK))
		n = n;	/* Ignore EINTR and EAGAIN */
	else if (!(stream->flags & FLAG_DONT_CLOSE))
		stop_stream(stream);

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
			stop_stream(stream);
	}

	stream->conn->expire_time = current_time + EXPIRE_TIME;
}

static void
write_stream(struct stream *from, struct stream *to)
{
	int	n, len;

	len = io_data_len(&from->io);
	assert(len > 0);

	/* TODO: should be assert on CAN_WRITE flag */
	n = to->io_class->write(to, io_data(&from->io), len);
	to->conn->expire_time = current_time + EXPIRE_TIME;
	DBG(("write_stream (%d %s): written %d/%d bytes (errno %d)",
	    to->conn->rem.chan.sock,
	    to->io_class ? to->io_class->name : "(null)", n, len, ERRNO));

	if (n > 0)
		io_inc_tail(&from->io, n);
	else if (n == -1 && (ERRNO == EINTR || ERRNO == EWOULDBLOCK))
		n = n;	/* Ignore EINTR and EAGAIN */
	else if (!(to->flags & FLAG_DONT_CLOSE))
		stop_stream(to);
}


static void
disconnect(struct llhead *lp)
{
	struct conn		*c = LL_ENTRY(lp, struct conn, link);
	static const struct vec	vec = {"close", 5};
	int			do_close;

	DBG(("Disconnecting %d (%.*s)", c->rem.chan.sock,
	    c->ch.connection.v_vec.len, c->ch.connection.v_vec.ptr));

	if (c->request != NULL && c->ctx->access_log != NULL)
		log_access(c->ctx->access_log, c);

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
	    !my_strncasecmp(vec.ptr, c->ch.connection.v_vec.ptr, vec.len)) ||
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
		c->rem.flags = FLAG_W | FLAG_R;
		c->query = c->request = c->uri = c->path_info = NULL;
		c->mime_type.len = 0;
		(void) memset(&c->ch, 0, sizeof(c->ch));
		io_clear(&c->loc.io);
		c->birth_time = current_time;
		if (io_data_len(&c->rem.io) > 0)
			process_connection(c, 0, 0);
	} else {
		if (c->rem.io_class != NULL)
			c->rem.io_class->close(&c->rem);

		EnterCriticalSection(&c->ctx->mutex);
		LL_DEL(&c->link);
		c->ctx->nactive--;
		assert(c->ctx->nactive >= 0);
		LeaveCriticalSection(&c->ctx->mutex);

		free(c);
	}
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

	DBG(("loc: %u [%.*s]", io_data_len(&c->loc.io),
	    io_data_len(&c->loc.io), io_data(&c->loc.io)));
	DBG(("rem: %u [%.*s]", io_data_len(&c->rem.io),
	    io_data_len(&c->rem.io), io_data(&c->rem.io)));

	/* Read from the local end if it is ready */
	if (local_ready && io_space_len(&c->loc.io))
		read_stream(&c->loc);

	if (io_data_len(&c->rem.io) > 0 && (c->loc.flags & FLAG_W) &&
	    c->loc.io_class != NULL && c->loc.io_class->write != NULL)
		write_stream(&c->rem, &c->loc);

	if (io_data_len(&c->loc.io) > 0 && c->rem.io_class != NULL)
		write_stream(&c->loc, &c->rem); 

	if (c->rem.nread_last > 0)
		c->ctx->in += c->rem.nread_last;
	if (c->loc.nread_last > 0)
		c->ctx->out += c->loc.nread_last;

	/* Check whether we should close this connection */
	if ((current_time > c->expire_time) ||
	    (c->rem.flags & FLAG_CLOSED) ||
	    ((c->loc.flags & FLAG_CLOSED) && !io_data_len(&c->loc.io)))
		disconnect(&c->link);
}

/*
 * One iteration of server loop. This is the core of the data exchange.
 */
void
shttpd_poll(struct shttpd_ctx *ctx, int milliseconds)
{
	struct llhead	*lp, *tmp;
	struct listener	*l;
	struct conn	*c;
	struct timeval	tv;			/* Timeout for select() */
	fd_set		read_set, write_set;
	int		sock, max_fd = -1, msec = milliseconds;
	struct usa	sa;

	current_time = time(0);
	FD_ZERO(&read_set);
	FD_ZERO(&write_set);

	/* Add listening sockets to the read set */
	LL_FOREACH(&ctx->listeners, lp) {
		l = LL_ENTRY(lp, struct listener, link);
		FD_SET(l->sock, &read_set);
		if (l->sock > max_fd)
			max_fd = l->sock;
		DBG(("FD_SET(%d) (listening)", l->sock));
	}

	/* Multiplex streams */
	LL_FOREACH(&ctx->connections, lp) {
		c = LL_ENTRY(lp, struct conn, link);
		
		/* If there is a space in remote IO, check remote socket */
		if (io_space_len(&c->rem.io))
			add_to_set(c->rem.chan.fd, &read_set, &max_fd);

#if !defined(NO_CGI)
		/*
		 * If there is a space in local IO, and local endpoint is
		 * CGI, check local socket for read availability
		 */
		if (io_space_len(&c->loc.io) && (c->loc.flags & FLAG_R) &&
		    c->loc.io_class == &io_cgi)
			add_to_set(c->loc.chan.fd, &read_set, &max_fd);

		/*
		 * If there is some data read from remote socket, and
		 * local endpoint is CGI, check local for write availability
		 */
		if (io_data_len(&c->rem.io) && (c->loc.flags & FLAG_W) &&
		    c->loc.io_class == &io_cgi)
			add_to_set(c->loc.chan.fd, &write_set, &max_fd);
#endif /* NO_CGI */

		/*
		 * If there is some data read from local endpoint, check the
		 * remote socket for write availability
		 */
		if (io_data_len(&c->loc.io))
			add_to_set(c->rem.chan.fd, &write_set, &max_fd);

		if (io_space_len(&c->loc.io) && (c->loc.flags & FLAG_R) &&
		    (c->loc.flags & FLAG_ALWAYS_READY))
			msec = 0;
		
		if (io_data_len(&c->rem.io) && (c->loc.flags & FLAG_W) &&
		    (c->loc.flags & FLAG_ALWAYS_READY))
			msec = 0;
	}

	tv.tv_sec = msec / 1000;
	tv.tv_usec = (msec % 1000) * 1000;

	/* Check IO readiness */
	if (select(max_fd + 1, &read_set, &write_set, NULL, &tv) < 0) {
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
		return;
	}

	/* Check for incoming connections on listener sockets */
	LL_FOREACH(&ctx->listeners, lp) {
		l = LL_ENTRY(lp, struct listener, link);
		if (!FD_ISSET(l->sock, &read_set))
			continue;
		do {
			sa.len = sizeof(sa.u.sin);
			if ((sock = accept(l->sock, &sa.u.sa, &sa.len)) != -1) {
#if defined(_WIN32)
				shttpd_add_socket(ctx, sock, l->is_ssl);
#else
				if (sock >= (int) FD_SETSIZE) {
					elog(E_LOG, NULL,
					   "shttpd_poll: ctx %p: disarding "
					   "socket %d, too busy", ctx, sock);
					(void) closesocket(sock);
				} else if (!is_allowed(ctx, &sa)) {
					elog(E_LOG, NULL, "shttpd_poll: %s "
					    "is not allowed to connect",
					   inet_ntoa(sa.u.sin.sin_addr));
					(void) closesocket(sock);
				} else {
					shttpd_add_socket(ctx, sock, l->is_ssl);
				}
#endif /* _WIN32 */
			}
		} while (sock != -1);
	}

	/* Process all connections */
	LL_FOREACH_SAFE(&ctx->connections, lp, tmp) {
		c = LL_ENTRY(lp, struct conn, link);
		process_connection(c, FD_ISSET(c->rem.chan.fd, &read_set),
		    ((c->loc.flags & FLAG_ALWAYS_READY)
#if !defined(NO_CGI)
		    || (c->loc.io_class == &io_cgi &&
		     FD_ISSET(c->loc.chan.fd, &read_set))
#endif /* NO_CGI */
		    ));
	}
}

void
free_list(struct llhead *head, void (*dtor)(struct llhead *))
{
	struct llhead	*lp, *tmp;

	LL_FOREACH_SAFE(head, lp, tmp) {
		LL_DEL(lp);
		dtor(lp);
	}
}

void
listener_destructor(struct llhead *lp)
{
	struct listener	*listener = LL_ENTRY(lp, struct listener, link);

	(void) closesocket(listener->sock);
	free(listener);
}

void
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

/*
 * Deallocate shttpd object, free up the resources
 */
void
shttpd_fini(struct shttpd_ctx *ctx)
{
	size_t	i;

	free_list(&ctx->connections, disconnect);
	free_list(&ctx->registered_uris, registered_uri_destructor);
	free_list(&ctx->acl, acl_destructor);
	free_list(&ctx->listeners, listener_destructor);
#if !defined(NO_SSI)
	free_list(&ctx->ssi_funcs, ssi_func_destructor);
#endif

	for (i = 0; i < NELEMS(ctx->options); i++)
		if (ctx->options[i] != NULL)
			free(ctx->options[i]);

	if (ctx->access_log)		(void) fclose(ctx->access_log);
	if (ctx->error_log)		(void) fclose(ctx->error_log);

	/* TODO: free SSL context */

	free(ctx);
}
