# This file Copyright © Mnemosyne LLC.
# It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
# or any future license endorsed by Mnemosyne LLC.
# License text can be found in the licenses/ folder.

# shellcheck shell=bash
# How the interop tests run mock-instance.py. Sourced by delegation-test.sh and
# busy-test.sh, which set MOCK to the mock's path and count failures in `failures`.
# Every mock started here is in `mock_pids`, for the caller's exit trap.

mock_pids=()

fail() {
    echo "FAIL: $1"
    failures=$((failures + 1))
}

# Starts a mock in `mode` claiming `dir`, logging to `log`, and returns once it is
# reachable. Only one process can own a given bus name, so a round stops its mocks
# before the next round starts its own.
start_mock() {
    local mode="$1"
    local dir="$2"
    local log="$3"

    python3 "$MOCK" "$mode" "$dir" "$log" &
    mock_pids+=("$!")

    for _ in $(seq 1 50); do
        grep -q '^ready$' "$log" 2> /dev/null && break
        sleep 0.1
    done

    grep -q '^ready$' "$log" || {
        echo "FAIL: mock never became ready"
        exit 1
    }
}

stop_mocks() {
    # A bare `wait` would wait on every child, a daemon among them.
    [ ${#mock_pids[@]} -gt 0 ] || return 0

    kill "${mock_pids[@]}" 2> /dev/null
    wait "${mock_pids[@]}" 2> /dev/null
    mock_pids=()
}

# The mock logs "ready" once and then one line per call, so anything else in `log` is
# a call it took. `what` names the recorded name no launch was allowed to call.
expect_mock_uncalled() {
    local log="$1"
    local what="$2"

    if [ "$(grep -cv '^ready$' "$log")" -eq 0 ]; then
        echo "ok: no launch called $what"
    else
        fail "a launch called $what:"
        grep -v '^ready$' "$log"
    fi
}
