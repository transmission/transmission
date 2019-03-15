#!/bin/sh

set -eu

TR_NAME=transmission
PACKAGE_NAMES="${PACKAGE_NAMES:-daemon,gtk,qt,utils}"
INSIDE_DOCKER=$(test -f /.dockerenv && echo 1 || true)
DESIRED_OWNER=$(stat -c '%u:%g' .)

extract_debug_info() {
    local p=$1
    local f=$2

    for b in $(find parts/${TR_NAME}-$p/build/$p -maxdepth 1 -type f -executable -name "$TR_NAME-*"); do
        objcopy --only-keep-debug $b $(basename $b).debug || return 1
    done

    tar cjf $f.debug.tar.bz2 $TR_NAME-*.debug
}

chown_if_needed() {
    [ -z "$INSIDE_DOCKER" ] || chown -R $DESIRED_OWNER "$@"
}

if [ -z "${SKIP_BUILD:-}" ]; then
    [ -z "$INSIDE_DOCKER" ] || apt-get update -y

    for p in $(echo "$PACKAGE_NAMES" | tr ',' ' '); do
        cd $p

        snapcraft clean || true
        rm *.snap || true
        rm *.debug || true

        if ! snapcraft; then
            chown_if_needed .
            exit 1
        fi

        for f in $TR_NAME-${p}_*.snap; do
            if ! extract_debug_info $p $f; then
                chown_if_needed .
                exit 1
            fi

            chown_if_needed .
            mv $f $f.debug.tar.bz2 ..
        done

        cd ..
    done
fi

if [ -z "${SKIP_PUSH:-}" ]; then
    echo "$SNAPCRAFT_LOGIN" | snapcraft login --with -

    for p in $(echo "$PACKAGE_NAMES" | tr ',' ' '); do
        snapcraft push $TR_NAME-${p}_*.snap
    done
fi
