AC_INIT([transmission],[1.01Z],[http://trac.transmissionbt.com/newticket])
PEERID_PREFIX="-TR101Z-"
USERAGENT_PREFIX="1.01+"
AC_SUBST(PEERID_PREFIX,[$PEERID_PREFIX])
AC_SUBST(USERAGENT_PREFIX,[$USERAGENT_PREFIX])

AC_PREREQ(2.54)
dnl AM_CONFIG_HEADER(config.h)
AC_CONFIG_SRCDIR(libtransmission/transmission.h)
AM_INIT_AUTOMAKE([1.9 tar-ustar])
AC_PROG_LIBTOOL

GLIB_MINIMUM=2.6.0
GTK_MINIMUM=2.6.0
WX_MINIMUM=2.6.0
AC_SUBST(GLIB_MINIMUM)
AC_SUBST(GTK_MINIMUM)
AC_SUBST(WX_MINIMUM)

AC_PROG_CC
AC_PROG_CXX
if test "x$GCC" = "xyes" ; then
    CFLAGS="-g -Wall -W -O3 -funroll-loops"
    CXXFLAGS="-g -Wall -W -O3 -funroll-loops"
fi
AC_HEADER_STDC
AC_HEADER_TIME
AC_CHECK_FUNCS([lrintf strlcpy strlcat daemon dirname basename])
AC_CHECK_SIZEOF([void*])
AC_PROG_INSTALL
AC_PROG_MAKE_SET
AC_PROG_RANLIB
ACX_PTHREAD

AC_SEARCH_LIBS([socket], [socket net])
AC_SEARCH_LIBS([gethostbyname], [nsl bind])
PKG_CHECK_MODULES(OPENSSL, [openssl >= 0.9.4])

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

AM_OPTIONS_WXCONFIG
AM_PATH_WXCONFIG($WX_MINIMUM,[have_wx=yes],[have_wx=no])
AC_ARG_ENABLE([wx],
              AC_HELP_STRING([--enable-wx],[build wxWidgets client]),
              [want_wx=${enableval}],
              [want_wx=no])
build_wx=no
if test "x$want_wx" = "xyes" ; then
    if test "x$have_wx" = "xyes"; then
      build_wx=yes
    else
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
     CPPFLAGS="$CPPFLAGS -DWIN32 -DWIN32_LEAN_AND_MEAN"
     LIBS="$LIBS -lshell32 -lws2_32"
     transmissionlocaledir="locale"
     if test -z "$host_alias"; then
       hostaliaswindres=
     else
       hostaliaswindres="$host_alias-windres";
     fi
     AC_PATH_PROGS([WINDRES], [windres $hostaliaswindres $host_os-windres])
     if test "x$WINDRES" = "x"; then
       AC_MSG_ERROR([windres could not be found within your path.])
     fi
     AC_SUBST(WINDRES)
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

AC_ARG_ENABLE([darwin],
              [AC_HELP_STRING([--enable-darwin],[build OS X client])],
              [build_darwin=${enableval}],
              [build_darwin=${have_darwin}])
AM_CONDITIONAL([BUILD_DARWIN],[test "x$build_darwin" = "xyes"])

AC_ARG_ENABLE([daemon],
              [AC_HELP_STRING([--enable-daemon],[build daemon])],
              [build_daemon=${enableval}],
              [build_daemon="yes"])
AM_CONDITIONAL([BUILD_DAEMON],[test "x$build_daemon" = "xyes"])


AC_DEFINE([HAVE_BEOS],[test "x$have_beos" = "xyes"])
AC_DEFINE([HAVE_DARWIN],[test "x$have_darwin" = "xyes"])
AC_DEFINE([HAVE_WINDOWS],[test "x$have_msw" = "xyes"])


dnl ----------------------------------------------------------------------------
dnl
dnl  Generate the output

AC_CONFIG_FILES([Makefile
                 transmission.spec
                 beos/Makefile
                 cli/Makefile
                 daemon/Makefile
                 libtransmission/Makefile
                 third-party/Makefile
                 third-party/miniupnp/Makefile
                 third-party/libnatpmp/Makefile
                 macosx/Makefile
                 wx/Makefile
                 wx/images/Makefile
                 gtk/Makefile
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
        Build OS X client:          ${build_darwin}
        Build wxWidgets client:     ${build_wx}

"
