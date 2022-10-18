#!/bin/bash

# Generate files to be included: only overwrite them if changed so make
# won't rebuild everything unless necessary
replace_if_differs() {
  if cmp "$1" "$2" > /dev/null 2>&1; then
    rm -f "$1"
  else
    mv -f "$1" "$2"
  fi
}

echo "creating libtransmission/version.h"

major_version=$(grep 'set[(]TR_VERSION_MAJOR' CMakeLists.txt | cut -d \" -f 2)
minor_version=$(grep 'set[(]TR_VERSION_MINOR' CMakeLists.txt | cut -d \" -f 2)
patch_version=$(grep 'set[(]TR_VERSION_PATCH' CMakeLists.txt | cut -d \" -f 2)
beta_number=$(grep 'set[(]TR_VERSION_BETA_NUMBER' CMakeLists.txt | cut -d \" -f 2)
if grep -q 'set[(]TR_VERSION_DEV TRUE)' CMakeLists.txt; then
  is_dev=true
else
  is_dev=false
fi

# derived from above: semver version string. https://semver.org/
# '4.0.0-beta.1'
# '4.0.0-beta.1.dev' (a dev release between beta 1 and 2)
# '4.0.0-beta.2'
# '4.0.0'
user_agent_prefix="${major_version}.${minor_version}.${patch_version}"
if [ "$is_dev" = true ] || [ -n "${beta_number}" ]; then
  user_agent_prefix="${user_agent_prefix}-"
  if [ -n "${beta_number}" ]; then
    user_agent_prefix="${user_agent_prefix}beta.${beta_number}"
  fi
  if [ "$is_dev" = true ] && [ -n "${beta_number}" ]; then
    user_agent_prefix="${user_agent_prefix}."
  fi
  if [ "$is_dev" = true ]; then
    user_agent_prefix="${user_agent_prefix}dev";
  fi
fi

# derived from above: peer-id prefix. https://www.bittorrent.org/beps/bep_0020.html
# chars 4, 5, 6 are major, minor, patch in https://en.wikipedia.org/wiki/Base62
# char 7 is '0' for a stable release, 'B' for a beta release, or 'Z' for a dev build
# '-TR400B-' (4.0.0 Beta)
# '-TR400Z-' (4.0.0 Dev)
# '-TR4000-' (4.0.0)
BASE62=($(echo {0..9} {A..A} {a..z}))
peer_id_prefix="-TR${BASE62[$(( 10#$major_version ))]}${BASE62[$(( 10#$minor_version ))]}${BASE62[$(( 10#$patch_version ))]}"
if [ "$is_dev" = true ]; then
  peer_id_prefix="${peer_id_prefix}Z"
elif [ -n "${beta_number}" ]; then
  peer_id_prefix="${peer_id_prefix}B"
else
  peer_id_prefix="${peer_id_prefix}0"
fi
peer_id_prefix="${peer_id_prefix}-"

vcs_revision=
vcs_revision_file=REVISION

if [ -n "$JENKINS_URL" ] && [ -n "$GIT_COMMIT" ]; then
  vcs_revision=$GIT_COMMIT
elif [ -n "$TEAMCITY_PROJECT_NAME" ] && [ -n "$BUILD_VCS_NUMBER" ]; then
  vcs_revision=$BUILD_VCS_NUMBER
elif [ -e ".git" ] && type git > /dev/null 2>&1; then
  vcs_revision=$(git rev-list --max-count=1 HEAD)
elif [ -f "$vcs_revision_file" ]; then
  vcs_revision=$(cat "$vcs_revision_file")
fi

if [ -n "$vcs_revision" ]; then
  [ -f "$vcs_revision_file" ] && [ "$(cat "$vcs_revision_file")" = "$vcs_revision" ] || echo "$vcs_revision" > "$vcs_revision_file"
else
  vcs_revision=0
  rm -f "$vcs_revision_file"
fi

vcs_revision=$(echo $vcs_revision | head -c10)

cat > libtransmission/version.h.new << EOF
#pragma once

#define PEERID_PREFIX             "${peer_id_prefix}"
#define USERAGENT_PREFIX          "${user_agent_prefix}"
#define VCS_REVISION              "${vcs_revision}"
#define VCS_REVISION_NUM          ${vcs_revision}
#define SHORT_VERSION_STRING      "${user_agent_prefix}"
#define LONG_VERSION_STRING       "${user_agent_prefix} (${vcs_revision})"
#define VERSION_STRING_INFOPLIST  ${user_agent_prefix}
#define BUILD_STRING_INFOPLIST    14714.${major_version}.${minor_version}.${patch_version}
#define MAJOR_VERSION             ${major_version}
#define MINOR_VERSION             ${minor_version}
#define PATCH_VERSION             ${patch_version}
EOF

# Add a release definition
case "${peer_id_prefix}" in
  *X-) echo '#define TR_BETA_RELEASE           1' ;;
  *Z-) echo '#define TR_NIGHTLY_RELEASE        1' ;;
  *)   echo '#define TR_STABLE_RELEASE         1' ;;
esac >> "libtransmission/version.h.new"

replace_if_differs libtransmission/version.h.new libtransmission/version.h
