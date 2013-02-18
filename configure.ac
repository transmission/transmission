dnl convention: -TR MAJOR MINOR MAINT STATUS - (each a single char)
dnl STATUS: "X" for prerelease beta builds,
dnl         "Z" for unsupported trunk builds,
dnl         "0" for stable, supported releases
dnl these should be the only two lines you need to change
m4_define([user_agent_prefix],[2.77])
m4_define([peer_id_prefix],[-TR2770-])

AC_INIT([transmission],[user_agent_prefix],[http://trac.transmissionbt.com/newticket])
AC_SUBST(USERAGENT_PREFIX,[user_agent_prefix])
AC_SUBST(PEERID_PREFIX,[peer_id_prefix])

m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])])

AC_CONFIG_MACRO_DIR([m4])

dnl AM_CONFIG_HEADER(config.h)
AC_CONFIG_SRCDIR(libtransmission/transmission.h)
AM_INIT_AUTOMAKE([1.9 tar-pax])
LT_INIT

if test m4_substr(peer_id_prefix,6,1) = "0"; then
  supported_build=yes
  CPPFLAGS="$CPPFLAGS -DNDEBUG"
else
  supported_build=no
  if test "x$GCC" = "xyes" ; then
    CFLAGS="$CFLAGS -g -O0"
    CXXFLAGS="$CXXFLAGS -g -O0"
  fi
fi
AM_CONDITIONAL(TR_UNSTABLE, test "x$supported_build" = "xno")

##
##
##   MANDATORY for everything
##
##
CURL_MINIMUM=7.15.4
AC_SUBST(CURL_MINIMUM)
LIBEVENT_MINIMUM=2.0.10
AC_SUBST(LIBEVENT_MINIMUM)
OPENSSL_MINIMUM=0.9.4
AC_SUBST(OPENSSL_MINIMUM)

##
##
##   MANDATORY for the GTK+ client
##
##

GTK_MINIMUM=3.4.0
AC_SUBST(GTK_MINIMUM)
GLIB_MINIMUM=2.32.0
AC_SUBST(GLIB_MINIMUM)
GIO_MINIMUM=2.26.0
AC_SUBST(GIO_MINIMUM)

##
##
##   OPTIONAL for the GTK+ client
##
##

# create the tray icon with AppIndicator
LIBAPPINDICATOR_MINIMUM=0.4.90
AC_SUBST(LIBAPPINDICATOR_MINIMUM)


AC_PROG_CC
AC_PROG_CXX
if test "x$CXX" != "x"; then # CXX is set...
  if test -f "$CXX"; then # maybe it's an absolute path passed in env variables...
    AC_MSG_CHECKING([for $CXX])
    HAVE_CXX="yes"
    AC_MSG_RESULT([$HAVE_CXX])
  else
    AC_CHECK_PROG([HAVE_CXX],[$CXX],[yes],[no])
  fi
fi
AC_C_INLINE
if test "x$GCC" = "xyes" ; then

    CFLAGS="$CFLAGS -std=gnu99 -ggdb3 -Wall -W -Wpointer-arith -Wformat-security -Wcast-align -Wundef -Wcast-align -Wstrict-prototypes -Wmissing-declarations -Wmissing-format-attribute -Wredundant-decls -Wnested-externs -Wunused-parameter -Wwrite-strings -Winline -Wfloat-equal"

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
    if test $GCC_VERSION_NUM -ge 403; then
        dnl these were added in 4.3
        CFLAGS="$CFLAGS -Wvariadic-macros"
    fi
fi

AC_HEADER_STDC
AC_HEADER_TIME

AC_CHECK_HEADERS([stdbool.h])
AC_CHECK_FUNCS([iconv_open pread pwrite lrintf strlcpy daemon dirname basename strcasecmp localtime_r fallocate64 posix_fallocate memmem strsep strtold syslog valloc getpagesize posix_memalign statvfs htonll ntohll mkdtemp])
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
PKG_CHECK_MODULES(LIBEVENT, [libevent >= $LIBEVENT_MINIMUM])
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

AC_CHECK_HEADERS([sys/statvfs.h \
                  xfs/xfs.h])


