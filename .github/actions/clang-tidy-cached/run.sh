#!/usr/bin/env bash
#
# Run clang-tidy over every compile_commands.json entry whose path matches
# TIDY_REGEX, routing each invocation through ctcache
# (https://github.com/matus-chochlik/ctcache). ctcache short-circuits any
# translation unit whose preprocessed source + .clang-tidy config + clang-tidy
# version are unchanged since a previous (cached) run, so a warm cache skips the
# expensive analysis. Exits non-zero if any TU reports findings.
#
# All configuration is passed via the environment (not argv) so the caller can
# invoke this cleanly through a pwsh -> cmd(vcvarsall) -> bash chain on Windows
# without quoting a regex full of |()./* through three shells.
#
#   TIDY_REGEX      (required) regex matched (anchored) against workspace-relative
#                   paths, e.g. (libtransmission|tests/libtransmission)/.*
#   CTCACHE_CLIENT  (required) path to ctcache's clang_tidy_cache.py
#   PY              (required) Python interpreter
#   BUILD_PATH      (default: obj) dir containing compile_commands.json
#   CLANG_TIDY      (default: clang-tidy) real clang-tidy executable
#   TIDY_EXTRA_ARG  (optional) single -extra-arg value appended to every invocation
#   CTCACHE_LOCAL=1 CTCACHE_DIR=<persisted dir> CTCACHE_STRIP=<workspace>  (ctcache)
#
set -uo pipefail

: "${TIDY_REGEX:?set TIDY_REGEX}"
: "${CTCACHE_CLIENT:?set CTCACHE_CLIENT}"
: "${PY:?set PY}"
build_path="${BUILD_PATH:-obj}"
clang_tidy="${CLANG_TIDY:-clang-tidy}"
extra_arg="${TIDY_EXTRA_ARG:-}"
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

if command -v nproc >/dev/null 2>&1; then jobs="$(nproc)"
elif [ -n "${NUMBER_OF_PROCESSORS:-}" ]; then jobs="$NUMBER_OF_PROCESSORS"
else jobs=4; fi

db="$build_path/compile_commands.json"
if [ ! -f "$db" ]; then
  echo "::error::compile database '$db' not found"
  exit 1
fi

# Files in the DB matching the regex, anchored against workspace-relative
# paths (with backslashes normalised) so a repo-dir regex like 'gtk/.*' can
# never match generated TUs under the build dir (e.g. obj/gtk/*-resources.c,
# obj/qt/*_autogen/mocs_compilation.cpp), which have no .clang-tidy config.
if ! files_out="$("$PY" "$script_dir/filter-compile-commands.py" "$db" "$TIDY_REGEX" "$PWD")"; then
  echo "::error::failed to read '$db'"
  exit 1
fi
if [ -z "$files_out" ]; then
  echo "::error::no translation units in '$db' match /${TIDY_REGEX}/"
  exit 1
fi
mapfile -t files <<< "$files_out"

echo "clang-tidy (ctcache): ${#files[@]} translation units match /${TIDY_REGEX}/ ; jobs=${jobs}"

# One clang-tidy invocation per file, run in parallel; ctcache serves cached TUs.
# -warnings-as-errors='*' makes a finding non-zero so it (a) fails the job and
# (b) prevents ctcache from caching a TU that still has findings.
extra=()
[ -n "$extra_arg" ] && extra=(-extra-arg="$extra_arg")

printf '%s\0' "${files[@]}" \
  | xargs -0 -P "$jobs" -I{} \
      "$PY" "$CTCACHE_CLIENT" "$clang_tidy" "{}" -p "$build_path" -quiet -warnings-as-errors='*' "${extra[@]}"
rc=$?

if [ "$rc" -ne 0 ]; then
  echo "::error::clang-tidy reported findings (or an invocation failed)"
  exit 1
fi
echo "clang-tidy (ctcache): all ${#files[@]} translation units clean"
