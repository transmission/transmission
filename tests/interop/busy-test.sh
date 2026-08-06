#!/usr/bin/env bash
# This file Copyright © Mnemosyne LLC.
# It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
# or any future license endorsed by Mnemosyne LLC.
# License text can be found in the licenses/ folder.

# A session can hold a config dir that a launch cannot delegate to. A daemon is the
# everyday case, since it answers no interop interface. The launch has nowhere to hand
# off to and must not start a second session on that dir. It says so and exits nonzero.
#
# The Qt client stands in for both here. This is the one path where a client
# reaches window creation, and only Qt has an offscreen platform to reach it
# without a display.
#
# Usage: busy-test.sh <transmission-daemon> <transmission-qt|->
# Exits 77 (ctest SKIP) when the environment cannot run the test at all.

set -u

SKIP=77
DAEMON_BIN="$1"
QT_BIN="$2"

[ -x "$DAEMON_BIN" ] || exit "$SKIP"
[ "$QT_BIN" != "-" ] || exit "$SKIP"

workdir="$(mktemp -d)"
daemon_pid=''
trap '[ -n "$daemon_pid" ] && kill "$daemon_pid" 2> /dev/null; rm -rf "$workdir"' EXIT

config_dir="$workdir/config"
mkdir -p "$config_dir"

# A port nobody else in this build is likely to want; the daemon needs one, and
# the test never speaks to it.
rpc_port=$((20000 + (RANDOM % 20000)))

"$DAEMON_BIN" -f -g "$config_dir" --port "$rpc_port" --no-portmap > "$workdir/daemon.log" 2>&1 &
daemon_pid=$!

# A session takes the config dir lock before it creates the directories it
# resumes from, so waiting for one of those is waiting for the lock. Asking
# for the lock here would answer nothing. It is an open-file-description lock,
# which a separate open in this shell does not contend with.
held=0
for _ in $(seq 1 300); do
    if [ -d "$config_dir/resume" ]; then
        held=1
        break
    fi
    kill -0 "$daemon_pid" 2> /dev/null || break
    sleep 0.1
done

if [ "$held" -ne 1 ]; then
    echo "FAIL: the daemon never took '$config_dir'"
    sed -n '1,10p' "$workdir/daemon.log"
    exit 1
fi

# The launcher never reaches window creation, so it needs no display.
export QT_QPA_PLATFORM=offscreen

failures=0

# Everything in the config dir is the daemon's. The rejected launch must leave all of
# it, settings and stats alike, exactly as found. Lock files are the exception, because
# their existence is how sessions negotiate, so a launch may create one.
snapshot() {
    (cd "$config_dir" && find . -type f ! -name 'lock*' -exec cksum {} + | sort)
}

before_files="$(snapshot)"

stderr_out="$(timeout 60 "$QT_BIN" -g "$config_dir" 2>&1 > /dev/null)"
rc=$?

# Exactly 1, the status a launch reports for a config dir it cannot have.
# Anything else is the failure this guards against, whether 0 for a second session
# started or 124 for a launch that hung until the timeout.
if [ "$rc" -eq 1 ]; then
    echo "ok: qt does not start on a config dir another session holds"
else
    echo "FAIL: qt exited $rc on a config dir another session holds"
    failures=$((failures + 1))
fi

case "$stderr_out" in
    *"already using"*) echo "ok: qt reports the config dir is in use" ;;
    *)
        echo "FAIL: qt said: '$stderr_out'"
        failures=$((failures + 1))
        ;;
esac

after_files="$(snapshot)"
if [ "$after_files" = "$before_files" ]; then
    echo "ok: qt left the owner's files alone"
else
    echo "FAIL: a rejected launch changed the owner's config dir:"
    diff <(echo "$before_files") <(echo "$after_files")
    failures=$((failures + 1))
fi

[ "$failures" -eq 0 ] && echo "PASS" || echo "$failures failure(s)"
exit "$failures"
