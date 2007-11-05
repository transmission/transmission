#! /bin/sh
#
# $Id$

# convention: -TR MAJOR MINOR MAINT BETA - (each a single char)
# BETA: "Z" for beta, "0" for stable 
# these should be the only two lines you need to change
PEERID_PREFIX="-TR092Z-"
USERAGENT_PREFIX="0.92+"


SVN_REVISION=`find ./ -name "*\.[ch]" -o -name "*\.cpp" -o -name "*\.po" | \
              xargs grep "\$Id:" | \
              grep -v third-party | \
              cut -d"$Id:" -f3 | cut -d" " -f3 | sort -n | tail -n 1`

if test "x${PEERID_PREFIX//Z/}" = "x$PEERID_PREFIX";
then
    STABLE_RELEASE=yes
else
    STABLE_RELEASE=no
fi
  
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
cat > macosx/version.mk.new << EOF
VERSION_REVISION    = "$SVN_REVISION"
VERSION_STRING      = "$USERAGENT_PREFIX ($SVN_REVISION)"
STABLE_RELEASE      = "$STABLE_RELEASE"
EOF
replace_if_differs macosx/version.mk.new macosx/version.mk

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
