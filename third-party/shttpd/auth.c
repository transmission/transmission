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

#if !defined(NO_AUTH)
/*
 * Stringify binary data. Output buffer must be twice as big as input,
 * because each byte takes 2 bytes in string representation
 */
static void
bin2str(char *to, const unsigned char *p, size_t len)
{
	const char	*hex = "0123456789abcdef";

	for (;len--; p++) {
		*to++ = hex[p[0] >> 4];
		*to++ = hex[p[0] & 0x0f];
	}
}

/*
 * Return stringified MD5 hash for list of vectors.
 * buf must point to at least 32-bytes long buffer
 */
static void
md5(char *buf, ...)
{
	unsigned char	hash[16];
	const struct vec *v;
	va_list		ap;
	MD5_CTX	ctx;
	int		i;

	MD5Init(&ctx);

	va_start(ap, buf);
	for (i = 0; (v = va_arg(ap, const struct vec *)) != NULL; i++) {
		assert(v->len >= 0);
		if (v->len == 0)
			continue;
		if (i > 0)
			MD5Update(&ctx, (unsigned char *) ":", 1);
		MD5Update(&ctx,(unsigned char *)v->ptr,(unsigned int)v->len);
	}
	va_end(ap);

	MD5Final(hash, &ctx);
	bin2str(buf, hash, sizeof(hash));
}

/*
 * Compare to vectors. Return 1 if they are equal
 */
static int
vcmp(const struct vec *v1, const struct vec *v2)
{
	return (v1->len == v2->len && !memcmp(v1->ptr, v2->ptr, v1->len));
}

struct digest {
	struct vec	user;
	struct vec	uri;
	struct vec	nonce;
	struct vec	cnonce;
	struct vec	resp;
	struct vec	qop;
	struct vec	nc;
};

static const struct auth_keyword {
	size_t		offset;
	struct vec	vec;
} known_auth_keywords[] = {
	{offsetof(struct digest, user),		{"username=",	9}},
	{offsetof(struct digest, cnonce),	{"cnonce=",	7}},
	{offsetof(struct digest, resp),		{"response=",	9}},
	{offsetof(struct digest, uri),		{"uri=",	4}},
	{offsetof(struct digest, qop),		{"qop=",	4}},
	{offsetof(struct digest, nc),		{"nc=",		3}},
	{offsetof(struct digest, nonce),	{"nonce=",	6}},
	{0,					{NULL,		0}}
};

static void
parse_authorization_header(const struct vec *h, struct digest *dig)
{
	const unsigned char	*p, *e, *s;
	struct vec		*v, vec;
	const struct auth_keyword *kw;

	(void) memset(dig, 0, sizeof(*dig));
	p = (unsigned char *) h->ptr + 7;
	e = (unsigned char *) h->ptr + h->len;

	while (p < e) {

		/* Skip spaces */
		while (p < e && (*p == ' ' || *p == ','))
			p++;

		/* Skip to "=" */
		for (s = p; s < e && *s != '='; )
			s++;
		s++;

		/* Is it known keyword ? */
		for (kw = known_auth_keywords; kw->vec.len > 0; kw++)
			if (kw->vec.len <= s - p &&
			    !memcmp(p, kw->vec.ptr, kw->vec.len))
				break;

		if (kw->vec.len == 0)
			v = &vec;		/* Dummy placeholder	*/
		else
			v = (struct vec *) ((char *) dig + kw->offset);

		if (*s == '"') {
			p = ++s;
			while (p < e && *p != '"')
				p++;
		} else {
			p = s;
			while (p < e && *p != ' ' && *p != ',')
				p++;
		}

		v->ptr = (char *) s;
		v->len = p - s;

		if (*p == '"')
			p++;

		DBG(("auth field [%.*s]", v->len, v->ptr));
	}
}

/*
 * Check the user's password, return 1 if OK
 */
