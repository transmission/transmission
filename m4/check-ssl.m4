# ===========================================================================
#               http://autoconf-archive.cryp.to/check_ssl.html
# ===========================================================================
#
# SYNOPSIS
#
#   CHECK_SSL
#
# DESCRIPTION
#
#   This macro will check various standard spots for OpenSSL including a
#   user-supplied directory. The user uses '--with-ssl' or
#   '--with-ssl=/path/to/ssl' as arguments to configure.
#
#   If OpenSSL is found the include directory gets added to CFLAGS and
#   CXXFLAGS as well as '-DHAVE_SSL', '-lssl' & '-lcrypto' get added to
#   LIBS, and the libraries location gets added to LDFLAGS. Finally
#   'HAVE_SSL' gets set to 'yes' for use in your Makefile.in I use it like
#   so (valid for gmake):
#
#       HAVE_SSL = @HAVE_SSL@
#       ifeq ($(HAVE_SSL),yes)
#           SRCS+= @srcdir@/my_file_that_needs_ssl.c
#       endif
#
#   For bsd 'bmake' use:
#
#       .if ${HAVE_SSL} == "yes"
#           SRCS+= @srcdir@/my_file_that_needs_ssl.c
#       .endif
#
# LAST MODIFICATION
#
#   2008-04-12
#
# COPYLEFT
#
#   Copyright (c) 2008 Mark Ethan Trostler <trostler@juniper.net>
#
#   Copying and distribution of this file, with or without modification, are
#   permitted in any medium without royalty provided the copyright notice
#   and this notice are preserved.

AC_DEFUN([CHECK_SSL],
[
    AC_MSG_CHECKING([for OpenSSL])

    for dir in $with_ssl /usr/local/ssl /usr/lib/ssl /usr/ssl /usr/pkg /usr/local /usr; do
        ssldir="$dir"
        if test -f "$dir/include/openssl/ssl.h"; then
            found_ssl="yes";
            OPENSSL_CFLAGS="-I$ssldir/include";
            break;
        fi
        if test -f "$dir/include/ssl.h"; then
            found_ssl="yes";
            OPENSSL_CFLAGS="-I$ssldir/include";
            break
        fi
    done
    if test x_$found_ssl != x_yes; then
        AC_MSG_ERROR([Cannot locate ssl])
    else
        AC_MSG_RESULT([$ssldir])
        OPENSSL_LIBS="-L$ssldir/lib -lssl -lcrypto";
    fi
])dnl

AC_SUBST(OPENSSL_CFLAGS)
AC_SUBST(OPENSSL_LIBS)
