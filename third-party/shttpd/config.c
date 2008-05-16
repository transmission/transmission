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

static int isbyte(int n) { return (n >= 0 && n <= 255); }

static void
set_acl(struct shttpd_ctx *ctx, const char *s)
{
	struct acl	*acl = NULL;
	char		flag;
	int		len, a, b, c, d, n, mask;
	struct llhead	*lp, *tmp;

	/* Delete the old ACLs if any */
	LL_FOREACH_SAFE(&ctx->acl, lp, tmp)
		free(LL_ENTRY(lp, struct acl, link));

	FOR_EACH_WORD_IN_LIST(s, len) {

		mask = 32;

		if (sscanf(s, "%c%d.%d.%d.%d%n",&flag,&a,&b,&c,&d,&n) != 5) {
			elog(E_FATAL, NULL, "[%s]: subnet must be "
			    "[+|-]x.x.x.x[/x]", s);
		} else if (flag != '+' && flag != '-') {
			elog(E_FATAL, NULL, "flag must be + or -: [%s]", s);
		} else if (!isbyte(a)||!isbyte(b)||!isbyte(c)||!isbyte(d)) {
			elog(E_FATAL, NULL, "bad ip address: [%s]", s);
		} else	if ((acl = malloc(sizeof(*acl))) == NULL) {
			elog(E_FATAL, NULL, "%s", "cannot malloc subnet");
		} else if (sscanf(s + n, "/%d", &mask) == 0) { 
			/* Do nothing, no mask specified */
		} else if (mask < 0 || mask > 32) {
			elog(E_FATAL, NULL, "bad subnet mask: %d [%s]", n, s);
		}

		acl->ip = (a << 24) | (b << 16) | (c << 8) | d;
		acl->mask = mask ? 0xffffffffU << (32 - mask) : 0;
		acl->flag = flag;
		LL_TAIL(&ctx->acl, &acl->link);
	}
}

#ifndef NO_SSL
/*
 * Dynamically load SSL library. Set up ctx->ssl_ctx pointer.
 */
static void
set_ssl(struct shttpd_ctx *ctx, const char *pem)
{
	SSL_CTX		*CTX;
	void		*lib;
	struct ssl_func	*fp;

	/* Load SSL library dynamically */
	if ((lib = dlopen(SSL_LIB, RTLD_LAZY)) == NULL)
		elog(E_FATAL, NULL, "set_ssl: cannot load %s", SSL_LIB);

	for (fp = ssl_sw; fp->name != NULL; fp++)
		if ((fp->ptr.v_void = dlsym(lib, fp->name)) == NULL)
			elog(E_FATAL, NULL,"set_ssl: cannot find %s", fp->name);

	/* Initialize SSL crap */
	SSL_library_init();

	if ((CTX = SSL_CTX_new(SSLv23_server_method())) == NULL)
		elog(E_FATAL, NULL, "SSL_CTX_new error");
	else if (SSL_CTX_use_certificate_file(CTX, pem, SSL_FILETYPE_PEM) == 0)
		elog(E_FATAL, NULL, "cannot open %s", pem);
	else if (SSL_CTX_use_PrivateKey_file(CTX, pem, SSL_FILETYPE_PEM) == 0)
		elog(E_FATAL, NULL, "cannot open %s", pem);
	ctx->ssl_ctx = CTX;
}
#endif /* NO_SSL */

static void
open_log_file(FILE **fpp, const char *path)
{
	if (*fpp != NULL)
		(void) fclose(*fpp);

	if (path == NULL) {
		*fpp = NULL;
	} else if ((*fpp = fopen(path, "a")) == NULL) {
		elog(E_FATAL, NULL, "cannot open log file %s: %s",
		    path, strerror(errno));
	}
}

static void
set_alog(struct shttpd_ctx *ctx, const char *path)
{
	open_log_file(&ctx->access_log, path);
}

static void
set_elog(struct shttpd_ctx *ctx, const char *path)
{
	open_log_file(&ctx->error_log, path);
}

static void show_cfg_page(struct shttpd_arg *arg);

static void
set_cfg_uri(struct shttpd_ctx *ctx, const char *uri)
{
	free_list(&ctx->registered_uris, &registered_uri_destructor);

	if (uri != NULL) {
		shttpd_register_uri(ctx, uri, &show_cfg_page, ctx);
	}
}

