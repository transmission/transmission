/*
 * Copyright (c) 2004-2005 Sergey Lyubka <valenok@gmail.com>
 * All rights reserved
 *
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Sergey Lyubka wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.
 */

#ifndef DEFS_HEADER_DEFINED
#define	DEFS_HEADER_DEFINED

#include "std_includes.h"
#include "llist.h"
#include "io.h"
#include "md5.h"
#include "config.h"
#include "shttpd.h"

#define	NELEMS(ar)	(sizeof(ar) / sizeof(ar[0]))

#ifdef _DEBUG
#define	DBG(x)	do { printf x ; putchar('\n'); fflush(stdout); } while (0)
#else
#define	DBG(x)
#endif /* DEBUG */

/*
 * Darwin prior to 7.0 and Win32 do not have socklen_t
 */
#ifdef NO_SOCKLEN_T
typedef int socklen_t;
#endif /* NO_SOCKLEN_T */

/*
 * For parsing. This guy represents a substring.
 */
struct vec {
	const char	*ptr;
	int		len;
};

#if !defined(FALSE)
enum {FALSE, TRUE};
#endif /* !FALSE */

enum {METHOD_GET, METHOD_POST, METHOD_PUT, METHOD_DELETE, METHOD_HEAD};
enum {HDR_DATE, HDR_INT, HDR_STRING};	/* HTTP header types		*/
enum {E_FATAL = 1, E_LOG = 2};		/* Flags for elog() function	*/
typedef unsigned long big_int_t;	/* Type for Content-Length	*/
	
/*
 * Unified socket address
 */
struct usa {
	socklen_t len;
	union {
		struct sockaddr	sa;
		struct sockaddr_in sin;
	} u;
};

/*
 * This thing is aimed to hold values of any type.
 * Used to store parsed headers' values.
 */
union variant {
	char		*v_str;
	int		v_int;
	big_int_t	v_big_int;
	time_t		v_time;
	void		(*v_func)(void);
	void		*v_void;
	struct vec	v_vec;
};

/*
 * This is used only in embedded configuration. This structure holds a
 * registered URI, associated callback function with callback data.
 * For non-embedded compilation shttpd_callback_t is not defined, so
 * we use union variant to keep the compiler silent.
 */
struct registered_uri {
	struct llhead	link;
	const char	*uri;
	union variant	callback;
	void		*callback_data;
};

/*
 * User may want to handle certain errors. This structure holds the
 * handlers for corresponding error codes.
 */
struct error_handler {
	struct llhead	link;
	int		code;
	union variant	callback;
	void		*callback_data;
};

struct http_header {
	int		len;		/* Header name length		*/
	int		type;		/* Header type			*/
	size_t		offset;		/* Value placeholder		*/
	const char	*name;		/* Header name			*/
};

/*
 * This guy holds parsed HTTP headers
 */
struct headers {
	union variant	cl;		/* Content-Length:		*/
	union variant	ct;		/* Content-Type:		*/
	union variant	connection;	/* Connection:			*/
	union variant	ims;		/* If-Modified-Since:		*/
	union variant	user;		/* Remote user name		*/
	union variant	auth;		/* Authorization		*/
	union variant	useragent;	/* User-Agent:			*/
	union variant	referer;	/* Referer:			*/
	union variant	cookie;		/* Cookie:			*/
	union variant	location;	/* Location:			*/
	union variant	range;		/* Range:			*/
	union variant	status;		/* Status:			*/
	union variant	transenc;	/* Transfer-Encoding:		*/
};

/* Must go after union variant definition */
#include "ssl.h"

/*
 * The communication channel
 */
union channel {
	int		fd;		/* Regular static file		*/
	int		sock;		/* Connected socket		*/
	struct {
		int		sock;	/* XXX important. must be first	*/
		SSL		*ssl;	/* shttpd_poll() assumes that	*/
	} ssl;				/* SSL-ed socket		*/
	struct {
		DIR	*dirp;
		char	*path;
	} dir;				/* Opened directory		*/
	struct {
		void		*state;	/* For keeping state		*/
		union variant	func;	/* User callback function	*/
		void		*data;	/* User defined parameters	*/
	} emb;				/* Embedded, user callback	*/
};

struct stream;

/*
 * IO class descriptor (file, directory, socket, SSL, CGI, etc)
 * These classes are defined in io_*.c files.
 */
struct io_class {
	const char *name;
	int (*read)(struct stream *, void *buf, size_t len);
	int (*write)(struct stream *, const void *buf, size_t len);
	void (*close)(struct stream *);
};

/*
 * Data exchange stream. It is backed by some communication channel:
 * opened file, socket, etc. The 'read' and 'write' methods are
 * determined by a communication channel.
 */
