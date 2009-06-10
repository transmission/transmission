dnl convention: -TR MAJOR MINOR MAINT STATUS - (each a single char)
dnl STATUS: "X" for prerelease beta builds,
dnl         "Z" for unsupported trunk builds,
dnl         "0" for stable, supported releases
dnl these should be the only two lines you need to change
m4_define([user_agent_prefix],[1.71])
m4_define([peer_id_prefix],[-TR1710-])

AC_INIT([transmission],
        [user_agent_prefix],
        [http://trac.transmissionbt.com/newticket])
AC_SUBST(USERAGENT_PREFIX,[user_agent_prefix])
AC_SUBST(PEERID_PREFIX,[peer_id_prefix])

AC_CONFIG_MACRO_DIR([m4])

dnl AM_CONFIG_HEADER(config.h)
AC_CONFIG_SRCDIR(libtransmission/transmission.h)
AM_INIT_AUTOMAKE([1.9 tar-ustar])
AC_PROG_LIBTOOL

if test m4_substr(peer_id_prefix,6,1) = "0"; then
  supported_build=yes
  if test "x$GCC" = "xyes" ; then
    CFLAGS="$CFLAGS -g -O3 "
    CXXFLAGS="$CXXFLAGS -g -O3 "
  fi
  AC_DEFINE(TR_STABLE_RELEASE, 1, [Define to 1 if this is a stable release of Transmission])
else
  supported_build=no
  if test "x$GCC" = "xyes" ; then
    CFLAGS="$CFLAGS -g -O0"
    CXXFLAGS="$CXXFLAGS -g -O0"
  fi
  if test m4_substr(peer_id_prefix,6,1) = "X"; then
    AC_DEFINE(TR_BETA_RELEASE, 1, [Define to 1 if this is a beta release of Transmission])
  else
    AC_DEFINE(TR_NIGHTLY_RELEASE, 1, [Define to 1 if this is a nightly release of Transmission])
  fi
fi
AM_CONDITIONAL(TR_UNSTABLE, test "x$supported_build" = "xno")

OPENSSL_MINIMUM=0.9.4
CURL_MINIMUM=7.16.3
GIO_MINIMUM=2.15.5
GLIB_MINIMUM=2.6.0
GTK_MINIMUM=2.6.0
LIBNOTIFY_MINIMUM=0.4.3
DBUS_GLIB_MINIMUM=0.70
LIBEVENT_MINIMUM=1.4.5
AC_SUBST(OPENSSL_MINIMUM)
AC_SUBST(CURL_MINIMUM)
AC_SUBST(GIO_MINIMUM)
AC_SUBST(GLIB_MINIMUM)
AC_SUBST(GTK_MINIMUM)
AC_SUBST(LIBNOTIFY_MINIMUM)
AC_SUBST(DBUS_GLIB_MINIMUM)
AC_SUBST(LIBEVENT_MINIUM)

AC_PROG_CC
AC_PROG_CXX
AC_C_INLINE
if test "x$GCC" = "xyes" ; then

    CFLAGS="$CFLAGS -std=gnu99 -ggdb3 -Wall -W -Wpointer-arith -Wformat-security -Wcast-align -Wundef -Wcast-align -Wstrict-prototypes -Wmissing-declarations -Wmissing-format-attribute -Wredundant-decls -Wnested-externs -Wunused-parameter -Wwrite-strings"

    dnl figure out gcc version
    AC_MSG_CHECKING([gcc version])
    GCC_VERSION=`$CC -dumpversion`
    GCC_MAJOR=`echo $GCC_VERSION | cut -d . -f1`
    GCC_MINOR=`echo $GCC_VERSION | cut -d . -f2`
    GCC_VERSION_NUM=`(expr $GCC_MAJOR "*" 100 + $GCC_MINOR) 2>/dev/null`

    AC_MSG_RESULT($GCC_VERSION)
    if test $GCC_VERSION_NUM -ge 304; then
        dnl these were added in 3.4
        CFLAGS="$CFLAGS -Wextra -Wdeclaration-after-statement -Winit-self"
    fi
fi

AC_HEADER_STDC
AC_HEADER_TIME

AC_MSG_CHECKING([for fallocate])
AC_TRY_LINK([#include <linux/falloc.h>],
            [return fallocate(-1,0,0,0);],
            [AC_DEFINE([HAVE_FALLOCATE],[1],[Defined if fallocate() exists])
             AC_MSG_RESULT([yes])],
            [AC_MSG_RESULT([no])])
AC_CHECK_FUNCS([lrintf strlcpy daemon dirname basename daemon strcasecmp localtime_r posix_fallocate memmem])
AC_PROG_INSTALL
AC_PROG_MAKE_SET
ACX_PTHREAD

AC_SEARCH_LIBS([socket], [socket net])
AC_SEARCH_LIBS([gethostbyname], [nsl bind])
PKG_CHECK_MODULES(OPENSSL, [openssl >= $OPENSSL_MINIMUM], , [CHECK_SSL()])
PKG_CHECK_MODULES(LIBCURL, [libcurl >= $CURL_MINIMUM])
AC_PATH_ZLIB

AC_SYS_LARGEFILE


dnl ----------------------------------------------------------------------------
dnl
dnl posix_fadvise

dnl can posix_fadvise be used
AC_CHECK_DECLS(posix_fadvise, [], [], [
#define _XOPEN_SOURCE 600
#include <fcntl.h>])
AC_CHECK_FUNCS([posix_fadvise])


dnl ----------------------------------------------------------------------------
dnl
dnl file monitoring for the daemon

AC_CHECK_HEADER([sys/inotify.h], 
                [AC_CHECK_FUNC([inotify_init],[have_inotify="yes"],[have_inotify="no"])], 
                [have_inotify="no"]) 
AC_ARG_WITH([inotify], 
            [AS_HELP_STRING([--with-inotify],[Enable inotify support (default=auto)])], 
            [want_inotify=${enableval}], 
            [want_inotify=${have_inotify}]) 
if test "x$want_inotify" = "xyes" ; then 
    if test "x$have_inotify" = "xyes"; then 
      AC_DEFINE([WITH_INOTIFY],[1]) 
    else 
      AC_MSG_ERROR("inotify not found!") 
    fi 
fi 
 
AC_CHECK_HEADER([sys/event.h], 
                [AC_CHECK_FUNC([kqueue],[have_kqueue="yes"],[have_kqueue="no"])], 
                [have_kqueue="no"]) 
AC_ARG_WITH([kqueue], 
            [AS_HELP_STRING([--with-kqueue],[Enable kqueue support (default=auto)])], 
            [want_kqueue=${enableval}], 
            [want_kqueue=${have_kqueue}]) 
if test "x$want_kqueue" = "xyes" ; then 
    if test "x$have_kqueue" = "xyes"; then 
      AC_DEFINE([WITH_KQUEUE],[1]) 
    else 
      AC_MSG_ERROR("kqueue not found!") 
    fi 
fi 

AC_CHECK_HEADERS([xfs/xfs.h])


dnl ----------------------------------------------------------------------------
dnl
dnl va_copy

AC_MSG_CHECKING([how to copy va_list])
AC_TRY_LINK([#include <stdarg.h>], [va_list ap1, ap2; va_copy(ap1, ap2);],
    AC_MSG_RESULT([va_copy]),
        [ AH_TEMPLATE([va_copy], [define if va_copy is not available])
        AC_TRY_LINK([#include <stdarg.h>], [va_list ap1, ap2; __va_copy(ap1, ap2);],
        [ AC_DEFINE([va_copy], [__va_copy])
        AC_MSG_RESULT([__va_copy])],
        [ AC_DEFINE([va_copy(dest,src)], [memcpy(&dest,&src,sizeof(va_list))])
        AC_MSG_RESULT([memcpy])]
    )
])


dnl build our copy of libevent whether we use it or not,
dnl because "make dist" needs its Makefiles to exist, and
dnl AM_CONDITIONAL + AC_CONFIG_SUBDIRS don't seem to play nice
AC_CONFIG_SUBDIRS([third-party/libevent])
libevent_source=bundled
AC_CHECK_LIB([event],[evutil_vsnprintf],
             [libevent_found=yes],
             [libevent_found=no],
             [-levent])
AC_CHECK_HEADER([event-config.h],
                [libevent_headers_found=yes],
                [libevent_headers_found=no])
libevent_source=bundled
if test "x$libevent_found" = "xyes" ; then
    if test "x$libevent_headers_found" = "xyes"; then
      libevent_source=system
    fi
fi
if test "x$libevent_source" = "xbundled"; then
    AC_MSG_WARN([using our own libevent from third-party/libevent/])
    AC_MSG_WARN([if you are cross-compiling this is probably NOT what you want.])
    LIBEVENT_CPPFLAGS="-I\$(top_srcdir)/third-party/libevent -I\$(top_builddir)/third-party/libevent"
    LIBEVENT_LIBS="\$(top_builddir)/third-party/libevent/libevent.la"
else
    LIBEVENT_CPPFLAGS=""
    LIBEVENT_LIBS="-levent -lrt"
fi
AC_SUBST(LIBEVENT_CPPFLAGS)
AC_SUBST(LIBEVENT_LIBS)


dnl ----------------------------------------------------------------------------
dnl
dnl  detection for the GTK+ client

PKG_CHECK_MODULES(GTK,
                  [gtk+-2.0 >= $GTK_MINIMUM
                   glib-2.0 >= $GLIB_MINIMUM
                   gmodule-2.0 >= $GLIB_MINIMUM
                   gthread-2.0 >= $GLIB_MINIMUM],
                  [have_gtk=yes],
                  [have_gtk=no])
AC_ARG_ENABLE([gtk],
              AS_HELP_STRING([--enable-gtk],[build gtk client]),
              [want_gtk=${enableval}],
              [want_gtk=${have_gtk}])
build_gtk=no
use_gio=no
use_libnotify=no
use_dbus_glib=no
if test "x$want_gtk" = "xyes" ; then
    if test "x$have_gtk" = "xyes"; then
      build_gtk=yes
    else
      AC_MSG_ERROR("GTK+ not found!")
    fi
fi
AM_CONDITIONAL([BUILD_GTK],[test "x$build_gtk" = "xyes"])
AC_SUBST(GTK_LIBS)
AC_SUBST(GTK_CFLAGS)

if test "x$build_gtk" = "xyes"; then

    PKG_CHECK_MODULES([GIO],
                      [gio-2.0 >= $GIO_MINIMUM],
                      [use_gio=yes],
                      [use_gio=no])
    AC_SUBST(GIO_LIBS)
    AC_SUBST(GIO_CFLAGS)
    if test "x$use_gio" = "xyes"; then
        AC_DEFINE([HAVE_GIO], 1)
    fi

    PKG_CHECK_MODULES([LIBNOTIFY],
                      [libnotify >= $LIBNOTIFY_MINIMUM],
                      [have_libnotify=yes],
                      [have_libnotify=no])
    AC_ARG_ENABLE([libnotify], 
                  AS_HELP_STRING([--enable-libnotify],[enable notifications]),, 
                  [enable_libnotify=yes]) 
    use_libnotify=no
    if test "x$enable_libnotify" = "xyes" ; then
        if test "x$have_libnotify" = "xyes"; then
            use_libnotify=yes
            AC_SUBST(LIBNOTIFY_LIBS) 
            AC_SUBST(LIBNOTIFY_CFLAGS) 
            AC_DEFINE([HAVE_LIBNOTIFY], 1) 
        fi
    fi

    PKG_CHECK_MODULES([DBUS_GLIB],
                      [dbus-glib-1 >= $DBUS_GLIB_MINIMUM],
                      [use_dbus_glib=yes],
                      [use_dbus_glib=no])
    AC_SUBST(DBUS_GLIB_LIBS)
    AC_SUBST(DBUS_GLIB_CFLAGS)
    if test "x$use_dbus_glib" = "xyes"; then
        AC_DEFINE([HAVE_DBUS_GLIB], 1)
    fi
    if test "x$use_dbus_glib" = "xyes"; then
        AC_PATH_PROG(DBUS_BINDING_TOOL, dbus-binding-tool, no)
        if test "x$DBUS_BINDING_TOOL" = xno; then
          AC_MSG_WARN([Cannot find dbus-binding-tool])
          use_dbus_glib="no (dbus-binding-tool not found)"
        fi
    fi
fi

AC_ARG_ENABLE([nls], 
              AS_HELP_STRING([--enable-nls],[enable native language support]),,
              [enable_nls=yes])

if test "x$build_gtk" = "xyes" -a  "x$enable_nls" = "xno" ; then
    AC_MSG_ERROR("The gtk client cannot be built without nls support.  Try adding either --enable-nls or --disable-gtk" )
fi

use_nls=no
if test "x$enable_nls" = "xyes" ; then
    use_nls=yes
    IT_PROG_INTLTOOL([0.40.0],[no-xml])
    AC_CHECK_HEADERS([libintl.h])
    GETTEXT_PACKAGE=transmission
    AC_SUBST(GETTEXT_PACKAGE)
    AC_DEFINE_UNQUOTED([GETTEXT_PACKAGE],["$GETTEXT_PACKAGE"],[Gettext package])
    AM_GLIB_GNU_GETTEXT
    transmissionlocaledir='${prefix}/${DATADIRNAME}/locale'
    AC_SUBST(transmissionlocaledir)
fi


dnl ----------------------------------------------------------------------------
dnl
dnl  platform-specific stuff.

AC_CANONICAL_HOST
have_darwin="no"
have_msw="no"
case $host_os in

   *cygwin|*mingw32*)
     have_msw="yes"
     CXXFLAGS="$CXXFLAGS -mms-bitfields -mwin32 -mwindows"
     CPPFLAGS="$CPPFLAGS -DWIN32 -D_WIN32 -DWIN32_LEAN_AND_MEAN"
     LIBS="$LIBS -lshell32 -lws2_32"
     transmissionlocaledir="locale"
     if test -z "$host_alias"; then
       hostaliaswindres=
     else
       hostaliaswindres="$host_alias-windres";
     fi
     AC_CHECK_TOOL(WINDRES, windres)
     ;;

  *darwin*)
    have_darwin="yes"
    # Make sure the Universal SDK is installed
    if test ! -d /Developer/SDKs/MacOSX10.4u.sdk; then
      cat << EOF
You need to install the Universal SDK in order to build Transmission:
  Get your Xcode CD or package
  Restart the install
  When it gets to "Installation Type", select "Customize"
  Select "Mac OS X 10.4 (Universal) SDK" under "Cross Development"
  Finish the install.
EOF
      exit 1
    fi
    ;;

esac

AC_ARG_ENABLE([cli],
              [AS_HELP_STRING([--enable-cli],[build command-line client])],
              [build_cli=${enableval}],
              [build_cli="yes"])
AM_CONDITIONAL([BUILD_CLI],[test "x$build_cli" = "xyes"])

AC_ARG_ENABLE([mac],
              [AS_HELP_STRING([--enable-mac],[build OS X client])],
              [build_mac=${enableval}],
              [build_mac=${have_darwin}])
AM_CONDITIONAL([BUILD_MAC],[test "x$build_mac" = "xyes"])

AC_ARG_ENABLE([daemon],
              [AS_HELP_STRING([--enable-daemon],[build daemon])],
              [build_daemon=${enableval}],
              [build_daemon="yes"])
AM_CONDITIONAL([BUILD_DAEMON],[test "x$build_daemon" = "xyes"])


if test "x$have_darwin" = "xyes"; then
    AC_DEFINE([HAVE_DARWIN], 1)
fi
if test "x$have_msw" = "xyes"; then
    AC_DEFINE([HAVE_MSW], 1)
fi
AM_CONDITIONAL(WIN32, test "x$have_msw" = "xyes")

dnl ----------------------------------------------------------------------------
dnl
dnl  Generate the output

AC_CONFIG_FILES([Makefile
                 transmission.spec
                 cli/Makefile
                 daemon/Makefile
                 doc/Makefile
                 libtransmission/Makefile
                 third-party/Makefile
                 third-party/miniupnp/Makefile
                 third-party/libnatpmp/Makefile
                 third-party/dht/Makefile
                 macosx/Makefile
                 gtk/Makefile
                 gtk/icons/Makefile
                 po/Makefile.in])

ac_configure_args="$ac_configure_args --enable-static --disable-shared -q"
AC_OUTPUT

echo "

Configuration:

        Source code location:          ${srcdir}
        Compiler:                      ${CXX}
        System or bundled libevent:    ${libevent_source}

        Build OS X client:             ${build_mac}
        Build GTK+ client:             ${build_gtk}
          ... with gio support:        ${use_gio}
          ... with dbus-glib support:  ${use_dbus_glib}
          ... with libnotify support:  ${use_libnotify}
        Build Command-Line client:     ${build_cli}
        Build Daemon:                  ${build_daemon}

"
