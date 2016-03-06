dnl convention: -TR MAJOR MINOR MAINT STATUS - (each a single char)
dnl STATUS: "X" for prerelease beta builds,
dnl         "Z" for unsupported trunk builds,
dnl         "0" for stable, supported releases
dnl these should be the only two lines you need to change
m4_define([user_agent_prefix],[2.92])
m4_define([peer_id_prefix],[-TR2920-])

AC_INIT([transmission],[user_agent_prefix],[http://trac.transmissionbt.com/newticket])
AC_SUBST(USERAGENT_PREFIX,[user_agent_prefix])
AC_SUBST(PEERID_PREFIX,[peer_id_prefix])

m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])])

AC_CONFIG_MACRO_DIR([m4])

dnl AM_CONFIG_HEADER(config.h)
AC_CONFIG_SRCDIR(libtransmission/transmission.h)
AM_INIT_AUTOMAKE([1.9 tar-pax no-dist-gzip dist-xz])
LT_INIT
LT_LIB_M

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
AM_CONDITIONAL(HAVE_REVISION_FILE, test -f REVISION)

##
##
##   MANDATORY for everything
##
##

CURL_MINIMUM=7.15.4
AC_SUBST(CURL_MINIMUM)
LIBEVENT_MINIMUM=2.0.10
AC_SUBST(LIBEVENT_MINIMUM)
ZLIB_MINIMUM=1.2.3
AC_SUBST(ZLIB_MINIMUM)

# crypto backends
OPENSSL_MINIMUM=0.9.7
AC_SUBST(OPENSSL_MINIMUM)
CYASSL_MINIMUM=3.0
AC_SUBST(CYASSL_MINIMUM)
POLARSSL_MINIMUM=0x01020000 # 1.2
AC_SUBST(POLARSSL_MINIMUM)

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
AC_C_INLINE
if test "x$GCC" = "xyes" ; then

    CFLAGS="$CFLAGS -std=gnu99 -ggdb3 -Wall -W -Wpointer-arith -Wformat-security -Wundef -Wcast-align -Wstrict-prototypes -Wmissing-declarations -Wmissing-format-attribute -Wredundant-decls -Wnested-externs -Wunused-parameter -Wwrite-strings -Winline -Wfloat-equal"

    dnl figure out gcc version
    AC_MSG_CHECKING([gcc version])
    GCC_VERSION=`$CC -dumpversion`
    GCC_MAJOR=`echo $GCC_VERSION | cut -d . -f1`
    GCC_MINOR=`echo $GCC_VERSION | cut -d . -f2`
    GCC_VERSION_NUM=`(expr $GCC_MAJOR "*" 100 + $GCC_MINOR) 2>/dev/null`

    AC_MSG_RESULT($GCC_VERSION)
    if test $GCC_VERSION_NUM -ge 304; then
        dnl these were added in 3.4
        CFLAGS="$CFLAGS -Wextra -Winit-self"
    fi
    if test $GCC_VERSION_NUM -ge 403; then
        dnl these were added in 4.3
        CFLAGS="$CFLAGS -Wvariadic-macros"
    fi
fi

AC_HEADER_STDC
AC_HEADER_TIME

AC_CHECK_HEADERS([stdbool.h xlocale.h])
AC_CHECK_FUNCS([iconv pread pwrite lrintf strlcpy daemon dirname basename canonicalize_file_name strcasecmp localtime_r fallocate64 posix_fallocate memmem strsep strtold syslog valloc getpagesize posix_memalign statvfs htonll ntohll mkdtemp uselocale _configthreadlocale])
AC_PROG_INSTALL
AC_PROG_MAKE_SET
ACX_PTHREAD

if test "x$ac_cv_func_strtold" != "xyes" ; then
    CPPFLAGS="$CPPFLAGS -Dstrtold=strtod"
fi

