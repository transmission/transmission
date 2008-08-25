/*
 * Copyright (c) 2004-2005 Sergey Lyubka <valenok@gmail.com>
 * All rights reserved
 *
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Sergey Lyubka wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.
 */

#ifndef CONFIG_HEADER_DEFINED
#define	CONFIG_HEADER_DEFINED

#define	VERSION		"1.42"		/* Version			*/
#define	CONFIG_FILE	"shttpd.conf"	/* Configuration file		*/
#define	HTPASSWD	".htpasswd"	/* Passwords file name		*/
#define	URI_MAX		16384		/* Default max request size	*/
#define	LISTENING_PORTS	"80"		/* Default listening ports	*/
#define	INDEX_FILES	"index.html,index.htm,index.php,index.cgi"
#define	CGI_EXT		"cgi,pl,php"	/* Default CGI extensions	*/
#define	SSI_EXT		"shtml,shtm"	/* Default SSI extensions	*/
#define	REALM		"mydomain.com"	/* Default authentication realm	*/
#define	DELIM_CHARS	","		/* Separators for lists		*/
#define	EXPIRE_TIME	3600		/* Expiration time, seconds	*/
#define	ENV_MAX		4096		/* Size of environment block	*/
#define	CGI_ENV_VARS	64		/* Maximum vars passed to CGI	*/
#define	SERVICE_NAME	"SHTTPD " VERSION	/* NT service name	*/

#endif /* CONFIG_HEADER_DEFINED */
