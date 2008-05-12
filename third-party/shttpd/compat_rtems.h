/**
 * @file rtems/rtems-shttpd.h
 */

#ifndef _rtems_rtems_webserver_h
#define _rtems_rtems_webserver_h

#include "shttpd.h"

#include <rtems.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <dirent.h>

/* RTEMS is an Real Time Embedded operating system, for operation in hardware.
  It does not have SSL or CGI support, as it does not have dynamic library
  loading or sub-processes. */
#define EMBEDDED
#define NO_SSL
#define NO_CGI

#define DIRSEP                          '/'
#define O_BINARY                        0
#define ERRNO                           errno

/* RTEMS version is Thread Safe */
#define InitializeCriticalSection(x)  rtems_semaphore_create( \
                                  rtems_build_name('H','T','P','X'), \
                                  1, /* Not Held Yet.*/ \
                                  RTEMS_FIFO | \
                                  RTEMS_BINARY_SEMAPHORE, \
                                  0, \
                                  x);
#define EnterCriticalSection(x) rtems_semaphore_obtain(*(x),RTEMS_WAIT,RTEMS_NO_TIMEOUT)
#define LeaveCriticalSection(x) rtems_semaphore_release(*(x))



#ifdef __cplusplus
extern "C" {
#endif

typedef void (*rtems_shttpd_addpages)(struct shttpd_ctx *ctx);
typedef void (*rtems_shttpd_init)(void);

rtems_status_code rtems_initialize_webserver(rtems_task_priority   initial_priority,
                                             rtems_unsigned32      stack_size,
                                             rtems_mode            initial_modes,
                                             rtems_attribute       attribute_set,
                                             rtems_shttpd_init     init_callback,
                                             rtems_shttpd_addpages addpages_callback,
                                             char                 *webroot
                                            );
void rtems_terminate_webserver(void);
int  rtems_webserver_ok(void);

#ifdef __cplusplus
}
#endif
#endif
