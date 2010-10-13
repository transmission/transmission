#!/bin/sh

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

echo "creating libtransmission/version.h"

user_agent_prefix=`grep m4_define configure.ac | sed "s/[][)(]/,/g" | grep user_agent_prefix  | cut -d , -f 6`

peer_id_prefix=`grep m4_define configure.ac | sed "s/[][)(]/,/g" | grep peer_id_prefix  | cut -d , -f 6`

major_version=`echo ${user_agent_prefix} | awk -F . '{print $1}'`
minor_version=`echo ${user_agent_prefix} | awk -F . '{print $2 + 0}'`

# If this is a svn tree, and svnversion is available in PATH, use it to
# grab the version.
if [ -d ".svn" ] && type svnversion >/dev/null 2>&1; then
    svn_revision=`svnversion -n . | cut -d: -f1 | cut -dM -f1 | cut -dS -f1`
else
    # Give up and check the source files
    svn_revision=`awk '/\\$Id: /{ if ($4>i) i=$4 } END {print i}' */*.cc */*.[chm] */*.po`
fi

cat > libtransmission/version.h.new << EOF
#define PEERID_PREFIX             "${peer_id_prefix}"
#define USERAGENT_PREFIX          "${user_agent_prefix}"
#define SVN_REVISION              "${svn_revision}"
#define SVN_REVISION_NUM          ${svn_revision}
#define SHORT_VERSION_STRING      "${user_agent_prefix}"
#define LONG_VERSION_STRING       "${user_agent_prefix} (${svn_revision})"
#define VERSION_STRING_INFOPLIST  ${user_agent_prefix}
#define MAJOR_VERSION             ${major_version}
#define MINOR_VERSION             ${minor_version}
EOF

# Add a release definition
case "${peer_id_prefix}" in
    *X-) echo '#define TR_BETA_RELEASE           1' ;;
    *Z-) echo '#define TR_NIGHTLY_RELEASE        1' ;;
    *)   echo '#define TR_STABLE_RELEASE         1' ;;
esac >> "libtransmission/version.h.new"

replace_if_differs libtransmission/version.h.new libtransmission/version.h
