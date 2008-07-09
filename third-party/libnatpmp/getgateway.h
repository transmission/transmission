/* $Id: getgateway.h,v 1.3 2008/07/02 22:33:06 nanard Exp $ */
/* libnatpmp
 * Copyright (c) 2007, Thomas BERNARD <miniupnp@free.fr>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE. */
#ifndef __GETGATEWAY_H__
#define __GETGATEWAY_H__

#ifdef WIN32
#include <stdint.h>
#define in_addr_t uint32_t
#endif
#include "declspec.h"

/* getdefaultgateway() :
 * return value :
 *    0 : success
 *   -1 : failure    */
LIBSPEC int getdefaultgateway(in_addr_t * addr);

#endif
