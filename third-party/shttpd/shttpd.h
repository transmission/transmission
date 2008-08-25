/*
 * Copyright (c) 2004-2008 Sergey Lyubka <valenok@gmail.com>
 * All rights reserved
 *
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Sergey Lyubka wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.
 *
 * $Id: shttpd.h,v 1.18 2008/08/23 08:34:50 drozd Exp $
 */

#ifndef SHTTPD_HEADER_INCLUDED
#define	SHTTPD_HEADER_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct ubuf {
	char		*buf;		/* Buffer pointer		*/
	int		len;		/* Size of a buffer		*/
	int		num_bytes;	/* Bytes processed by callback	*/
};

/*
 * This structure is passed to the user callback function
 */
struct shttpd_arg {
	void		*priv;		/* Private! Do not touch!	*/
	void		*state;		/* User state			*/
	void		*user_data;	/* Data from register_uri()	*/
	struct ubuf	in;		/* Input is here, POST data	*/
	struct ubuf	out;		/* Output goes here		*/

	unsigned int	flags;
#define	SHTTPD_END_OF_OUTPUT	1	/* No more data do send		*/
#define	SHTTPD_CONNECTION_ERROR	2	/* Server closed the connection	*/
#define	SHTTPD_MORE_POST_DATA	4	/* arg->in has incomplete data	*/
#define	SHTTPD_POST_BUFFER_FULL	8	/* arg->in has max data		*/
#define	SHTTPD_SSI_EVAL_TRUE	16	/* SSI eval callback must set it*/
#define	SHTTPD_SUSPEND		32	/* User wants to suspend output	*/
};

/*
 * User callback function. Called when certain registered URLs have been
 * requested. These are the requirements to the callback function:
 *
 * 1. It must copy data into 'out.buf' buffer, not more than 'out.len' bytes,
 *	and record how many bytes are copied, into 'out.num_bytes'
 * 2. It must not call any blocking functions
 * 3. It must set SHTTPD_END_OF_OUTPUT flag when there is no more data to send
 * 4. For POST requests, it must process the incoming data (in.buf) of length
 *	'in.len', and set 'in.num_bytes', which is how many bytes of POST
 *	data was processed and can be discarded by SHTTPD.
 * 5. If callback allocates arg->state, to keep state, it must deallocate it
 *    at the end of coonection SHTTPD_CONNECTION_ERROR or SHTTPD_END_OF_OUTPUT
 * 6. If callback function wants to suspend until some event, it must store
 *	arg->priv pointer elsewhere, set SHTTPD_SUSPEND flag and return. When
 *	the event happens, user code should call shttpd_wakeup(priv).
 *	It is safe to call shttpd_wakeup() from any thread. User code must
 *	not call shttpd_wakeup once the connection is closed.
 */
typedef void (*shttpd_callback_t)(struct shttpd_arg *);

/*
 * shttpd_init		Initialize shttpd context
 * shttpd_fini		Dealocate the context, close all connections
 * shttpd_set_option	Set new value for option
 * shttpd_register_uri	Setup the callback function for specified URL
 * shttpd_poll		Do connections processing
 * shttpd_version	return string with SHTTPD version
 * shttpd_get_var	Fetch POST/GET variable value by name. Return value len
 * shttpd_get_header	return value of the specified HTTP header
 * shttpd_get_env	return values for the following	pseudo-variables:
 			"REQUEST_METHOD", "REQUEST_URI",
 *			"REMOTE_USER" and "REMOTE_ADDR"
 * shttpd_printf	helper function to output data
 * shttpd_handle_error	register custom HTTP error handler
 * shttpd_wakeup	clear SHTTPD_SUSPEND state for the connection
 */

struct shttpd_ctx;

struct shttpd_ctx *shttpd_init(int argc, char *argv[]);
int shttpd_set_option(struct shttpd_ctx *, const char *opt, const char *val);
void shttpd_fini(struct shttpd_ctx *);
void shttpd_register_uri(struct shttpd_ctx *ctx, const char *uri,
		shttpd_callback_t callback, void *const user_data);
void shttpd_poll(struct shttpd_ctx *, int milliseconds);
const char *shttpd_version(void);
int shttpd_get_var(const char *var, const char *buf, int buf_len,
		char *value, int value_len);
const char *shttpd_get_header(struct shttpd_arg *, const char *header_name);
const char *shttpd_get_env(struct shttpd_arg *, const char *name);
void shttpd_get_http_version(struct shttpd_arg *,
		unsigned long *major, unsigned long *minor);
size_t shttpd_printf(struct shttpd_arg *, const char *fmt, ...);
void shttpd_handle_error(struct shttpd_ctx *ctx, int status,
		shttpd_callback_t func, void *const data);
void shttpd_register_ssi_func(struct shttpd_ctx *ctx, const char *name,
		shttpd_callback_t func, void *const user_data);
void shttpd_wakeup(const void *priv);
int shttpd_join(struct shttpd_ctx *, fd_set *, fd_set *, int *max_fd);
int  shttpd_socketpair(int sp[2]);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SHTTPD_HEADER_INCLUDED */