static int
check_password(int method, const struct vec *ha1, const struct digest *digest)
{
	char		a2[32], resp[32];
	struct vec	vec_a2;

	/* XXX  Due to a bug in MSIE, we do not compare the URI	 */
	/* Also, we do not check for authentication timeout */
	if (/*strcmp(dig->uri, c->ouri) != 0 || */
	    digest->resp.len != 32 /*||
	    now - strtoul(dig->nonce, NULL, 10) > 3600 */)
		return (0);

	md5(a2, &_shttpd_known_http_methods[method], &digest->uri, NULL);
	vec_a2.ptr = a2;
	vec_a2.len = sizeof(a2);
	md5(resp, ha1, &digest->nonce, &digest->nc,
	    &digest->cnonce, &digest->qop, &vec_a2, NULL);
	DBG(("%s: uri [%.*s] expected_resp [%.*s] resp [%.*s]",
	    "check_password", digest->uri.len, digest->uri.ptr,
	    32, resp, digest->resp.len, digest->resp.ptr));

	return (!memcmp(resp, digest->resp.ptr, 32));
}

static FILE *
open_auth_file(struct shttpd_ctx *ctx, const char *path)
{
	char 		name[FILENAME_MAX];
	const char	*p, *e;
	FILE		*fp = NULL;
	int		fd;

	if (ctx->options[OPT_AUTH_GPASSWD] != NULL) {
		/* Use global passwords file */
		_shttpd_snprintf(name, sizeof(name), "%s",
		    ctx->options[OPT_AUTH_GPASSWD]);
	} else {
		/*
		 * Try to find .htpasswd in requested directory.
		 * Given the path, create the path to .htpasswd file
		 * in the same directory. Find the right-most
		 * directory separator character first. That would be the
		 * directory name. If directory separator character is not
		 * found, 'e' will point to 'p'.
		 */
		for (p = path, e = p + strlen(p) - 1; e > p; e--)
			if (IS_DIRSEP_CHAR(*e))
				break;

		/*
		 * Make up the path by concatenating directory name and
		 * .htpasswd file name.
		 */
		(void) _shttpd_snprintf(name, sizeof(name), "%.*s/%s",
		    (int) (e - p), p, HTPASSWD);
	}

	if ((fd = _shttpd_open(name, O_RDONLY, 0)) == -1) {
		DBG(("open_auth_file: open(%s)", name));
	} else if ((fp = fdopen(fd, "r")) == NULL) {
		DBG(("open_auth_file: fdopen(%s)", name));
		(void) close(fd);
	}

	return (fp);
}

/*
 * Parse the line from htpasswd file. Line should be in form of
 * "user:domain:ha1". Fill in the vector values. Return 1 if successful.
 */
static int
parse_htpasswd_line(const char *s, struct vec *user,
				struct vec *domain, struct vec *ha1)
{
	user->len = domain->len = ha1->len = 0;

	for (user->ptr = s; *s != '\0' && *s != ':'; s++, user->len++);
	if (*s++ != ':')
		return (0);

	for (domain->ptr = s; *s != '\0' && *s != ':'; s++, domain->len++);
	if (*s++ != ':')
		return (0);

	for (ha1->ptr = s; *s != '\0' && !isspace(* (unsigned char *) s);
	    s++, ha1->len++);

	DBG(("parse_htpasswd_line: [%.*s] [%.*s] [%.*s]", user->len, user->ptr,
	    domain->len, domain->ptr, ha1->len, ha1->ptr));

	return (user->len > 0 && domain->len > 0 && ha1->len > 0);
}

/*
 * Authorize against the opened passwords file. Return 1 if authorized.
 */
static int
authorize(struct conn *c, FILE *fp)
{
	struct vec 	*auth_vec = &c->ch.auth.v_vec;
	struct vec	*user_vec = &c->ch.user.v_vec;
	struct vec	user, domain, ha1;
	struct digest	digest;
	int		ok = 0;
	char		line[256];

	if (auth_vec->len > 20 &&
	    !_shttpd_strncasecmp(auth_vec->ptr, "Digest ", 7)) {

		parse_authorization_header(auth_vec, &digest);
		*user_vec = digest.user;

		while (fgets(line, sizeof(line), fp) != NULL) {

			if (!parse_htpasswd_line(line, &user, &domain, &ha1))
				continue;

			DBG(("[%.*s] [%.*s] [%.*s]", user.len, user.ptr,
			    domain.len, domain.ptr, ha1.len, ha1.ptr));

			if (vcmp(user_vec, &user) &&
			    !memcmp(c->ctx->options[OPT_AUTH_REALM],
			    domain.ptr, domain.len)) {
				ok = check_password(c->method, &ha1, &digest);
				break;
			}
		}
	}

	return (ok);
}

