dnl convention: -TR MAJOR MINOR MAINT STATUS - (each a single char)
dnl STATUS: "X" for prerelease test builds,
dnl         "Z" for unsupported trunk builds,
dnl         "0" for stable, supported releases
dnl these should be the only two lines you need to change
m4_define([user_agent_prefix],[1.42])
m4_define([peer_id_prefix],[-TR1420-])

AC_INIT([transmission],[user_agent_prefix],[http://trac.transmissionbt.com/newticket])
AC_SUBST(USERAGENT_PREFIX,[user_agent_prefix])
AC_SUBST(PEERID_PREFIX,[peer_id_prefix])

AC_CONFIG_MACRO_DIR([m4])

dnl AM_CONFIG_HEADER(config.h)
AC_CONFIG_SRCDIR(libtransmission/transmission.h)
AM_INIT_AUTOMAKE([1.9 tar-ustar])
AC_PROG_LIBTOOL

if test m4_substr(peer_id_prefix,6,1) = "Z"; then
  AC_DEFINE(TR_UNSTABLE, 1, [Define to 1 if this is an unstable version of Transmission])
  TR_UNSTABLE=yes
fi
AM_CONDITIONAL(TR_UNSTABLE, test "x$TR_UNSTABLE" = "xyes")

OPENSSL_MINIMUM=0.9.4
CURL_MINIMUM=7.16.3
GIO_MINIMUM=2.15.5
GLIB_MINIMUM=2.6.0
GTK_MINIMUM=2.6.0
WX_MINIMUM=2.6.0
LIBNOTIFY_MINIMUM=0.4.4
DBUS_GLIB_MINIMUM=0.70
AC_SUBST(OPENSSL_MINIMUM)
AC_SUBST(CURL_MINIMUM)
AC_SUBST(GIO_MINIMUM)
AC_SUBST(GLIB_MINIMUM)
AC_SUBST(GTK_MINIMUM)
AC_SUBST(WX_MINIMUM)
AC_SUBST(LIBNOTIFY_MINIMUM)
AC_SUBST(DBUS_GLIB_MINIMUM)

AC_PROG_CC
AC_PROG_CXX
if test "x$GCC" = "xyes" ; then
    CFLAGS="-g -Wall -Wformat-security -W -Wmissing-prototypes -Wdeclaration-after-statement -O3 -funroll-loops"
    CXXFLAGS="-g -Wall -Wformat-security -W -O3 -funroll-loops"
fi

AC_HEADER_STDC
AC_HEADER_TIME

AC_MSG_CHECKING([for fallocate])
AC_TRY_LINK([#include <linux/falloc.h>],
            [return fallocate(-1,0,0,0);],
            [AC_DEFINE([HAVE_FALLOCATE],[1],[Defined if fallocate() exists])
             AC_MSG_RESULT([yes])],
            [AC_MSG_RESULT([no])])
AC_CHECK_FUNCS([lrintf strlcpy daemon dirname basename daemon strcasecmp localtime_r posix_fallocate])
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


dnl ----------------------------------------------------------------------------
dnl
dnl  libevent fun

AC_CONFIG_SUBDIRS([third-party/libevent])
AC_MSG_NOTICE([invoking libevent's configure script])
LIBEVENT_CPPFLAGS="-I\$(top_srcdir)/third-party/libevent"
AC_SUBST(LIBEVENT_CPPFLAGS)


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
              AC_HELP_STRING([--enable-gtk],[build gtk client]),
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

AC_CHECK_HEADERS([libintl.h])
IT_PROG_INTLTOOL([0.23],[no-xml])
GETTEXT_PACKAGE=transmission
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED([GETTEXT_PACKAGE],["$GETTEXT_PACKAGE"],[Gettext package])
AM_GLIB_GNU_GETTEXT
transmissionlocaledir='${prefix}/${DATADIRNAME}/locale'
AC_SUBST(transmissionlocaledir)


dnl ----------------------------------------------------------------------------
dnl
dnl  wxWidgets detection for the wxWidgets client

build_wx=no
AC_ARG_ENABLE([wx],
              AC_HELP_STRING([--enable-wx],[build wxWidgets client]),
              [want_wx=${enableval}],
              [want_wx=maybe])
if test "x$want_wx" != "xno"; then
    AM_OPTIONS_WXCONFIG
    AM_PATH_WXCONFIG($WX_MINIMUM,[have_wx=yes],[have_wx=no])
    if test "x$have_wx" = "xyes"; then
        build_wx=yes
    elif test "x$want_wx" = "xyes"; then 
        AC_MSG_ERROR("wxWidgets not found!")
    fi
fi
AM_CONDITIONAL([BUILD_WX],[test "x$build_wx" = "xyes"])



dnl ----------------------------------------------------------------------------
dnl
dnl  platform-specific stuff.

AC_CANONICAL_HOST
have_beos="no"
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

  *beos*)
    have_beos="yes"
    RELEASE=`uname -r`
    case $RELEASE in
      6.*|5.0.4) # Zeta or R5 / BONE beta 7
        ;;
      5.0*)       # R5 / net_server
        CPPFLAGS="$CPPFLAGS -DBEOS_NETSERVER"
        ;;
      *)
        AC_MSG_ERROR("Unsupported BeOS version")
        ;;
    esac
    GCCVER=`$CC -dumpversion`
    case $GCCVER in
      2.95.3*|3*|4*)
        ;;
      2.9*)
        BEOS_OLDCC=yes
        ;;
      *)
        AC_MSG_ERROR("Unsupported gcc version")
        ;;
    esac
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