dnl ----------------------------------------------------------------------------
dnl
dnl  dht

DHT_CFLAGS="-I\$(top_srcdir)/third-party/dht"
DHT_LIBS="\$(top_builddir)/third-party/dht/libdht.a"
AC_SUBST(DHT_CFLAGS)
AC_SUBST(DHT_LIBS)


dnl ----------------------------------------------------------------------------
dnl
dnl  utp

AC_CHECK_LIB([rt],
             [clock_gettime],
             [libutp_extra_libs="-lrt"],
             [libutp_extra_libs=""])

AC_MSG_CHECKING([µTP])
build_utp="no"
if test "x$HAVE_CXX" = "xyes" ; then
    have_utp="yes"
else
    have_utp="no"
fi
AC_ARG_ENABLE([utp],
              AS_HELP_STRING([--enable-utp],[build µTP support]),
              [want_utp=${enableval}],
              [want_utp=${have_utp}])
if test "x$want_utp" = "xyes" ; then
    if test "x$have_utp" = "xyes"; then
        LIBUTP_CFLAGS="-I\$(top_srcdir)/third-party/"
        LIBUTP_LIBS="\$(top_builddir)/third-party/libutp/libutp.a"
        LIBUTP_LIBS_QT="\$\${TRANSMISSION_TOP}/third-party/libutp/libutp.a"
        if test "x$libutp_extra_libs" != "x" ; then
            LIBUTP_LIBS="$LIBUTP_LIBS $libutp_extra_libs"
            LIBUTP_LIBS_QT="$LIBUTP_LIBS_QT $libutp_extra_libs"
        fi
        AC_DEFINE([WITH_UTP],[1])
        build_utp="yes"
    else
      AC_MSG_ERROR("Unable to build uTP support -- C++ compiler not found")
    fi
fi
AC_SUBST(LIBUTP_CFLAGS)
AC_SUBST(LIBUTP_LIBS)
AC_SUBST(LIBUTP_LIBS_QT)
AM_CONDITIONAL([BUILD_UTP],[test "x$build_utp" = "xyes"])
AC_MSG_RESULT([$build_utp])


dnl
dnl  look for preinstalled miniupnpc...
dnl

AC_MSG_CHECKING([supported miniupnp library])
upnp_version="none"
ac_save_LIBS="$LIBS"
LIBS="-lminiupnpc"
# See if the OS has its miniupnp 1.5 installed
AC_LINK_IFELSE([AC_LANG_PROGRAM([[
    #include <stdlib.h>
    #include <miniupnpc/miniupnpc.h>
    #include <miniupnpc/upnpcommands.h>
]], [[
    struct UPNPDev * devlist;
    struct UPNPUrls    urls;
    struct IGDdatas    data;
    char lanaddr[16];
    char portStr[8];
    char intPort[8];
    char intClient[16];
    upnpDiscover( 2000, NULL, NULL, 0 );
    UPNP_GetValidIGD( devlist, &urls, &data, lanaddr, sizeof( lanaddr ) ); 
    UPNP_GetSpecificPortMappingEntry( urls.controlURL, data.first.servicetype,
                        portStr, "TCP", intClient, intPort );
]])],[
AC_DEFINE(HAVE_MINIUPNP_15, 1, [Define to 1 if you have miniupnpc version 1.5])
upnp_version="1.5"],[])

# See if the OS has its miniupnp 1.6 installed
AC_LINK_IFELSE([AC_LANG_PROGRAM([[
    #include <stdlib.h>
    #include <errno.h>
    #include <miniupnpc/miniupnpc.h>
    #include <miniupnpc/upnpcommands.h>
]], [[
    struct UPNPDev * devlist;
    struct UPNPUrls    urls;
    struct IGDdatas    data;
    char lanaddr[16];
    char portStr[8];
    char intPort[8];
    char intClient[16];
    upnpDiscover( 2000, NULL, NULL, 0, 0, &errno );
    UPNP_GetValidIGD( devlist, &urls, &data, lanaddr, sizeof( lanaddr ) ); 
    UPNP_GetSpecificPortMappingEntry( urls.controlURL, data.first.servicetype,
                        portStr, "TCP", intClient, intPort, NULL, NULL, NULL );
]])],[
AC_DEFINE(HAVE_MINIUPNP_16, 1, [Define to 1 if you have miniupnpc version 1.6])
upnp_version="1.6"],[])

