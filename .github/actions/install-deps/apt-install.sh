#!/usr/bin/env bash
#
# `apt-get install` wrapper that tolerates dpkg configuration-ordering failures.
# When a single transaction unpacks a large dependency graph, dpkg can try to
# configure a package before its freshly-unpacked dependencies, aborting the run
# even though nothing is actually broken. On failure, complete the deferred
# configuration and retry once.
#
# Pass the sudo prefix (if any) via the SUDO_CMD environment variable.
#
set -euo pipefail

: "${SUDO_CMD:=}"

if $SUDO_CMD apt-get install -y --no-install-recommends "$@"; then
  exit 0
fi

echo "::warning title=apt-get retry::install failed; completing deferred dpkg configuration and retrying"
$SUDO_CMD dpkg --configure -a || true
$SUDO_CMD apt-get install -f -y
$SUDO_CMD apt-get install -y --no-install-recommends "$@"
