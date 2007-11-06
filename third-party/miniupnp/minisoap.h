/* $Id: minisoap.h,v 1.3 2006/11/19 22:32:34 nanard Exp $ */
/* Project : miniupnp
 * Author : Thomas Bernard
 * Copyright (c) 2005 Thomas Bernard
 * This software is subject to the conditions detailed in the
 * LICENCE file provided in this distribution. */
#ifndef __MINISOAP_H__
#define __MINISOAP_H__

/*int httpWrite(int, const char *, int, const char *);*/
int soapPostSubmit(int, const char *, const char *, unsigned short,
				   const char *, const char *);

#endif