struct stream {
	struct conn		*conn;
	union channel		chan;		/* Descriptor		*/
	struct io		io;		/* IO buffer		*/
	const struct io_class	*io_class;	/* IO class		*/
	int			headers_len;
	big_int_t		content_len;
	unsigned int		flags;
#define	FLAG_HEADERS_PARSED	1
#define	FLAG_SSL_ACCEPTED	2
#define	FLAG_R			4		/* Can read in general	*/
#define	FLAG_W			8		/* Can write in general	*/
#define	FLAG_CLOSED		16
#define	FLAG_DONT_CLOSE		32
#define	FLAG_ALWAYS_READY	64		/* File, dir, user_func	*/
#define	FLAG_SUSPEND		128
};

struct worker {
	struct llhead	link;
	int		num_conns;	/* Num of active connections 	*/
	int		exit_flag;	/* Ditto - exit flag		*/
	int		ctl[2];		/* Control socket pair		*/
	struct shttpd_ctx *ctx;		/* Context reference		*/
	struct llhead	connections;	/* List of connections		*/
};

struct conn {
	struct llhead	link;		/* Connections chain		*/
	struct worker	*worker;	/* Worker this conn belongs to	*/
	struct shttpd_ctx *ctx;		/* Context this conn belongs to */
	struct usa	sa;		/* Remote socket address	*/
	time_t		birth_time;	/* Creation time		*/
	time_t		expire_time;	/* Expiration time		*/

	int		loc_port;	/* Local port			*/
	int		status;		/* Reply status code		*/
	int		method;		/* Request method		*/
	char		*uri;		/* Decoded URI			*/
	unsigned long	major_version;	/* Major HTTP version number    */
	unsigned long	minor_version;	/* Minor HTTP version number    */
	char		*request;	/* Request line			*/
	char		*headers;	/* Request headers		*/
	char		*query;		/* QUERY_STRING part of the URI	*/
	char		*path_info;	/* PATH_INFO thing		*/
	struct vec	mime_type;	/* Mime type			*/

	struct headers	ch;		/* Parsed client headers	*/

	struct stream	loc;		/* Local stream			*/
	struct stream	rem;		/* Remote stream		*/

#if !defined(NO_SSI)
	void			*ssi;	/* SSI descriptor		*/
#endif /* NO_SSI */
};

enum {
	OPT_ROOT, OPT_INDEX_FILES, OPT_PORTS, OPT_DIR_LIST,
	OPT_CGI_EXTENSIONS, OPT_CGI_INTERPRETER, OPT_CGI_ENVIRONMENT,
	OPT_SSI_EXTENSIONS, OPT_AUTH_REALM, OPT_AUTH_GPASSWD,
	OPT_AUTH_PUT, OPT_ACCESS_LOG, OPT_ERROR_LOG, OPT_MIME_TYPES,
	OPT_SSL_CERTIFICATE, OPT_ALIASES, OPT_ACL, OPT_INETD, OPT_UID,
	OPT_CFG_URI, OPT_PROTECT, OPT_SERVICE, OPT_HIDE, OPT_THREADS,
	NUM_OPTIONS
};

/*
 * SHTTPD context
 */
struct shttpd_ctx {
	SSL_CTX		*ssl_ctx;	/* SSL context			*/

	struct llhead	registered_uris;/* User urls			*/
	struct llhead	error_handlers;	/* Embedded error handlers	*/
	struct llhead	acl;		/* Access control list		*/
	struct llhead	ssi_funcs;	/* SSI callback functions	*/
	struct llhead	listeners;	/* Listening sockets		*/
	struct llhead	workers;	/* Worker workers		*/

	FILE		*access_log;	/* Access log stream		*/
	FILE		*error_log;	/* Error log stream		*/

	char	*options[NUM_OPTIONS];	/* Configurable options		*/
#if defined(__rtems__)
	rtems_id         mutex;
#endif /* _WIN32 */
};

struct listener {
	struct llhead		link;
	struct shttpd_ctx	*ctx;	/* Context that socket belongs	*/
	int			sock;	/* Listening socket		*/
	int			is_ssl;	/* Should be SSL-ed		*/
};

/* Types of messages that could be sent over the control socket */
enum {CTL_PASS_SOCKET, CTL_WAKEUP};

/*
 * In SHTTPD, list of values are represented as comma or space separated
 * string. For example, list of CGI extensions can be represented as
 * ".cgi,.php,.pl", or ".cgi .php .pl". The macro that follows allows to
 * loop through the individual values in that list.
 *
 * A "const char *" pointer and size_t variable must be passed to the macro.
 * Spaces or commas can be used as delimiters (macro DELIM_CHARS).
 *
 * In every iteration of the loop, "s" points to the current value, and
 * "len" specifies its length. The code inside loop must not change
 * "s" and "len" parameters.
 */