int
_shttpd_check_authorization(struct conn *c, const char *path)
{
	FILE		*fp = NULL;
	int		len, n, authorized = 1;
	const char	*p, *s = c->ctx->options[OPT_PROTECT];
	char		protected_path[FILENAME_MAX];

	FOR_EACH_WORD_IN_LIST(s, len) {

		if ((p = memchr(s, '=', len)) == NULL || p >= s + len || p == s)
			continue;

		if (!memcmp(c->uri, s, p - s)) {
			
			n = s + len - p;
			if (n > (int) sizeof(protected_path) - 1)
				n = sizeof(protected_path) - 1;

			_shttpd_strlcpy(protected_path, p + 1, n);

			if ((fp = fopen(protected_path, "r")) == NULL)
				_shttpd_elog(E_LOG, c,
				    "check_auth: cannot open %s: %s",
				    protected_path, strerror(errno));
			break;
		}
	}

	if (fp == NULL)
		fp = open_auth_file(c->ctx, path);

	if (fp != NULL) {
		authorized = authorize(c, fp);
		(void) fclose(fp);
	}

	return (authorized);
}

int
_shttpd_is_authorized_for_put(struct conn *c)
{
	FILE	*fp;
	int	ret = 0;

	if ((fp = fopen(c->ctx->options[OPT_AUTH_PUT], "r")) != NULL) {
		ret = authorize(c, fp);
		(void) fclose(fp);
	}

	return (ret);
}

void
_shttpd_send_authorization_request(struct conn *c)
{
	char	buf[512];

	(void) _shttpd_snprintf(buf, sizeof(buf), "Unauthorized\r\n"
	    "WWW-Authenticate: Digest qop=\"auth\", realm=\"%s\", "
	    "nonce=\"%lu\"", c->ctx->options[OPT_AUTH_REALM],
	    (unsigned long) _shttpd_current_time);

	_shttpd_send_server_error(c, 401, buf);
}

/*
 * Edit the passwords file.
 */
int
_shttpd_edit_passwords(const char *fname, const char *domain,
		const char *user, const char *pass)
{
	int		ret = EXIT_SUCCESS, found = 0;
	struct vec	u, d, p;
	char		line[512], tmp[FILENAME_MAX], ha1[32];
	FILE		*fp = NULL, *fp2 = NULL;

	(void) _shttpd_snprintf(tmp, sizeof(tmp), "%s.tmp", fname);

	/* Create the file if does not exist */
	if ((fp = fopen(fname, "a+")))
		(void) fclose(fp);

	/* Open the given file and temporary file */
	if ((fp = fopen(fname, "r")) == NULL)
		_shttpd_elog(E_FATAL, NULL,
		    "Cannot open %s: %s", fname, strerror(errno));
	else if ((fp2 = fopen(tmp, "w+")) == NULL)
		_shttpd_elog(E_FATAL, NULL,
		    "Cannot open %s: %s", tmp, strerror(errno));

	p.ptr = pass;
	p.len = strlen(pass);

	/* Copy the stuff to temporary file */
	while (fgets(line, sizeof(line), fp) != NULL) {
		u.ptr = line;
		if ((d.ptr = strchr(line, ':')) == NULL)
			continue;
		u.len = d.ptr - u.ptr;
		d.ptr++;
		if (strchr(d.ptr, ':') == NULL)
			continue;
		d.len = strchr(d.ptr, ':') - d.ptr;

		if ((int) strlen(user) == u.len &&
		    !memcmp(user, u.ptr, u.len) &&
		    (int) strlen(domain) == d.len &&
		    !memcmp(domain, d.ptr, d.len)) {
			found++;
			md5(ha1, &u, &d, &p, NULL);
			(void) fprintf(fp2, "%s:%s:%.32s\n", user, domain, ha1);
		} else {
			(void) fprintf(fp2, "%s", line);
		}
	}

	/* If new user, just add it */
	if (found == 0) {
		u.ptr = user; u.len = strlen(user);
		d.ptr = domain; d.len = strlen(domain);
		md5(ha1, &u, &d, &p, NULL);
		(void) fprintf(fp2, "%s:%s:%.32s\n", user, domain, ha1);
	}

	/* Close files */
	(void) fclose(fp);
	(void) fclose(fp2);

	/* Put the temp file in place of real file */
	(void) _shttpd_remove(fname);
	(void) _shttpd_rename(tmp, fname);

	return (ret);
}
#endif /* NO_AUTH */
