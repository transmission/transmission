/*
 * Copyright (c) 2004-2005 Sergey Lyubka <valenok@gmail.com>
 * All rights reserved
 *
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Sergey Lyubka wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.
 */

#ifndef STD_HEADERS_INCLUDED
#define	STD_HEADERS_INCLUDED

#ifndef _WIN32_WCE /* Some ANSI #includes are not available on Windows CE */
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#endif /* _WIN32_WCE */

#include <stdlib.h>
#include <stdarg.h>
#include <assert.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <wchar.h>

#if defined(_WIN32)		/* Windows specific	*/
#include "compat_win32.h"
#elif defined(__rtems__)	/* RTEMS specific	*/
#include "compat_rtems.h"
#else				/* UNIX  specific	*/
#include "compat_unix.h"
#endif /* _WIN32 */

#endif /* STD_HEADERS_INCLUDED */
