/**********************************************************************
 *
 *  rtems shttpd management
 *
 *  FILE NAME   : rtems_shttpd.c
 *
 *  AUTHOR      : Steven Johnson
 *
 *  DESCRIPTION : Defines the interface functions to the shttp daemon
 *
 *  REVISION    : $Id: compat_rtems.c,v 1.2 2006/11/12 03:29:17 infidel Exp $
 *
 *  COMMENTS    :
 *
 **********************************************************************/

 /**********************************************************************
 * INCLUDED MODULES
 **********************************************************************/
#include <rtems.h>
#include "defs.h"

#define MAX_WEB_BASE_PATH_LENGTH 256
#define MIN_SHTTPD_STACK         (8*1024)

typedef struct RTEMS_HTTPD_ARGS {
    rtems_shttpd_init     init_callback;
    rtems_shttpd_addpages addpages_callback;
    char                  webroot[MAX_WEB_BASE_PATH_LENGTH];
} RTEMS_HTTPD_ARGS;

static int rtems_webserver_running = FALSE; //not running.

static rtems_task rtems_httpd_daemon(rtems_task_argument args )
{
  RTEMS_HTTPD_ARGS *httpd_args = (RTEMS_HTTPD_ARGS*)args;

  struct shttpd_ctx       *ctx;

  if (httpd_args != NULL)
    if (httpd_args->init_callback != NULL)
      httpd_args->init_callback();

/**************************************
 *  Initialize the web server
 */
  /*
    * Initialize SHTTPD context.
    * Set WWW root to current WEB_ROOT_PATH.
    */
  ctx = shttpd_init(NULL, "document_root", httpd_args->webroot, NULL);

  if (httpd_args != NULL)
    if (httpd_args->addpages_callback != NULL)
      httpd_args->addpages_callback(ctx);

  /* Finished with args, so free them */
  if (httpd_args != NULL)
    free(httpd_args);

  /* Open listening socket */
  shttpd_listen(ctx, 9000);

  rtems_webserver_running = TRUE;

  /* Serve connections infinitely until someone kills us */
  while (rtems_webserver_running)
    shttpd_poll(ctx, 1000);

  /* Unreached, because we will be killed by a signal */
  shttpd_fini(ctx);

  rtems_task_delete( RTEMS_SELF );
}

rtems_status_code rtems_initialize_webserver(rtems_task_priority   initial_priority,
                                             rtems_unsigned32      stack_size,
                                             rtems_mode            initial_modes,
                                             rtems_attribute       attribute_set,
                                             rtems_shttpd_init     init_callback,
                                             rtems_shttpd_addpages addpages_callback,
                                             char                 *webroot
                                            )
{
  rtems_status_code   sc;
  rtems_id            tid;
  RTEMS_HTTPD_ARGS    *args;

  if (stack_size < MIN_SHTTPD_STACK)
    stack_size = MIN_SHTTPD_STACK;

  args = malloc(sizeof(RTEMS_HTTPD_ARGS));

  if (args != NULL)
  {
    args->init_callback = init_callback;
    args->addpages_callback = addpages_callback;
    strncpy(args->webroot,webroot,MAX_WEB_BASE_PATH_LENGTH);

    sc = rtems_task_create(rtems_build_name('H', 'T', 'P', 'D'),
                           initial_priority,
                           stack_size,
                           initial_modes,
                           attribute_set,
                           &tid);

    if (sc == RTEMS_SUCCESSFUL)
    {
      sc = rtems_task_start(tid, rtems_httpd_daemon, (rtems_task_argument)args);
    }
  }
  else
  {
    sc = RTEMS_NO_MEMORY;
  }

  return sc;
}

void rtems_terminate_webserver(void)
{
  rtems_webserver_running = FALSE; //not running, so terminate
}

int rtems_webserver_ok(void)
{
  return rtems_webserver_running;
}

void
set_close_on_exec(int fd)
{
        // RTEMS Does not have a functional "execve"
        // so technically this call does not do anything,
        // but it doesnt hurt either.
        (void) fcntl(fd, F_SETFD, FD_CLOEXEC);
}

int
my_stat(const char *path, struct stat *stp)
{
        return (stat(path, stp));
}

int
my_open(const char *path, int flags, int mode)
{
        return (open(path, flags, mode));
}

int
my_remove(const char *path)
{
	return (remove(path));
}

int
my_rename(const char *path1, const char *path2)
{
	return (rename(path1, path2));
}

int
my_mkdir(const char *path, int mode)
{
	return (mkdir(path, mode));
}

char *
my_getcwd(char *buffer, int maxlen)
{
	return (getcwd(buffer, maxlen));
}

int
set_non_blocking_mode(int fd)
{
        int     ret = -1;
        int     flags;

        if ((flags = fcntl(fd, F_GETFL, 0)) == -1) {
                DBG(("nonblock: fcntl(F_GETFL): %d", ERRNO));
        } else if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
                DBG(("nonblock: fcntl(F_SETFL): %d", ERRNO));
        } else {
                ret = 0;        /* Success */
        }

        return (ret);
}

#if !defined(NO_CGI)
int
spawn_process(struct conn *c, const char *prog, char *envblk, char **envp)
{
        return (-1); // RTEMS does not have subprocess support as standard.
}
#endif
