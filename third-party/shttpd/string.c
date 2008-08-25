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

void
_shttpd_strlcpy(register char *dst, register const char *src, size_t n)
{
	for (; *src != '\0' && n > 1; n--)
		*dst++ = *src++;
	*dst = '\0';
}

int
_shttpd_strncasecmp(const char *str1, const char *str2, size_t len)
{
	register const unsigned char	*s1 = (unsigned char *) str1,
		 			*s2 = (unsigned char *) str2, *e;
	int				ret;

	for (e = s1 + len - 1; s1 < e && *s1 != '\0' && *s2 != '\0' &&
	    tolower(*s1) == tolower(*s2); s1++, s2++) ;
	ret = tolower(*s1) - tolower(*s2);

	return (ret);
}

char *
_shttpd_strndup(const char *ptr, size_t len)
{
	char	*p;

	if ((p = malloc(len + 1)) != NULL)
		_shttpd_strlcpy(p, ptr, len + 1);

	return (p);

}

char *
_shttpd_strdup(const char *str)
{
	return (_shttpd_strndup(str, strlen(str)));
}

/*
 * Sane snprintf(). Acts like snprintf(), but never return -1 or the
 * value bigger than supplied buffer.
 * Thanks Adam Zeldis to pointing snprintf()-caused vulnerability
 * in his audit report.
 */
int
_shttpd_snprintf(char *buf, size_t buflen, const char *fmt, ...)
{
	va_list		ap;
	int		n;

	if (buflen == 0)
		return (0);

	va_start(ap, fmt);
	n = vsnprintf(buf, buflen, fmt, ap);
	va_end(ap);

	if (n < 0 || (size_t) n >= buflen)
		n = buflen - 1;
	buf[n] = '\0';

	return (n);
}

/*
 * Verify that given file has certain extension
 */
int
_shttpd_match_extension(const char *path, const char *ext_list)
{
	size_t		len, path_len;
	
	path_len = strlen(path);

	FOR_EACH_WORD_IN_LIST(ext_list, len)
		if (len < path_len && path[path_len - len - 1] == '.' &&
		    !_shttpd_strncasecmp(path + path_len - len, ext_list, len))
			return (TRUE);

	return (FALSE);
}
