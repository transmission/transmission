/*
 * Copyright (c) 2004-2005 Sergey Lyubka <valenok@gmail.com>
 * All rights reserved
 *
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Sergey Lyubka wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.
 */

#ifndef MD5_HEADER_INCLUDED
#define	MD5_HEADER_INCLUDED

typedef struct MD5Context {
	uint32_t	buf[4];
	uint32_t	bits[2];
	unsigned char	in[64];
} MD5_CTX;

extern void MD5Init(MD5_CTX *ctx);
extern void MD5Update(MD5_CTX *ctx, unsigned char const *buf, unsigned len);
extern void MD5Final(unsigned char digest[16], MD5_CTX *ctx);

#endif /*MD5_HEADER_INCLUDED */
