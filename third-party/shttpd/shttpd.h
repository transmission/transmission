/*
 * Copyright (c) 2004-2005 Sergey Lyubka <valenok@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * $Id: shttpd.h,v 1.9 2008/02/13 20:44:32 drozd Exp $
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
	void		*user_data;	/* User-defined data		*/
	struct ubuf	in;		/* Input is here, POST data	*/
	struct ubuf	out;		/* Output goes here		*/
	unsigned int	flags;
#define	SHTTPD_END_OF_OUTPUT	1
#define	SHTTPD_CONNECTION_ERROR	2
#define	SHTTPD_MORE_POST_DATA	4
#define	SHTTPD_POST_BUFFER_FULL	8
#define	SHTTPD_SSI_EVAL_TRUE	16
};

/*
 * User callback function. Called when certain registered URLs have been
 * requested. These are the requirements to the callback function:
 *
 * 1. it must copy data into 'out.buf' buffer, not more than 'out.len' bytes,
 *	and record how many bytes are copied, into 'out.num_bytes'
 * 2. it must not block the execution
 * 3. it must set SHTTPD_END_OF_OUTPUT flag when finished
 * 4. for POST requests, it must process the incoming data (in.buf) of length
 *	'in.len', and set 'in.num_bytes', which is how many bytes of POST
 *	data is read and can be discarded by SHTTPD.
 * 5. If callback allocates arg->state, to keep state, it must deallocate it
 *    at the end of coonection SHTTPD_CONNECTION_ERROR or SHTTPD_END_OF_OUTPUT
 */
typedef void (*shttpd_callback_t)(struct shttpd_arg *);

/*
 * shttpd_init		Initialize shttpd context.
 * shttpd_set_option	Set new value for option.
 * shttpd_fini		Dealocate the context
 * shttpd_register_uri	Setup the callback function for specified URL.
 * shtppd_listen	Setup a listening socket in the SHTTPD context
 * shttpd_poll		Do connections processing
 * shttpd_version	return string with SHTTPD version
 * shttpd_get_var	Fetch POST/GET variable value by name. Return value len
 * shttpd_get_header	return value of the specified HTTP header
 * shttpd_get_env	return string values for the following
 *			pseudo-variables: "REQUEST_METHOD", "REQUEST_URI",
 *			"REMOTE_USER" and "REMOTE_ADDR".
 */

struct shttpd_ctx;

struct shttpd_ctx *shttpd_init(void);
void shttpd_set_option(struct shttpd_ctx *, const char *opt, const char *val);
void shttpd_fini(struct shttpd_ctx *);
int shttpd_listen(struct shttpd_ctx *ctx, int port, int is_ssl);
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

/*
 * The following three functions are for applications that need to
 * load-balance the connections on their own. Many threads may be spawned
 * with one SHTTPD context per thread. Boss thread may only wait for
 * new connections by means of shttpd_accept(). Then it may scan thread
 * pool for the idle thread by means of shttpd_active(), and add new
 * connection to the context by means of shttpd_add().
 */
void shttpd_add_socket(struct shttpd_ctx *, int sock, int is_ssl);
int shttpd_accept(int lsn_sock, int milliseconds);
int shttpd_active(struct shttpd_ctx *);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SHTTPD_HEADER_INCLUDED */
