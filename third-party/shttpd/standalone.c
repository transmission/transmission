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

static int		exit_flag;

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

void
process_command_line_arguments(struct shttpd_ctx *ctx, char *argv[])
{
	const char	*config_file = CONFIG_FILE;
	char		line[BUFSIZ], opt[BUFSIZ],
			val[BUFSIZ], path[FILENAME_MAX], *p;
	FILE		*fp;
	size_t		i, line_no = 0;

	/* First find out, which config file to open */
	for (i = 1; argv[i] != NULL && argv[i][0] == '-'; i += 2)
		if (argv[i + 1] == NULL)
			usage(argv[0]);

	if (argv[i] != NULL && argv[i + 1] != NULL) {
		/* More than one non-option arguments are given w*/
		usage(argv[0]);
	} else if (argv[i] != NULL) {
		/* Just one non-option argument is given, this is config file */
		config_file = argv[i];
	} else {
		/* No config file specified. Look for one where shttpd lives */
		if ((p = strrchr(argv[0], DIRSEP)) != 0) {
			my_snprintf(path, sizeof(path), "%.*s%s",
			    p - argv[0] + 1, argv[0], config_file);
			config_file = path;
		}
	}

	fp = fopen(config_file, "r");

	/* If config file was set in command line and open failed, exit */
	if (fp == NULL && argv[i] != NULL)
		elog(E_FATAL, NULL, "cannot open config file %s: %s",
		    config_file, strerror(errno));

	if (fp != NULL) {

		elog(E_LOG, NULL, "Loading config file %s", config_file);

		/* Loop over the lines in config file */
		while (fgets(line, sizeof(line), fp) != NULL) {

			line_no++;

			/* Ignore empty lines and comments */
			if (line[0] == '#' || line[0] == '\n')
				continue;

			if (sscanf(line, "%s %[^\n#]", opt, val) != 2)
				elog(E_FATAL, NULL, "line %d in %s is invalid",
				    line_no, config_file);

			shttpd_set_option(ctx, opt, val);
		}

		(void) fclose(fp);
	}

	/* Now pass through the command line options */
	for (i = 1; argv[i] != NULL && argv[i][0] == '-'; i += 2)
		shttpd_set_option(ctx, &argv[i][1], argv[i + 1]);
}

int
main(int argc, char *argv[])
{
	struct shttpd_ctx	*ctx;

#if !defined(NO_AUTH)
	if (argc > 1 && argv[1][0] == '-' && argv[1][1] == 'A') {
		if (argc != 6)
			usage(argv[0]);
		exit(edit_passwords(argv[2],argv[3],argv[4],argv[5]));
	}
#endif /* NO_AUTH */

	if (argc == 2 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")))
		usage(argv[0]);

	ctx = shttpd_init();
	process_command_line_arguments(ctx, argv);

#ifndef _WIN32
	/* Switch to alternate UID, it is safe now, after shttpd_listen() */
	if (ctx->options[OPT_UID] != NULL) {
		struct passwd	*pw;

		if ((pw = getpwnam(ctx->options[OPT_UID])) == NULL)
			elog(E_FATAL, 0, "main: unknown user [%s]",
			    ctx->options[OPT_UID]);
		else if (setgid(pw->pw_gid) == -1)
			elog(E_FATAL, NULL, "main: setgid(%s): %s",
			    ctx->options[OPT_UID], strerror(errno));
		else if (setuid(pw->pw_uid) == -1)
			elog(E_FATAL, NULL, "main: setuid(%s): %s",
			    ctx->options[OPT_UID], strerror(errno));
	}
	(void) signal(SIGCHLD, signal_handler);
	(void) signal(SIGPIPE, SIG_IGN);
#endif /* _WIN32 */

	(void) signal(SIGTERM, signal_handler);
	(void) signal(SIGINT, signal_handler);

	if (IS_TRUE(ctx, OPT_INETD)) {
		shttpd_set_option(ctx, "ports", NULL);
		(void) freopen("/dev/null", "a", stderr);
		shttpd_add_socket(ctx, fileno(stdin), 0);
	}

	elog(E_LOG, NULL, "shttpd %s started on port(s) %s, serving %s",
	    VERSION, ctx->options[OPT_PORTS], ctx->options[OPT_ROOT]);

	while (exit_flag == 0)
		shttpd_poll(ctx, 5000);

	elog(E_LOG, NULL, "%d requests %.2lf Mb in %.2lf Mb out. "
	    "Exit on signal %d", ctx->nrequests, (double) (ctx->in / 1048576),
	    (double) ctx->out / 1048576, exit_flag);

	shttpd_fini(ctx);

	return (EXIT_SUCCESS);
}
