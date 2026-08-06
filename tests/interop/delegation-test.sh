#!/usr/bin/env bash
# This file Copyright © Mnemosyne LLC.
# It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
# or any future license endorsed by Mnemosyne LLC.
# License text can be found in the licenses/ folder.

# Exercises the real client binaries as delegating launchers against a mock
# instance (see mock-instance.py). Each launcher must hand its torrent to the
# mock, must present through the mock on a bare launch, and must exit instead
# of starting.
#
# The rounds cover both ways a client is reachable, and both ways a config dir
# is named:
#
#   named / default   -- the dir given with -g, then the default dir, which
#                        TRANSMISSION_HOME points somewhere this test owns.
#                        The second is the one nearly every user is in, and
#                        the only cover for resolving the default dir at all.
#   foreign           -- a mock holding a bus name, interface and object path
#                        that share nothing with this build's, published in
#                        the config dir's peer record. This is how a launcher
#                        reaches a separately-built client, so it is the round
#                        that fails if a launcher calls the names it was built
#                        with instead of the ones it was given.
#   legacy            -- a mock owning the shared name but answering no
#                        ConfigDir method, standing in for a release older
#                        than the method. This is the upgrade path: the first
#                        launch of a new build must hand off to the old
#                        client, not open a second window beside it.
#
# Usage: delegation-test.sh <transmission-qt|-> <transmission-gtk|-> <mock-instance.py>
# Pass "-" for a client that is not built. Exits 77 (ctest SKIP) when the
# environment cannot run the test at all.

set -u

SKIP=77
QT_BIN="$1"
GTK_BIN="$2"
MOCK="$3"

command -v dbus-run-session > /dev/null || exit "$SKIP"
python3 -c 'import gi; from gi.repository import Gio, GLib' 2> /dev/null || exit "$SKIP"

# Everything below talks over a private bus, so it never touches a developer's own
# session or any Transmission running on it.
if [ -z "${DELEGATION_TEST_ON_PRIVATE_BUS:-}" ]; then
    DELEGATION_TEST_ON_PRIVATE_BUS=1 exec dbus-run-session -- "$0" "$@"
fi

workdir="$(mktemp -d)"
mock_pid=''
trap '[ -n "$mock_pid" ] && kill "$mock_pid" 2> /dev/null; rm -rf "$workdir"' EXIT

torrent="$workdir/example.torrent"
printf 'torrent-file-payload' > "$torrent"

failures=0
log=''
config_args=()

# Only one process can own a given bus name, so each round gets the mock to
# itself, claiming that round's config dir.
start_mock() {
    local mode="$1"
    local dir="$2"
    log="$3"

    python3 "$MOCK" "$mode" "$dir" "$log" &
    mock_pid=$!

    for _ in $(seq 1 50); do
        grep -q '^ready$' "$log" 2> /dev/null && break
        sleep 0.1
    done

    grep -q '^ready$' "$log" || {
        echo "FAIL: mock never owned the name"
        exit 1
    }
}

stop_mock() {
    kill "$mock_pid" 2> /dev/null
    wait "$mock_pid" 2> /dev/null
    mock_pid=''
}

# Checks that one launcher invocation made exactly one expected call. Counting
# from a snapshot keeps one client's result from satisfying the next client's
# assertion and catches accidental double delivery.
expect_one_new_call() {
    local label="$1"
    local call="$2"
    local before="$3"
    local after
    after="$(grep -Fxc "$call" "$log" || true)"

    if [ "$after" -eq $((before + 1)) ]; then
        echo "ok: $1"
    else
        echo "FAIL: $label -- expected one new '$call' call, saw $((after - before))"
        failures=$((failures + 1))
    fi
}

call_count() {
    grep -Fxc "$1" "$log" || true
}

# Runs the launcher the way this round names its config dir: with -g, or with
# nothing at all so that the client has to find the default itself.
launch() {
    timeout 30 "$1" ${config_args[@]+"${config_args[@]}"} "${@:2}"
}