AC_SEARCH_LIBS(cos, [m])
AC_SEARCH_LIBS([socket], [socket net])
AC_SEARCH_LIBS([gethostbyname], [nsl bind])
AC_SEARCH_LIBS([quotacursor_skipidtype], [quota])
PKG_CHECK_MODULES(LIBCURL, [libcurl >= $CURL_MINIMUM])
PKG_CHECK_MODULES(LIBEVENT, [libevent >= $LIBEVENT_MINIMUM])
PKG_CHECK_MODULES(ZLIB, [zlib >= $ZLIB_MINIMUM])


AC_ARG_WITH([crypto], AS_HELP_STRING([--with-crypto=PKG],
            [Use specified crypto library: auto (default), openssl, cyassl, polarssl]),
            [want_crypto=$withval], [want_crypto=auto])

AS_IF([test "x$want_crypto" = "xauto" -o "x$want_crypto" = "xopenssl"], [
    PKG_CHECK_MODULES(OPENSSL, [openssl >= $OPENSSL_MINIMUM],
        [want_crypto="openssl"; CRYPTO_PKG="openssl"; CRYPTO_CFLAGS="$OPENSSL_CFLAGS"; CRYPTO_LIBS="$OPENSSL_LIBS"],
        [AS_IF([test "x$want_crypto" = "xopenssl"],
            [AC_MSG_ERROR([OpenSSL support requested, but library not found.])]
        )]
    )
])
AS_IF([test "x$want_crypto" = "xauto" -o "x$want_crypto" = "xcyassl"], [
    PKG_CHECK_MODULES(WOLFSSL, [wolfssl >= $CYASSL_MINIMUM],
        [want_crypto="cyassl"; CRYPTO_PKG="cyassl"; CRYPTO_CFLAGS="$WOLFSSL_CFLAGS"; CRYPTO_LIBS="$WOLFSSL_LIBS"],
        [PKG_CHECK_MODULES(CYASSL, [cyassl >= $CYASSL_MINIMUM],
            [want_crypto="cyassl"; CRYPTO_PKG="cyassl"; CRYPTO_CFLAGS="$CYASSL_CFLAGS"; CRYPTO_LIBS="$CYASSL_LIBS"],
            [AS_IF([test "x$want_crypto" = "xcyassl"],
                [AC_MSG_ERROR([CyaSSL support requested, but library not found.])]
            )]
        )]
    )
])
AS_IF([test "x$want_crypto" = "xauto" -o "x$want_crypto" = "xpolarssl"], [
    AC_CHECK_HEADER([polarssl/version.h],
        [AC_EGREP_CPP([version_ok], [#include <polarssl/version.h>
                                     #if defined (POLARSSL_VERSION_NUMBER) && POLARSSL_VERSION_NUMBER >= $POLARSSL_MINIMUM
                                     version_ok
                                     #endif],
            [AC_CHECK_LIB([polarssl], [dhm_calc_secret],
                [want_crypto="polarssl"; CRYPTO_PKG="polarssl"; CRYPTO_CFLAGS=""; CRYPTO_LIBS="-lpolarssl"],
                [AS_IF([test "x$want_crypto" = "xpolarssl"],
                    [AC_MSG_ERROR([PolarSSL support requested, but library not found.])]
                )]
            )],
            [AS_IF([test "x$want_crypto" = "xpolarssl"],
                [AC_MSG_ERROR([PolarSSL support requested, but version not suitable.])]
            )]
        )],
        [AS_IF([test "x$want_crypto" = "xpolarssl"],
            [AC_MSG_ERROR([PolarSSL support requested, but headers not found.])]
        )]
    )
])
# we should have found the library by now
AS_IF([test "x$CRYPTO_PKG" = "x"], [
    AS_IF([test "x$want_crypto" = "xauto"],
        [AC_MSG_ERROR([Unable to find any supported crypto library.])],
        [AC_MSG_ERROR([Requested crypto library "$want_crypto" is not supported.])],
    )
])

AM_CONDITIONAL([CRYPTO_USE_OPENSSL],[test "x$CRYPTO_PKG" = "xopenssl"])
AM_CONDITIONAL([CRYPTO_USE_CYASSL],[test "x$CRYPTO_PKG" = "xcyassl"])
AM_CONDITIONAL([CRYPTO_USE_POLARSSL],[test "x$CRYPTO_PKG" = "xpolarssl"])
AC_SUBST(CRYPTO_PKG)
AC_SUBST(CRYPTO_CFLAGS)
AC_SUBST(CRYPTO_LIBS)


AC_SYS_LARGEFILE

AC_FUNC_GETMNTENT

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

AC_ARG_WITH([inotify],
            [AS_HELP_STRING([--with-inotify], [Enable inotify support (default=auto)])],
            [WANT_INOTIFY=${withval}],
            [WANT_INOTIFY=auto])
HAVE_INOTIFY=0
AS_IF([test "x$WANT_INOTIFY" != "xno"],
      [AC_CHECK_HEADER([sys/inotify.h],
                       [AC_CHECK_FUNC([inotify_init],
                                      [HAVE_INOTIFY=1])],
                       [AS_IF([test "x$WANT_INOTIFY" = "xyes"],
                              [AC_MSG_ERROR("inotify not found!")])])])
AM_CONDITIONAL([USE_INOTIFY], [test "x$WANT_INOTIFY" != "xno" -a $HAVE_INOTIFY -eq 1])

AC_ARG_WITH([kqueue],
            [AS_HELP_STRING([--with-kqueue],[Enable kqueue support (default=auto)])],
            [WANT_KQUEUE=${withval}],
            [WANT_KQUEUE=auto])
HAVE_KQUEUE=0
AS_IF([test "x$WANT_KQUEUE" != "xno"],
      [AC_CHECK_HEADER([sys/event.h],
                       [AC_CHECK_FUNC([kqueue],
                                      [HAVE_KQUEUE=1])],
                       [AS_IF([test "x$WANT_KQUEUE" = "xyes"],
                              [AC_MSG_ERROR("kqueue not found!")])])])
AM_CONDITIONAL([USE_KQUEUE], [test "x$WANT_KQUEUE" != "xno" -a $HAVE_KQUEUE -eq 1])


AC_CHECK_HEADERS([sys/statvfs.h \
                  xfs/xfs.h])


dnl ----------------------------------------------------------------------------
dnl
dnl file monitoring for the daemon

# Check whether to enable systemd startup notification.
# This requires libsystemd-daemon.
AC_ARG_WITH([systemd-daemon], AS_HELP_STRING([--with-systemd-daemon],
            [Add support for systemd startup notification (default is autodetected)]),
            [USE_SYSTEMD_DAEMON=$withval], [USE_SYSTEMD_DAEMON=auto])
AS_IF([test "x$USE_SYSTEMD_DAEMON" != "xno"], [
    PKG_CHECK_MODULES([SYSTEMD_DAEMON], [libsystemd-daemon],
        [AC_DEFINE(USE_SYSTEMD_DAEMON,1,[Use systemd startup notification])],
        [AS_IF([test "x$USE_SYSTEMD_DAEMON" = "xyes"],
            [AC_MSG_ERROR([systemd startup notification support requested, but libsystemd-daemon not found.])]
        )]
    )
])


dnl ----------------------------------------------------------------------------
dnl
dnl  dht

DHT_CFLAGS="-I\$(top_srcdir)/third-party/dht"
DHT_LIBS="\$(top_builddir)/third-party/dht/libdht.a"
DHT_LIBS_QT="\$\${TRANSMISSION_TOP}/third-party/dht/libdht.a"
build_bundled_dht="yes"
AC_ARG_ENABLE([external-dht],
              AS_HELP_STRING([--enable-external-dht],[Use system external-dht]),
              [want_external_dht=${enableval}],
              [want_external_dht=no])
if test "x$want_external_dht" != "xno" ; then
       dnl Would be lovely if it had pkgconfig
       DHT_CFLAGS=""
       DHT_LIBS="-ldht"
       DHT_LIBS_QT="-ldht"
       build_bundled_dht="no"
fi

AM_CONDITIONAL([BUILD_DHT],[test "x$build_bundled_dht" = "xyes"])
AC_SUBST(DHT_CFLAGS)
AC_SUBST(DHT_LIBS)
AC_SUBST(DHT_LIBS_QT)


dnl ----------------------------------------------------------------------------
dnl
dnl  libb64

LIBB64_CFLAGS="-I\$(top_srcdir)/third-party/libb64"
LIBB64_LIBS="\$(top_builddir)/third-party/libb64/libb64.a"
LIBB64_LIBS_QT="\$\${TRANSMISSION_TOP}/third-party/libb64/libb64.a"
build_bundled_b64="yes"
AC_ARG_ENABLE([external-b64],
              AS_HELP_STRING([--enable-external-b64],[Use system libb64]),
              [want_external_b64=${enableval}],
              [want_external_b64=no])
if test "x$want_external_b64" != "xno" ; then
    LIBB64_CFLAGS="-DUSE_SYSTEM_B64"
    LIBB64_LIBS="-lb64"
    LIBB64_LIBS_QT="-lb64"
    build_bundled_b64="no"
fi

AM_CONDITIONAL([BUILD_B64],[test "x$build_bundled_b64" = "xyes"])
AC_SUBST(LIBB64_CFLAGS)
AC_SUBST(LIBB64_LIBS)
AC_SUBST(LIBB64_LIBS_QT)


dnl ----------------------------------------------------------------------------
dnl
dnl  utp

AC_CHECK_LIB([rt],
             [clock_gettime],
             [libutp_extra_libs="-lrt"],
             [libutp_extra_libs=""])

AC_MSG_CHECKING([µTP])
build_utp="no"

ac_save_LIBS="$LIBS"
LIBS="-lutp"
AC_LINK_IFELSE(
  [AC_LANG_PROGRAM([#include <libutp/utp.h>],
                   [struct UTPFunctionTable func;])],
  [have_utp="yes"],
  [have_utp="no"]
)
LIBS="$ac_save_LIBS"

if test "x$have_utp" = "xyes" -o "x$CXX" != "x"; then
    want_utp_default="yes"
else
    want_utp_default="no"
fi

AC_ARG_ENABLE([utp],
              AS_HELP_STRING([--enable-utp],[build µTP support]),
              [want_utp=${enableval}],
              [want_utp=${want_utp_default}])
if test "x$want_utp" = "xyes"; then
    if test "x$have_utp" = "xyes"; then
        dnl Would be lovely if it had pkgconfig
        LIBUTP_CFLAGS=""
        LIBUTP_LIBS="-lutp $libutp_extra_libs"
        LIBUTP_LIBS_QT="-lutp $libutp_extra_libs"
        AC_DEFINE([WITH_UTP],[1])
        build_utp="system"
    elif test "x$CXX" != "x"; then
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

ac_save_LIBS="$LIBS"
LIBS="-lminiupnpc"
AC_MSG_CHECKING([system miniupnpc library])

dnl See if ANY version of miniupnpc is installed
AC_LINK_IFELSE(
  [AC_LANG_PROGRAM([#include <miniupnpc/miniupnpc.h>],
                   [struct UPNPDev dev;])],
  [upnp_version="unknown"],
  [upnp_version="none"]
)

dnl Let's hope it's 1.7 or higher, since it provides
dnl MINIUPNPC_API_VERSION and we won't have to figure
dnl it out on our own
if test "x$upnp_version" = "xunknown" ; then
  AC_RUN_IFELSE(
    [AC_LANG_PROGRAM(
      [#include <stdlib.h>
       #include <miniupnpc/miniupnpc.h>],
      [#ifdef MINIUPNPC_API_VERSION
       return EXIT_SUCCESS;
       #else
       return EXIT_FAILURE;
       #endif]
    )],
    [upnp_version=">= 1.7"]
  )
fi

dnl Or maybe it's miniupnp 1.6
if test "x$upnp_version" = "xunknown" ; then
  AC_LINK_IFELSE([AC_LANG_PROGRAM([[
    #include <stdlib.h>
    #include <errno.h>
    #include <miniupnpc/miniupnpc.h>
    #include <miniupnpc/upnpcommands.h>
  ]], [[
    struct UPNPDev * devlist;
    struct UPNPUrls urls;
    struct IGDdatas data;
    char lanaddr[16];
    char portStr[8];
    char intPort[8];
    char intClient[16];
    upnpDiscover( 2000, NULL, NULL, 0, 0, &errno );
    UPNP_GetValidIGD( devlist, &urls, &data, lanaddr, sizeof( lanaddr ) ); 
    UPNP_GetSpecificPortMappingEntry( urls.controlURL, data.first.servicetype,
                        portStr, "TCP", intClient, intPort, NULL, NULL, NULL );
  ]])],[
  AC_DEFINE(MINIUPNPC_API_VERSION, 8, [miniupnpc 1.6 has API version 8])
  upnp_version="1.6"])
fi

dnl Or maybe it's miniupnp 1.5
if test "x$upnp_version" = "xunknown" ; then
  AC_LINK_IFELSE([AC_LANG_PROGRAM([[
    #include <stdlib.h>
    #include <miniupnpc/miniupnpc.h>
    #include <miniupnpc/upnpcommands.h>
  ]], [[
    struct UPNPDev * devlist;
    struct UPNPUrls urls;
    struct IGDdatas data;
    char lanaddr[16];
    char portStr[8];
    char intPort[8];
    char intClient[16];
    upnpDiscover( 2000, NULL, NULL, 0 );
    UPNP_GetValidIGD( devlist, &urls, &data, lanaddr, sizeof( lanaddr ) ); 
    UPNP_GetSpecificPortMappingEntry( urls.controlURL, data.first.servicetype,
                        portStr, "TCP", intClient, intPort );
  ]])],[
  AC_DEFINE(MINIUPNPC_API_VERSION, 5, [miniupnpc 1.5 has API version 5])
  upnp_version="1.5"])
fi

# ... and the results of our tests
LIBS="$ac_save_LIBS"
AC_MSG_RESULT([$upnp_version])
AM_CONDITIONAL([BUILD_MINIUPNP],[test "x$upnp_version" = "xnone"])
if test "x$upnp_version" = "xnone" ; then
    LIBUPNP_CFLAGS="-I\$(top_srcdir)/third-party/"
    LIBUPNP_LIBS="\$(top_builddir)/third-party/miniupnp/libminiupnp.a"
    LIBUPNP_LIBS_QT="\$\${TRANSMISSION_TOP}/third-party/miniupnp/libminiupnp.a"
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
     CPPFLAGS="$CPPFLAGS -DWINVER=0x0600 -D_WIN32_WINNT=0x0600 -DWIN32_LEAN_AND_MEAN"
     # CPPFLAGS="$CPPFLAGS -D__USE_MINGW_ANSI_STDIO=1 -D__STDC_FORMAT_MACROS=1"
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
              [build_cli="no"])
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
   AC_DEFINE([BUILD_MAC_CLIENT], 1)
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
                 third-party/libb64/Makefile
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
      * crypto library:                               ${CRYPTO_PKG}

   Build Command-Line client:                         ${build_cli}

   Build GTK+ client:                                 ${with_gtk}
      * libappindicator for an Ubuntu-style tray:     ${use_libappindicator}

   Build Daemon:                                      ${build_daemon}

   Build Mac client:                                  ${build_mac}

"
