#!/usr/bin/env bash

set -euo pipefail

clang_tidy_bin="$1"
shift

if [ "${TR_CLANG_TIDY_ENABLE:-0}" != "1" ]; then
  exit 0
fi

exec "$clang_tidy_bin" "$@"
