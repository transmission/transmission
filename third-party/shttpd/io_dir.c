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
 * For a given PUT path, create all intermediate subdirectories
 * for given path. Return 0 if the path itself is a directory,
 * or -1 on error, 1 if OK.
 */
int
_shttpd_put_dir(const char *path)
{
	char		buf[FILENAME_MAX];
	const char	*s, *p;
	struct stat	st;
	size_t		len;

	for (s = p = path + 2; (p = strchr(s, '/')) != NULL; s = ++p) {
		len = p - path;
		assert(len < sizeof(buf));
		(void) memcpy(buf, path, len);
		buf[len] = '\0';

		/* Try to create intermediate directory */
		if (_shttpd_stat(buf, &st) == -1 &&
		    _shttpd_mkdir(buf, 0755) != 0)
			return (-1);

		/* Is path itself a directory ? */
		if (p[1] == '\0')
			return (0);
	}

	return (1);
}

static int
read_dir(struct stream *stream, void *buf, size_t len)
{
	static const char footer[] = "</table></body></html>\n";

	struct dirent	*dp = NULL;
	char		file[FILENAME_MAX], line[FILENAME_MAX + 512],
				size[64], mod[64];
	struct stat	st;
	struct conn	*c = stream->conn;
	int		n, nwritten = 0;
	const char	*slash = "";

	assert(stream->chan.dir.dirp != NULL);
	assert(stream->conn->uri[0] != '\0');

	do {
		if (len < sizeof(line))
			break;

		if ((dp = readdir(stream->chan.dir.dirp)) == NULL)
			break;
		DBG(("read_dir: %s", dp->d_name));

		/* Do not show current dir and passwords file */
		if (strcmp(dp->d_name, ".") == 0 ||
		   strcmp(dp->d_name, HTPASSWD) == 0)
			continue;

		(void) _shttpd_snprintf(file, sizeof(file),
		    "%s%s%s", stream->chan.dir.path, slash, dp->d_name);
		(void) _shttpd_stat(file, &st);
		if (S_ISDIR(st.st_mode)) {
			_shttpd_snprintf(size,sizeof(size),"%s","&lt;DIR&gt;");
		} else {
			if (st.st_size < 1024)
				(void) _shttpd_snprintf(size, sizeof(size),
				    "%lu", (unsigned long) st.st_size);
			else if (st.st_size < 1024 * 1024)
				(void) _shttpd_snprintf(size,
				    sizeof(size), "%luk",
				    (unsigned long) (st.st_size >> 10)  + 1);
			else
				(void) _shttpd_snprintf(size, sizeof(size),
				    "%.1fM", (float) st.st_size / 1048576);
		}
		(void) strftime(mod, sizeof(mod), "%d-%b-%Y %H:%M",
			localtime(&st.st_mtime));

		n = _shttpd_snprintf(line, sizeof(line),
		    "<tr><td><a href=\"%s%s%s\">%s%s</a></td>"
		    "<td>&nbsp;%s</td><td>&nbsp;&nbsp;%s</td></tr>\n",
		    c->uri, slash, dp->d_name, dp->d_name,
		    S_ISDIR(st.st_mode) ? "/" : "", mod, size);
		(void) memcpy(buf, line, n);
		buf = (char *) buf + n;
		nwritten += n;
		len -= n;
	} while (dp != NULL);

	/* Append proper HTML footer for the page */
	if (dp == NULL && len >= sizeof(footer)) {
		(void) memcpy(buf, footer, sizeof(footer));
		nwritten += sizeof(footer);
		stream->flags |= FLAG_CLOSED;
	}

	return (nwritten);
}

static void
close_dir(struct stream *stream)
{
	assert(stream->chan.dir.dirp != NULL);
	assert(stream->chan.dir.path != NULL);
	(void) closedir(stream->chan.dir.dirp);
	free(stream->chan.dir.path);
}

void
_shttpd_get_dir(struct conn *c)
{
	if ((c->loc.chan.dir.dirp = opendir(c->loc.chan.dir.path)) == NULL) {
		(void) free(c->loc.chan.dir.path);
		_shttpd_send_server_error(c, 500, "Cannot open directory");
	} else {
		c->loc.io.head = _shttpd_snprintf(c->loc.io.buf, c->loc.io.size,
		    "HTTP/1.1 200 OK\r\n"
		    "Connection: close\r\n"
		    "Content-Type: text/html; charset=utf-8\r\n\r\n"
		    "<html><head><title>Index of %s</title>"
		    "<style>th {text-align: left;}</style></head>"
		    "<body><h1>Index of %s</h1><pre><table cellpadding=\"0\">"
		    "<tr><th>Name</th><th>Modified</th><th>Size</th></tr>"
		    "<tr><td colspan=\"3\"><hr></td></tr>",
		    c->uri, c->uri);
		io_clear(&c->rem.io);
		c->status = 200;
		c->loc.io_class = &_shttpd_io_dir;
		c->loc.flags |= FLAG_R | FLAG_ALWAYS_READY;
	}
}

const struct io_class	_shttpd_io_dir =  {
	"dir",
	read_dir,
	NULL,
	close_dir
};
