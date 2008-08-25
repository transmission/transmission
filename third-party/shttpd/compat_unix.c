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
_shttpd_set_close_on_exec(int fd)
{
	(void) fcntl(fd, F_SETFD, FD_CLOEXEC);
}

int
_shttpd_stat(const char *path, struct stat *stp)
{
	return (stat(path, stp));
}

int
_shttpd_open(const char *path, int flags, int mode)
{
	return (open(path, flags, mode));
}

int
_shttpd_remove(const char *path)
{
	return (remove(path));
}

int
_shttpd_rename(const char *path1, const char *path2)
{
	return (rename(path1, path2));
}

int
_shttpd_mkdir(const char *path, int mode)
{
	return (mkdir(path, mode));
}

char *
_shttpd_getcwd(char *buffer, int maxlen)
{
	return (getcwd(buffer, maxlen));
}

int
_shttpd_set_non_blocking_mode(int fd)
{
	int	ret = -1;
	int	flags;

	if ((flags = fcntl(fd, F_GETFL, 0)) == -1) {
		DBG(("nonblock: fcntl(F_GETFL): %d", ERRNO));
	} else if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
		DBG(("nonblock: fcntl(F_SETFL): %d", ERRNO));
	} else {
		ret = 0;	/* Success */
	}

	return (ret);
}

#ifndef NO_CGI
int
_shttpd_spawn_process(struct conn *c, const char *prog, char *envblk,
		char *envp[], int sock, const char *dir)
{
	int		ret;
	pid_t		pid;
	const char	*p, *interp = c->ctx->options[OPT_CGI_INTERPRETER];

	envblk = NULL;	/* unused */

	if ((pid = vfork()) == -1) {

		ret = -1;
		_shttpd_elog(E_LOG, c, "redirect: fork: %s", strerror(errno));

	} else if (pid == 0) {

		/* Child */

		(void) chdir(dir);
		(void) dup2(sock, 0);
		(void) dup2(sock, 1);
		(void) closesocket(sock);

		/* If error file is specified, send errors there */
		if (c->ctx->error_log)
			(void) dup2(fileno(c->ctx->error_log), 2);

		if ((p = strrchr(prog, '/')) != NULL)
			p++;
		else
			p = prog;

		/* Execute CGI program */
		if (interp == NULL) {
			(void) execle(p, p, NULL, envp);
			_shttpd_elog(E_FATAL, c, "redirect: exec(%s)", prog);
		} else {
			(void) execle(interp, interp, p, NULL, envp);
			_shttpd_elog(E_FATAL, c, "redirect: exec(%s %s)",
			    interp, prog);
		}

		/* UNREACHED */
		exit(EXIT_FAILURE);

	} else {

		/* Parent */
		ret = 0;
		(void) closesocket(sock);
	}

	return (ret);
}
#endif /* !NO_CGI */
