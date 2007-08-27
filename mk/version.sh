#! /bin/sh
#
# $Id$

# constraint: strlen(MAJOR MINOR MAINT BETA) must be 4
# convention: BETA: "Z" for a beta, "0" for a stable
MAJOR="0"
MINOR="8"
MAINT="1"
BETA="Z"
STRING=0.81+

PEERID_PREFIX="-TR080Z-"
USERAGENT_PREFIX="0.80+"
SVN_REVISION=`find ./ -name "*\.[ch]" -o -name "*\.cpp" -o -name "*\.po" | \
              xargs grep "\$Id:" | \
              grep -v third-party | \
              cut -d"$Id:" -f3 | cut -d" " -f3 | sort -n | tail -n 1`
  
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
VERSION_MAJOR       = "$MAJOR"
VERSION_MINOR       = "$MINOR"
VERSION_MAINTENANCE = "$MAINT"
VERSION_REVISION    = "$SVN_REVISION"
VERSION_STRING      = "$STRING ($SVN_REVISION)"
EOF
replace_if_differs mk/version.mk.new mk/version.mk

# Generate version.h
cat > libtransmission/version.h.new << EOF
#define PEERID_PREFIX         "$PEERID_PREFIX"
#define USERAGENT_PREFIX      "$USERAGENT_PREFIX"
#define SVN_REVISION          "$SVN_REVISION"
#define SHORT_VERSION_STRING  "$USERAGENT_PREFIX"
#define LONG_VERSION_STRING   "$USERAGENT_PREFIX ($SVN_REVISION)"
EOF
replace_if_differs libtransmission/version.h.new libtransmission/version.h

# Generate Info.plist from Info.plist.in
sed -e "s/%%BUNDLE_VERSION%%/$SVN_REVISION/" -e "s/%%SHORT_VERSION_STRING%%/$USERAGENT_PREFIX/" \
        < macosx/Info.plist.in > macosx/Info.plist.new
replace_if_differs macosx/Info.plist.new macosx/Info.plist

exit 0
