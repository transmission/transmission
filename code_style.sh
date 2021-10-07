#!/usr/bin/env bash

# Usage: ./code_style.sh
# Usage: ./code_style.sh --check

set -o noglob

if [[ "x$1" == *"check"* ]]; then
  echo "checking code format"
else
  fix=1
fi

root="$(dirname "$0")"
root="$(cd "${root}" && pwd)"
cd "${root}" || exit 1

cfile_includes=(
  '*.c'
  '*.cc'
  '*.h'
  '*.m'
)
cfile_excludes=(
  'build/*'
  'libtransmission/ConvertUTF.*'
  'libtransmission/jsonsl.*'
  'libtransmission/wildmat.*'
  'macosx/Sparkle.framework/*'
  'macosx/VDKQueue/*'
  'third-party/*'
  'web/*'
)

get_find_path_args() {
  local args=$(printf " -o -path ./%s" "$@")
  echo "${args:4}"
}

find_cfiles() {
  find . \( $(get_find_path_args "${cfile_includes[@]}") \) ! \( $(get_find_path_args "${cfile_excludes[@]}") \) "$@"
}

# format C/C++
clang_format_args="$([ -n "$fix" ] && echo '-i' || echo '--dry-run --Werror')"
if ! find_cfiles -exec clang-format $clang_format_args '{}' '+'; then
  [ -n "$fix" ] || echo 'C/C++ code needs formatting'
  exitcode=1
fi

# enforce east const
matches="$(find_cfiles -exec perl -ne 'print "west const:",$ARGV,":",$_ if /((?:^|[(<,;]|\bstatic\s+)\s*)\b(const)\b(?!\s+\w+\s*\[)/' '{}' '+')"
if [ -n "$matches" ]; then
  echo "$matches"
  exitcode=1
fi
if [ -n "$fix" ]; then
  find_cfiles -exec perl -pi -e 's/((?:^|[(<,;]|\bstatic\s+)\s*)\b(const)\b(?!\s+\w+\s*\[)/\1>\2</g' '{}' '+'
fi

# format JS
cd "${root}/web" || exit 1
yarn_args='--silent --no-progress --non-interactive'
yarn_lint_args="$([ -n "$fix" ] && echo 'lint:fix' || echo 'lint')"
if ! yarn $yarn_args install; then
  [ -n "$fix" ] || echo 'JS code could not be checked -- "yarn install" failed'
  exitcode=1
elif ! yarn $yarn_args $yarn_lint_args; then
  [ -n "$fix" ] || echo 'JS code needs formatting'
  exitcode=1
fi

exit $exitcode
