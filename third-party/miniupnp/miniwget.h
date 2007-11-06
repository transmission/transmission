/* $Id: miniwget.h,v 1.5 2007/01/29 20:27:23 nanard Exp $ */
/* Project : miniupnp
 * Author : Thomas Bernard
 * Copyright (c) 2005 Thomas Bernard
 * This software is subject to the conditions detailed in the
 * LICENCE file provided in this distribution.
 * */
#ifndef __MINIWGET_H__
#define __MINIWGET_H__

#include "declspec.h"

#ifdef __cplusplus
extern "C" {
#endif

LIBSPEC void * miniwget(const char *, int *);

LIBSPEC void * miniwget_getaddr(const char *, int *, char *, int);

int parseURL(const char *, char *, unsigned short *, char * *);

#ifdef __cplusplus
}
#endif

#endif