run_launcher_cases() {
    local label="$1"
    local bin="$2"
    local magnet="magnet:?xt=urn:btih:00000000000000000000000000000000000000$3&dn=test"
    local encoded_torrent="dG9ycmVudC1maWxlLXBheWxvYWQ="
    local call
    local before

    # A torrent launch hands the torrent over and exits quietly.
    call="AddMetainfo $magnet"
    before="$(call_count "$call")"
    launch "$bin" "$magnet" > /dev/null 2>&1
    local rc=$?
    [ "$rc" -eq 0 ] || {
        echo "FAIL: $label torrent launch exited $rc"
        failures=$((failures + 1))
    }
    expect_one_new_call "$label hands its magnet to the running instance" "$call" "$before"

    # File arguments travel as base64 contents, both as an argv path and as
    # the file URI supplied by a desktop Exec=%U launch.
    call="AddMetainfo $encoded_torrent"
    before="$(call_count "$call")"
    launch "$bin" "$torrent" > /dev/null 2>&1
    rc=$?
    [ "$rc" -eq 0 ] || {
        echo "FAIL: $label torrent-file launch exited $rc"
        failures=$((failures + 1))
    }
    expect_one_new_call "$label hands a torrent file to the running instance" "$call" "$before"

    before="$(call_count "$call")"
    launch "$bin" "file://$torrent" > /dev/null 2>&1
    rc=$?
    [ "$rc" -eq 0 ] || {
        echo "FAIL: $label file-URI launch exited $rc"
        failures=$((failures + 1))
    }
    expect_one_new_call "$label hands a file URI to the running instance" "$call" "$before"

    # A launch whose only argument names nothing exits on the argument error.
    # Falling through to start would end on the running instance's config dir lock,
    # blaming the dir for what is an argument problem.
    local add_before present_before
    add_before="$(grep -Fc "AddMetainfo" "$log" || true)"
    present_before="$(call_count "PresentWindow")"
    stderr_out="$(launch "$bin" "$workdir/no-such-file.torrent" 2>&1 > /dev/null)"
    rc=$?
    if [ "$rc" -eq 1 ]; then
        echo "ok: $label unusable-argument launch exits with the argument error"
    else
        echo "FAIL: $label unusable-argument launch exited $rc"
        failures=$((failures + 1))
    fi
    case "$stderr_out" in
        *Skipping*) echo "ok: $label reports the unusable argument" ;;
        *)
            echo "FAIL: $label unusable-argument launch said: '$stderr_out'"
            failures=$((failures + 1))
            ;;
    esac
    if [ "$(grep -Fc "AddMetainfo" "$log" || true)" -eq "$add_before" ] &&
        [ "$(call_count "PresentWindow")" -eq "$present_before" ]; then
        echo "ok: $label unusable-argument launch bothered nobody"
    else
        echo "FAIL: $label unusable-argument launch reached the running instance"
        failures=$((failures + 1))
    fi

    # A bare launch presents the running instance and says so.
    local stderr_out
    call="PresentWindow"
    before="$(call_count "$call")"
    stderr_out="$(launch "$bin" 2>&1 > /dev/null)"
    rc=$?
    [ "$rc" -eq 0 ] || {
        echo "FAIL: $label bare launch exited $rc"
        failures=$((failures + 1))
    }
    expect_one_new_call "$label presents the running instance" "$call" "$before"
    case "$stderr_out" in
        *"Already running on"*) echo "ok: $label bare launch reports the busy config dir" ;;
        *)
            echo "FAIL: $label bare launch said: '$stderr_out'"
            failures=$((failures + 1))
            ;;
    esac
}

ran_any=0

# `mode` is how the mock makes itself reachable (see mock-instance.py); `dir` is
# the config dir it claims; the arguments after it are how a launcher is told to
# use that same dir.
run_round() {
    local round="$1"
    local mode="$2"
    local dir="$3"
    shift 3
    config_args=("$@")

    mkdir -p "$dir"
    start_mock "$mode" "$dir" "$workdir/calls-$round.log"

    if [ "$QT_BIN" != "-" ]; then
        run_launcher_cases "qt ($round)" "$QT_BIN" aa
        ran_any=1
    fi

    if [ "$GTK_BIN" != "-" ]; then
        run_launcher_cases "gtk ($round)" "$GTK_BIN" bb
        ran_any=1
    fi

    stop_mock
}

# The launchers never reach window creation, because delegation runs first, so they
# need no display. Offscreen covers the Qt fallthrough path.
export QT_QPA_PLATFORM=offscreen

run_round named --well-known "$workdir/named-config" -g "$workdir/named-config"

# The handoff a fork depends on. Nothing here shares a D-Bus name with the
# launchers, so every one of them has to come out of the peer record.
run_round foreign --foreign "$workdir/foreign-config" -g "$workdir/foreign-config"

# The upgrade path. A release predating ConfigDir() cannot say which dir it is on,
# and a launcher must read that as "too old to say" and hand off anyway.
run_round legacy --legacy "$workdir/legacy-config" -g "$workdir/legacy-config"

# tr_getDefaultConfigDir() returns TRANSMISSION_HOME verbatim when it is set,
# so this is the default config dir for every client started below.
export TRANSMISSION_HOME="$workdir/default-config"
run_round default --well-known "$TRANSMISSION_HOME"
unset TRANSMISSION_HOME

[ "$ran_any" -eq 1 ] || exit "$SKIP"

[ "$failures" -eq 0 ] && echo "PASS" || echo "$failures failure(s)"
exit "$failures"
