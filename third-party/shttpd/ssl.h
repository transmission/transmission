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
 * Snatched from OpenSSL includes. I put the prototypes here to be independent
 * from the OpenSSL source installation. Having this, shttpd + SSL can be
 * built on any system with binary SSL libraries installed.
 */

typedef struct ssl_st SSL;
typedef struct ssl_method_st SSL_METHOD;
typedef struct ssl_ctx_st SSL_CTX;

#define	SSL_ERROR_WANT_READ	2
#define	SSL_ERROR_WANT_WRITE	3
#define SSL_FILETYPE_PEM	1

/*
 * Dynamically loaded SSL functionality
 */
struct ssl_func {
	const char	*name;		/* SSL function name	*/
	union variant	ptr;		/* Function pointer	*/
};

extern struct ssl_func	ssl_sw[];

#define	FUNC(x)	ssl_sw[x].ptr.v_func

#define	SSL_free(x)	(* (void (*)(SSL *)) FUNC(0))(x)
#define	SSL_accept(x)	(* (int (*)(SSL *)) FUNC(1))(x)
#define	SSL_connect(x)	(* (int (*)(SSL *)) FUNC(2))(x)
#define	SSL_read(x,y,z)	(* (int (*)(SSL *, void *, int)) FUNC(3))((x),(y),(z))
#define	SSL_write(x,y,z) \
	(* (int (*)(SSL *, const void *,int)) FUNC(4))((x), (y), (z))
#define	SSL_get_error(x,y)(* (int (*)(SSL *, int)) FUNC(5))((x), (y))
#define	SSL_set_fd(x,y)	(* (int (*)(SSL *, int)) FUNC(6))((x), (y))
#define	SSL_new(x)	(* (SSL * (*)(SSL_CTX *)) FUNC(7))(x)
#define	SSL_CTX_new(x)	(* (SSL_CTX * (*)(SSL_METHOD *)) FUNC(8))(x)
#define	SSLv23_server_method()	(* (SSL_METHOD * (*)(void)) FUNC(9))()
#define	SSL_library_init() (* (int (*)(void)) FUNC(10))()
#define	SSL_CTX_use_PrivateKey_file(x,y,z)	(* (int (*)(SSL_CTX *, \
		const char *, int)) FUNC(11))((x), (y), (z))
#define	SSL_CTX_use_certificate_file(x,y,z)	(* (int (*)(SSL_CTX *, \
		const char *, int)) FUNC(12))((x), (y), (z))
