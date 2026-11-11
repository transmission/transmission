#!/usr/bin/env bash
# This file Copyright © Mnemosyne LLC.
# It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
# or any future license endorsed by Mnemosyne LLC.
# License text can be found in the licenses/ folder.

# A session can hold a config dir that a launch cannot delegate to. A daemon is the
# everyday case, since it answers no interop interface. The launch has nowhere to hand
# off to and must not start a second session on that dir. It says so and exits nonzero.
#
# The held dir then hosts the peer records a launch must not be stalled by (see
# mock-instance.py): one from a release before the bus ID, whose name a process that
# never answers now holds, and one from a bus that has since restarted. Each launch
# still ends on the daemon's lock. How long it takes to get there is the point, and
# the mock's log shows whether it was called at all.
#
# The Qt client stands in for both here. This is the one path where a client
# reaches window creation, and only Qt has an offscreen platform to reach it
# without a display.
#
# Usage: busy-test.sh <transmission-daemon> <transmission-qt|-> <mock-instance.py>
# Exits 77 (ctest SKIP) when the environment cannot run all of it. Without the mock's
# prerequisites the bare round still runs, and a failure there still fails the test.

set -u

SKIP=77
DAEMON_BIN="$1"
QT_BIN="$2"
MOCK="$3"

[ -x "$DAEMON_BIN" ] || exit "$SKIP"
[ "$QT_BIN" != "-" ] || exit "$SKIP"

# The record rounds put a mock on the launcher's bus, so they run on a private one,
# never touching a developer's own session or any Transmission running on it.
# Without the means for that, only the bare round runs.
record_rounds=1
if ! command -v dbus-run-session > /dev/null ||
    ! python3 -c 'import gi; from gi.repository import Gio, GLib' 2> /dev/null; then
    record_rounds=0
elif [ -z "${BUSY_TEST_ON_PRIVATE_BUS:-}" ]; then
    BUSY_TEST_ON_PRIVATE_BUS=1 exec dbus-run-session -- "$0" "$@"
fi

. "$(dirname "$0")/mock-lib.sh"

workdir="$(mktemp -d)"
daemon_pid=''
trap 'kill ${mock_pids[@]+"${mock_pids[@]}"} 2> /dev/null; [ -n "$daemon_pid" ] && kill "$daemon_pid" 2> /dev/null; rm -rf "$workdir"' EXIT

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

# Everything in the config dir is the daemon's, or was put there for the launch to
# read. The rejected launch must leave all of it, settings and stats alike, exactly
# as found. Lock files are the exception, because their existence is how sessions
# negotiate, so a launch may create one.
snapshot() {
    (cd "$config_dir" && find . -type f ! -name 'lock*' -exec cksum {} + | sort)
}

# One launch on the held dir. It must be refused with the busy report and must change
# nothing. With `limit`, it must also be done within that many seconds.
# `elapsed` is left set to how long it took.
launch_expecting_busy() {
    local label="$1"
    local limit="${2:-}"
    local before_files after_files started stderr_out rc

    before_files="$(snapshot)"
    started="$(date +%s)"
    stderr_out="$(timeout 60 "$QT_BIN" -g "$config_dir" 2>&1 > /dev/null)"
    rc=$?
    elapsed=$(($(date +%s) - started))

    # Exactly 1, the status a launch reports for a config dir it cannot have.
    # Anything else is the failure this guards against, whether 0 for a second session
    # started or 124 for a launch that hung until the timeout.
    if [ "$rc" -eq 1 ]; then
        echo "ok: $label does not start on a config dir another session holds"
    else
        fail "$label exited $rc on a config dir another session holds"
    fi

    case "$stderr_out" in
        *"already using"*) echo "ok: $label reports the config dir is in use" ;;
        *) fail "$label said: '$stderr_out'" ;;
    esac

    after_files="$(snapshot)"
    if [ "$after_files" = "$before_files" ]; then
        echo "ok: $label left the owner's files alone"
    else
        fail "$label changed the owner's config dir:"
        diff <(echo "$before_files") <(echo "$after_files")
    fi

    if [ -z "$limit" ]; then
        return
    fi

    if [ "$elapsed" -le "$limit" ]; then
        echo "ok: $label was refused in ${elapsed}s"
    else
        fail "$label took ${elapsed}s to be refused; the limit is ${limit}s"
    fi
}

elapsed=0
launch_expecting_busy "qt"

# What a stale record may add to that launch. A probe adds 3 s. A stall adds a default
# D-Bus call timeout, 25 s, or two or three of them. Measuring from the bare launch
# rather than from zero keeps a slow machine from reading as a stall.
record_limit=$((elapsed + 15))

if [ "$record_rounds" -eq 1 ]; then
    # The record a release before the bus ID left, once the bus has restarted and
    # given its name to a process that ignores calls. The launch asks that name once,
    # briefly, and starts.
    start_mock --silent "$config_dir" "$workdir/silent.log"
    launch_expecting_busy "qt (record naming a silent process)" "$record_limit"
    stop_mocks

    # A record from another bus, whose name a live client now holds. The bus ID alone
    # settles it: the launch starts without calling the name, and the mock, which would
    # answer and log any call, logs none.
    log="$workdir/other-bus.log"
    start_mock --other-bus "$config_dir" "$log"
    launch_expecting_busy "qt (record from another bus)" "$record_limit"
    expect_mock_uncalled "$log" "the name in a record from another bus"
    stop_mocks
fi

if [ "$failures" -ne 0 ]; then
    echo "$failures failure(s)"
    exit "$failures"
fi

# The bare round alone is not this test, so a pass without the record rounds is a skip.
if [ "$record_rounds" -ne 1 ]; then
    echo "skip: the record rounds need dbus-run-session and python3-gi"
    exit "$SKIP"
fi

echo "PASS"
