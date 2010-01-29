dnl convention: -TR MAJOR MINOR MAINT STATUS - (each a single char)
dnl STATUS: "X" for prerelease beta builds,
dnl         "Z" for unsupported trunk builds,
dnl         "0" for stable, supported releases
dnl these should be the only two lines you need to change
m4_define([user_agent_prefix],[1.83])
m4_define([peer_id_prefix],[-TR1830-])

AC_INIT([transmission],
        [user_agent_prefix],
        [http://trac.transmissionbt.com/newticket])
AC_SUBST(USERAGENT_PREFIX,[user_agent_prefix])
AC_SUBST(PEERID_PREFIX,[peer_id_prefix])

m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])])

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
  CPPFLAGS="$CPPFLAGS -DNDEBUG"
else
  supported_build=no
  if test "x$GCC" = "xyes" ; then
    CFLAGS="$CFLAGS -g -O0"
    CXXFLAGS="$CXXFLAGS -g -O0"
  fi
fi
AM_CONDITIONAL(TR_UNSTABLE, test "x$supported_build" = "xno")

# MANDATORY for libtransmission
CURL_MINIMUM=7.16.3
AC_SUBST(CURL_MINIMUM)
LIBEVENT_MINIMUM=1.4.5
AC_SUBST(LIBEVENT_MINIUM)
OPENSSL_MINIMUM=0.9.4
AC_SUBST(OPENSSL_MINIMUM)

# MANDATORY for the GTK+ client
GLIB_MINIMUM=2.6.0
AC_SUBST(GLIB_MINIMUM)
GTK_MINIMUM=2.6.0
AC_SUBST(GTK_MINIMUM)

# OPTIONAL for the GTK+ client
# play the XDG "download done" sound...
CANBERRA_MINIMUM=0.10
AC_SUBST(CANBERRA_MINIMUM)
# inhibit hibernation when a torrent is active...
DBUS_GLIB_MINIMUM=0.70
AC_SUBST(DBUS_GLIB_MINIMUM)
# register the GTK+ client as a magnet link handler...
GCONF2_MINIMUM=2.20.0
AC_SUBST(GCONF2_MINIMUM)
# implement "watch" directories to use new .torrent files...
GIO_MINIMUM=2.15.5
AC_SUBST(GIO_MINIMUM)
# pop up a "download done" notice...
LIBNOTIFY_MINIMUM=0.4.3
AC_SUBST(LIBNOTIFY_MINIMUM)


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

AC_CHECK_FUNCS([pread pwrite lrintf strlcpy daemon dirname basename strcasecmp localtime_r posix_fallocate memmem strtold syslog])
AC_PROG_INSTALL
AC_PROG_MAKE_SET
ACX_PTHREAD

if test "x$ac_cv_func_strtold" != "xyes" ; then
    CPPFLAGS="$CPPFLAGS -Dstrtold=strtod"
fi

AC_SEARCH_LIBS(cos, [m])
AC_SEARCH_LIBS([socket], [socket net])
AC_SEARCH_LIBS([gethostbyname], [nsl bind])
PKG_CHECK_MODULES(OPENSSL, [openssl >= $OPENSSL_MINIMUM], , [CHECK_SSL()])
PKG_CHECK_MODULES(LIBCURL, [libcurl >= $CURL_MINIMUM])
AC_PATH_ZLIB

AC_SYS_LARGEFILE
AC_CHECK_FUNCS([lseek64])


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
            [want_inotify=${withval}], 
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
            [want_kqueue=${withval}], 
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


dnl libevent likes to link against librt if possible
dnl for clock_gettime() and clock_settime()
dnl TODO(libevent2): this can probably be removed after
dnl we switch to libevent2, since it will have PKG_CONFIG
AC_CHECK_LIB([rt],
             [clock_gettime],
             [libevent_extra_libs="-lrt"],
             [libevent_extra_libs=""])

dnl build our copy of libevent whether we use it or not,
dnl because "make dist" needs its Makefiles to exist, and
dnl AM_CONDITIONAL + AC_CONFIG_SUBDIRS don't seem to play nice
AC_CONFIG_SUBDIRS([third-party/libevent])
libevent_source=bundled
AC_CHECK_LIB([event],[evutil_vsnprintf],
             [libevent_found=yes],
             [libevent_found=no],
             [$libevent_extra_libs])
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
    LIBEVENT_CFLAGS="-I\$(top_srcdir)/third-party/libevent -I\$(top_builddir)/third-party/libevent"
    LIBEVENT_LIBS="\$(top_builddir)/third-party/libevent/libevent.la"
