#! /bin/sh
#
# $Id$

MAJOR=0
MINOR=7
MAINT=9
STRING=0.80-svn

# Get current SVN revision from Ids in all source files
REV=`( find . '(' -name '*.[chm]' -o -name '*.cpp' -o -name '*.po' \
            -o -name '*.mk' -o -name '*.in' -o -name 'Makefile' \
            -o -name 'configure' ')' -exec cat '{}' ';' ) | \
          sed -e '/\$Id:/!d' -e \
            's/.*\$Id: [^ ]* \([0-9]*\) .*/\1/' |
          awk 'BEGIN { REV=0 }
               //    { if ( $1 > REV ) REV=$1 }
               END   { print REV }'`
  
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
cat > mk/version.mk.new << EOF
VERSION_MAJOR       = $MAJOR
VERSION_MINOR       = $MINOR
VERSION_MAINTENANCE = $MAINT
VERSION_STRING      = $STRING
VERSION_REVISION    = $REV
EOF
replace_if_differs mk/version.mk.new mk/version.mk

# Generate version.h
cat > libtransmission/version.h.new << EOF
#define VERSION_MAJOR       $MAJOR
#define VERSION_MINOR       $MINOR
#define VERSION_MAINTENANCE $MAINT
#define VERSION_STRING      "$STRING"
#define VERSION_REVISION    $REV
EOF
replace_if_differs libtransmission/version.h.new libtransmission/version.h

# Generate Info.plist from Info.plist.in
sed -e "s/%%BUNDLE_VERSION%%/$REV/" -e "s/%%SHORT_VERSION_STRING%%/$STRING/" \
        < macosx/Info.plist.in > macosx/Info.plist.new
replace_if_differs macosx/Info.plist.new macosx/Info.plist

exit 0
