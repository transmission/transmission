#! /bin/sh
#
# $Id$

# Get current SVN revision from Ids in all source files
REVMAX=0
for pattern in '*.[chm]' '*.cpp' '*.po' '*.mk'; do
  for f in `find . -name "$pattern"` Makefile configure; do 
    REV=`sed -e '/\$Id:/!d; s/.*\$Id$f`
    if [ -n "$REV" ]; then
      if [ "$REV" -gt "$REVMAX" ]; then
        REVMAX="$REV"
      fi
    fi
  done
done

# Generate files to be included: only overwrite them if changed so make
# won't rebuild everything unless necessary
replace_if_differs ()
{
    if cmp $1 $2 > /dev/null 2>&1; then
      rm -f $1
    else
      mv -f $1 $2
    fi
}

# Generate version.mk
cp -f mk/version.mk.in mk/version.mk.new
echo "VERSION_REVISION = $REVMAX" >> mk/version.mk.new
replace_if_differs mk/version.mk.new mk/version.mk

# Generate version.h from version.mk
grep "^VER" mk/version.mk | sed -e 's/^/#define /g' -e 's/= //g' \
    -e 's/\(VERSION_STRING[ ]*\)\(.*\)/\1"\2"/' > \
    libtransmission/version.h.new
replace_if_differs libtransmission/version.h.new libtransmission/version.h

exit 0