# ... and the results of our tests
LIBS="$ac_save_LIBS"
AC_MSG_RESULT([$upnp_version])
AM_CONDITIONAL([BUILD_MINIUPNP],[test "x$upnp_version" = "xnone"])
if test "x$upnp_version" = "xnone" ; then
    LIBUPNP_CFLAGS="-I\$(top_srcdir)/third-party/"
    LIBUPNP_LIBS="\$(top_builddir)/third-party/miniupnp/libminiupnp.a"
    LIBUPNP_LIBS_QT="\$\${TRANSMISSION_TOP}/third-party/miniupnp/libminiupnp.a"
    dnl because this is the version that we bundle...
    AC_DEFINE(HAVE_MINIUPNP_16, 1, [Define to 1 if you have miniupnpc version 1.6])
else
    AC_DEFINE([SYSTEM_MINIUPNP])
    LIBUPNP_CFLAGS="" 
    LIBUPNP_LIBS="-lminiupnpc"
    LIBUPNP_LIBS_QT="-lminiupnpc"
fi
AC_SUBST(LIBUPNP_CFLAGS)
AC_SUBST(LIBUPNP_LIBS)
AC_SUBST(LIBUPNP_LIBS_QT)


 

dnl ----------------------------------------------------------------------------
dnl
dnl  Allow usage of system natpmp library 

LIBNATPMP_CFLAGS="-I\$(top_srcdir)/third-party/libnatpmp/" 
LIBNATPMP_LIBS="\$(top_builddir)/third-party/libnatpmp/libnatpmp.a" 
LIBNATPMP_LIBS_QT="\$\${TRANSMISSION_TOP}/third-party/libnatpmp/libnatpmp.a" 
build_bundled_natpmp="yes" 
AC_ARG_ENABLE([external-natpmp], 
              AS_HELP_STRING([--enable-external-natpmp],[Use system external-natpmp]), 
              [want_external_natpmp=${enableval}], 
              [want_external_natpmp=no]) 
if test "x$want_external_natpmp" != "xno" ; then 
	dnl Would be lovely if it had pkgconfig 
	LIBNATPMP_CFLAGS=""  
	LIBNATPMP_LIBS="-lnatpmp" 
	LIBNATPMP_LIBS_QT="-lnatpmp" 
	build_bundled_natpmp="no" 
fi 
AM_CONDITIONAL([BUILD_NATPMP],[test "x$build_bundled_natpmp" = "xyes"]) 
AC_SUBST(LIBNATPMP_CFLAGS) 
AC_SUBST(LIBNATPMP_LIBS) 
AC_SUBST(LIBNATPMP_LIBS_QT) 


dnl ----------------------------------------------------------------------------
dnl
dnl  detection for the GTK+ client

AC_ARG_ENABLE([nls],
              [AS_HELP_STRING([--enable-nls],[enable native language support])],,
              [enable_nls=yes])
PKG_CHECK_EXISTS([gtk+-3.0 >= $GTK_MINIMUM
                  glib-2.0 >= $GLIB_MINIMUM
                  gio-2.0 >= $GIO_MINIMUM,
                  gmodule-2.0 >= $GLIB_MINIMUM
                  gthread-2.0 >= $GLIB_MINIMUM],
                 [have_gtk=yes],
                 [have_gtk=no])
AC_ARG_WITH([gtk], AS_HELP_STRING([--with-gtk],[with Gtk]),
            [with_gtk=$withval],
            [with_gtk=$have_gtk])