else
    LIBEVENT_CFLAGS=""
    LIBEVENT_LIBS="-levent $libevent_extra_libs"
fi
AC_ARG_VAR([LIBEVENT_CFLAGS], [C compiler flags for LIBEVENT, overriding pkg-config])dnl
AC_ARG_VAR([LIBEVENT_LIBS], [linker flags for LIBEVENT, overriding pkg-config])dnl



dnl ----------------------------------------------------------------------------
dnl
dnl  dht

DHT_CFLAGS="-I\$(top_srcdir)/third-party/dht"
DHT_LIBS="\$(top_builddir)/third-party/dht/libdht.a"
AC_SUBST(DHT_CFLAGS)
AC_SUBST(DHT_LIBS)


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

if test "x$build_gtk" = "xyes"; then

    PKG_CHECK_MODULES([GIO],
                      [gio-2.0 >= $GIO_MINIMUM],
                      [use_gio=yes],
                      [use_gio=no])
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
            AC_DEFINE([HAVE_LIBNOTIFY], 1) 
        fi
    fi

    PKG_CHECK_MODULES([LIBCANBERRA],
                      [libcanberra-gtk >= $CANBERRA_MINIMUM],
                      [have_libcanberra=yes],
                      [have_libcanberra=no])
    AC_ARG_ENABLE([libcanberra], 
                  AS_HELP_STRING([--enable-libcanberra],[enable sounds]),, 
                  [enable_libcanberra=yes]) 
    use_canberra=no
    if test "x$enable_libcanberra" = "xyes" ; then
        if test "x$have_libcanberra" = "xyes"; then
            use_canberra=yes
            AC_DEFINE([HAVE_LIBCANBERRA], 1) 
        fi
    fi

    PKG_CHECK_MODULES([LIBGCONF],
                      [gconf-2.0 >= $GCONF2_MINIMUM],
                      [have_libgconf=yes],
                      [have_libgconf=no])
    AC_ARG_ENABLE([gconf2], 
                  AS_HELP_STRING([--enable-libgconf],[enable GConf support]),, 
                  [enable_libgconf=yes]) 
    use_libgconf=no
    if test "x$enable_libgconf" = "xyes" ; then
        if test "x$have_libgconf" = "xyes"; then
            use_libgconf=yes
            AC_DEFINE([HAVE_LIBGCONF], 1) 
        fi
    fi

    PKG_CHECK_MODULES([DBUS_GLIB],
                      [dbus-glib-1 >= $DBUS_GLIB_MINIMUM],
                      [use_dbus_glib=yes],
                      [use_dbus_glib=no])
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

dnl This section is only used for internationalization.
dnl If you don't need translations and this section gives you trouble --
dnl such as if you're building for a headless system --
dnl it's okay to tear this section out and re-build the configure script.
dnl
dnl Note to packagers: the bump to intltool 0.40 was made to fix
dnl a "make check" failure on some systems.  if upgrading to 0.40 is
dnl a problem and the old version was working fine for you,
dnl it should be safe to re-edit 0.40 back down to 0.23

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

   *mingw32*)
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
    # Make sure the proper Mac SDK is installed
    if test ! -d /Developer/SDKs/MacOSX10.5.sdk; then
      cat << EOF
You need to install the Mac OS X 10.5 SDK in order to build Transmission:
  Get your Xcode CD or package
  Restart the install
  When it gets to "Installation Type", select "Customize"
  Select "Mac OS X 10.5 SDK" under "Cross Development"
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
              [AS_HELP_STRING([--enable-mac],[build Mac client])],
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
                 web/Makefile
                 web/images/Makefile
                 web/images/buttons/Makefile
                 web/images/graphics/Makefile
                 web/images/progress/Makefile
                 web/javascript/Makefile
                 web/javascript/jquery/Makefile
                 web/stylesheets/Makefile
                 po/Makefile.in])

ac_configure_args="$ac_configure_args --enable-static --disable-shared -q"
AC_OUTPUT

echo "

Configuration:

        Source code location:          ${srcdir}
        Compiler:                      ${CXX}
        System or bundled libevent:    ${libevent_source}

        Build Mac client:              ${build_mac}
        Build GTK+ client:             ${build_gtk}
          ... with canberra support:   ${use_canberra}
          ... with gio support:        ${use_gio}
          ... with dbus-glib support:  ${use_dbus_glib}
          ... with libgconf support:   ${use_libgconf}
          ... with libnotify support:  ${use_libnotify}
        Build Command-Line client:     ${build_cli}
        Build Daemon:                  ${build_daemon}

"