static void
set_ports(struct shttpd_ctx *ctx, const char *p)
{
	int		len, is_ssl;

	free_list(&ctx->listeners, &listener_destructor);

	FOR_EACH_WORD_IN_LIST(p, len) {
		is_ssl = p[len - 1] == 's' ? 1 : 0;
		if (shttpd_listen(ctx, atoi(p), is_ssl) == -1)
			elog(E_FATAL, NULL,
			    "Cannot open socket on port %d", atoi(p));
	}
}

static const struct opt {
	int		index;		/* Index in shttpd_ctx		*/
	const char	*name;		/* Option name in config file	*/
	const char	*description;	/* Description			*/
	const char	*default_value;	/* Default option value		*/
	void (*setter)(struct shttpd_ctx *, const char *);
} known_options[] = {
	{OPT_ROOT, "root", "\tWeb root directory", ".", NULL},
	{OPT_INDEX_FILES, "index_files", "Index files", INDEX_FILES, NULL},
	{OPT_PORTS, "ports", "Listening ports", LISTENING_PORTS, set_ports},
	{OPT_DIR_LIST, "dir_list", "Directory listing", "1", NULL},
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
	{OPT_ACCESS_LOG, "access_log", "Access log file", NULL, set_alog},
	{OPT_ERROR_LOG, "error_log", "Error log file", NULL, set_elog},
	{OPT_MIME_TYPES, "mime_types", "Additional mime types list", NULL,NULL},
#ifndef NO_SSL
	{OPT_SSL_CERTIFICATE, "ssl_cert", "SSL certificate file", NULL,set_ssl},
#endif /* NO_SSL */
	{OPT_ALIASES, "aliases", "Path=URI mappings", NULL, NULL},
	{OPT_ACL, "acl", "\tAllow/deny IP addresses/subnets", NULL, set_acl},
#ifdef _WIN32
#else
	{OPT_INETD, "inetd", "Inetd mode", "0", NULL},
	{OPT_UID, "uid", "\tRun as user", NULL, NULL},
#endif /* _WIN32 */
	{-1, NULL, NULL, NULL, NULL}
};

void shttpd_set_option(struct shttpd_ctx *ctx, const char *opt, const char *val)
{
	const struct opt	*o;

	for (o = known_options; o->name != NULL; o++)
		if (!strcmp(opt, o->name))
			break;

	if (o->name == NULL)
		elog(E_FATAL, NULL, "no such option: [%s]", opt);

	/* Call option setter first, so it can use both new and old values */
	if (o->setter != NULL)
		o->setter(ctx, val);

	/* Free old value if any */
	if (ctx->options[o->index] != NULL)
		free(ctx->options[o->index]);
	
	/* Set new option value */
	ctx->options[o->index] = val ? my_strdup(val) : NULL;
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
usage(const char *prog)
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

struct shttpd_ctx *shttpd_init(void)
{
	struct shttpd_ctx	*ctx;
	struct tm		*tm;
	const struct opt	*o;

	if ((ctx = calloc(1, sizeof(*ctx))) == NULL)
		elog(E_FATAL, NULL, "cannot allocate shttpd context");

	/* Set default values */
	for (o = known_options; o->name != NULL; o++) {
		ctx->options[o->index] = o->default_value == NULL ?
		    NULL : my_strdup(o->default_value);
	}

	current_time = ctx->start_time = time(NULL);
	tm = localtime(&current_time);
	tz_offset = 0;
#if 0
	tm->tm_gmtoff - 3600 * (tm->tm_isdst > 0 ? 1 : 0);
#endif

	InitializeCriticalSection(&ctx->mutex);

	LL_INIT(&ctx->connections);
	LL_INIT(&ctx->registered_uris);
	LL_INIT(&ctx->error_handlers);
	LL_INIT(&ctx->acl);
#if !defined(NO_SSI)
	LL_INIT(&ctx->ssi_funcs);
#endif
	LL_INIT(&ctx->listeners);

#ifdef _WIN32
	{WSADATA data;	WSAStartup(MAKEWORD(2,2), &data);}
#endif /* _WIN32 */

	return (ctx);
}
