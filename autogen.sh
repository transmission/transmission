#!/bin/sh
srcdir=$(dirname "$0")
test -z "$srcdir" && srcdir=.

ORIGDIR=$(pwd)
cd "$srcdir" || exit 1
PROJECT=Transmission

GETTEXTIZE="glib-gettextize"
$GETTEXTIZE --version < /dev/null > /dev/null 2>&1
if test $? -ne 0; then
  GETTEXTIZE=""
fi

LIBTOOLIZE=libtoolize
if libtoolize --help > /dev/null 2>&1; then
  :
elif glibtoolize --help > /dev/null 2>&1; then
  LIBTOOLIZE=glibtoolize
fi
export LIBTOOLIZE

./update-version-h.sh

autoreconf -fi || exit 1

if test "$GETTEXTIZE"; then
  echo "Creating aclocal.m4 ..."
  test -r aclocal.m4 || touch aclocal.m4
  echo "Running $GETTEXTIZE...  Ignore non-fatal messages."
  echo "no" | $GETTEXTIZE --force --copy
  echo "Making aclocal.m4 writable ..."
  test -r aclocal.m4 && chmod u+w aclocal.m4
  echo "Running intltoolize..."
  intltoolize --copy --force --automake
fi

cd "$ORIGDIR" || exit $?

if test -z "$AUTOGEN_SUBDIR_MODE"; then
  echo Running $srcdir/configure "$@"
  $srcdir/configure "$@"

  echo
  echo "Now type 'make' to compile $PROJECT."
fi
