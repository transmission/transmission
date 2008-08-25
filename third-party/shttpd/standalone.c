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

static int	exit_flag;	/* Program termination flag	*/

static void
signal_handler(int sig_num)
{
	switch (sig_num) {
#ifndef _WIN32
	case SIGCHLD:
		while (waitpid(-1, &sig_num, WNOHANG) > 0) ;
		break;
#endif /* !_WIN32 */
	default:
		exit_flag = sig_num;
		break;
	}
}

int
main(int argc, char *argv[])
{
	struct shttpd_ctx	*ctx;

#if !defined(NO_AUTH)
	if (argc > 1 && argv[1][0] == '-' && argv[1][1] == 'A') {
		if (argc != 6)
			_shttpd_usage(argv[0]);
		exit(_shttpd_edit_passwords(argv[2],argv[3],argv[4],argv[5]));
	}
#endif /* NO_AUTH */

	if (argc == 2 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")))
		_shttpd_usage(argv[0]);

#if defined(_WIN32)
	try_to_run_as_nt_service();
#endif /* _WIN32 */

#ifndef _WIN32
	(void) signal(SIGCHLD, signal_handler);
	(void) signal(SIGPIPE, SIG_IGN);
#endif /* _WIN32 */

	(void) signal(SIGTERM, signal_handler);
	(void) signal(SIGINT, signal_handler);

	if ((ctx = shttpd_init(argc, argv)) == NULL)
		_shttpd_elog(E_FATAL, NULL, "%s",
		    "Cannot initialize SHTTPD context");

	_shttpd_elog(E_LOG, NULL, "shttpd %s started on port(s) %s, serving %s",
	    VERSION, ctx->options[OPT_PORTS], ctx->options[OPT_ROOT]);

	while (exit_flag == 0)
		shttpd_poll(ctx, 10 * 1000);

	_shttpd_elog(E_LOG, NULL, "Exit on signal %d", exit_flag);
	shttpd_fini(ctx);

	return (EXIT_SUCCESS);
}