AC_ARG_ENABLE([beos],
              [AC_HELP_STRING([--enable-beos],[build OS X client])],
              [build_beos=${enableval}],
              [build_beos=${have_beos}])
AM_CONDITIONAL([BUILD_BEOS],[test "x$build_beos" = "xyes"])

AC_ARG_ENABLE([cli],
              [AC_HELP_STRING([--enable-cli],[build command-line client])],
              [build_cli=${enableval}],
              [build_cli="yes"])
AM_CONDITIONAL([BUILD_CLI],[test "x$build_cli" = "xyes"])

AC_ARG_ENABLE([mac],
              [AC_HELP_STRING([--enable-mac],[build OS X client])],
              [build_mac=${enableval}],
              [build_mac=${have_darwin}])
AM_CONDITIONAL([BUILD_MAC],[test "x$build_mac" = "xyes"])

AC_ARG_ENABLE([daemon],
              [AC_HELP_STRING([--enable-daemon],[build daemon])],
              [build_daemon=${enableval}],
              [build_daemon="yes"])
AM_CONDITIONAL([BUILD_DAEMON],[test "x$build_daemon" = "xyes"])


if test "x$have_beos" = "xyes"; then
    AC_DEFINE([HAVE_BEOS], 1)
fi
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
                 beos/Makefile
                 cli/Makefile
                 daemon/Makefile
                 doc/Makefile
                 libtransmission/Makefile
                 third-party/Makefile
                 third-party/miniupnp/Makefile
                 third-party/libnatpmp/Makefile
                 macosx/Makefile
                 wx/Makefile
                 wx/images/Makefile
                 gtk/Makefile
                 gtk/icons/Makefile
                 po/Makefile.in])

ac_configure_args="$ac_configure_args --enable-static --disable-shared -q"
AC_OUTPUT

echo "

Configuration:

        Source code location:       ${srcdir}
        Compiler:                   ${CXX}
        Build Command-Line client:  ${build_cli}
        Build Daemon:               ${build_daemon}
        Build BeOS client:          ${build_beos}
        Build GTK+ client:          ${build_gtk}
          ... gio support:          ${use_gio}
          ... dbus-glib support:    ${use_dbus_glib}
          ... libnotify support:    ${use_libnotify}
        Build OS X client:          ${build_mac}
        Build wxWidgets client:     ${build_wx}

"
