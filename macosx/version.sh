#! /bin/sh
#
# $Id$

# convention: -TR MAJOR MINOR MAINT STATUS - (each a single char)
# STATUS: "X" for prerelease beta builds,
#         "Z" for unsupported trunk builds,
#         "0" for stable, supported releases
# these should be the only two lines you need to change
PEERID_PREFIX="-TR151Z-"
USERAGENT_PREFIX="1.51+"

SVN_REVISION=`find -E ./libtransmission ./macosx                     \
                  -regex ".*\.([chmp]|cpp|po|sh)"                    \
                  -exec grep -oh '\$Id: [^ ]\+ [0-9]\+' {} +         \
                  | awk '{ if ($3 > max) max = $3} END { print max }'`

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

# Generate version.h
cat > libtransmission/version.h.new << EOF
#define PEERID_PREFIX             "$PEERID_PREFIX"
#define USERAGENT_PREFIX          "$USERAGENT_PREFIX"
#define SVN_REVISION              "$SVN_REVISION"
#define SHORT_VERSION_STRING      "$USERAGENT_PREFIX"
#define LONG_VERSION_STRING       "$USERAGENT_PREFIX ($SVN_REVISION)"

#define VERSION_STRING_INFOPLIST  $USERAGENT_PREFIX
#define BUNDLE_VERSION_INFOPLIST  $SVN_REVISION
EOF

# Add a release definition
if [ ${PEERID_PREFIX:6:1} = X ]; then
    line='#define TR_BETA_RELEASE     "BETA"'
elif [ ${PEERID_PREFIX:6:1} = Z ]; then
    line='#define TR_NIGHTLY_RELEASE  "NIGHTLY"'
else
    line='#define TR_STABLE_RELEASE   "STABLE"'
fi
echo $line >> libtransmission/version.h.new

replace_if_differs libtransmission/version.h.new libtransmission/version.h

exit 0