#define	FOR_EACH_WORD_IN_LIST(s,len)					\
	for (; s != NULL && (len = strcspn(s, DELIM_CHARS)) != 0;	\
			s += len, s+= strspn(s, DELIM_CHARS))

/*
 * IPv4 ACL entry. Specifies subnet with deny/allow flag
 */
struct acl {
	struct llhead	link;
	uint32_t	ip;		/* IP, in network byte order	*/
	uint32_t	mask;		/* Also in network byte order	*/
	int		flag;		/* Either '+' or '-'		*/
};

/*
 * shttpd.c
 */
extern time_t	_shttpd_current_time;	/* Current UTC time		*/
extern int	_shttpd_tz_offset;	/* Offset from GMT time zone	*/
extern const struct vec _shttpd_known_http_methods[];

extern void	_shttpd_stop_stream(struct stream *stream);
extern int	_shttpd_url_decode(const char *, int, char *dst, int);
extern void	_shttpd_send_server_error(struct conn *, int, const char *);
extern int	_shttpd_get_headers_len(const char *buf, size_t buflen);
extern void	_shttpd_parse_headers(const char *s, int, struct headers *);
extern int	_shttpd_is_true(const char *str);
extern int	_shttpd_socketpair(int pair[2]);
extern void	_shttpd_get_mime_type(struct shttpd_ctx *,
			const char *, int, struct vec *);

#define	IS_TRUE(ctx, opt)	_shttpd_is_true((ctx)->options[opt])

/*
 * config.c
 */
extern void	_shttpd_usage(const char *prog);

/*
 * log.c
 */
extern void	_shttpd_elog(int flags, struct conn *c, const char *fmt, ...);
extern void	_shttpd_log_access(FILE *fp, const struct conn *c);

/*
 * string.c
 */
extern void	_shttpd_strlcpy(register char *, register const char *, size_t);
extern int	_shttpd_strncasecmp(register const char *,
			register const char *, size_t);
extern char	*_shttpd_strndup(const char *ptr, size_t len);
extern char	*_shttpd_strdup(const char *str);
extern int	_shttpd_snprintf(char *buf, size_t len, const char *fmt, ...);
extern int	_shttpd_match_extension(const char *path, const char *ext_list);

/*
 * compat_*.c
 */
extern void	_shttpd_set_close_on_exec(int fd);
extern int	_shttpd_set_non_blocking_mode(int fd);
extern int	_shttpd_stat(const char *, struct stat *stp);
extern int	_shttpd_open(const char *, int flags, int mode);
extern int	_shttpd_remove(const char *);
extern int	_shttpd_rename(const char *, const char *);
extern int	_shttpd_mkdir(const char *, int);
extern char *	_shttpd_getcwd(char *, int);
extern int	_shttpd_spawn_process(struct conn *c, const char *prog,
			char *envblk, char *envp[], int sock, const char *dir);

extern int	_shttpd_set_nt_service(struct shttpd_ctx *, const char *);
extern int	_shttpd_set_systray(struct shttpd_ctx *, const char *);
extern void	_shttpd_try_to_run_as_nt_service(void);

/*
 * io_*.c
 */
extern const struct io_class	_shttpd_io_file;
extern const struct io_class	_shttpd_io_socket;
extern const struct io_class	_shttpd_io_ssl;
extern const struct io_class	_shttpd_io_cgi;
extern const struct io_class	_shttpd_io_dir;
extern const struct io_class	_shttpd_io_embedded;
extern const struct io_class	_shttpd_io_ssi;

extern int	_shttpd_put_dir(const char *path);
extern void	_shttpd_get_dir(struct conn *c);
extern void	_shttpd_get_file(struct conn *c, struct stat *stp);
extern void	_shttpd_ssl_handshake(struct stream *stream);
extern void	_shttpd_setup_embedded_stream(struct conn *,
			union variant, void *);
extern struct registered_uri *_shttpd_is_registered_uri(struct shttpd_ctx *,
			const char *uri);
extern void	_shttpd_do_ssi(struct conn *);
extern void	_shttpd_ssi_func_destructor(struct llhead *lp);

/*
 * auth.c
 */
extern int	_shttpd_check_authorization(struct conn *c, const char *path);
extern int	_shttpd_is_authorized_for_put(struct conn *c);
extern void	_shttpd_send_authorization_request(struct conn *c);
extern int	_shttpd_edit_passwords(const char *fname, const char *domain,
			const char *user, const char *pass);

/*
 * cgi.c
 */
extern int	_shttpd_run_cgi(struct conn *c, const char *prog);
extern void	_shttpd_do_cgi(struct conn *c);

#define CGI_REPLY	"HTTP/1.1     OK\r\n"
#define	CGI_REPLY_LEN	(sizeof(CGI_REPLY) - 1)

#endif /* DEFS_HEADER_DEFINED */
