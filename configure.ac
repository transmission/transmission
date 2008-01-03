AC_INIT([transmission],[1.00],[http://www.transmissionbt.com/trac/newticket])
PEERID_PREFIX="-TR1000-"
USERAGENT_PREFIX="1.00"
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

have_gtk=yes
PKG_CHECK_MODULES(GTK, [gtk+-2.0 >= $GTK_MINIMUM
                        glib-2.0 >= $GLIB_MINIMUM
                        gmodule-2.0 >= $GLIB_MINIMUM
                        gthread-2.0 >= $GLIB_MINIMUM],, [have_gtk=no])
AC_ARG_WITH(gtk, AC_HELP_STRING([--with-gtk], [Build gtk client]),
                 [want_gtk=$withval],
                 [want_gtk=$have_gtk])
use_gtk=no
if test "x$want_gtk" = "xyes" ; then
    if test "x$have_gtk" = "xyes"; then
      use_gtk=yes
    else
      AC_MSG_ERROR("GTK+ not found!")
    fi
fi
AM_CONDITIONAL([WITH_GTK],[test "x$use_gtk" = "xyes"])
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
AC_ARG_WITH(wx, AC_HELP_STRING([--with-wx], [Build wxWidgets client]),
                [want_wx=$withval],
                [want_wx=no])
dnl                [want_wx=$have_wx])
use_wx=no
if test "x$want_wx" = "xyes" ; then
    if test "x$have_wx" = "xyes"; then
      use_wx=yes
    else
      AC_MSG_ERROR("wxWidgets not found!")
    fi
fi
AM_CONDITIONAL([HAVE_WX],[test "x$use_wx" = "xyes"])



dnl ----------------------------------------------------------------------------
dnl
dnl  platform-specific stuff.

AC_CANONICAL_HOST
beos="no"
darwin="no"
msw="no"
case $host_os in

   *cygwin|*mingw32*)
     msw="yes"
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
    beos="yes"
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
    darwin="yes"
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
AM_CONDITIONAL([HAVE_BEOS],[test "x$beos" = "xyes"])
AM_CONDITIONAL([HAVE_DARWIN],[test "x$darwin" = "xyes"])
AM_CONDITIONAL([HAVE_WINDOWS],[test "x$msw" = "xyes"])


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

        Source code location:    ${srcdir}
        Compiler:                ${CXX}
        Build libtransmission:   yes
        Build Daemon:            yes
        Build BeOS client:       ${beos}
        Build GTK+ client:       ${use_gtk}
        Build OS X client:       ${darwin}
        Build wxWidgets client:  ${use_wx}

"
