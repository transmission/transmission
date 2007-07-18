AC_INIT([Transmission],[0.72Z],[http://bugzilla.gnome.org/enter_bug.cgi?product=Pan])
AC_PREREQ(2.54)
AM_CONFIG_HEADER(config.h)
AC_CONFIG_SRCDIR(libtransmission/transmission.h)
AM_INIT_AUTOMAKE(AC_PACKAGE_NAME,AC_PACKAGE_VERSION)
AM_MAINTAINER_MODE

GLIB_MINIMUM=2.4.0
GTK_MINIMUM=2.4.0
WX_MINIMUM=2.6.0
AC_SUBST(GLIB_MINIMUM)
AC_SUBST(GTK_MINIMUM)
AC_SUBST(WX_MINIMUM)

AC_PROG_CC
AC_PROG_CXX
AC_HEADER_STDC
AC_HEADER_TIME
AC_CHECK_FUNCS([lrintf strlcpy strlcat])
AC_PROG_INSTALL
AC_PROG_MAKE_SET
AC_PROG_RANLIB
ACX_PTHREAD

dnl AC_PROG_INTLTOOL([0.23],[no-xml])
GETTEXT_PACKAGE=transmission
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED([GETTEXT_PACKAGE],["$GETTEXT_PACKAGE"],[Gettext package])
ALL_LINGUAS="bg es fi fr it pl ro ru sv"
AM_GLIB_GNU_GETTEXT
transmissionlocaledir='${prefix}/${DATADIRNAME}/locale'

AC_CHECK_LIB([crypto],[CRYPTO_new_ex_data])
AC_CHECK_LIB([ssl],[SSL_library_init])

dnl ----------------------------------------------------------------------------
dnl
dnl  libevent detection for the Daemon


AC_CHECK_LIB([event],[event_init],[have_libevent=yes],[have_libevent=no])
AC_ARG_WITH(libevent,
            AC_HELP_STRING([--with-libevent], [Build Daemon]),
            [want_libevent=$withval],
            [want_libevent=$have_libevent])
use_libevent=no
if test "x$want_libevent" = "xyes" ; then
    if test "x$have_libevent" = "xyes"; then
      use_libevent=yes
    else
      AC_MSG_ERROR("libevent not found!")
    fi
fi
AM_CONDITIONAL([WITH_LIBEVENT],[test "x$use_libevent" = "xyes"])



dnl ----------------------------------------------------------------------------
dnl
dnl  GTK+ detection for the GTK+ client

AM_PATH_GTK_2_0($GTK_MINIMUM,[have_gtk=yes],[have_gtk=no][gthread])
AC_ARG_WITH(gtk, AC_HELP_STRING([--with-gtk], [Build gtk client]),
                 [want_gtk=$withval],
                 [want_gtk=$have_gtk])
use_gtk=no
if test "x$want_gtk" = "xyes" ; then
    if test "x$have_gtk" = "xyes"; then
      AM_PATH_GLIB_2_0($GLIB_MINIMUM,,exit 1,gobject gmodule gthread)
      AM_PATH_GTK_2_0($GTK_MINIMUM,,exit 1,gthread)
      use_gtk=yes
    else
      AC_MSG_ERROR("GTK+ not found!")
    fi
fi
AM_CONDITIONAL([WITH_GTK],[test "x$use_gtk" = "xyes"])



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
     CPPFLAGS="$CPPFLAGS -DWIN32_LEAN_AND_MEAN"
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

  *BeOS*)
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

  *Darwin*)
    darwin="yes"
    # Make sure the Universal SDK is installed
    if [ ! -d /Developer/SDKs/MacOSX10.4u.sdk ]; then
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
dnl  Version files


dnl constraint: strlen(MAJOR MINOR MAINT BETA) must be 4
dnl convention: BETA: "Z" for a beta, "0" for a stable
MAJOR="0"
MINOR="7"
MAINT="2"
BETA="Z"
STRING=0.80-svn
REV=`svnversion -n`
AC_SUBST(VERSION_MAJOR,[$MAJOR])
AC_SUBST(VERSION_MINOR,[$MINOR])
AC_SUBST(VERSION_MAINTENANCE,[$MAINT])
AC_SUBST(VERSION_REVISION,[$REV])
AC_SUBST(VERSION_BETA,[$BETA])
AC_SUBST(VERSION_STRING,[$MAJOR.$MINOR$MAINT$BETA])
AC_SUBST(VERSION_STRING_LONG,[$MAJOR.$MINOR$MAINT$BETA-r$REV])

dnl for macosx
AC_SUBST(BUNDLE_VERSION,[$REV])
dnl if it's a beta version, include the svn revision number
if test x${BETA} = "xZ"; then
  AC_SUBST(VERSION_PACKAGE,$MAJOR.$MINOR$MAINT$BETA-r$REV)
else
  AC_SUBST(VERSION_PACKAGE,$MAJOR.$MINOR$MAINT$BETA)
fi
AC_SUBST(VERSION_PACKAGE)



dnl ----------------------------------------------------------------------------
dnl
dnl  Generate the output

AC_CONFIG_FILES([Makefile
                 transmission.spec
                 beos/Makefile
                 cli/Makefile
                 daemon/Makefile
                 gtk/Makefile
                 libtransmission/Makefile
                 libtransmission/version.h
                 macosx/Makefile
                 macosx/Info.plist])

AC_OUTPUT

echo "

Configuration:

        Source code location:  ${srcdir}
        Compiler:              ${CXX}
        Build BeOS client:     ${beos}
        Build Daemon:          ${use_libevent}
        Build GTK+ client:     ${use_gtk}
        Build OS X client:     ${darwin}

"