AM_CONDITIONAL([BUILD_GTK],[test "x$with_gtk" = "xyes"])
use_libappindicator=no
if test "x$with_gtk" = "xyes" ; then

    if test "x$enable_nls" = "xno" ; then
        AC_MSG_ERROR("The gtk client cannot be built without nls support.  Try adding either --enable-nls or --without-gtk" )
    fi

    PKG_CHECK_MODULES([GTK], [gtk+-3.0 >= $GTK_MINIMUM
                              glib-2.0 >= $GLIB_MINIMUM
                              gio-2.0 >= $GIO_MINIMUM,
                              gmodule-2.0 >= $GLIB_MINIMUM
                              gthread-2.0 >= $GLIB_MINIMUM])
    PKG_CHECK_MODULES([LIBAPPINDICATOR],
                      [appindicator3-0.1 >= $LIBAPPINDICATOR_MINIMUM],
                      [have_libappindicator=yes],
                      [have_libappindicator=no])
    if test "x$have_libappindicator" = "xyes"; then
        use_libappindicator=yes
        AC_DEFINE([HAVE_LIBAPPINDICATOR], 1)
    else
        LIBAPPINDICATOR_CFLAGS=
        LIBAPPINDICATOR_LIBS=
    fi
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
    m4_ifdef([IT_PROG_INTLTOOL],
             [IT_PROG_INTLTOOL([0.35.0],[no-xml])],
             [AC_MSG_ERROR("--enable-nls requires intltool to be installed.")])
    AC_CHECK_HEADERS([libintl.h])
    GETTEXT_PACKAGE=transmission-gtk
    AC_SUBST(GETTEXT_PACKAGE)
    AC_DEFINE_UNQUOTED([GETTEXT_PACKAGE],["$GETTEXT_PACKAGE"],[Gettext package])
    AM_GLIB_GNU_GETTEXT
    transmissionlocaledir='${prefix}/${DATADIRNAME}/locale'
    AC_SUBST(transmissionlocaledir)
fi
AC_SUBST(INTLLIBS)


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
     LIBS="$LIBS -liphlpapi -lshell32 -lws2_32"
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
    CFLAGS="-DMACOSX $CFLAGS"
    ;;

esac

AC_ARG_ENABLE([lightweight],
              AS_HELP_STRING([--enable-lightweight],[optimize libtransmission for low-resource systems: smaller cache size, prefer unencrypted peer connections, etc.]),
              [enable_lightweight=${enableval}],
              [enable_lightweight="no"])
if test "x$enable_lightweight" = "xyes" ; then
  AC_DEFINE([TR_LIGHTWEIGHT],[1],[optimize libtransmission for low-resource systems])
fi

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


if test "x$build_mac" = "xyes" ; then
    # Make sure the proper Mac SDK is installed
    if test ! -d /Developer/SDKs/MacOSX10.5.sdk; then
      cat << EOF
You need to install the Mac OS X 10.5 SDK in order to build Transmission
with --enable-mac:
  Get your Xcode CD or package
  Restart the install
  When it gets to "Installation Type", select "Customize"
  Select "Mac OS X 10.5 SDK" under "Cross Development"
  Finish the install.
EOF
      exit 1
    fi
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
                 transmission-gtk.spec
                 cli/Makefile
                 daemon/Makefile
                 extras/Makefile
                 libtransmission/Makefile
                 utils/Makefile
                 third-party/Makefile
                 third-party/dht/Makefile
                 third-party/libutp/Makefile
                 third-party/libnatpmp/Makefile
                 third-party/miniupnp/Makefile
                 macosx/Makefile
                 gtk/Makefile
                 gtk/icons/Makefile
                 qt/config.pri
                 web/Makefile
                 web/images/Makefile
                 web/style/Makefile
                 web/style/jqueryui/Makefile
                 web/style/jqueryui/images/Makefile
                 web/style/transmission/Makefile
                 web/style/transmission/images/Makefile
                 web/style/transmission/images/buttons/Makefile
                 web/javascript/Makefile
                 web/javascript/jquery/Makefile
                 po/Makefile.in])

AC_OUTPUT

echo "

Configuration:

   Source code location:                              ${srcdir}
   Compiler:                                          ${CXX}

   Build libtransmission:                             yes
      * optimized for low-resource systems:           ${enable_lightweight}
      * µTP enabled:                                  ${build_utp}

   Build Command-Line client:                         ${build_cli}

   Build GTK+ client:                                 ${with_gtk}
      * libappindicator for an Ubuntu-style tray:     ${use_libappindicator}

   Build Daemon:                                      ${build_daemon}

   Build Mac client:                                  ${build_mac}

"
